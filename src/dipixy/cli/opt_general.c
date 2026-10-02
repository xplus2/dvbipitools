/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/log.h"
#include "lib/mux/fec2022.h"

#include "priv.h"

args_status_t dixy_opt_general(dixy_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (c) {
    case OPT_METRICS:
      cfg->metrics_sock = optarg;
      break;
    case OPT_METRICS_ID:
      cfg->metrics_id = optarg;
      break;
    case OPT_METRICS_INTERVAL:
      if (argutil_metrics_interval_opt(TOOL_NAME, optarg, &cfg->metrics_interval_s)) {
        args_free(cfg);
        return ARGS_ERR;
      }
      break;
    case OPT_METRICS_INSPECT_TS:
      if (argutil_metrics_inspect_ts_opt(TOOL_NAME, optarg, &cfg->metrics_inspect_ts)) return ARGS_ERR;
      break;
    case OPT_METRICS_HTTP:
      cfg->metrics_http = 1;
      break;
    case 'i': {
      char err[200];
      if (!p->cli_input) {
        dixy_cfg_reset_inputs(cfg);
        p->cli_input = 1;
      }
      if (dixy_cfg_add_input(cfg, optarg, err, sizeof err)) {
        argerr("-i %s: %s", optarg, err);
        args_free(cfg);
        return ARGS_ERR;
      }
      break;
    }
    case 'n': {
      char err[200];
      if (dixy_cfg_set_name(cfg, optarg, err, sizeof err)) {
        argerr("-n/--name %s: %s", optarg, err);
        args_free(cfg);
        return ARGS_ERR;
      }
      break;
    }
    case OPT_MEDIA_TYPE: {
      char err[200];
      if (dixy_cfg_set_media_type(cfg, optarg, err, sizeof err)) {
        argerr("--media-type %s: %s", optarg, err);
        args_free(cfg);
        return ARGS_ERR;
      }
      break;
    }
    case 'J':
      cfg->join_all = 1;
      break;
    case 'k':
      cfg->insecure_tls = 1;
      break;
    case 'd':
      cfg->daemonize = 1;
      break;
    case 'v':
      cfg->verbose = 1;
      break;
    case OPT_COLOR: {
      log_color_t v;
      if (log_color_from_string(optarg, &v)) {
        argerr("invalid --color: %s (auto|always|never)", optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      cfg->color_mode = v;
      break;
    }
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
