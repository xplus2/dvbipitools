/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "lib/demux/mpts_probe.h"
#include "lib/demux/psi/psi.h"
#include "lib/demux/tspack.h"
#include "lib/log.h"
#include "lib/metrics/export.h"
#include "lib/mux/flv/flv.h"
#include "lib/net/rtmp/rtmpout.h"
#include "lib/net/tssource.h"
#include "lib/signal.h"

#include "args.h"
#include "biss_ca_state.h"
#include "device.h"
#include "emmcache.h"
#include "ipiclient.h"
#include "pipeline.h"
#include "version.h"

#define MPTS_NAME_WAIT_MS 3000

static int open_output(const char *path) {
  int fd;
  if (strcmp(path, "-") == 0)
    return STDOUT_FILENO;
  fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
    log_line(TOOL_NAME ": cannot open -o %s: %s", path, strerror(errno));
  return fd;
}

static tssrc_kind_t tssrc_kind_of(input_kind_t k) {
  switch (k) {
  case INPUT_RTP:
    return TSSRC_RTP;
  case INPUT_UDP:
    return TSSRC_UDP;
  case INPUT_STDIN:
    return TSSRC_STDIN;
  case INPUT_RIST:
    return TSSRC_RIST;
  }
  return TSSRC_STDIN;
}

static int cfg_has_rtmp(const config_t *cfg) {
  for (int i = 0; i < cfg->n_out; i++)
    if (cfg->out[i].kind == OUT_RTMP || cfg->out[i].kind == OUT_RTMPS)
      return 1;
  return 0;
}

/* mpts discovery + -p decision. 0: proceed (pmt_pid/all_pids/n_all_pids
 * filled in). 1: abort, message already printed. */
static int resolve_pmt_selection(const config_t *cfg, tssrc_t *src, unsigned *pmt_pid,
                                  unsigned *all_pids, int *n_all_pids) {
  mpts_probe_result_t probe;

  *pmt_pid = 0;
  *n_all_pids = 0;

  probe = mpts_probe_run(src, MPTS_NAME_WAIT_MS);
  if (probe.kind == MPTS_PROBE_FAIL) {
    log_line(TOOL_NAME ": no PAT received, giving up");
    return 1;
  }
  if (probe.kind == MPTS_PROBE_SPTS) {
    if (cfg->pmt_sel != PMT_SEL_AUTO)
      log_line(TOOL_NAME ": -p ignored, single-program source");
    return 0;
  }

  if (cfg->pmt_sel == PMT_SEL_AUTO) {
    mpts_probe_print_programs(TOOL_NAME, &probe);
    log_line(TOOL_NAME ": MPTS source, pick one with -p <pid>, or -p all");
    return 1;
  }
  if (cfg->pmt_sel == PMT_SEL_ALL) {
    if (cfg->format == FMT_MKV || cfg_has_rtmp(cfg)) {
      log_line(TOOL_NAME ": -f mkv/-o rtmp:// can't hold multiple programs, pick one with -p <pid>");
      mpts_probe_print_programs(TOOL_NAME, &probe);
      return 1;
    }
    for (int k = 0; k < probe.program_count; k++)
      all_pids[(*n_all_pids)++] = probe.programs[k].pmt_pid;
    return 0;
  }
  for (int k = 0; k < probe.program_count; k++)
    if (probe.programs[k].pmt_pid == cfg->pmt_pid) {
      *pmt_pid = cfg->pmt_pid;
      return 0;
    }
  log_line(TOOL_NAME ": -p 0x%04x not found in this MPTS", cfg->pmt_pid);
  mpts_probe_print_programs(TOOL_NAME, &probe);
  return 1;
}

/* opens every -o target: plain files into lc->outfd[] (or one -f mkv/mka
   file into *mkv_fd), rtmp(s) targets into lc->rtmp[]. 0 ok, -1 fail
   (message already logged, caller closes whatever this left open via lc) */
