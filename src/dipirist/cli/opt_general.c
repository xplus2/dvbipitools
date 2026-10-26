/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/net/netconnect.h"
#include "priv.h"

args_status_t dipirist_opt_general(dipirist_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (c) {
    case 'i':
      if (!p->cli_in) {
        memset(&cfg->in, 0, sizeof cfg->in);
        cfg->n_in = 0;
        p->cli_in = 1;
      }
      if (dipirist_parse_endpoint_uri(optarg, &cfg->in, 0, &cfg->n_in)) {
        argerr("VS000149", "invalid -i: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case 'o':
      if (!p->cli_out) {
        memset(&cfg->out, 0, sizeof cfg->out);
        cfg->n_out = 0;
        p->cli_out = 1;
      }
      if (dipirist_parse_endpoint_uri(optarg, &cfg->out, 1, &cfg->n_out)) {
        argerr("VS000150", "invalid -o: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case 'I':
      cfg->iface = optarg;
      break;
    case 'k':
      cfg->insecure_tls = 1;
      break;
    case OPT_PACE:
      cfg->pace = 1;
      break;
    case OPT_TTL: {
      unsigned v;
      if (argutil_uint_range(optarg, 0, 255, &v)) {
        argerr("VS000558", "invalid --ttl: %s (0..255)", optarg);
        return ARGS_ERR;
      }
      cfg->out_ttl = (int)v;
      break;
    }
    case OPT_DSCP:
      if (argutil_int_opt(TOOL_NAME, "VS000106", "dscp", optarg, net_dscp_parse, NET_DSCP_NAMES, &cfg->out_tos)) return ARGS_ERR;
      break;
    case OPT_COLOR: {
      if (argutil_color_opt(TOOL_NAME, "VS000105", optarg, &cfg->color_mode)) return ARGS_ERR;
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
