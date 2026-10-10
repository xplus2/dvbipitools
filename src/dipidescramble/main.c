/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lib/demux/mpts_probe.h"
#include "lib/demux/psi/psi.h"
#include "lib/demux/tspack.h"
#include "lib/sys/antidebug.h"
#include "lib/helper/log.h"
#include "lib/helper/secure_zero.h"
#include "lib/helper/toolmain.h"
#include "lib/metrics/export.h"
#include "lib/tsinspect/inspect.h"
#include "lib/mux/flv/flv.h"
#include "lib/net/rtmp/rtmpout.h"
#include "lib/net/ts/source.h"
#include "lib/sys/signal.h"

#include "cli/args.h"
#include "biss_ca_state.h"
#include "device.h"
#include "emmcache.h"
#include "ipiclient.h"
#include "outputs.h"
#include "pipeline.h"
#include "pmt_select.h"
#include "version.h"

#define DIPIDESCRAMBLE_SRT_DRAIN_MS_DEFAULT 1000 /* srt's own default latency buffer */

static tssrc_kind_t tssrc_kind_of(input_kind_t k) {
  switch (k) {
    case INPUT_RTP:       return TSSRC_RTP;
    case INPUT_UDP:       return TSSRC_UDP;
    case INPUT_STDIN:     return TSSRC_STDIN;
    case INPUT_RIST:      return TSSRC_RIST;
    case INPUT_SRT:       return TSSRC_SRT;
  }
  return TSSRC_STDIN;
}