static int open_outputs(const config_t *cfg, loop_ctx_t *lc, int *mkv_fd) {
  int is_mkv_fmt = (cfg->format == FMT_MKV || cfg->format == FMT_MKA);

  lc->n_outfd = 0;
  lc->n_rtmp = 0;
  *mkv_fd = -1;

  for (int i = 0; i < cfg->n_out; i++) {
    const out_target_t *o = &cfg->out[i];

    if (o->kind == OUT_RTMP || o->kind == OUT_RTMPS) {
      rtmpout_cfg_t rc;
      memset(&rc, 0, sizeof rc);
      rc.url = o->rtmp_url;
      rc.insecure = cfg->insecure_tls;
      lc->rtmp[lc->n_rtmp] = rtmpout_open(&rc);
      if (!lc->rtmp[lc->n_rtmp])
        return -1;
      lc->rtmp_had_error[lc->n_rtmp] = 0;
      lc->n_rtmp++;
      continue;
    }
    if (is_mkv_fmt) {
      *mkv_fd = open_output(o->file_path);
      if (*mkv_fd < 0)
        return -1;
      continue;
    }
    lc->outfd[lc->n_outfd] = open_output(o->file_path);
    if (lc->outfd[lc->n_outfd] < 0)
      return -1;
    lc->n_outfd++;
  }
  return 0;
}

static void push_metrics(metrics_exporter_t *mx, const loop_ctx_t *lc) {
  metrics_writer_t w;

  if (!metrics_exporter_due(mx, mono_seconds()) || metrics_exporter_begin(mx, &w, TOOL_VERSION))
    return;
  if (lc->cas_mode) {
    metrics_writer_put(&w, METRICS_ID_DESCRAMBLE_MODE, lc->cas_mode, 1);
    metrics_writer_put(&w, METRICS_ID_CAS_CRYPTOPERIOD_TRANSITIONS_TOTAL, lc->cas_mode, lc->cryptoperiod_transitions_total);
    metrics_writer_put(&w, METRICS_ID_CAS_ECM_TOTAL, lc->cas_mode, lc->ecm_total);
    metrics_writer_put(&w, METRICS_ID_CAS_ECM_ERRORS_TOTAL, lc->cas_mode, lc->ecm_errors_total);
    metrics_writer_put(&w, METRICS_ID_CAS_EMM_TOTAL, lc->cas_mode, lc->emm_total);
    metrics_writer_put(&w, METRICS_ID_CAS_EMM_DROPPED_TOTAL, lc->cas_mode, lc->dev ? emmcache_dropped_total(lc->cache) : 0);
  }
  metrics_writer_put(&w, METRICS_ID_CAS_SCRAMBLED_PACKETS_TOTAL, NULL, lc->scrambled_packets_total);
  metrics_writer_put(&w, METRICS_ID_CAS_UNEXPECTED_CLEAR_PACKETS_TOTAL, NULL, lc->unexpected_clear_packets_total);
  metrics_writer_put(&w, METRICS_ID_DESCRAMBLE_KEY_LOAD_ERRORS_TOTAL, NULL, lc->key_load_errors_total);
  metrics_writer_put(&w, METRICS_ID_DESCRAMBLE_OUTPUT_ERRORS_TOTAL, NULL, lc->output_errors_total);
  metrics_exporter_send(mx, &w);
}

static void close_outputs(loop_ctx_t *lc, int mkv_fd) {
  for (int i = 0; i < lc->n_rtmp; i++)
    rtmpout_close(lc->rtmp[i]);
  for (int i = 0; i < lc->n_outfd; i++)
    if (lc->outfd[i] != STDOUT_FILENO)
      close(lc->outfd[i]);
  if (mkv_fd >= 0 && mkv_fd != STDOUT_FILENO)
    close(mkv_fd);
}

