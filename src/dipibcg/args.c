/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/log.h"
#include "lib/helper/uriparse.h"
#include "lib/net/netconnect.h"

#include "args.h"
#include "config.h"
#include "version.h"

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

static int mcast_parse(const char *s, config_t *cfg) {
  return uriparse_mcast_addrport(s, &cfg->family, cfg->mcast_group, sizeof cfg->mcast_group, &cfg->mcast_port);
}

void mcast_describe(const config_t *cfg, char *buf, size_t n) {
  uriparse_mcast_describe(cfg->family, cfg->mcast_group, cfg->mcast_port, buf, n);
}

static void print_help(void) {
  printf(
      "usage: %s -a -i <xmltv> -M <map.csv> -m <mcast>:<port> [options]\n"
      "       %s -l -m <mcast>:<port> [options]\n\n"
      "DVB-IPI EPG/BCG (ETSI TS 102 539): announce an xmltv guide on multicast as\n"
      "BiM-encoded TVA fragments, or listen for one and write xmltv\n\n"
      "options:\n"
      "  -a, --announce         headend mode: read -i, transmit on -m\n"
      "  -l, --listen           client mode: receive on -m, write -o\n"
      "  -i, --input <path>     announce: xmltv source (required)\n"
      "  -M, --map <path>       announce: xmltv id -> uri,tsid,onid,sid csv (required)\n"
      "  -w, --window <hours>   announce: only events starting within this (default 24)\n"
      "  -m, --mcast <g>:<p>    multicast group:port ([addr6]:port for v6)\n"
      "  -I, --iface <iface>    multicast interface\n"
      "      --dscp <v>         announce: output DSCP marking: video-high|video-low|voice|\n"
      "                         signalling|best-effort|0..63 (default: signalling)\n"
      "  -t, --interval <s>     announce: repeat interval (default 5)\n"
      "  -t, --timeout <s>      listen: stop after N seconds (default 35)\n"
      "  -o, --output <path>    listen: xmltv output path, - for stdout (default)\n"
      "  -C, --csv-map <path>   listen: also write a mapping csv (feeds back into -M)\n"
      "  -Z, --compress         announce: zlib-compress BCG containers (RFC 1950)\n"
      "  -v, --verbose          periodic stats on stderr\n"
      "      --color <when>     auto|always|never (default auto)\n"
      "      --metrics <path>   announce: Unix datagram socket for metrics (default: /run/dvbipitools/metrics.sock)\n"
      "      --metrics-id <name> announce: stable instance id; metrics disabled unless set\n"
      "      --metrics-interval <s> announce: snapshot interval in seconds (default: 5)\n"
      "  -d, --daemonize        fork to background after startup, detach from terminal\n"
      "  -c, --config <path>    YAML config file (default: %s, if present)\n"
      "      --configtest       check the config file, then exit\n"
      "  -h, --help             this help\n\n"
      "examples:\n"
      "  %s -a -i guide.xml -M mapping.csv -m 239.255.0.2:3938\n"
      "  %s -l -m 239.255.0.2:3938 -o guide.xml -C mapping.csv\n",
      TOOL_NAME, TOOL_NAME, DEFAULT_CONFIG_PATH, TOOL_NAME, TOOL_NAME);
}

static const char *const shortopts = "ali:M:w:m:I:t:o:C:c:Zvdh";

static const struct option longopts[] = {
    {"announce", no_argument, 0, 'a'},
    {"listen", no_argument, 0, 'l'},
    {"input", required_argument, 0, 'i'},
    {"map", required_argument, 0, 'M'},
    {"window", required_argument, 0, 'w'},
    {"mcast", required_argument, 0, 'm'},
    {"iface", required_argument, 0, 'I'},
    {"interval", required_argument, 0, 't'},
    {"timeout", required_argument, 0, 't'},
    {"output", required_argument, 0, 'o'},
    {"csv-map", required_argument, 0, 'C'},
    {"compress", no_argument, 0, 'Z'},
    {"verbose", no_argument, 0, 'v'},
    {"color", required_argument, 0, 1000},
    {"metrics", required_argument, 0, 1001},
    {"metrics-id", required_argument, 0, 1002},
    {"metrics-interval", required_argument, 0, 1003},
    {"dscp", required_argument, 0, 1004},
    {"daemonize", no_argument, 0, 'd'},
    {"config", required_argument, 0, 'c'},
    {"configtest", no_argument, 0, 1005},
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0}};

