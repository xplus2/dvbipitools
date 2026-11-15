/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */


#include <getopt.h>

#include "lib/helper/argutil.h"
#include "priv.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"

static const yamlcfg_opt_t opts[] = {
    {'n', "nit", "LS000685", "-n nit-text too long", 0},
    {OPT_DEFAULT_PROVIDER, "default-provider", "LS000685", "--default-provider text too long", 0},
    {'b', "bitrate", "TS000603", "invalid -b bitrate: %s (kbps)", 0},
    {'e', "error", "TS000498", "invalid -e seconds: %s", 0},
    {OPT_TSID, "tsid", "TS000499", "invalid --tsid: %s (1..65535)", 0},
    {OPT_ONID, "onid", "TS000500", "invalid --onid: %s (1..65535)", 0},
};

args_status_t tvh_opt_general(tvh_opt_t *p, int c) {
  config_t *cfg = p->cfg;
  char err[192];

  switch (yamlcfg_opt_apply(TOOL_NAME, opts, ARRAY_SIZE(opts), tvh_cfg_keys, tvh_cfg_nkeys, cfg, c, optarg)) {
    case 1: return ARGS_OK;
    case -1: return ARGS_ERR;
    default: break;
  }
  switch (c) {
    case 'S':
      cfg->stuff = 1;
      break;
    case 'B':
      cfg->burst_limit = 1;
      break;
    case OPT_PCR_MODE:
      CHECK(tvh_cfg_pcr_mode(cfg, optarg, err, sizeof err), "--pcr-mode");
      break;
    case OPT_PCR_LEAD_MS:
      CHECK(tvh_cfg_pcr_lead_ms(cfg, optarg, err, sizeof err), "--pcr-lead-ms");
      break;
    case 'k':
      cfg->insecure_tls = 1;
      break;
    case OPT_COLOR: {
      if (argutil_color_opt(TOOL_NAME, "TS000105", optarg, &cfg->color_mode)) return ARGS_ERR;
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
    case OPT_METRICS_INSPECT_TS_PIDS:
      if (argutil_metrics_known_pids_opt(TOOL_NAME, optarg, cfg->metrics_known_pids, &cfg->metrics_n_known_pids)) return ARGS_ERR;
      break;
    case 'v':
      cfg->verbose = 1;
      break;
    case 'd':
      cfg->daemonize = 1;
      break;
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