int main(int argc, char **argv) {
  config_t cfg;
  args_status_t st;
  tssrc_cfg_t tc;
  tssrc_t *src = NULL;
  tspack_t pz;
  loop_ctx_t lc;
  metrics_exporter_t mx;
  unsigned char buf[65536];
  char mkv_app_name[64];
  mkv_opts_t mkv_opts;
  flv_opts_t flv_opts;
  double start, last_stat;
  char in_desc[128], outdesc[2048];
  unsigned pmt_pid, all_pids[PSI_MAX_PROGRAMS];
  int n_all_pids;
  int mkv_fd = -1;
  int rc = 1;

  memset(&lc, 0, sizeof lc);

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

  {
    int on = 0;
    for (int i = 0; i < cfg.n_out; i++) {
      char one[600];
      int r;
      out_describe(&cfg.out[i], one, sizeof one);
      r = snprintf(outdesc + on, sizeof outdesc - (size_t)on, "%s%s", i ? "," : "", one);
      if (r > 0 && (size_t)on + (size_t)r < sizeof outdesc)
        on += r;
    }
  }
  input_describe(&cfg.input, in_desc, sizeof in_desc);
  log_line(TOOL_NAME ": i:%s k:%s s:%s e:%s o:%s%s", in_desc, cfg.key_path ? cfg.key_path : "(none)", cfg.serial ? cfg.serial : "(none)", cfg.emm_file ? cfg.emm_file : "(none)", outdesc, cfg.unicast_emm_uri ? " unicast-emm:yes" : "");

  lc.cache = emmcache_new(cfg.max_services);
  if (!lc.cache)
    goto cleanup;

  memset(&tc, 0, sizeof tc);
  tc.kind = tssrc_kind_of(cfg.input.kind);
  tc.user_agent = TOOL_NAME "/" TOOL_VERSION;
  if (cfg.input.kind == INPUT_RIST) {
    tc.rist_uri = cfg.input.rist_uri;
    tc.rist_profile_main = cfg.rist_profile_main;
  } else {
    tc.family = cfg.input.family;
    tc.group = cfg.input.group;
    tc.port = cfg.input.port;
    tc.iface = cfg.iface_in;
  }

  src = tssrc_open(&tc, NULL);
  if (!src) {
    log_line(TOOL_NAME ": cannot open -i %s", in_desc);
    goto cleanup;
  }

  if (resolve_pmt_selection(&cfg, src, &pmt_pid, all_pids, &n_all_pids))
    goto cleanup;

  if (open_outputs(&cfg, &lc, &mkv_fd))
    goto cleanup;
  if (cfg.format == FMT_MKV || cfg.format == FMT_MKA) {
    snprintf(mkv_app_name, sizeof mkv_app_name, "%s %s", TOOL_NAME, TOOL_VERSION);
    memset(&mkv_opts, 0, sizeof mkv_opts);
    mkv_opts.audio_all = 1;
    mkv_opts.app_name = mkv_app_name;
    mkv_opts.source_desc = in_desc;
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
  if (pmt_pid)
    psi_select_pmt_pid(lc.psi, pmt_pid); /* CW derivation is mux-wide either way, nicer stats only */

  memset(&pz, 0, sizeof pz);
  signals_install();
  start = last_stat = mono_seconds();
  metrics_exporter_init(&mx, METRICS_COMPONENT_DESCRAMBLE, cfg.metrics_id, cfg.metrics_sock, (double)cfg.metrics_interval_s);

  while (!signal_stop_requested()) {
    ssize_t n = tssrc_read(src, buf, sizeof buf, NULL);
    if (n < 0)
      break;
    if (n == 0)
      continue;
    if (tspack_feed(&pz, buf, (size_t)n, pkt_cb, &lc))
      break;
    if (cfg.verbose && mono_seconds() - last_stat >= 1.0) {
      log_line(TOOL_NAME ": %llu packets, %.0fs elapsed", lc.packets, mono_seconds() - start);
      last_stat = mono_seconds();
    }
    if (metrics_exporter_enabled(&mx))
      push_metrics(&mx, &lc);
  }

  metrics_exporter_close(&mx);
  pipeline_flush(&lc);
  rc = (lc.fatal || lc.emit_failed) ? 1 : 0;

cleanup:
  ipiclient_free(lc.ipi);
  scrambler_free(lc.scr);
  psi_free(lc.psi);
  flv_close(lc.flv);
  mkv_close(lc.mkv);
  close_outputs(&lc, mkv_fd);
  tssrc_close(src);
  emmcache_free(lc.cache);
  device_state_free(lc.dev);
  biss_ca_state_free(lc.biss_ca);
  return rc;
}