static args_status_t prescan(int argc, char **argv, const char **cfg_path, int *configtest) {
  int c;
  optind = 1;
  opterr = 0;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    if (c == 'c') *cfg_path = optarg;
    if (c == 1005) *configtest = 1;
    if (c == 'h') {
      print_help();
      opterr = 1;
      return ARGS_HELP;
    }
  }
  opterr = 1;
  return ARGS_OK;
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  const char *cfg_path = NULL;
  int configtest = 0;
  args_status_t pst;
  int cli_mode = 0;
  int c;

  pst = prescan(argc, argv, &cfg_path, &configtest);
  if (pst != ARGS_OK) return pst;
  if (configtest) return bcg_cfg_test(cfg_path) ? ARGS_ERR : ARGS_HELP;
  bcg_cfg_defaults(cfg);
  if (bcg_cfg_load(cfg, cfg_path)) return ARGS_ERR;

  optind = 1;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    switch (c) {
    case 'a':
    case 'l':
      if (!cli_mode) cfg->fl.have_a = cfg->fl.have_l = 0;
      cli_mode = 1;
      if (c == 'a') cfg->fl.have_a = 1;
      else          cfg->fl.have_l = 1;
      break;
    case 1005:
    case 'c':
      break;
    case 'i':
      cfg->input_path = optarg;
      break;
    case 'M':
      cfg->map_path = optarg;
      break;
    case 'w': {
      unsigned v;
      if (argutil_uint_range(optarg, 1, UINT_MAX, &v)) {
        argerr("invalid -w window hours: %s", optarg);
        return ARGS_ERR;
      }
      cfg->window_hours = v;
      break;
    }
    case 'm':
      if (mcast_parse(optarg, cfg)) {
        argerr("invalid -m group:port: %s", optarg);
        return ARGS_ERR;
      }
      cfg->fl.have_mcast = 1;
      break;
    case 'I':
      cfg->iface = optarg;
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
    case 'C':
      cfg->csvmap_path = optarg;
      break;
    case 'Z':
      cfg->compress = 1;
      break;
    case 'v':
      cfg->verbose = 1;
      break;
    case 'd':
      cfg->daemonize = 1;
      break;
    case 1000: {
      log_color_t v;
      if (log_color_from_string(optarg, &v)) {
        argerr("invalid --color: %s (auto|always|never)", optarg);
        return ARGS_ERR;
      }
      cfg->color_mode = v;
      break;
    }
    case 1001:
      cfg->metrics_sock = optarg;
      break;
    case 1002:
      cfg->metrics_id = optarg;
      break;
    case 1003:
      if (argutil_metrics_interval_opt(TOOL_NAME, optarg, &cfg->metrics_interval_s)) return ARGS_ERR;
      break;
    case 1004:
      if (net_dscp_parse(optarg, &cfg->dscp)) {
        argerr("invalid --dscp: %s (video-high|video-low|voice|signalling|best-effort|0..63)", optarg);
        return ARGS_ERR;
      }
      break;
    case 'h':
      print_help();
      return ARGS_HELP;
    default:
      return ARGS_ERR;
    }
  }
  if (optind < argc) {
    argerr("unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  cfg->mode = cfg->fl.have_l ? MODE_LISTEN : MODE_ANNOUNCE;
  if (cfg->fl.have_a == cfg->fl.have_l) {
    argerr("exactly one of -a/--announce or -l/--listen is required");
    return ARGS_ERR;
  }
  if (!cfg->fl.have_mcast) {
    argerr("missing -m multicast group:port");
    return ARGS_ERR;
  }
  if (argutil_metrics_opts_validate(TOOL_NAME, cfg->metrics_sock, cfg->metrics_id, cfg->metrics_interval_s)) return ARGS_ERR;

  if (cfg->mode == MODE_ANNOUNCE) {
    if (!cfg->input_path) {
      argerr("missing -i input");
      return ARGS_ERR;
    }
    if (!cfg->map_path) {
      argerr("missing -M map");
      return ARGS_ERR;
    }
    if (cfg->fl.have_t) cfg->interval_s = cfg->fl.t_value;
  } else {
    if (cfg->metrics_id) {
      argerr("--metrics-id is announce-only");
      return ARGS_ERR;
    }
    if (cfg->compress) {
      argerr("-Z/--compress is announce-only");
      return ARGS_ERR;
    }
    if (!cfg->output_path) cfg->output_path = "-";
    if (cfg->fl.have_t) cfg->timeout_s = cfg->fl.t_value;
  }
  return ARGS_OK;
}
