/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/argutil.h"
#include "lib/log.h"

#include "args.h"
#include "version.h"

#define ARGS_DEFAULT_PORT 27500u
#define ARGS_DEFAULT_PASSWORD TOOL_NAME

static void argerr(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void argerr(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  argutil_verr(TOOL_NAME, fmt, ap);
  va_end(ap);
}

static int caid_parse(const char *p, unsigned *out) {
  char *end;
  unsigned long v;
  if (*p == '\0')
    return -1;
  v = strtoul(p, &end, 16);
  if (*end != '\0' || v == 0 || v > 0xFFFF)
    return -1;
  *out = (unsigned)v;
  return 0;
}

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
      "  -h, --help                 this help\n\n"
      "example:\n"
      "  %s -k device.key -s e2e-01 -p %u\n",
      TOOL_NAME, ARGS_DEFAULT_PORT, ARGS_DEFAULT_PASSWORD, TOOL_NAME, ARGS_DEFAULT_PORT);
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
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
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int have_key = 0;
  int c;

  memset(cfg, 0, sizeof *cfg);
  cfg->port = ARGS_DEFAULT_PORT;
  cfg->password = ARGS_DEFAULT_PASSWORD;
  cfg->cw_len = 16;
  optind = 1;
  while ((c = getopt_long(argc, argv, "k:s:p:a:vdh", longopts, NULL)) != -1) {
    switch (c) {
      case 'k':
        cfg->key_path = optarg;
        have_key = 1;
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
        if (caid_parse(optarg, &cfg->caid)) {
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
      case 1005: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0 || v > 86400UL) {
          argerr("invalid --metrics-interval: %s (seconds, 1..86400)", optarg);
          return ARGS_ERR;
        }
        cfg->metrics_interval_s = (unsigned)v;
        break;
      }
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
  if (!have_key) {
    argerr("missing -k device key");
    return ARGS_ERR;
  }
  if ((cfg->metrics_sock || cfg->metrics_interval_s) && !cfg->metrics_id) {
    argerr("--metrics/--metrics-interval require --metrics-id");
    return ARGS_ERR;
  }
  return ARGS_OK;
}
