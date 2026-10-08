/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/demux/mpts_probe.h"
#include "lib/helper/log.h"

#include "pmt_select.h"
#include "version.h"

#define MPTS_NAME_WAIT_MS 3000

static int cfg_has_rtmp(const config_t *cfg) {
  for (int i = 0; i < cfg->n_out; i++) {
    if (cfg->out[i].kind == OUT_RTMP || cfg->out[i].kind == OUT_RTMPS) return 1;
  }
  return 0;
}

/* mpts discovery + -p decision. 0: proceed (pmt_pid/all_pids/n_all_pids filled in). 1: abort, message already printed. */
int dscr_resolve_pmt_selection(const config_t *cfg, tssrc_t *src, unsigned *pmt_pid, unsigned *all_pids, int *n_all_pids) {
  mpts_probe_result_t probe;
  *pmt_pid = 0;
  *n_all_pids = 0;

  probe = mpts_probe_run(src, MPTS_NAME_WAIT_MS);
  if (probe.kind == MPTS_PROBE_FAIL) {
    log_line(TOOL_NAME ": no PAT received, giving up");
    return 1;
  }
  if (probe.kind == MPTS_PROBE_SPTS) {
    if (cfg->pmt_sel != PMT_SEL_AUTO) log_line(TOOL_NAME ": -p ignored, single-program source");
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
    for (int k = 0; k < probe.program_count; k++) all_pids[(*n_all_pids)++] = probe.programs[k].pmt_pid;
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
