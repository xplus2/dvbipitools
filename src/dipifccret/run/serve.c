/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "lib/helper/log.h"
#include "lib/sys/signal.h"
#include "../channel/channel.h"
#include "../listen.h"
#include "../version.h"
#include "run.h"

#define MC_SEND_TTL 1 /* fixed, no CLI flag */
#define FCC_LIMIT_SLOTS 4096 /* per-source RAMS-R buckets, direct-mapped */

#define FCC_ASSUMED_MAX_BITRATE_BPS 20000000.0
#define FCC_ASSUMED_TS_PACKET_BYTES 1316.0

#define RET_RING_MAX_SLOTS 65536 /* ring indexed by 16-bit seq, more is dead weight */

static size_t cache_cap_from_gop_ms(unsigned gop_cap_ms) {
  double packets_per_sec = FCC_ASSUMED_MAX_BITRATE_BPS / 8.0 / FCC_ASSUMED_TS_PACKET_BYTES;
  double entries = packets_per_sec * (double)gop_cap_ms / 1000.0;
  return (size_t)entries + 1;
}

capture_t *fccret_open_capture(const config_t *cfg, char *errbuf, size_t errbuf_len) {
  return capture_open(cfg->iface, cfg->range_ptrs, cfg->range_count, errbuf, errbuf_len);
}

typedef struct {
  const config_t *cfg;
  metrics_exporter_t *mx;
  size_t max_channels;
  channel_table_t *channels;
  tsinspect_agg_t *agg;
  mcsend_table_t *mt;
  mcsend_table_t *rsi_mt;
  int rsi_mc;
  ret_ctx_t *ret;
  burst_table_t *bursts;
  ratelimit_t *fcc_limiter;
  capture_t *cap;
  ret_send_ctx_t ret_send_ctx;
  ret_send_ctx_t rsi_send_ctx;
  dispatch_ctx_t dispatch_ctx;
  listen_pool_t *pool;
  listen_multi_t *resolve_pool;
  unsigned resolve_base_port;
  unsigned cpu_next;
  pacer_ctx_t pacer_ctx;
  pthread_t pacer_thread;
  int pacer_started;
  rsi_pacer_ctx_t rsi_ctx;
  pthread_t rsi_thread;
  int rsi_started;
  metrics_ctx_t metrics_ctx;
  pthread_t metrics_thread;
  int metrics_started;
} serve_t;

static int alloc_tables(serve_t *v) {
  const config_t *cfg = v->cfg;
  size_t ring_slots = cfg->no_ret ? 0 : cfg->buffer_ms; /* 1 slot per ms, ~1000 packets/s */
  if (ring_slots > RET_RING_MAX_SLOTS) ring_slots = RET_RING_MAX_SLOTS;
  v->channels = channel_table_new(v->max_channels, ring_slots, cfg->no_fcc ? 0 : cache_cap_from_gop_ms(cfg->gop_cap_ms));
  if (!v->channels) {
    log_code("FS500181", "%s: out of memory allocating channel table", TOOL_NAME);
    return -1;
  }
  if (!cfg->no_ret && !cfg->no_mc_ret) {
    v->mt = mcsend_table_new(v->max_channels, cfg->iface, MC_SEND_TTL);
    if (!v->mt) {
      log_code("FS500182", "%s: out of memory allocating MC RET session table", TOOL_NAME);
      return -1;
    }
  }
  if (!cfg->no_ret && !cfg->no_rsi) {
    if (cfg->listen_family == AF_INET6) {
      log_code("FA700140", TOOL_NAME ": RSI self-announcement needs an IPv4 -l address (F.5.3 IPv6 unicast feedback is not supported in DVB), disabling it");
    } else if (cfg->rsi_mc_ret) {
      v->rsi_mc = v->mt != NULL;
    } else {
      v->rsi_mt = mcsend_table_new(v->max_channels, cfg->iface, MC_SEND_TTL);
      if (!v->rsi_mt) {
        log_code("FS500183", "%s: out of memory allocating RSI announcement table", TOOL_NAME);
        return -1;
      }
    }
  }
  if (cfg->no_fcc) return 0;
  v->bursts = burst_table_new(cfg->max_bursts);
  if (!v->bursts) {
    log_code("FS500184", "%s: out of memory allocating burst table", TOOL_NAME);
    return -1;
  }
  if (cfg->fcc_client_rate) {
    v->fcc_limiter = ratelimit_new(FCC_LIMIT_SLOTS);
    if (!v->fcc_limiter) {
      log_code("FS500185", "%s: out of memory allocating FCC rate limiter", TOOL_NAME);
      return -1;
    }
  }
  if (cfg->fcc_client_range_count == 0)
    log_code("FA000462", TOOL_NAME ": --fcc-client-range not set, any source address can request FCC bursts, set it on exposed networks");
  return 0;
}

