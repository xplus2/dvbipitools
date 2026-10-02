/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/log.h"
#include "lib/metrics/protocol.h"
#include "priv.h"

static int basic_auth_parse(const char *val, char *out, size_t outsz) {
  char err[64];
  if (metrics_cfg_auth(val, out, outsz, err, sizeof err)) {
    argerr("invalid --auth: %s", err);
    return -1;
  }
  return 0;
}

static const char *const shortopts = "S:l:e:c:vdh";

static const struct option longopts[] = {
  {"sock", required_argument, 0, 'S'},
  {"listen", required_argument, 0, 'l'},
  {"expiry", required_argument, 0, 'e'},
  {"tls-cert", required_argument, 0, OPT_TLS_CERT},
  {"tls-key", required_argument, 0, OPT_TLS_KEY},
  {"auth", required_argument, 0, OPT_AUTH},
  {"verbose", no_argument, 0, 'v'},
  {"color", required_argument, 0, OPT_COLOR},
  {"daemonize", no_argument, 0, 'd'},
  {"config", required_argument, 0, 'c'},
  {"config-strict", no_argument, 0, OPT_CONFIG_STRICT},
  {"configtest", no_argument, 0, OPT_CONFIGTEST},
  {"help", no_argument, 0, 'h'},
  {0, 0, 0, 0}};

static args_status_t prescan(int argc, char **argv, const char **cfg_path, int *configtest, int *strict) {
  int c;
  optind = 1;
  opterr = 0;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    if (c == 'c') *cfg_path = optarg;
    if (c == OPT_CONFIGTEST) *configtest = 1;
    if (c == OPT_CONFIG_STRICT) *strict = 1;
    if (c == 'h') {
      metrics_print_help();
      opterr = 1;
      return ARGS_HELP;
    }
  }
  opterr = 1;
  return ARGS_OK;
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  const char *cfg_path = NULL;
  const char *conflict;
  int configtest = 0;
  int strict = 0;
  args_status_t st;
  int c;

  st = prescan(argc, argv, &cfg_path, &configtest, &strict);
  if (st != ARGS_OK) return st;
  if (configtest) return metrics_cfg_test(cfg_path, strict) ? ARGS_ERR : ARGS_HELP;

  metrics_cfg_defaults(cfg);
  if (metrics_cfg_load(cfg, cfg_path, strict)) return ARGS_ERR;

  optind = 1;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    switch (c) {
      case 'S':
        cfg->sock_path = optarg;
        break;
      case 'l':
        if (argutil_addrport_parse(optarg, &cfg->family, cfg->listen_addr, sizeof cfg->listen_addr, &cfg->listen_port)) {
          argerr("invalid -l addr:port: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 'e': {
        unsigned v;
        if (argutil_uint_range(optarg, 1, UINT_MAX, &v)) {
          argerr("invalid -e expiry seconds: %s", optarg);
          return ARGS_ERR;
        }
        cfg->expiry_s = v;
        break;
      }
      case OPT_TLS_CERT:
        cfg->tls_cert = optarg;
        break;
      case OPT_TLS_KEY:
        cfg->tls_key = optarg;
        break;
      case OPT_AUTH:
        if (basic_auth_parse(optarg, cfg->http_auth, sizeof cfg->http_auth)) return ARGS_ERR;
        break;
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
      case 'c':
      case OPT_CONFIG_STRICT:
      case OPT_CONFIGTEST:
        break;
      default:
        return ARGS_ERR;
    }
  }
  if (optind < argc) {
    argerr("unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  conflict = metrics_cfg_conflict(cfg);
  if (conflict) {
    argerr("%s", conflict);
    return ARGS_ERR;
  }
  return ARGS_OK;
}
