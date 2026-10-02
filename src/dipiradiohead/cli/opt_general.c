/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lib/cas/biss/biss.h"
#include "lib/cas/cas_args.h"
#include "lib/cas/emmg_server/emmg_server.h"
#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/mux/fec2022.h"
#include "lib/net/netconnect.h"

#include "priv.h"

args_status_t rdh_opt_general(rdh_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (c) {
    case 'i':
      if (!p->cli_inputs) {
        cfg->n_inputs = 0;
        p->cli_inputs = 1;
      }
      if (cfg->n_inputs >= RADIOHEAD_MAX_INPUTS) {
        argerr("too many -i inputs (max %d)", RADIOHEAD_MAX_INPUTS);
        return ARGS_ERR;
      }
      memset(&cfg->inputs[cfg->n_inputs], 0, sizeof cfg->inputs[0]);
      cfg->inputs[cfg->n_inputs].uri = optarg;
      cfg->n_inputs++;
      break;
    case 'm':
      if (rdh_mcast_parse(optarg, cfg)) {
        argerr("invalid -m group:port: %s", optarg);
        return ARGS_ERR;
      }
      p->have_mcast = 1;
      break;
    case 'O':
      cfg->iface = optarg;
      break;
    case 'r':
      cfg->rtp = 1;
      break;
    case 'T': {
      unsigned v;
      if (argutil_uint_range(optarg, 1, 255, &v)) {
        argerr("invalid -T ttl: %s (1..255)", optarg);
        return ARGS_ERR;
      }
      cfg->ttl = v;
      break;
    }
    case 'n':
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->nit_text, sizeof cfg->nit_text, optarg, "-n nit-text"))
        return ARGS_ERR;
      break;
    case 's':
      if (!p->cli_inputs) {
        argerr("--sdt/-s must follow -i");
        return ARGS_ERR;
      }
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->inputs[cfg->n_inputs - 1].sdt_text, sizeof cfg->inputs[0].sdt_text, optarg, "-s sdt-text"))
        return ARGS_ERR;
      break;
    case OPT_PROVIDER:
      if (!p->cli_inputs) {
        argerr("--provider must follow -i");
        return ARGS_ERR;
      }
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->inputs[cfg->n_inputs - 1].provider_text, sizeof cfg->inputs[0].provider_text, optarg, "--provider text"))
        return ARGS_ERR;
      break;
    case OPT_DEFAULT_PROVIDER:
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->default_provider_text, sizeof cfg->default_provider_text, optarg, "--default-provider text"))
        return ARGS_ERR;
      break;
    case 'e': {
      unsigned v;
      if (argutil_uint_range(optarg, 0, UINT_MAX, &v)) {
        argerr("invalid -e seconds: %s", optarg);
        return ARGS_ERR;
      }
      cfg->error_retry_s = v;
      break;
    }
    case 'k':
      cfg->insecure_tls = 1;
      break;
    case OPT_TSID:
      if (rdh_id_parse(optarg, &cfg->tsid)) {
        argerr("invalid --tsid: %s (1..65535)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_ONID:
      if (rdh_id_parse(optarg, &cfg->onid)) {
        argerr("invalid --onid: %s (1..65535)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_SID:
      if (!p->cli_inputs) {
        argerr("--sid must follow -i");
        return ARGS_ERR;
      }
      if (rdh_id_parse(optarg, &cfg->inputs[cfg->n_inputs - 1].sid)) {
        argerr("invalid --sid: %s (1..65535)", optarg);
        return ARGS_ERR;
      }
      break;
    case OPT_JITTER_MS:
      if (!p->cli_inputs) {
        argerr("--jitter-ms must follow -i");
        return ARGS_ERR;
      }
      if (argutil_uint_range(optarg, 1, RADIOHEAD_MAX_JITTER_MS, &cfg->inputs[cfg->n_inputs - 1].jitter_ms)) {
        argerr("invalid --jitter-ms: %s (1..%d ms)", optarg, RADIOHEAD_MAX_JITTER_MS);
        return ARGS_ERR;
      }
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