static int start_ret(serve_t *v) {
  const config_t *cfg = v->cfg;
  if (cfg->no_ret) return 0;
  v->ret_send_ctx.mt = v->mt;
  v->ret = ret_ctx_new(v->channels, cfg->rtx_pt, cfg->max_ret_clients, ret_send_mc_impl, ret_send_unicast_impl, &v->ret_send_ctx);
  if (!v->ret) {
    log_code("FS500187", "%s: out of memory creating ret context", TOOL_NAME);
    return -1;
  }
  if (ret_ctx_set_limits(v->ret, cfg->ret_client_rate, cfg->ret_mc_dedup_ms) != 0) {
    log_code("FS500188", "%s: out of memory allocating RET rate limiter", TOOL_NAME);
    return -1;
  }
  return 0;
}

static void dispatch_init(serve_t *v) {
  const config_t *cfg = v->cfg;
  v->agg = tsinspect_agg_new(cfg->metrics_inspect_ts);
  if (v->agg) {
    channel_table_set_inspect(v->channels, v->agg);
    metrics_exporter_set_extra(v->mx, tsinspect_agg_put_cb, v->agg);
  }
  v->dispatch_ctx = (dispatch_ctx_t){
    .channels = v->channels,
    .agg = v->agg,
    .mt = v->mt,
    .ff_port = cfg->ff_port,
    .rsi_mt = v->rsi_mt,
    .rsi_active = v->rsi_mt != NULL || v->rsi_mc,
    .ret = v->ret,
    .bursts = v->bursts,
    .burst_multiplier = cfg->burst_multiplier,
    .duration_cap_ms = cfg->duration_cap_ms,
    .max_buffer_fill_bound_ms = cfg->max_buffer_fill_bound_ms,
    .congestion_nack_threshold = cfg->congestion_nack_threshold,
    .fcc_ranges = cfg->fcc_ranges,
    .fcc_range_count = cfg->fcc_range_count,
    .fcc_client_ranges = cfg->fcc_client_ranges,
    .fcc_client_range_count = cfg->fcc_client_range_count,
    .fcc_limiter = v->fcc_limiter,
    .fcc_client_rate = cfg->fcc_client_rate,
    .rtx_pt = cfg->rtx_pt,
    .idle_timeout_s = cfg->channel_idle_timeout_s,
    .ret_client_idle_timeout_s = cfg->ret_client_idle_timeout_s,
    .nack_truncated_logged = 0,
  };
}

static int start_listeners(serve_t *v) {
  const config_t *cfg = v->cfg;
  v->pool = listen_pool_start(cfg->listen_family, cfg->listen_addr, cfg->listen_port, cfg->workers, listen_cb, &v->dispatch_ctx);
  if (!v->pool) {
    log_code("FS500189", "%s: failed to start listen workers on %s:%u", TOOL_NAME, cfg->listen_addr, cfg->listen_port);
    return -1;
  }
  v->resolve_base_port = cfg->fcc_resolve_base_port ? cfg->fcc_resolve_base_port : cfg->listen_port + 1;
  if (cfg->fcc_resolve_by_port) {
    v->resolve_pool = listen_multi_start(cfg->listen_family, cfg->listen_addr, v->resolve_base_port, v->max_channels, listen_resolve_cb, &v->dispatch_ctx);
    if (!v->resolve_pool) {
      log_code("FS500190", "%s: failed to start FCC resolve-by-port sockets at %s:%u..%u", TOOL_NAME, cfg->listen_addr, v->resolve_base_port, v->resolve_base_port + (unsigned)v->max_channels - 1);
      return -1;
    }
  }
  if (capture_drop_privileges(cfg->user) != 0) { /* after all binds: -l below 1024 needs root */
    log_code("FS300157", "%s: failed to drop privileges to -u %s", TOOL_NAME, cfg->user);
    return -1;
  }
  return 0;
}

