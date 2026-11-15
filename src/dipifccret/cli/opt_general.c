/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>

#include "lib/helper/argutil.h"
#include "priv.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"

static const yamlcfg_opt_t opts[] = {
    {'g', "range", "FS000450", "invalid -g range: %s", 0},
    {'l', "listen", "FS000135", "invalid -l addr:port: %s", 0},
    {'M', "max-channels", "FS000451", "invalid -M max-channels: %s", 0},
    {OPT_CHANNEL_IDLE_TIMEOUT, "channel-idle-timeout", "FS000452", "invalid --channel-idle-timeout: %s (s)", 0},
    {'R', "rtx-pt", "FS000453", "invalid -R rtx-pt: %s (0..127)", 0},
    {'w', "workers", "FS000454", "invalid -w workers: %s", 0},
    {OPT_CPU_AFFINITY, "cpu-affinity", "FS000455", "invalid --cpu-affinity: %s (off, auto, or list like 2-5,8)", 0},
};

args_status_t fccret_opt_general(fccret_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (yamlcfg_opt_apply(TOOL_NAME, opts, ARRAY_SIZE(opts), fccret_cfg_keys, fccret_cfg_nkeys, cfg, c, optarg)) {
    case 1: return ARGS_OK;
    case -1: return ARGS_ERR;
    default: break;
  }
  switch (c) {
    case 'I':
      cfg->iface = optarg;
      break;
    case 'u':
      cfg->user = optarg;
      break;
    case 'v':
      cfg->verbose = 1;
      break;
    case 'd':
      cfg->daemonize = 1;
      break;
    case OPT_COLOR: {
      if (argutil_color_opt(TOOL_NAME, "FS000105", optarg, &cfg->color_mode)) return ARGS_ERR;
      break;
    }
    case OPT_METRICS:
      cfg->metrics_sock = optarg;
      break;
    case OPT_METRICS_ID:
      cfg->metrics_id = optarg;
      break;
    case OPT_METRICS_INTERVAL:
      if (argutil_metrics_interval_opt(TOOL_NAME, optarg, &cfg->metrics_interval_s)) return ARGS_ERR;
      break;
    case OPT_METRICS_INSPECT_TS:
      if (argutil_metrics_inspect_ts_opt(TOOL_NAME, optarg, &cfg->metrics_inspect_ts)) return ARGS_ERR;
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
