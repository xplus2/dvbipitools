/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lib/demux/psi/psi.h"
#include "lib/demux/tspack.h"
#include "lib/sys/antidebug.h"
#include "lib/helper/log.h"
#include "lib/helper/secure_zero.h"
#include "lib/helper/toolmain.h"
#include "lib/metrics/export.h"
#include "lib/tsinspect/inspect.h"
#include "lib/mux/flv/flv.h"
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
#include "lib/helper/dateutil.h"
#include "lib/helper/strutil.h"

#define DIPIDESCRAMBLE_READS_MAX 64 /* datagrams drained per wakeup, keeps service ticking */
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

static void wipe_cfg_secrets(config_t *cfg) {
  secure_zero(cfg->biss2_sw, sizeof cfg->biss2_sw);
  secure_zero(cfg->biss2_esw, sizeof cfg->biss2_esw);
  secure_zero(cfg->biss2_id, sizeof cfg->biss2_id);
  secure_zero(cfg->biss1_sw, sizeof cfg->biss1_sw);
  secure_zero(cfg->biss1_esw, sizeof cfg->biss1_esw);
  secure_zero(cfg->biss1_id, sizeof cfg->biss1_id);
  secure_zero(cfg->srt_passphrase_in, sizeof cfg->srt_passphrase_in);
  secure_zero(cfg->srt_passphrase, sizeof cfg->srt_passphrase);
}

static void log_startup(const config_t *cfg, char *in_desc, size_t in_desc_sz) {
  char outdesc[2048];
  sbuf_t od;
  sbuf_init(&od, outdesc, sizeof outdesc);
  for (int i = 0; i < cfg->n_out; i++) {
    char one[600];
    out_describe(&cfg->out[i], one, sizeof one);
    if (i) sbuf_add(&od, ",");
    sbuf_add(&od, one);
  }
  input_describe(&cfg->input, in_desc, in_desc_sz);
  log_line(TOOL_NAME ": i:%s k:%s s:%s e:%s o:%s%s", in_desc, cfg->key_path ? cfg->key_path : "(none)", cfg->serial ? cfg->serial : "(none)", cfg->emm_file ? cfg->emm_file : "(none)", outdesc, cfg->unicast_emm_uri ? " unicast-emm:yes" : "");
}

static void source_cfg(tssrc_cfg_t *tc, const config_t *cfg) {
  memset(tc, 0, sizeof *tc);
  tc->kind = tssrc_kind_of(cfg->input.kind);
  tc->user_agent = TOOL_NAME "/" TOOL_VERSION;
  switch (cfg->input.kind) {
    case INPUT_RIST:
      tc->rist_uri = cfg->input.rist_uri;
      tc->rist_profile_main = cfg->rist_profile_main;
      tc->rist_key_size = cfg->rist_key_size;
      break;
    case INPUT_SRT:
      tc->srt_host = cfg->input.srt_host;
      tc->srt_port = cfg->input.srt_port;
      tc->srt_listen = cfg->input.srt_listen;
      tc->srt_passphrase = cfg->srt_passphrase_in;
      tc->srt_pbkeylen = cfg->srt_pbkeylen_in;
      tc->srt_streamid = cfg->srt_streamid_in;
      tc->srt_packetfilter = cfg->srt_packetfilter_in;
      tc->srt_latency_ms = cfg->srt_latency_in_ms;
      tc->srt_verbose = cfg->verbose;
      break;
    case INPUT_RTP:
    case INPUT_UDP:
    case INPUT_STDIN:
      tc->family = cfg->input.family;
      tc->group = cfg->input.group;
      tc->source = cfg->input.source;
      tc->port = cfg->input.port;
      tc->iface = cfg->iface_in;
      break;
  }
}

typedef struct {
  char app_name[64];
  mkv_opts_t mkv;
  flv_opts_t flv;
} mux_opts_t;

