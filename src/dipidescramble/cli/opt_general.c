/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lib/cas/biss/biss.h"
#include "lib/cas/device_state_core.h"
#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/uriparse.h"
#include "lib/net/netconnect.h"

#include "priv.h"

args_status_t dscr_opt_general(dscr_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (c) {
    case 'i':
      if (dscr_cfg_set_input(cfg, optarg)) {
        argerr("invalid -i input: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case 'k':
      cfg->key_path = optarg;
      break;
    case 's':
      cfg->serial = optarg;
      break;
    case 'e':
      cfg->emm_file = optarg;
      break;
    case 'u':
      cfg->unicast_emm_uri = optarg;
      break;
    case OPT_INSECURE:
      cfg->insecure_tls = 1;
      break;
    case OPT_STRIP_LCEVC:
      cfg->strip_lcevc = 1;
      break;
    case OPT_TOKEN_HEADER:
      if (dscr_cfg_token_header(cfg, optarg)) {
        argerr("invalid --token-header: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case 'o':
      if (!p->cli_out) {
        cfg->n_out = 0;
        p->cli_out = 1;
      }
      if (cfg->n_out >= DIPIDESCRAMBLE_MAX_OUT) {
        argerr("too many -o targets (max %d)", DIPIDESCRAMBLE_MAX_OUT);
        return ARGS_ERR;
      }
      if (dscr_cfg_add_out(cfg, optarg)) {
        argerr("invalid -o target: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case 'f':
      if (dscr_cfg_format(cfg, optarg)) {
        argerr("invalid -f format: %s (ts|mkv|mka)", optarg);
        return ARGS_ERR;
      }
      break;
    case 'p':
      if (dscr_cfg_pmt(cfg, optarg)) {
        argerr("invalid -p pmt-pid: %s (0x0010..0x1FFE, or \"all\")", optarg);
        return ARGS_ERR;
      }
      break;
    case 'I':
      cfg->iface_in = optarg;
      break;
    case 'v':
      cfg->verbose = 1;
      break;
    case 'd':
      cfg->daemonize = 1;
      break;
    case OPT_COLOR:
      {
        log_color_t v;
        if (log_color_from_string(optarg, &v)) {
          argerr("invalid --color: %s (auto|always|never)", optarg);
          return ARGS_ERR;
        }
        cfg->color_mode = v;
      }
      break;
    case OPT_ECM_PROFILE:
      if (ecm_profile_parse(optarg, &cfg->ecm_profile) != 0 || ecm_profile_validate(&cfg->ecm_profile) != 0) {
        argerr("invalid --ecm-profile: %s", optarg);
        return ARGS_ERR;
      }
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
    case OPT_MAX_SERVICES: {
      unsigned v;
      if (argutil_uint_range(optarg, 1, DEVICE_MAX_SERVICES_CEILING, &v)) {
        argerr("invalid --max-services: %s (1..%u)", optarg, DEVICE_MAX_SERVICES_CEILING);
        return ARGS_ERR;
      }
      cfg->max_services = v;
      break;
    }
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
