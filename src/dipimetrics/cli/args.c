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
#include "priv.h"

static int basic_auth_parse(const char *val, char *out, size_t outsz) {
  char err[64];
  if (metrics_cfg_auth(val, out, outsz, err, sizeof err)) {
    argerr("MS000134", "invalid --auth: %s", err);
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

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  const char *cfg_path = NULL;
  const char *conflict;
  const char *conflict_code;
  int configtest = 0;
  int strict = 0;
  int c;

  if (argutil_prescan(argc, argv, shortopts, longopts, OPT_CONFIGTEST, OPT_CONFIG_STRICT, &cfg_path, &configtest, &strict)) {
    metrics_print_help();
    return ARGS_HELP;
  }
  if (configtest) return metrics_cfg_test(cfg_path, strict) ? ARGS_ERR : ARGS_HELP;

  metrics_cfg_defaults(cfg);
  if (metrics_cfg_load(cfg, cfg_path, strict)) return ARGS_ERR;

  optind = 1;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    switch (c) {
      case 'S':
        if (!*optarg) {
          argerr("MS000715", "empty -S path");
          return ARGS_ERR;
        }
        cfg->sock_path = optarg;
        break;
      case 'l':
        if (argutil_addrport_parse(optarg, &cfg->family, cfg->listen_addr, sizeof cfg->listen_addr, &cfg->listen_port)) {
          argerr("MS000135", "invalid -l addr:port: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 'e': {
        unsigned v;
        if (argutil_uint_range(optarg, 1, UINT_MAX, &v)) {
          argerr("MS000136", "invalid -e expiry seconds: %s", optarg);
          return ARGS_ERR;
        }
        cfg->expiry_s = v;
        break;
      }
      case OPT_TLS_CERT:
        if (!*optarg) {
          argerr("MS000716", "empty --tls-cert path");
          return ARGS_ERR;
        }
        cfg->tls_cert = optarg;
        break;
      case OPT_TLS_KEY:
        if (!*optarg) {
          argerr("MS000717", "empty --tls-key path");
          return ARGS_ERR;
        }
        cfg->tls_key = optarg;
        break;
      case OPT_AUTH:
        if (basic_auth_parse(optarg, cfg->http_auth, sizeof cfg->http_auth)) return ARGS_ERR;
        log_code("MA300172", "--auth is visible in the process list, use 'auth' in the config file");
        break;
      case 'v':
        cfg->verbose = 1;
        break;
      case 'd':
        cfg->daemonize = 1;
        break;
      case OPT_COLOR: {
        if (argutil_color_opt(TOOL_NAME, "MS000105", optarg, &cfg->color_mode)) return ARGS_ERR;
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
    argerr("MS000107", "unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  conflict = metrics_cfg_conflict(cfg, &conflict_code);
  if (conflict) {
    argerr(conflict_code, "%s", conflict);
    return ARGS_ERR;
  }
  return ARGS_OK;
}