static int start_rsi(serve_t *v) {
  const config_t *cfg = v->cfg;
  if (!cfg->rsi_mc_ret) v->rsi_send_ctx.mt = v->rsi_mt;
  v->rsi_ctx = (rsi_pacer_ctx_t){
    .channels = v->channels,
    .send_ctx = cfg->rsi_mc_ret ? &v->ret_send_ctx : &v->rsi_send_ctx, /* dvb-rsi-mc-ret: rides mt, F.6.2.2 same group:port */
    .interval_s = cfg->rsi_interval_s,
    .port = (uint16_t)cfg->listen_port,
    .hostname = cfg->rsi_hostname[0] ? cfg->rsi_hostname : NULL,
    .hostname_len = strlen(cfg->rsi_hostname),
    .resolve_by_port = cfg->fcc_resolve_by_port,
    .resolve_base_port = v->resolve_base_port,
    .cpuaff = &cfg->cpu_affinity,
    .cpu_idx = v->cpu_next++,
  };
  if (inet_pton(AF_INET, cfg->listen_addr, v->rsi_ctx.addr) != 1) {
    log_code("FS000463", "%s: failed to parse -l address for RSI announcement", TOOL_NAME);
    return -1;
  }
  if (pthread_create(&v->rsi_thread, NULL, rsi_pacer_main, &v->rsi_ctx) != 0) {
    log_code("FS500192", "%s: failed to start RSI announcement thread", TOOL_NAME);
    return -1;
  }
  v->rsi_started = 1;
  return 0;
}

static int start_threads(serve_t *v) {
  const config_t *cfg = v->cfg;
  if (v->bursts) {
    v->pacer_ctx = (pacer_ctx_t){.bursts = v->bursts, .duration_cap_ms = cfg->duration_cap_ms, .cpuaff = &cfg->cpu_affinity, .cpu_idx = v->cpu_next++};
    if (pthread_create(&v->pacer_thread, NULL, pacer_main, &v->pacer_ctx) != 0) {
      log_code("FS500191", "%s: failed to start burst pacing thread", TOOL_NAME);
      return -1;
    }
    v->pacer_started = 1;
  }
  if ((v->rsi_mt || v->rsi_mc) && start_rsi(v)) return -1;
  if (metrics_exporter_enabled(v->mx)) {
    v->metrics_ctx = (metrics_ctx_t){.mx = v->mx, .channels = v->channels, .ret = v->ret, .bursts = v->bursts};
    if (pthread_create(&v->metrics_thread, NULL, metrics_thread_main, &v->metrics_ctx) != 0) {
      log_code("FS500193", "%s: failed to start metrics thread", TOOL_NAME);
      return -1;
    }
    v->metrics_started = 1;
  }
  return 0;
}

static void serve_cleanup(serve_t *v) {
  signal_request_stop(); /* threads exit only on stop */
  if (v->cap) capture_close(v->cap);
  if (v->pacer_started) pthread_join(v->pacer_thread, NULL);
  if (v->rsi_started) pthread_join(v->rsi_thread, NULL);
  if (v->metrics_started) pthread_join(v->metrics_thread, NULL);
  metrics_exporter_close(v->mx);
  if (v->pool) listen_pool_stop(v->pool);
  if (v->resolve_pool) listen_multi_stop(v->resolve_pool);
  if (v->ret) ret_ctx_free(v->ret);
  if (v->bursts) burst_table_free(v->bursts);
  ratelimit_free(v->fcc_limiter);
  if (v->mt) mcsend_table_free(v->mt);
  if (v->rsi_mt) mcsend_table_free(v->rsi_mt);
  if (v->channels) channel_table_free(v->channels);
  tsinspect_agg_free(v->agg);
}

int fccret_serve(const config_t *cfg, metrics_exporter_t *mx, fccret_open_capture_fn open_capture) {
  serve_t v = {.cfg = cfg, .mx = mx, .max_channels = cfg->max_channels ? cfg->max_channels : CHANNEL_DEFAULT_MAX};
  char errbuf[256];
  int rc = 1;

  if (alloc_tables(&v)) goto cleanup;
  v.cap = open_capture(cfg, errbuf, sizeof errbuf);
  if (!v.cap) {
    log_code("FS500186", "%s: cannot open capture: %s", TOOL_NAME, errbuf);
    goto cleanup;
  }
  if (start_ret(&v)) goto cleanup;
  dispatch_init(&v);
  if (start_listeners(&v) || start_threads(&v)) goto cleanup;

  log_line(TOOL_NAME ": capturing, %u worker(s) on %s:%u, %zu channel slots [%s%s%s%s]", cfg->workers ? cfg->workers : 1u, cfg->listen_addr, cfg->listen_port, v.max_channels,
      cfg->no_ret ? "no RET" : (v.mt ? "RET+MC" : "RET unicast-only"), cfg->no_ret || cfg->no_fcc ? "" : ", ", cfg->no_fcc ? "no FCC" : "FCC", v.rsi_started ? "+RSI" : "");
  rc = 0;
  if (capture_run(v.cap, capture_cb, &v.dispatch_ctx) != 0) {
    log_code("FE500244", "%s: capture poll failed: %s", TOOL_NAME, strerror(errno));
    rc = 1;
  }

cleanup:
  serve_cleanup(&v);
  return rc;
}