int main(int argc, char **argv) {
  config_t cfg;
  tssrc_cfg_t tc;
  tssrc_t *src = NULL;
  tspack_t pz;
  loop_ctx_t lc;
  tsinspect_set_t insp_set = {&lc.insp_in, 1, &lc.insp_out, 1};
  srtsink_queue_ctx_t qctx;
  metrics_exporter_t mx;
  unsigned char buf[65536];
  char mkv_app_name[64];
  mkv_opts_t mkv_opts;
  flv_opts_t flv_opts;
  double start;
  double last_stat;
  char in_desc[128];
  char outdesc[2048];
  unsigned pmt_pid;
  unsigned all_pids[PSI_MAX_PROGRAMS];
  int n_all_pids;
  int mkv_fd = -1;
  int rc = 1;
  int on = 0;

  memset(&lc, 0, sizeof lc);
  antidebug_install();
  TOOLMAIN_STARTUP(argc, argv, &cfg, args_parse);
  if (toolmain_daemonize(cfg.daemonize, TOOL_NAME)) return 1;

  for (int i = 0; i < cfg.n_out; i++) {
    char one[600];
    int r;
    out_describe(&cfg.out[i], one, sizeof one);
    r = snprintf(outdesc + on, sizeof outdesc - (size_t)on, "%s%s", i ? "," : "", one);
    if (r > 0 && (size_t)on + (size_t)r < sizeof outdesc) on += r;
  }
  input_describe(&cfg.input, in_desc, sizeof in_desc);
  log_line(TOOL_NAME ": i:%s k:%s s:%s e:%s o:%s%s", in_desc, cfg.key_path ? cfg.key_path : "(none)", cfg.serial ? cfg.serial : "(none)", cfg.emm_file ? cfg.emm_file : "(none)", outdesc, cfg.unicast_emm_uri ? " unicast-emm:yes" : "");

  lc.cache = emmcache_new(cfg.max_services);
  if (!lc.cache) goto cleanup;

  memset(&tc, 0, sizeof tc);
  tc.kind = tssrc_kind_of(cfg.input.kind);
  tc.user_agent = TOOL_NAME "/" TOOL_VERSION;
  switch (cfg.input.kind) {
    case INPUT_RIST:
      tc.rist_uri = cfg.input.rist_uri;
      tc.rist_profile_main = cfg.rist_profile_main;
      tc.rist_key_size = cfg.rist_key_size;
      break;
    case INPUT_SRT:
      tc.srt_host = cfg.input.srt_host;
      tc.srt_port = cfg.input.srt_port;
      tc.srt_listen = cfg.input.srt_listen;
      tc.srt_passphrase = cfg.srt_passphrase_in;
      tc.srt_pbkeylen = cfg.srt_pbkeylen_in;
      tc.srt_streamid = cfg.srt_streamid_in;
      tc.srt_packetfilter = cfg.srt_packetfilter_in;
      tc.srt_latency_ms = cfg.srt_latency_in_ms;
      tc.srt_verbose = cfg.verbose;
      break;
    case INPUT_RTP:
    case INPUT_UDP:
    case INPUT_STDIN:
      tc.family = cfg.input.family;
      tc.group = cfg.input.group;
      tc.source = cfg.input.source;
      tc.port = cfg.input.port;
      tc.iface = cfg.iface_in;
      break;
  }

  src = tssrc_open(&tc, NULL);
  if (!src) {
    log_line(TOOL_NAME ": cannot open -i %s", in_desc);
    goto cleanup;
  }

  if (dscr_resolve_pmt_selection(&cfg, src, &pmt_pid, all_pids, &n_all_pids))
    goto cleanup;

  if (dscr_open_outputs(&cfg, &lc, &mkv_fd))
    goto cleanup;
  if (cfg.format == FMT_MKV || cfg.format == FMT_MKA) {
    snprintf(mkv_app_name, sizeof mkv_app_name, "%s %s", TOOL_NAME, TOOL_VERSION);
    memset(&mkv_opts, 0, sizeof mkv_opts);
    mkv_opts.audio_all = 1;
    mkv_opts.app_name = mkv_app_name;
    mkv_opts.source_desc = in_desc;
    mkv_opts.strip_lcevc = cfg.strip_lcevc;
    if (n_all_pids > 0)
      lc.mkv = mkv_new(mkv_fd, &mkv_opts, cfg.format == FMT_MKV, &lc.mkv_bytes, all_pids, n_all_pids);
    else if (pmt_pid)
      lc.mkv = mkv_new(mkv_fd, &mkv_opts, cfg.format == FMT_MKV, &lc.mkv_bytes, &pmt_pid, 1);
    else
      lc.mkv = mkv_new(mkv_fd, &mkv_opts, cfg.format == FMT_MKV, &lc.mkv_bytes, NULL, 0);
    if (!lc.mkv) {
      log_line(TOOL_NAME ": cannot start mkv/mka mux");
      goto cleanup;
    }
  }
  if (lc.n_rtmp > 0) {
    memset(&flv_opts, 0, sizeof flv_opts);
    flv_opts.strip_lcevc = cfg.strip_lcevc;
    lc.flv = flv_new(&flv_opts, pmt_pid, rtmp_fanout_cb, &lc, NULL);
    if (!lc.flv) {
      log_line(TOOL_NAME ": cannot start flv mux");
      goto cleanup;
    }
  }
  lc.emm_file = cfg.emm_file;
  lc.cfg = &cfg;
  lc.psi = psi_new();
  if (!lc.psi)
    goto cleanup;
  if (pmt_pid) psi_select_pmt_pid(lc.psi, pmt_pid); /* CW derivation is mux-wide either way, nicer stats only */

  memset(&pz, 0, sizeof pz);
  signals_install();
  start = last_stat = mono_seconds();
  metrics_exporter_init(&mx, METRICS_COMPONENT_DESCRAMBLE, cfg.metrics_id, cfg.metrics_sock, (double)cfg.metrics_interval_s);
  lc.insp_in = tsinspect_new(cfg.metrics_inspect_ts);
  lc.insp_out = tsinspect_new(cfg.metrics_inspect_ts);
  if (lc.insp_in) {
    tsinspect_bind_psi(lc.insp_in, lc.psi);
    tsinspect_set_known_pids(lc.insp_in, cfg.metrics_known_pids, cfg.metrics_n_known_pids);
  }
  if (lc.insp_out) tsinspect_set_known_pids(lc.insp_out, cfg.metrics_known_pids, cfg.metrics_n_known_pids);
  if (tsinspect_wants_rx_ns(lc.insp_in)) tssrc_enable_rx_timestamps(src);
  if (lc.insp_out && tsinspect_enable_own_psi(lc.insp_out, 0)) {
    tsinspect_free(lc.insp_out);
    lc.insp_out = NULL;
  }
  if (lc.insp_in || lc.insp_out) metrics_exporter_set_extra(&mx, tsinspect_set_put, &insp_set);
  qctx.sinks = lc.srt;
  qctx.n = (unsigned)lc.n_srt;
  qctx.queue_metrics = metrics_queue_level(cfg.metrics_inspect_ts);
  if (lc.n_srt > 0 && qctx.queue_metrics) metrics_exporter_add_extra(&mx, srtsink_put_queue_metrics, &qctx);

  while (!signal_stop_requested()) {
    struct pollfd pfd;
    ssize_t n;
    int pr;
    if (lc.insp_in || lc.insp_out) {
      double now = mono_seconds();
      if (lc.insp_in) tsinspect_tick(lc.insp_in, now);
      if (lc.insp_out) tsinspect_tick(lc.insp_out, now);
    }
    srt_service_all(&lc);
    pipeline_service_unicast_emm(&lc);
    pipeline_service_emmcache(&lc);

    pfd.fd = tssrc_fd(src);
    pfd.events = POLLIN;
    pr = poll(&pfd, 1, 100); /* bounded: keeps srt_service_all() ticking on quiet input too */
    if (pr < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (pr == 0) continue;

    n = tssrc_read(src, buf, sizeof buf, NULL);
    if (n < 0) break;
    if (n == 0) continue;
    if (lc.insp_in) {
      tsinspect_set_rx_ns(lc.insp_in, tssrc_last_rx_ns(src));
      if (tspack_feed_sync(&pz, buf, (size_t)n, pkt_cb_inspect, &lc, tsinspect_sync(lc.insp_in))) break;
    } else if (tspack_feed(&pz, buf, (size_t)n, pkt_cb, &lc)) {
      break;
    }
    if (cfg.verbose && mono_seconds() - last_stat >= 1.0) {
      log_line(TOOL_NAME ": %llu packets, %.0fs elapsed", lc.packets, mono_seconds() - start);
      last_stat = mono_seconds();
    }
    if (metrics_exporter_enabled(&mx)) pipeline_push_metrics(&mx, &lc);
  }

  metrics_exporter_clear_extras(&mx);
  metrics_exporter_close(&mx);
  pipeline_flush(&lc);
  pipeline_flush_emmcache(&lc);
  if (!signal_stop_requested() && lc.n_srt > 0) {
    /* eof/err, not live stop: drain srt retransmits before teardown */
    unsigned drain_ms = cfg.srt_latency_ms ? cfg.srt_latency_ms : DIPIDESCRAMBLE_SRT_DRAIN_MS_DEFAULT;
    struct timespec ts = {(time_t)(drain_ms / 1000), (long)(drain_ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
  }
  rc = (lc.fatal || lc.emit_failed) ? 1 : 0;

cleanup:
  tsinspect_free(lc.insp_in);
  tsinspect_free(lc.insp_out);
  ipiclient_poll_free(lc.ipi_pending);
  ipiclient_free(lc.ipi);
  scrambler_free(lc.scr);
  psi_free(lc.psi);
  flv_close(lc.flv);
  mkv_close(lc.mkv);
  dscr_close_outputs(&lc, mkv_fd);
  tssrc_close(src);
  emmcache_free(lc.cache);
  device_state_free(lc.dev);
  biss_ca_state_free(lc.biss_ca);
  secure_zero(lc.last_cw, sizeof lc.last_cw);
  return rc;
}
