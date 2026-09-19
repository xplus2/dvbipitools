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
#include "args.h"
#include "config.h"
#include "version.h"

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

static int basic_auth_parse(const char *val, char *out, size_t outsz) {
  char err[64];
  if (metrics_cfg_auth(val, out, outsz, err, sizeof err)) {
    argerr("invalid --auth: %s", err);
    return -1;
  }
  return 0;
}

static void print_help(void) {
  printf(
      "usage: %s [options]\n\n"
      "DVB-IPI headend metrics collector: receives snapshots from dipitvhead,\n"
      "dipiradiohead, dipisds and dipibcg over a Unix datagram socket, serves\n"
      "them as Prometheus/OpenMetrics text at GET /metrics\n\n"
      "options:\n"
      "  -S, --sock <path>        socket for snapshots on (default: %s)\n"
      "  -l, --listen <a>:<p>     HTTP listen address:port (default: %s:%u)\n"
      "      --tls-cert <path>    certificate file (PEM), HTTPS on -l, requires --tls-key\n"
      "      --tls-key <path>     private key file (PEM), requires --tls-cert\n"
      "      --auth <user>:<pass> HTTP Basic Auth for GET /metrics (default: off)\n"
      "  -e, --expiry <s>         drop an instance after this many seconds without a\n"
      "                           new snapshot (default: %d)\n"
      "  -v, --verbose            log rejected/dropped snapshots to stderr\n"
      "      --color <when>       auto|always|never (default auto)\n"
      "  -d, --daemonize          fork to background after startup, detach from terminal\n"
      "  -c, --config <path>      YAML config file (default: %s, if present)\n"
      "      --configtest         check the config file, then exit\n"
      "  -h, --help               this help\n\n"
      "example:\n"
      "  %s -l 0.0.0.0:9109\n"
      "  %s -l 0.0.0.0:9109 --tls-cert srv.crt --tls-key srv.key\n",
      TOOL_NAME, METRICS_DEFAULT_SOCK_PATH, DEFAULT_LISTEN_ADDR, (unsigned)DEFAULT_LISTEN_PORT, DEFAULT_EXPIRY_S, DEFAULT_CONFIG_PATH, TOOL_NAME, TOOL_NAME);
}

#define OPT_COLOR 1000
#define OPT_TLS_CERT 1001
#define OPT_TLS_KEY 1002
#define OPT_AUTH 1003
#define OPT_CONFIGTEST 1004

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
    {"configtest", no_argument, 0, OPT_CONFIGTEST},
    {"help", no_argument, 0, 'h'},
    {0, 0, 0, 0}};

static args_status_t prescan(int argc, char **argv, const char **cfg_path, int *configtest) {
  int c;
  optind = 1;
  opterr = 0;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    if (c == 'c') *cfg_path = optarg;
    if (c == OPT_CONFIGTEST) *configtest = 1;
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
  const char *conflict;
  int configtest = 0;
  args_status_t st;
  int c;

  st = prescan(argc, argv, &cfg_path, &configtest);
  if (st != ARGS_OK) return st;
  if (configtest) return metrics_cfg_test(cfg_path) ? ARGS_ERR : ARGS_HELP;

  metrics_cfg_defaults(cfg);
  if (metrics_cfg_load(cfg, cfg_path)) return ARGS_ERR;

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
