/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/log.h"
#include "lib/mux/fec2022.h"

#include "priv.h"

args_status_t rec_opt_general(rec_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (c) {
    case 'o':
      if (!p->cli_out) {
        cfg->n_out = 0;
        p->cli_out = 1;
      }
      if (cfg->n_out >= DIPIREC_MAX_OUT) {
        argerr("too many -o targets (max %d)", DIPIREC_MAX_OUT);
        return ARGS_ERR;
      }
      if (rec_cfg_add_out(cfg, optarg)) {
        argerr("invalid -o target: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case 'i':
      if (rec_cfg_set_in(cfg, optarg)) {
        argerr("invalid -i uri: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case 'a':
      if (rec_cfg_audio(cfg, optarg)) {
        argerr("invalid -a track: %s (1..N or \"all\")", optarg);
        return ARGS_ERR;
      }
      break;
    case 'f':
      if (rec_cfg_format(cfg, optarg)) {
        argerr("invalid -f format: %s (raw|ts|mkv|mka|mp4|m4a)", optarg);
        return ARGS_ERR;
      }
      break;
    case 'p':
      if (rec_cfg_pmt(cfg, optarg)) {
        argerr("invalid -p pmt-pid: %s (0x0010..0x1FFE, or \"all\")", optarg);
        return ARGS_ERR;
      }
      break;
    case 's':
      if (rec_cfg_subs(cfg, optarg)) {
        argerr("invalid -s: %s (strip|keep|srt)", optarg);
        return ARGS_ERR;
      }
      break;
    case 't':
      if (rec_cfg_time(cfg, optarg)) {
        argerr("invalid -t duration: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case 'I':
      cfg->iface_in = optarg;
      break;
    case OPT_COLOR: {
      log_color_t v;
      if (log_color_from_string(optarg, &v)) {
        argerr("invalid --color: %s (auto|always|never)", optarg);
        return ARGS_ERR;
      }
      cfg->color_mode = v;
      break;
    }
    case OPT_SUB_LEAD: {
      unsigned v;
      if (argutil_uint_range(optarg, 0, 10000, &v)) {
        argerr("invalid --sub-lead: %s (0..10000 ms)", optarg);
        return ARGS_ERR;
      }
      cfg->sub_lead_ms = (long)v;
      break;
    }
    case 'v':
      cfg->verbose = 1;
      break;
    case OPT_INSECURE:
      cfg->insecure_tls = 1;
      break;
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
    case OPT_METRICS_INSPECT_TS_PIDS:
      if (argutil_metrics_known_pids_opt(TOOL_NAME, optarg, cfg->metrics_known_pids, &cfg->metrics_n_known_pids)) return ARGS_ERR;
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