/* mo outlives the muxes */
static int start_muxes(const config_t *cfg, loop_ctx_t *lc, mux_opts_t *mo, int mkv_fd, unsigned pmt_pid, const unsigned *all_pids, int n_all_pids, const char *in_desc) {
  memset(mo, 0, sizeof *mo);
  if (cfg->format == FMT_MKV || cfg->format == FMT_MKA) {
    snprintf(mo->app_name, sizeof mo->app_name, "%s %s", TOOL_NAME, TOOL_VERSION);
    mo->mkv.audio_all = 1;
    mo->mkv.app_name = mo->app_name;
    mo->mkv.source_desc = in_desc;
    mo->mkv.strip_lcevc = cfg->strip_lcevc;
    if (n_all_pids > 0) lc->mkv = mkv_new(mkv_fd, &mo->mkv, cfg->format == FMT_MKV, &lc->mkv_bytes, all_pids, n_all_pids);
    else if (pmt_pid) lc->mkv = mkv_new(mkv_fd, &mo->mkv, cfg->format == FMT_MKV, &lc->mkv_bytes, &pmt_pid, 1);
    else lc->mkv = mkv_new(mkv_fd, &mo->mkv, cfg->format == FMT_MKV, &lc->mkv_bytes, NULL, 0);
    if (!lc->mkv) {
      log_code("DS500172", TOOL_NAME ": cannot start mkv/mka mux");
      return -1;
    }
  }
  if (lc->n_rtmp > 0) {
    mo->flv.strip_lcevc = cfg->strip_lcevc;
    lc->flv = flv_new(&mo->flv, pmt_pid, rtmp_fanout_cb, lc, NULL);
    if (!lc->flv) {
      log_code("DS500173", TOOL_NAME ": cannot start flv mux");
      return -1;
    }
  }
  return 0;
}

static void metrics_setup(const config_t *cfg, loop_ctx_t *lc, tssrc_t *src, metrics_exporter_t *mx, tsinspect_set_t *insp_set, srtsink_queue_ctx_t *qctx) {
  metrics_exporter_init(mx, METRICS_COMPONENT_DESCRAMBLE, cfg->metrics_id, cfg->metrics_sock, (double)cfg->metrics_interval_s);
  lc->insp_in = tsinspect_new(cfg->metrics_inspect_ts);
  lc->insp_out = tsinspect_new(cfg->metrics_inspect_ts);
  if (lc->insp_in) {
    tsinspect_bind_psi(lc->insp_in, lc->psi);
    tsinspect_set_known_pids(lc->insp_in, cfg->metrics_known_pids, cfg->metrics_n_known_pids);
  }
  if (lc->insp_out) tsinspect_set_known_pids(lc->insp_out, cfg->metrics_known_pids, cfg->metrics_n_known_pids);
  if (tsinspect_wants_rx_ns(lc->insp_in)) tssrc_enable_rx_timestamps(src);
  if (lc->insp_out && tsinspect_enable_own_psi(lc->insp_out, 0)) {
    tsinspect_free(lc->insp_out);
    lc->insp_out = NULL;
  }
  if (lc->insp_in || lc->insp_out) metrics_exporter_set_extra(mx, tsinspect_set_put, insp_set);
  qctx->sinks = lc->srt;
  qctx->n = (unsigned)lc->n_srt;
  qctx->queue_metrics = metrics_queue_level(cfg->metrics_inspect_ts);
  if (lc->n_srt > 0 && qctx->queue_metrics) metrics_exporter_add_extra(mx, srtsink_put_queue_metrics, qctx);
}

static int read_burst_done(loop_ctx_t *lc, tssrc_t *src, tspack_t *pz, int drain) {
  unsigned char buf[65536];
  for (unsigned reads = 0; reads < DIPIDESCRAMBLE_READS_MAX; reads++) {
    net_err_reason_t reason = NET_ERR_OTHER;
    ssize_t n = tssrc_read(src, buf, sizeof buf, &reason);
    int done;
    if (n < 0) {
      if (reason != NET_ERR_EOF) {
        log_code("DE500248", TOOL_NAME ": input read error: %s", net_err_reason_name(reason));
        lc->fatal = 1;
      }
      return 1;
    }
    if (n == 0) return 0;
    if (lc->insp_in) {
      tsinspect_set_rx_ns(lc->insp_in, tssrc_last_rx_ns(src));
      done = tspack_feed_sync(pz, buf, (size_t)n, pkt_cb_inspect, lc, tsinspect_sync(lc->insp_in));
    } else {
      done = tspack_feed(pz, buf, (size_t)n, pkt_cb, lc);
    }
    if (done) return 1;
    if (!drain) return 0;
  }
  return 0;
}

