/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */


#include <getopt.h>
#include <limits.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "priv.h"

args_status_t tvh_opt_general(tvh_opt_t *p, int c) {
  config_t *cfg = p->cfg;
  char err[192];

  switch (c) {
    case 'n':
      if (strcmp(optarg, "-") == 0) {
        cfg->nit_mode = TABLE_DROP;
      } else {
        cfg->nit_mode = TABLE_OVERRIDE;
        if (argutil_bufcpy_opt(TOOL_NAME, cfg->nit_text, sizeof cfg->nit_text, optarg, "-n nit-text"))
          return ARGS_ERR;
      }
      break;
    case OPT_DEFAULT_PROVIDER:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->default_provider_text, sizeof cfg->default_provider_text, optarg, "--default-provider text"))
        return ARGS_ERR;
      break;
    case 'b':
      if (argutil_uint_range(optarg, 1, 1000000, &cfg->bitrate_kbps)) {
        argerr("TS000603", "invalid -b bitrate: %s (kbps)", optarg);
        return ARGS_ERR;
      }
      break;
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
    case 'e': {
      unsigned v;
      if (argutil_uint_range(optarg, 0, UINT_MAX, &v)) {
        argerr("TS000498", "invalid -e seconds: %s", optarg);
        return ARGS_ERR;
      }
      cfg->error_retry_s = (long)v;
      break;
    }
    case 'k':
      cfg->insecure_tls = 1;
      break;
    case OPT_TSID:
      if (tvh_id_parse(optarg, &cfg->tsid)) {
        argerr("TS000499", "invalid --tsid: %s (1..65535)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_ONID:
      if (tvh_id_parse(optarg, &cfg->onid)) {
        argerr("TS000500", "invalid --onid: %s (1..65535)", optarg);
        return ARGS_ERR;
      }
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
