/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>

#include "lib/helper/argutil.h"
#include "lib/net/netconnect.h"
#include "priv.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/growutil.h"

static const yamlcfg_opt_t opts[] = {
    {'L', "lang", "AS000194", "invalid -L lang: %s (3-letter ISO 639-2 code)", 0},
    {'m', "mcast", "AS000103", "invalid -m group:port: %s", 0},
    {'T', "ttl", "AS000495", "invalid -T ttl: %s (1..255)", 0},
    {'f', "format", "AS000195", "invalid --format: %s (m3u|csv|xspf|xml|null)", 0},
};

args_status_t sds_opt_general(sds_opt_t *p, int c) {
  config_t *cfg = p->cfg;

  switch (yamlcfg_opt_apply(TOOL_NAME, opts, ARRAY_SIZE(opts), sds_cfg_keys, sds_cfg_nkeys, cfg, c, optarg)) {
    case 1: return ARGS_OK;
    case -1: return ARGS_ERR;
    default: break;
  }
  switch (c) {
    case 'a':
    case 'l':
      if (!p->cli_mode) {
        cfg->fl.have_a = 0;
        cfg->fl.have_l = 0;
      }
      p->cli_mode = 1;
      if (c == 'a')
        cfg->fl.have_a = 1;
      else
        cfg->fl.have_l = 1;
      break;
    case 'i':
      cfg->input_path = optarg;
      break;
    case 'p':
      cfg->provider = optarg;
      break;
    case 'O':
      cfg->offering = optarg;
      break;
    case 'I':
      cfg->iface = optarg;
      break;
    case OPT_DSCP:
      if (argutil_int_opt(TOOL_NAME, "AS000106", "dscp", optarg, net_dscp_parse, NET_DSCP_NAMES, &cfg->dscp)) return ARGS_ERR;
      cfg->fl.have_dscp = 1;
      break;
    case 't': {
      unsigned v;
      if (argutil_uint_range(optarg, 0, UINT_MAX, &v)) {
        argerr("AS000104", "invalid -t seconds: %s", optarg);
        return ARGS_ERR;
      }
      cfg->fl.t_value = v;
      cfg->fl.have_t = 1;
      break;
    }
    case 'o':
      cfg->output_path = optarg;
      break;
    case 'v':
      cfg->verbose = 1;
      break;
    case 'd':
      cfg->daemonize = 1;
      break;
    case OPT_COLOR: {
      if (argutil_color_opt(TOOL_NAME, "AS000105", optarg, &cfg->color_mode)) return ARGS_ERR;
      break;
    }
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
