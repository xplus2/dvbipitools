/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "lib/log.h"
#include "lib/signal.h"

#include "args.h"
#include "channel/channel.h"
#include "listen.h"
#include "run/run.h"
#include "version.h"

#define MC_SEND_TTL 1 /* fixed, no CLI flag */

#define FCC_ASSUMED_MAX_BITRATE_BPS 20000000.0
#define FCC_ASSUMED_TS_PACKET_BYTES 1316.0

static size_t cache_cap_from_gop_ms(unsigned gop_cap_ms) {
  double packets_per_sec = FCC_ASSUMED_MAX_BITRATE_BPS / 8.0 / FCC_ASSUMED_TS_PACKET_BYTES;
  double entries = packets_per_sec * (double)gop_cap_ms / 1000.0;
  return (size_t)entries + 1;
}

int main(int argc, char **argv) {
  config_t cfg;
  args_status_t st;
  size_t max_channels, ring_slots, cache_cap;
  channel_table_t *channels = NULL;
  mcsend_table_t *mt = NULL;
  mcsend_table_t *rsi_mt = NULL;
  ret_ctx_t *ret = NULL;
  burst_table_t *bursts = NULL;
  capture_t *cap = NULL;
  char errbuf[256];
  ret_send_ctx_t ret_send_ctx;
  ret_send_ctx_t rsi_send_ctx;
  dispatch_ctx_t dispatch_ctx;
  listen_pool_t *pool = NULL;
  listen_multi_t *resolve_pool = NULL;
  unsigned resolve_base_port;
  int rc = 0;
  pacer_ctx_t pacer_ctx;
  pthread_t pacer_thread;
  int pacer_started = 0;
  rsi_pacer_ctx_t rsi_ctx;
  pthread_t rsi_thread;
  int rsi_started = 0;
  metrics_exporter_t mx;
  metrics_ctx_t metrics_ctx;
  pthread_t metrics_thread;
  int metrics_started = 0;

  log_set_color(log_color_prescan(argc, argv));
  log_line_ansi("\e[1m%s\e[0m \e[0;32mv%s\e[0m \e[0;37m%s\e[0m \e[0;37m%s\e[0m \e[0;34m%s\e[0m", TOOL_NAME, TOOL_VERSION, BUILD_ARCH, BUILD_TYPE, BUILD_LINK);
  st = args_parse(argc, argv, &cfg);
  if (st == ARGS_OK)
    log_set_color((log_color_t)cfg.color_mode);
  if (st == ARGS_HELP)
    return 0;
  if (st == ARGS_ERR) {
    fprintf(stderr, "try '%s --help' for usage\n", TOOL_NAME);
    return 2;
  }
  if (cfg.daemonize && daemon(1, 1) != 0) {
    log_line(TOOL_NAME ": daemonize failed: %s", strerror(errno));
    return 1;
  }
  metrics_exporter_init(&mx, METRICS_COMPONENT_FCCRET, cfg.metrics_id, cfg.metrics_sock, (double)cfg.metrics_interval_s);

  max_channels = cfg.max_channels ? cfg.max_channels : CHANNEL_DEFAULT_MAX;
  ring_slots = cfg.no_ret ? 0 : cfg.buffer_ms; /* ring_slots ~= buffer_ms: ~1 packet/ms assumption */
  cache_cap = cfg.no_fcc ? 0 : cache_cap_from_gop_ms(cfg.gop_cap_ms);
  channels = channel_table_new(max_channels, ring_slots, cache_cap);
  if (!channels) {
    fprintf(stderr, "%s: out of memory allocating channel table\n", TOOL_NAME);
    rc = 1;
    goto cleanup;
  }

  if (!cfg.no_ret && !cfg.no_mc_ret) {
    mt = mcsend_table_new(max_channels, cfg.iface, MC_SEND_TTL);
    if (!mt) {
      fprintf(stderr, "%s: out of memory allocating MC RET session table\n", TOOL_NAME);
      rc = 1;
      goto cleanup;
    }
  }
  if (!cfg.no_ret && !cfg.no_rsi) {
    if (cfg.listen_family == AF_INET6) {
      log_line(TOOL_NAME ": RSI self-announcement needs an IPv4 -l address (F.5.3 IPv6 unicast feedback is not supported in DVB), disabling it");
    } else if (cfg.rsi_mc_ret) {
      /* dvb-rsi-mc-ret: RSI rides mt itself (F.6.2.2 same group:port), no separate socket */
    } else {
      rsi_mt = mcsend_table_new(max_channels, cfg.iface, MC_SEND_TTL);
      if (!rsi_mt) {
        fprintf(stderr, "%s: out of memory allocating RSI announcement table\n", TOOL_NAME);
        rc = 1;
        goto cleanup;
      }
    }
  }
  if (!cfg.no_fcc) {
    bursts = burst_table_new(cfg.max_bursts);
    if (!bursts) {
      fprintf(stderr, "%s: out of memory allocating burst table\n", TOOL_NAME);
      rc = 1;
      goto cleanup;
    }
  }

  cap = capture_open(cfg.iface, cfg.range_ptrs, cfg.range_count, errbuf, sizeof errbuf);
  if (!cap) {
    fprintf(stderr, "%s: %s\n", TOOL_NAME, errbuf);
    rc = 1;
    goto cleanup;
  }
  if (capture_drop_privileges(cfg.user) != 0) {
    fprintf(stderr, "%s: failed to drop privileges to -u %s\n", TOOL_NAME, cfg.user);
    rc = 1;
    goto cleanup;
  }

  if (!cfg.no_ret) {
    ret_send_ctx.mt = mt;
    ret = ret_ctx_new(channels, cfg.rtx_pt, cfg.max_ret_clients, ret_send_mc_impl, ret_send_unicast_impl, &ret_send_ctx);
    if (!ret) {
      fprintf(stderr, "%s: out of memory creating ret context\n", TOOL_NAME);
      rc = 1;
      goto cleanup;
    }
  }

  dispatch_ctx.channels = channels;
  dispatch_ctx.mt = mt;
  dispatch_ctx.ff_port = cfg.ff_port;
  dispatch_ctx.rsi_mt = rsi_mt;
  dispatch_ctx.rsi_active = rsi_mt != NULL || (cfg.rsi_mc_ret && mt != NULL);
  dispatch_ctx.ret = ret;
  dispatch_ctx.bursts = bursts;
  dispatch_ctx.burst_multiplier = cfg.burst_multiplier;
  dispatch_ctx.duration_cap_ms = cfg.duration_cap_ms;
  dispatch_ctx.max_buffer_fill_bound_ms = cfg.max_buffer_fill_bound_ms;
  dispatch_ctx.congestion_nack_threshold = cfg.congestion_nack_threshold;
  dispatch_ctx.fcc_ranges = cfg.fcc_ranges;
  dispatch_ctx.fcc_range_count = cfg.fcc_range_count;
  dispatch_ctx.fcc_client_ranges = cfg.fcc_client_ranges;
  dispatch_ctx.fcc_client_range_count = cfg.fcc_client_range_count;
  dispatch_ctx.rtx_pt = cfg.rtx_pt;
  dispatch_ctx.idle_timeout_s = cfg.channel_idle_timeout_s;
  dispatch_ctx.ret_client_idle_timeout_s = cfg.ret_client_idle_timeout_s;
  dispatch_ctx.nack_truncated_logged = 0;

  pool = listen_pool_start(cfg.listen_family, cfg.listen_addr, cfg.listen_port, cfg.workers, listen_cb, &dispatch_ctx);
  if (!pool) {
    fprintf(stderr, "%s: failed to start listen workers on %s:%u\n", TOOL_NAME, cfg.listen_addr, cfg.listen_port);
    rc = 1;
    goto cleanup;
  }

  resolve_base_port = cfg.fcc_resolve_base_port ? cfg.fcc_resolve_base_port : cfg.listen_port + 1;
  if (cfg.fcc_resolve_by_port) {
    resolve_pool = listen_multi_start(cfg.listen_family, cfg.listen_addr, resolve_base_port, max_channels, listen_resolve_cb, &dispatch_ctx);
    if (!resolve_pool) {
      fprintf(stderr, "%s: failed to start FCC resolve-by-port sockets at %s:%u..%u\n", TOOL_NAME, cfg.listen_addr, resolve_base_port, resolve_base_port + (unsigned)max_channels - 1);
      rc = 1;
      goto cleanup;
    }
  }

  if (bursts) {
    pacer_ctx.bursts = bursts;
    pacer_ctx.duration_cap_ms = cfg.duration_cap_ms;
    if (pthread_create(&pacer_thread, NULL, pacer_main, &pacer_ctx) != 0) {
      fprintf(stderr, "%s: failed to start burst pacing thread\n", TOOL_NAME);
      rc = 1;
      goto cleanup;
    }
    pacer_started = 1;
  }

  if (rsi_mt || (cfg.rsi_mc_ret && mt)) {
    if (cfg.rsi_mc_ret) {
      rsi_ctx.send_ctx = &ret_send_ctx; /* dvb-rsi-mc-ret: rides mt, F.6.2.2 same group:port */
    } else {
      rsi_send_ctx.mt = rsi_mt;
      rsi_ctx.send_ctx = &rsi_send_ctx;
    }
    rsi_ctx.channels = channels;
    rsi_ctx.interval_s = cfg.rsi_interval_s;
    rsi_ctx.port = (uint16_t)cfg.listen_port;
    rsi_ctx.resolve_by_port = cfg.fcc_resolve_by_port;
    rsi_ctx.resolve_base_port = resolve_base_port;
    rsi_ctx.hostname = cfg.rsi_hostname[0] ? cfg.rsi_hostname : NULL;
    rsi_ctx.hostname_len = strlen(cfg.rsi_hostname);
    if (inet_pton(AF_INET, cfg.listen_addr, rsi_ctx.addr) != 1) {
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

  if (metrics_exporter_enabled(&mx)) {
    metrics_ctx.mx = &mx;
    metrics_ctx.channels = channels;
    metrics_ctx.ret = ret;
    metrics_ctx.bursts = bursts;
    if (pthread_create(&metrics_thread, NULL, metrics_thread_main, &metrics_ctx) != 0) {
      fprintf(stderr, "%s: failed to start metrics thread\n", TOOL_NAME);
      rc = 1;
      goto cleanup;
    }
    metrics_started = 1;
  }

  signals_install();
  log_line(TOOL_NAME ": capturing, %u worker(s) on %s:%u, %zu channel slots [%s%s%s%s]", cfg.workers, cfg.listen_addr, cfg.listen_port, max_channels,
      cfg.no_ret ? "no RET" : (mt ? "RET+MC" : "RET unicast-only"), cfg.no_ret || cfg.no_fcc ? "" : ", ", cfg.no_fcc ? "no FCC" : "FCC", rsi_started ? "+RSI" : "");
  capture_run(cap, capture_cb, &dispatch_ctx);

cleanup:
  if (cap)
    capture_close(cap);
  if (pacer_started)
    pthread_join(pacer_thread, NULL);
  if (rsi_started)
    pthread_join(rsi_thread, NULL);
  if (metrics_started)
    pthread_join(metrics_thread, NULL);
  metrics_exporter_close(&mx);
  if (pool)
    listen_pool_stop(pool);
  if (resolve_pool)
    listen_multi_stop(resolve_pool);
  if (ret)
    ret_ctx_free(ret);
  if (bursts)
    burst_table_free(bursts);
  if (mt)
    mcsend_table_free(mt);
  if (rsi_mt)
    mcsend_table_free(rsi_mt);
  if (channels)
    channel_table_free(channels);
  return rc;
}
