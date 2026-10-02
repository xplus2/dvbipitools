/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <unistd.h>

#include "lib/helper/argutil.h"

#include "priv.h"

args_status_t fccret_cli_check(config_t *cfg) {
  if (!cfg->range_count) {
    argerr("missing -g range");
    return ARGS_ERR;
  }
  if (!cfg->listen_port) {
    argerr("missing -l listen");
    return ARGS_ERR;
  }
  if (!cfg->iface) {
    argerr("missing -I iface");
    return ARGS_ERR;
  }
  if (cfg->no_ret && cfg->no_fcc) {
    argerr("--no-ret and --no-fcc together leave nothing to run");
    return ARGS_ERR;
  }
  if (cfg->rsi_mc_ret && (cfg->no_mc_ret || cfg->no_ret)) {
    argerr("--rsi-mc-ret requires RET and MC RET (--no-ret/--no-mc-ret not given)");
    return ARGS_ERR;
  }
  if (cfg->workers == 0) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    cfg->workers = n > 0 ? (unsigned)n : 1;
  }
  if (argutil_metrics_opts_validate(TOOL_NAME, cfg->metrics_sock, cfg->metrics_id, cfg->metrics_interval_s)) return ARGS_ERR;
  if (argutil_metrics_inspect_ts_validate(TOOL_NAME, cfg->metrics_id, cfg->metrics_inspect_ts)) return ARGS_ERR;
  return ARGS_OK;
}
