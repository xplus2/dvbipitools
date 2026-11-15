/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>

#include "lib/helper/argutil.h"
#include "priv.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"

static const yamlcfg_opt_t opts[] = {
    {'i', "in", "CS000546", "invalid -i uri: %s", 0},
    {'a', "audio", "CS000547", "invalid -a track: %s (1..N or \"all\")", 0},
    {'f', "format", "CS000548", "invalid -f format: %s (raw|ts|mkv|mka|mp4|m4a)", 0},
    {'p', "pmt-pid", "CS000354", "invalid -p pmt-pid: %s (0x0010..0x1FFE, or \"all\")", 0},
    {'s', "subtitles", "CS000549", "invalid -s: %s (strip|keep|srt)", 0},
    {'t', "time", "CS000550", "invalid -t duration: %s", 0},
    {OPT_SUB_LEAD, "sub-lead", "CS000551", "invalid --sub-lead: %s (0..10000 ms)", 0},
};

args_status_t rec_opt_general(rec_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (yamlcfg_opt_apply(TOOL_NAME, opts, ARRAY_SIZE(opts), rec_cfg_keys, rec_cfg_nkeys, cfg, c, optarg)) {
    case 1: return ARGS_OK;
    case -1: return ARGS_ERR;
    default: break;
  }
  switch (c) {
    case 'o':
      if (!p->cli_out) {
        cfg->n_out = 0;
        p->cli_out = 1;
      }
      if (cfg->n_out >= DIPIREC_MAX_OUT) {
        argerr("CS000351", "too many -o targets (max %d)", DIPIREC_MAX_OUT);
        return ARGS_ERR;
      }
      if (rec_cfg_add_out(cfg, optarg)) {
        argerr("CS000352", "invalid -o target: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case 'I':
      cfg->iface_in = optarg;
      break;
    case OPT_COLOR: {
      if (argutil_color_opt(TOOL_NAME, "CS000105", optarg, &cfg->color_mode)) return ARGS_ERR;
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