static void run_loop(const config_t *cfg, loop_ctx_t *lc, tssrc_t *src, metrics_exporter_t *mx) {
  tspack_t pz;
  double start = mono_seconds();
  double last_stat = start;
  int drain = tssrc_set_nonblock(src);
  memset(&pz, 0, sizeof pz);
  while (!signal_stop_requested()) {
    int pr;
    if (lc->insp_in || lc->insp_out) {
      double now = mono_seconds();
      if (lc->insp_in) tsinspect_tick(lc->insp_in, now);
      if (lc->insp_out) tsinspect_tick(lc->insp_out, now);
    }
    if (metrics_exporter_enabled(mx)) pipeline_push_metrics(mx, lc);
    srt_service_all(lc);
    rtmp_service_all(lc);
    pipeline_service_unicast_emm(lc);
    pipeline_service_emmcache(lc);

    pr = tssrc_wait(src, 100); /* bounded: keeps srt/rtmp service ticking on quiet input too. sees replay */
    if (pr < 0) {
      log_code("DE500247", TOOL_NAME ": input poll failed: %s", strerror(errno));
      lc->fatal = 1;
      return;
    }
    if (pr == 0) {
      pipeline_flush(lc); /* idle: release held batches */
      if (lc->emit_failed) return;
      continue;
    }
    if (read_burst_done(lc, src, &pz, drain)) return;
    if (cfg->verbose && mono_seconds() - last_stat >= 1.0) {
      log_line(TOOL_NAME ": %llu packets, %.0fs elapsed", lc->packets, mono_seconds() - start);
      last_stat = mono_seconds();
    }
  }
}

int main(int argc, char **argv) {
  config_t cfg = {0};
  tssrc_cfg_t tc;
  tssrc_t *src = NULL;
  loop_ctx_t lc;
  tsinspect_set_t insp_set = {&lc.insp_in, 1, &lc.insp_out, 1};
  srtsink_queue_ctx_t qctx;
  metrics_exporter_t mx;
  mux_opts_t mux_opts;
  char in_desc[128];
  unsigned pmt_pid;
  unsigned all_pids[PSI_MAX_PROGRAMS];
  int n_all_pids;
  int mkv_fd = -1;
  int rc = 1;

  memset(&lc, 0, sizeof lc);
  antidebug_install();
  TOOLMAIN_STARTUP(argc, argv, &cfg, args_parse);
  if (toolmain_daemonize(cfg.daemonize, TOOL_NAME)) {
    wipe_cfg_secrets(&cfg);
    return 1;
  }
  log_startup(&cfg, in_desc, sizeof in_desc);

  signals_install();
  lc.cache = emmcache_new(cfg.max_services);
  if (!lc.cache) goto cleanup;

  source_cfg(&tc, &cfg);
  src = tssrc_open_blocking(&tc, 30.0, NULL);
  if (!src) {
    log_code("DS000420", TOOL_NAME ": cannot open -i %s", in_desc);
    goto cleanup;
  }

  if (dscr_resolve_pmt_selection(&cfg, src, &pmt_pid, all_pids, &n_all_pids))
    goto cleanup;
  if (!tssrc_rewind(src))
    log_code("DA000711", TOOL_NAME ": probe consumed the stream head, input starts later");

  if (dscr_open_outputs(&cfg, &lc, &mkv_fd) || start_muxes(&cfg, &lc, &mux_opts, mkv_fd, pmt_pid, all_pids, n_all_pids, in_desc))
    goto cleanup;
  lc.emm_file = cfg.emm_file;
  lc.cfg = &cfg;
  lc.psi = psi_new();
  if (!lc.psi)
    goto cleanup;
  if (pmt_pid) psi_select_pmt_pid(lc.psi, pmt_pid); /* CW derivation is mux-wide either way, nicer stats only */

  metrics_setup(&cfg, &lc, src, &mx, &insp_set, &qctx);
  run_loop(&cfg, &lc, src, &mx);

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
  if (flv_close(lc.flv) < 0 || mkv_close(lc.mkv) < 0) {
    lc.emit_failed = 1;
    rc = 1;
  }
  dscr_close_outputs(&lc, mkv_fd);
  tssrc_close(src);
  emmcache_free(lc.cache);
  device_state_free(lc.dev);
  biss_ca_state_free(lc.biss_ca);
  secure_zero(lc.last_cw, sizeof lc.last_cw);
  wipe_cfg_secrets(&cfg);
  return rc;
}
