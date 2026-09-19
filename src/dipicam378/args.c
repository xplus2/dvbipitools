/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/log.h"
#include "args.h"
#include "config.h"
#include "version.h"

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

static void print_help(void) {
  printf(
      "usage: %s -k <keyfile> [options]\n\n"
      "cs378x (camd35/TCP) key server: holds a device's RSA private key, answers\n"
      "oscam's ECM/EMM with a control word - a software smartcard, nothing more.\n\n"
      "options:\n"
      "  -k, --key <path>           RSA private key, PEM (required)\n"
      "  -s, --serial <id>          device's serial, matched against EMM-U\n"
      "  -p, --port <n>             cs378x TCP listen port (default: %u)\n"
      "  -a, --auth [user:]<pass>   password must match the reader's \"password =\"\n"
      "                             (default: \"%s\") - its digest is the AES-128 key.\n"
      "      --caid <hex>           ECMs for any other CAID get a CMD08 (\"stop asking\")\n"
      "                             (optional, default: no CMD08 ever sent)\n"
      "      --algo <a>             cissa|csa2 (default: cissa)\n"
      "  -v, --verbose              protocol/decode detail on stderr\n"
      "      --color <when>         auto|always|never (default auto)\n"
      "      --metrics <path>       Unix datagram socket for metrics (default: /run/dvbipitools/metrics.sock)\n"
      "      --metrics-id <name>    stable instance id; metrics disabled unless set\n"
      "      --metrics-interval <s> snapshot interval in seconds (default: 5)\n"
      "  -d, --daemonize            fork to background after startup, detach from terminal\n"
      "  -c, --config <path>        YAML config file (default: %s, if present)\n"
      "      --config-strict        fail on config file issues instead of warnings\n"
      "      --configtest           check the config file, then exit\n"
      "  -h, --help                 this help\n\n"
      "example:\n"
      "  %s -k device.key -s e2e-01 -p %u\n",
      TOOL_NAME, ARGS_DEFAULT_PORT, ARGS_DEFAULT_PASSWORD, DEFAULT_CONFIG_PATH, TOOL_NAME, ARGS_DEFAULT_PORT);
}

static const char *const shortopts = "k:s:p:a:c:vdh";

static const struct option longopts[] = {
    {"key", required_argument, 0, 'k'},
    {"serial", required_argument, 0, 's'},
    {"port", required_argument, 0, 'p'},
    {"auth", required_argument, 0, 'a'},
    {"caid", required_argument, 0, 1002},
    {"algo", required_argument, 0, 1001},
    {"verbose", no_argument, 0, 'v'},
    {"color", required_argument, 0, 1000},
    {"metrics", required_argument, 0, 1003},
    {"metrics-id", required_argument, 0, 1004},
    {"metrics-interval", required_argument, 0, 1005},
    {"daemonize", no_argument, 0, 'd'},
    {"config", required_argument, 0, 'c'},
    {"config-strict", no_argument, 0, 1007},
    {"configtest", no_argument, 0, 1006},
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0}};

static args_status_t prescan(int argc, char **argv, const char **cfg_path, int *configtest, int *strict) {
  int c;
  optind = 1;
  opterr = 0;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    if (c == 'c') *cfg_path = optarg;
    if (c == 1006) *configtest = 1;
    if (c == 1007) *strict = 1;
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
      case 'p':
        if (argutil_port_parse(optarg, &cfg->port)) {
          argerr("invalid -p port: %s", optarg);
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
      case 1002:
        if (cam378_cfg_caid(optarg, &cfg->caid)) {
          argerr("invalid --caid: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 1001:
        if (!strcmp(optarg, "csa2"))
          cfg->cw_len = 8;
        else if (!strcmp(optarg, "cissa"))
          cfg->cw_len = 16;
        else {
          argerr("invalid --algo: %s (cissa|csa2)", optarg);
          return ARGS_ERR;
        }
        break;
      case 'v':
        cfg->verbose = 1;
        break;
      case 'd':
        cfg->daemonize = 1;
        break;
      case 1000:
        {
          log_color_t v;
          if (log_color_from_string(optarg, &v)) {
            argerr("invalid --color: %s (auto|always|never)", optarg);
            return ARGS_ERR;
          }
          cfg->color_mode = v;
        }
        break;
      case 1003:
        cfg->metrics_sock = optarg;
        break;
      case 1004:
        cfg->metrics_id = optarg;
        break;
      case 1005:
        if (argutil_metrics_interval_opt(TOOL_NAME, optarg, &cfg->metrics_interval_s)) return ARGS_ERR;
        break;
      case 'c':
      case 1007:
      case 1006:
        break;
      case 'h':
        print_help();
        return ARGS_HELP;
      default:
        return ARGS_ERR; /* getopt already reported */
    }
  }
  if (optind < argc) {
    argerr("unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  if (!cfg->key_path) {
    argerr("missing -k device key");
    return ARGS_ERR;
  }
  if (argutil_metrics_opts_validate(TOOL_NAME, cfg->metrics_sock, cfg->metrics_id, cfg->metrics_interval_s)) return ARGS_ERR;
  return ARGS_OK;
}
