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
  }
  return TSSRC_STDIN;
}

/* mpts discovery + -p decision. 0: proceed (pmt_pid/all_pids/n_all_pids
 * filled in). 1: abort, message already printed. */
static int resolve_pmt_selection(const config_t *cfg, tssrc_t *src, unsigned *pmt_pid,
                                  unsigned *all_pids, int *n_all_pids) {
  mpts_probe_result_t probe;
  int k;

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
    log_line(TOOL_NAME ": MPTS source - pick one with -p <pid>, or -p all");
    return 1;
  }
  if (cfg->pmt_sel == PMT_SEL_ALL) {
    if (cfg->format == FMT_MKV) {
      log_line(TOOL_NAME ": -f mkv can't hold multiple programs, pick one with -p <pid>");
      mpts_probe_print_programs(TOOL_NAME, &probe);
      return 1;
    }
    for (k = 0; k < probe.program_count; k++)
      all_pids[(*n_all_pids)++] = probe.programs[k].pmt_pid;
    return 0;
  }
  for (k = 0; k < probe.program_count; k++)
    if (probe.programs[k].pmt_pid == cfg->pmt_pid) {
      *pmt_pid = cfg->pmt_pid;
      return 0;
    }
  log_line(TOOL_NAME ": -p 0x%04x not found in this MPTS", cfg->pmt_pid);
  mpts_probe_print_programs(TOOL_NAME, &probe);
  return 1;
}

int main(int argc, char **argv) {
  config_t cfg;
  args_status_t st;
  tssrc_cfg_t tc;
  tssrc_t *src;
  tspack_t pz;
  loop_ctx_t lc;
  unsigned char buf[65536];
  char mkv_app_name[64];
  mkv_opts_t mkv_opts;
  double start, last_stat;
  char in_desc[128];
  unsigned pmt_pid, all_pids[PSI_MAX_PROGRAMS];
  int n_all_pids;

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

  input_describe(&cfg.input, in_desc, sizeof in_desc);
  log_line(TOOL_NAME ": i:%s k:%s s:%s e:%s o:%s%s", in_desc, cfg.key_path ? cfg.key_path : "(none)", cfg.serial ? cfg.serial : "(none)", cfg.emm_file ? cfg.emm_file : "(none)", cfg.out_path, cfg.unicast_emm_uri ? " unicast-emm:yes" : "");

  lc.dev = NULL; /* lazily created once the stream reveals it needs ECM/EMM-driven CAS */
  lc.biss_ca = NULL; /* lazily created once the stream reveals BISS Mode CA */
  lc.ipi = NULL;
  lc.cache = emmcache_new();
  if (!lc.cache)
    return 1;

  memset(&tc, 0, sizeof tc);
  tc.kind = tssrc_kind_of(cfg.input.kind);
  tc.family = cfg.input.family;
  tc.group = cfg.input.group;
  tc.port = cfg.input.port;
  tc.iface = cfg.iface_in;
  tc.user_agent = TOOL_NAME "/" TOOL_VERSION;

  src = tssrc_open(&tc, NULL);
  if (!src) {
    log_line(TOOL_NAME ": cannot open -i %s", in_desc);
    emmcache_free(lc.cache);
    device_state_free(lc.dev);
    biss_ca_state_free(lc.biss_ca);
    return 1;
  }

  if (resolve_pmt_selection(&cfg, src, &pmt_pid, all_pids, &n_all_pids)) {
    tssrc_close(src);
    emmcache_free(lc.cache);
    device_state_free(lc.dev);
    biss_ca_state_free(lc.biss_ca);
    return 1;
  }

  lc.out = open_output(cfg.out_path);
  if (lc.out < 0) {
    tssrc_close(src);
    emmcache_free(lc.cache);
    device_state_free(lc.dev);
    biss_ca_state_free(lc.biss_ca);
    return 1;
  }
  lc.mkv = NULL;
  lc.mkv_bytes = 0;
  if (cfg.format == FMT_MKV || cfg.format == FMT_MKA) {
    snprintf(mkv_app_name, sizeof mkv_app_name, "%s %s", TOOL_NAME, TOOL_VERSION);
    memset(&mkv_opts, 0, sizeof mkv_opts);
    mkv_opts.audio_all = 1;
    mkv_opts.app_name = mkv_app_name;
    mkv_opts.source_desc = in_desc;
    if (n_all_pids > 0)
      lc.mkv = mkv_new(lc.out, &mkv_opts, cfg.format == FMT_MKV, &lc.mkv_bytes, all_pids, n_all_pids);
    else if (pmt_pid)
      lc.mkv = mkv_new(lc.out, &mkv_opts, cfg.format == FMT_MKV, &lc.mkv_bytes, &pmt_pid, 1);
    else
      lc.mkv = mkv_new(lc.out, &mkv_opts, cfg.format == FMT_MKV, &lc.mkv_bytes, NULL, 0);
    if (!lc.mkv) {
      log_line(TOOL_NAME ": cannot start mkv/mka mux");
      if (lc.out != STDOUT_FILENO)
        close(lc.out);
      tssrc_close(src);
      emmcache_free(lc.cache);
      device_state_free(lc.dev);
      biss_ca_state_free(lc.biss_ca);
      return 1;
    }
  }
  lc.packets = 0;
  lc.ecm_pid = 0;
  lc.emm_pid = 0;
  lc.cas_logged = 0;
  lc.emm_file = cfg.emm_file;
  lc.cfg = &cfg;
  memset(&lc.ecm_asm, 0, sizeof lc.ecm_asm);
  memset(&lc.emm_asm, 0, sizeof lc.emm_asm);
  lc.scr = NULL;
  lc.cw_len = 0;
  lc.have_cw[0] = lc.have_cw[1] = 0;
  lc.emit_failed = 0;
  lc.fatal = 0;
  lc.psi = psi_new();
  if (!lc.psi) {
    if (lc.out != STDOUT_FILENO)
      close(lc.out);
    tssrc_close(src);
    emmcache_free(lc.cache);
    device_state_free(lc.dev);
    biss_ca_state_free(lc.biss_ca);
    return 1;
  }
  if (pmt_pid)
    psi_select_pmt_pid(lc.psi, pmt_pid); /* CW derivation is mux-wide either way; nicer stats only */

  memset(&pz, 0, sizeof pz);
  signals_install();
  start = last_stat = mono_seconds();

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
  }

  pipeline_flush(&lc);
  ipiclient_free(lc.ipi);
  scrambler_free(lc.scr);
  psi_free(lc.psi);
  if (lc.mkv)
    mkv_close(lc.mkv);
  if (lc.out != STDOUT_FILENO)
    close(lc.out);
  tssrc_close(src);
  emmcache_free(lc.cache);
  device_state_free(lc.dev);
  biss_ca_state_free(lc.biss_ca);
  return (lc.fatal || lc.emit_failed) ? 1 : 0;
}
