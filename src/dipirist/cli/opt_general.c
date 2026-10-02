/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/mux/fec2022.h"
#include "lib/helper/uriparse.h"
#include "lib/net/netconnect.h"

#include "priv.h"

args_status_t rist_opt_general(rist_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (c) {
    case 'i':
      if (!p->cli_in) {
        memset(&cfg->in, 0, sizeof cfg->in);
        cfg->n_in = 0;
        p->cli_in = 1;
      }
      if (rist_parse_endpoint_uri(optarg, &cfg->in, 0, &cfg->n_in)) {
        argerr("invalid -i: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case 'o':
      if (!p->cli_out) {
        memset(&cfg->out, 0, sizeof cfg->out);
        cfg->n_out = 0;
        p->cli_out = 1;
      }
      if (rist_parse_endpoint_uri(optarg, &cfg->out, 1, &cfg->n_out)) {
        argerr("invalid -o: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case 'I':
      cfg->iface = optarg;
      break;
    case 'k':
      cfg->insecure_tls = 1;
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
