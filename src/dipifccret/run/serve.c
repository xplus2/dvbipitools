/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

#include "lib/helper/log.h"
#include "../channel/channel.h"
#include "../listen.h"
#include "../version.h"
#include "run.h"

#define MC_SEND_TTL 1 /* fixed, no CLI flag */

#define FCC_ASSUMED_MAX_BITRATE_BPS 20000000.0
#define FCC_ASSUMED_TS_PACKET_BYTES 1316.0

static size_t cache_cap_from_gop_ms(unsigned gop_cap_ms) {
  double packets_per_sec = FCC_ASSUMED_MAX_BITRATE_BPS / 8.0 / FCC_ASSUMED_TS_PACKET_BYTES;
  double entries = packets_per_sec * (double)gop_cap_ms / 1000.0;
  return (size_t)entries + 1;
}

capture_t *fccret_open_capture(const config_t *cfg, char *errbuf, size_t errbuf_len) {
  return capture_open(cfg->iface, cfg->range_ptrs, cfg->range_count, errbuf, errbuf_len);
}

int fccret_serve(const config_t *cfg, metrics_exporter_t *mx, fccret_open_capture_fn open_capture) {
  size_t max_channels, ring_slots, cache_cap;
  channel_table_t *channels = NULL;
  tsinspect_agg_t *agg = NULL;
  mcsend_table_t *mt = NULL;
  mcsend_table_t *rsi_mt = NULL;
  ret_ctx_t *ret = NULL;
  burst_table_t *bursts = NULL;
  capture_t *cap = NULL;
  char errbuf[256];
  ret_send_ctx_t ret_send_ctx = {NULL, -1};
  ret_send_ctx_t rsi_send_ctx = {NULL, -1};
  dispatch_ctx_t dispatch_ctx;
  listen_pool_t *pool = NULL;
  listen_multi_t *resolve_pool = NULL;
  unsigned resolve_base_port;
  int rc = 0;
  unsigned cpu_next = 0;
  pacer_ctx_t pacer_ctx;
  pthread_t pacer_thread;
  int pacer_started = 0;
  rsi_pacer_ctx_t rsi_ctx;
  pthread_t rsi_thread;
  int rsi_started = 0;
  metrics_ctx_t metrics_ctx;
  pthread_t metrics_thread;
  int metrics_started = 0;

  max_channels = cfg->max_channels ? cfg->max_channels : CHANNEL_DEFAULT_MAX;
  ring_slots = cfg->no_ret ? 0 : cfg->buffer_ms; /* ring_slots ~= buffer_ms: ~1 packet/ms assumption */
  cache_cap = cfg->no_fcc ? 0 : cache_cap_from_gop_ms(cfg->gop_cap_ms);
  channels = channel_table_new(max_channels, ring_slots, cache_cap);
  if (!channels) {
    fprintf(stderr, "%s: out of memory allocating channel table\n", TOOL_NAME);
    rc = 1;
    goto cleanup;
  }

  if (!cfg->no_ret && !cfg->no_mc_ret) {
    mt = mcsend_table_new(max_channels, cfg->iface, MC_SEND_TTL);
    if (!mt) {
      fprintf(stderr, "%s: out of memory allocating MC RET session table\n", TOOL_NAME);
      rc = 1;
      goto cleanup;
    }
  }
  if (!cfg->no_ret && !cfg->no_rsi) {
    if (cfg->listen_family == AF_INET6) {
      log_line(TOOL_NAME ": RSI self-announcement needs an IPv4 -l address (F.5.3 IPv6 unicast feedback is not supported in DVB), disabling it");
    } else if (!cfg->rsi_mc_ret) {
      rsi_mt = mcsend_table_new(max_channels, cfg->iface, MC_SEND_TTL);
      if (!rsi_mt) {
        fprintf(stderr, "%s: out of memory allocating RSI announcement table\n", TOOL_NAME);
        rc = 1;
        goto cleanup;
      }
    }
  }
  if (!cfg->no_fcc) {
    bursts = burst_table_new(cfg->max_bursts);
    if (!bursts) {
      fprintf(stderr, "%s: out of memory allocating burst table\n", TOOL_NAME);
      rc = 1;
      goto cleanup;
    }
  }

  cap = open_capture(cfg, errbuf, sizeof errbuf);
  if (!cap) {
    fprintf(stderr, "%s: %s\n", TOOL_NAME, errbuf);
    rc = 1;
    goto cleanup;
  }
  if (capture_drop_privileges(cfg->user) != 0) {
    fprintf(stderr, "%s: failed to drop privileges to -u %s\n", TOOL_NAME, cfg->user);
    rc = 1;
    goto cleanup;
  }

  if (!cfg->no_ret) {
    ret_send_ctx.mt = mt;
    ret = ret_ctx_new(channels, cfg->rtx_pt, cfg->max_ret_clients, ret_send_mc_impl, ret_send_unicast_impl, &ret_send_ctx);
    if (!ret) {
      fprintf(stderr, "%s: out of memory creating ret context\n", TOOL_NAME);
      rc = 1;
      goto cleanup;
    }
  }

  agg = tsinspect_agg_new(cfg->metrics_inspect_ts);
  if (agg) {
    channel_table_set_inspect(channels, agg);
    metrics_exporter_set_extra(mx, tsinspect_agg_put_cb, agg);
  }
  dispatch_ctx = (dispatch_ctx_t){
    .channels = channels,
    .agg = agg,
    .mt = mt,
    .ff_port = cfg->ff_port,
    .rsi_mt = rsi_mt,
    .rsi_active = rsi_mt != NULL || (cfg->rsi_mc_ret && mt != NULL),
    .ret = ret,
    .bursts = bursts,
    .burst_multiplier = cfg->burst_multiplier,
    .duration_cap_ms = cfg->duration_cap_ms,
    .max_buffer_fill_bound_ms = cfg->max_buffer_fill_bound_ms,
    .congestion_nack_threshold = cfg->congestion_nack_threshold,
    .fcc_ranges = cfg->fcc_ranges,
    .fcc_range_count = cfg->fcc_range_count,
    .fcc_client_ranges = cfg->fcc_client_ranges,
    .fcc_client_range_count = cfg->fcc_client_range_count,
    .rtx_pt = cfg->rtx_pt,
    .idle_timeout_s = cfg->channel_idle_timeout_s,
    .ret_client_idle_timeout_s = cfg->ret_client_idle_timeout_s,
    .nack_truncated_logged = 0,
  };

  pool = listen_pool_start(cfg->listen_family, cfg->listen_addr, cfg->listen_port, cfg->workers, listen_cb, &dispatch_ctx);
  if (!pool) {
    fprintf(stderr, "%s: failed to start listen workers on %s:%u\n", TOOL_NAME, cfg->listen_addr, cfg->listen_port);
    rc = 1;
    goto cleanup;
  }

  resolve_base_port = cfg->fcc_resolve_base_port ? cfg->fcc_resolve_base_port : cfg->listen_port + 1;
  if (cfg->fcc_resolve_by_port) {
    resolve_pool = listen_multi_start(cfg->listen_family, cfg->listen_addr, resolve_base_port, max_channels, listen_resolve_cb, &dispatch_ctx);
    if (!resolve_pool) {
      fprintf(stderr, "%s: failed to start FCC resolve-by-port sockets at %s:%u..%u\n", TOOL_NAME, cfg->listen_addr, resolve_base_port, resolve_base_port + (unsigned)max_channels - 1);
      rc = 1;
      goto cleanup;
    }
  }

  if (bursts) {
    pacer_ctx = (pacer_ctx_t){.bursts = bursts, .duration_cap_ms = cfg->duration_cap_ms, .cpuaff = &cfg->cpu_affinity, .cpu_idx = cpu_next++};
    if (pthread_create(&pacer_thread, NULL, pacer_main, &pacer_ctx) != 0) {
      fprintf(stderr, "%s: failed to start burst pacing thread\n", TOOL_NAME);
      rc = 1;
      goto cleanup;
    }
    pacer_started = 1;
  }

  if (rsi_mt || (cfg->rsi_mc_ret && mt)) {
    if (!cfg->rsi_mc_ret) rsi_send_ctx.mt = rsi_mt;
    rsi_ctx = (rsi_pacer_ctx_t){
      .channels = channels,
      .send_ctx = cfg->rsi_mc_ret ? &ret_send_ctx : &rsi_send_ctx, /* dvb-rsi-mc-ret: rides mt, F.6.2.2 same group:port */
      .interval_s = cfg->rsi_interval_s,
      .port = (uint16_t)cfg->listen_port,
      .hostname = cfg->rsi_hostname[0] ? cfg->rsi_hostname : NULL,
      .hostname_len = strlen(cfg->rsi_hostname),
      .resolve_by_port = cfg->fcc_resolve_by_port,
      .resolve_base_port = resolve_base_port,
      .cpuaff = &cfg->cpu_affinity,
      .cpu_idx = cpu_next++,
    };
    if (inet_pton(AF_INET, cfg->listen_addr, rsi_ctx.addr) != 1) {
      fprintf(stderr, "%s: failed to parse -l address for RSI announcement\n", TOOL_NAME);
      rc = 1;
      goto cleanup;
    }
    if (pthread_create(&rsi_thread, NULL, rsi_pacer_main, &rsi_ctx) != 0) {
      fprintf(stderr, "%s: failed to start RSI announcement thread\n", TOOL_NAME);
      rc = 1;
      goto cleanup;
    }
    rsi_started = 1;
  }

  if (metrics_exporter_enabled(mx)) {
    metrics_ctx = (metrics_ctx_t){.mx = mx, .channels = channels, .ret = ret, .bursts = bursts};
    if (pthread_create(&metrics_thread, NULL, metrics_thread_main, &metrics_ctx) != 0) {
      fprintf(stderr, "%s: failed to start metrics thread\n", TOOL_NAME);
      rc = 1;
      goto cleanup;
    }
    metrics_started = 1;
  }

  log_line(TOOL_NAME ": capturing, %u worker(s) on %s:%u, %zu channel slots [%s%s%s%s]", cfg->workers, cfg->listen_addr, cfg->listen_port, max_channels,
      cfg->no_ret ? "no RET" : (mt ? "RET+MC" : "RET unicast-only"), cfg->no_ret || cfg->no_fcc ? "" : ", ", cfg->no_fcc ? "no FCC" : "FCC", rsi_started ? "+RSI" : "");
  capture_run(cap, capture_cb, &dispatch_ctx);

cleanup:
  if (cap) capture_close(cap);
  if (pacer_started) pthread_join(pacer_thread, NULL);
  if (rsi_started) pthread_join(rsi_thread, NULL);
  if (metrics_started) pthread_join(metrics_thread, NULL);
  metrics_exporter_close(mx);
  if (pool) listen_pool_stop(pool);
  if (resolve_pool) listen_multi_stop(resolve_pool);
  if (ret) ret_ctx_free(ret);
  if (bursts) burst_table_free(bursts);
  if (mt) mcsend_table_free(mt);
  if (rsi_mt) mcsend_table_free(rsi_mt);
  if (channels) channel_table_free(channels);
  tsinspect_agg_free(agg);
  return rc;
}
