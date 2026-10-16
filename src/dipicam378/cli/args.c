/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/cas/device_state_core.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/argutil.h"
#include "lib/helper/log.h"
#include "priv.h"

static const char *const shortopts = "k:s:b:p:a:c:vdh";

static const struct option longopts[] = {
  {"key", required_argument, 0, 'k'},
  {"serial", required_argument, 0, 's'},
  {"bind", required_argument, 0, 'b'},
  {"port", required_argument, 0, 'p'},
  {"auth", required_argument, 0, 'a'},
  {"caid", required_argument, 0, OPT_CAID},
  {"algo", required_argument, 0, OPT_ALGO},
  {"verbose", no_argument, 0, 'v'},
  {"color", required_argument, 0, OPT_COLOR},
  {"metrics", required_argument, 0, OPT_METRICS},
  {"metrics-id", required_argument, 0, OPT_METRICS_ID},
  {"metrics-interval", required_argument, 0, OPT_METRICS_INTERVAL},
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
      cam378_print_help();
      opterr = 1;
      return ARGS_HELP;
    }
  }
  opterr = 1;
  return ARGS_OK;
}

static args_status_t parse_inner(int argc, char **argv, config_t *cfg) {
  const char *cfg_path = NULL;
  int configtest = 0;
  int strict = 0;
  args_status_t pst;
  int c;

  pst = prescan(argc, argv, &cfg_path, &configtest, &strict);
  if (pst != ARGS_OK) return pst;
  if (configtest) return cam378_cfg_test(cfg_path, strict) ? ARGS_ERR : ARGS_HELP;

  cam378_cfg_defaults(cfg);
  if (cam378_cfg_load(cfg, cfg_path, strict)) return ARGS_ERR;

  optind = 1;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    switch (c) {
      case 'k':
        cfg->key_path = optarg;
        break;
      case 's':
        cfg->serial = optarg;
        break;
      case 'b':
        cfg->bind = optarg;
        break;
      case 'p':
        if (argutil_port_parse(optarg, &cfg->port)) {
          argerr("3S000125", "invalid -p port: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 'a': {
        char *colon = strchr(optarg, ':');
        if (colon) {
          *colon = '\0';
          cfg->username = optarg;
          cfg->password = colon + 1;
        } else {
          cfg->password = optarg;
        }
        break;
      }
      case OPT_CAID:
        if (cam378_cfg_caid(optarg, &cfg->caid)) {
          argerr("3S000126", "invalid --caid: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case OPT_ALGO:
        if (!strcmp(optarg, "csa2"))
          cfg->cw_len = 8;
        else if (!strcmp(optarg, "cissa"))
          cfg->cw_len = 16;
        else {
          argerr("3S000127", "invalid --algo: %s (cissa|csa2)", optarg);
          return ARGS_ERR;
        }
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
            argerr("3S000105", "invalid --color: %s (auto|always|never)", optarg);
            return ARGS_ERR;
          }
          cfg->color_mode = v;
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
      case 'c':
      case OPT_CONFIG_STRICT:
      case OPT_CONFIGTEST:
        break;
      case 'h':
        cam378_print_help();
        return ARGS_HELP;
      default:
        return ARGS_ERR; /* getopt already reported */
    }
  }
  if (optind < argc) {
    argerr("3S000107", "unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  if (!cfg->key_path) {
    argerr("3S000128", "missing -k device key");
    return ARGS_ERR;
  }
  if (!cfg->serial || !*cfg->serial) {
    argerr("3S000130", "missing -s device serial");
    return ARGS_ERR;
  }
  if (strlen(cfg->serial) >= DEVICE_SERIAL_MAX) {
    argerr("3S000723", "invalid -s serial: longer than %d bytes", DEVICE_SERIAL_MAX - 1);
    return ARGS_ERR;
  }
  if (argutil_metrics_opts_validate(TOOL_NAME, cfg->metrics_sock, cfg->metrics_id, cfg->metrics_interval_s)) return ARGS_ERR;
  return ARGS_OK;
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  args_status_t st;
  cfg->str_pool = NULL;
  st = parse_inner(argc, argv, cfg);
  if (st != ARGS_OK) {
    yamlcfg_strpool_free(cfg->str_pool);
    cfg->str_pool = NULL;
  }
  return st;
}
