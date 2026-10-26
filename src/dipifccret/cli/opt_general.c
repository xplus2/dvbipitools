/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "priv.h"

args_status_t fccret_opt_general(fccret_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (c) {
    case 'g':
      if (fccret_cfg_range(cfg, optarg)) {
        argerr("FS000450", "invalid -g range: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case 'l':
      if (argutil_addrport_parse(optarg, &cfg->listen_family, cfg->listen_addr, sizeof cfg->listen_addr, &cfg->listen_port)) {
        argerr("FS000135", "invalid -l addr:port: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case 'I':
      cfg->iface = optarg;
      break;
    case 'M': {
      unsigned v;
      if (argutil_uint_range(optarg, 0, UINT_MAX, &v)) {
        argerr("FS000451", "invalid -M max-channels: %s", optarg);
        return ARGS_ERR;
      }
      cfg->max_channels = (size_t)v;
      break;
    }
    case OPT_CHANNEL_IDLE_TIMEOUT:
      if (argutil_uint_range(optarg, 0, UINT_MAX, &cfg->channel_idle_timeout_s)) {
        argerr("FS000452", "invalid --channel-idle-timeout: %s (s)", optarg);
        return ARGS_ERR;
      }
      break;
    case 'R': {
      unsigned v;
      if (argutil_uint_range(optarg, 0, 127, &v)) {
        argerr("FS000453", "invalid -R rtx-pt: %s (0..127)", optarg);
        return ARGS_ERR;
      }
      cfg->rtx_pt = (unsigned char)v;
      break;
    }
    case 'w':
      if (argutil_uint_range(optarg, 0, UINT_MAX, &cfg->workers)) {
        argerr("FS000454", "invalid -w workers: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_CPU_AFFINITY:
      if (cpuaff_parse(&cfg->cpu_affinity, optarg)) {
        argerr("FS000455", "invalid --cpu-affinity: %s (off, auto, or list like 2-5,8)", optarg);
        return ARGS_ERR;
      }
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
