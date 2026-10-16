/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <unistd.h>

#include "lib/helper/argutil.h"

#include "../channel/channel.h"
#include "priv.h"

args_status_t fccret_cli_check(config_t *cfg) {
  if (!cfg->range_count) {
    argerr("FS000428", "missing -g range");
    return ARGS_ERR;
  }
  if (!cfg->listen_port) {
    argerr("FS000429", "missing -l listen");
    return ARGS_ERR;
  }
  if (!cfg->iface) {
    argerr("FS000430", "missing -I iface");
    return ARGS_ERR;
  }
  if (cfg->no_ret && cfg->no_fcc) {
    argerr("FS000431", "--no-ret and --no-fcc together leave nothing to run");
    return ARGS_ERR;
  }
  if (cfg->rsi_mc_ret && (cfg->no_mc_ret || cfg->no_ret)) {
    argerr("FS000432", "--rsi-mc-ret requires RET and MC RET (--no-ret/--no-mc-ret not given)");
    return ARGS_ERR;
  }
  if (cfg->fcc_resolve_by_port) {
    unsigned long base = cfg->fcc_resolve_base_port ? cfg->fcc_resolve_base_port : (unsigned long)cfg->listen_port + 1;
    unsigned long count = cfg->max_channels ? cfg->max_channels : CHANNEL_DEFAULT_MAX;
    unsigned long last = base + count - 1;
    if (last > 65535UL) {
      argerr("FS000694", "--fcc-resolve-by-port port range %lu..%lu exceeds 65535 (lower --fcc-resolve-base-port or -M)", base, last);
      return ARGS_ERR;
    }
    if (cfg->listen_port >= base && cfg->listen_port <= last) {
      argerr("FS000696", "--fcc-resolve-by-port port range %lu..%lu contains the -l port %u", base, last, cfg->listen_port);
      return ARGS_ERR;
    }
  }
  if (cfg->workers == 0) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    cfg->workers = n > 0 ? (unsigned)n : 1;
  }
  if (argutil_metrics_opts_validate(TOOL_NAME, cfg->metrics_sock, cfg->metrics_id, cfg->metrics_interval_s)) return ARGS_ERR;
  if (argutil_metrics_inspect_ts_validate(TOOL_NAME, cfg->metrics_id, cfg->metrics_inspect_ts)) return ARGS_ERR;
  return ARGS_OK;
}
