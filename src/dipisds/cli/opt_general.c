/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/uriparse.h"
#include "lib/net/netconnect.h"

#include "priv.h"

args_status_t sds_opt_general(sds_opt_t *p, int c) {
  config_t *cfg = p->cfg;

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
    case 'L':
      if (strlen(optarg) != 3) {
        argerr("invalid -L lang: %s (3-letter ISO 639-2 code)", optarg);
        return ARGS_ERR;
      }
      memcpy(cfg->lang, optarg, 3);
      break;
    case 'm':
      if (sds_mcast_parse(optarg, cfg)) {
        argerr("invalid -m group:port: %s", optarg);
        return ARGS_ERR;
      }
      cfg->fl.have_mcast = 1;
      break;
    case 'I':
      cfg->iface = optarg;
      break;
    case OPT_DSCP:
      if (net_dscp_parse(optarg, &cfg->dscp)) {
        argerr("invalid --dscp: %s (video-high|video-low|voice|signalling|best-effort|0..63)", optarg);
        return ARGS_ERR;
      }
      break;
    case 't': {
      unsigned v;
      if (argutil_uint_range(optarg, 0, UINT_MAX, &v)) {
        argerr("invalid -t seconds: %s", optarg);
        return ARGS_ERR;
      }
      cfg->fl.t_value = v;
      cfg->fl.have_t = 1;
      break;
    }
    case 'o':
      cfg->output_path = optarg;
      break;
    case 'f': {
      static const enum_map_t map[] = {{"m3u", OUT_M3U}, {"csv", OUT_CSV}, {"xspf", OUT_XSPF}, {"xml", OUT_XML}, {"null", OUT_NULL}};
      int v;
      if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
        argerr("invalid --format: %s (m3u|csv|xspf|xml|null)", optarg);
        return ARGS_ERR;
      }
      cfg->format = (out_fmt_t)v;
      cfg->fl.have_format = 1;
      break;
    }
    case 'v':
      cfg->verbose = 1;
      break;
    case 'd':
      cfg->daemonize = 1;
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
    default:
      return OPT_UNHANDLED;
  }
  return ARGS_OK;
}
