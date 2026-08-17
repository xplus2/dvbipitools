/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/argutil.h"
#include "lib/ioutil.h"
#include "lib/log.h"
#include "lib/metrics/protocol.h"

#include "args.h"
#include "version.h"

#define DEFAULT_LISTEN_ADDR "127.0.0.1"
#define DEFAULT_LISTEN_PORT 9109
#define DEFAULT_EXPIRY_S 30

static void argerr(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void argerr(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  argutil_verr(TOOL_NAME, fmt, ap);
  va_end(ap);
}

static void print_help(void) {
  printf(
      "usage: %s [options]\n\n"
      "DVB-IPI headend metrics collector: receives snapshots from dipitvhead,\n"
      "dipiradiohead, dipisds and dipibcg over a Unix datagram socket, serves\n"
      "them as Prometheus/OpenMetrics text at GET /metrics\n\n"
      "options:\n"
      "  -S, --sock <path>    Unix datagram socket to receive snapshots on\n"
      "                       (default: %s)\n"
      "  -l, --listen <a>:<p> HTTP listen address:port (default: %s:%u)\n"
      "  -e, --expiry <s>     drop an instance after this many seconds without a\n"
      "                       new snapshot (default: %d)\n"
      "  -v, --verbose        log rejected/dropped snapshots to stderr\n"
      "      --color <when>   auto|always|never (default auto)\n"
      "  -d, --daemonize      fork to background after startup, detach from terminal\n"
      "  -h, --help           this help\n\n"
      "example:\n"
      "  %s -l 0.0.0.0:9109\n",
      TOOL_NAME, METRICS_DEFAULT_SOCK_PATH, DEFAULT_LISTEN_ADDR, (unsigned)DEFAULT_LISTEN_PORT, DEFAULT_EXPIRY_S, TOOL_NAME);
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  static const struct option longopts[] = {
      {"sock", required_argument, 0, 'S'},
      {"listen", required_argument, 0, 'l'},
      {"expiry", required_argument, 0, 'e'},
      {"verbose", no_argument, 0, 'v'},
      {"color", required_argument, 0, 1000},
      {"daemonize", no_argument, 0, 'd'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int have_l = 0, have_e = 0;
  long e_value = 0;
  int c;

  memset(cfg, 0, sizeof *cfg);
  optind = 1;
  while ((c = getopt_long(argc, argv, "S:l:e:vdh", longopts, NULL)) != -1) {
    switch (c) {
    case 'S':
      cfg->sock_path = optarg;
      break;
    case 'l':
      if (argutil_addrport_parse(optarg, &cfg->family, cfg->listen_addr, sizeof cfg->listen_addr, &cfg->listen_port)) {
        argerr("invalid -l addr:port: %s", optarg);
        return ARGS_ERR;
      }
      have_l = 1;
      break;
    case 'e': {
      char *end;
      long v = strtol(optarg, &end, 10);
      if (*end != '\0' || v <= 0) {
        argerr("invalid -e expiry seconds: %s", optarg);
        return ARGS_ERR;
      }
      e_value = v;
      have_e = 1;
      break;
    }
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

  if (!cfg->sock_path)
    cfg->sock_path = METRICS_DEFAULT_SOCK_PATH;
  if (!have_l) {
    cfg->family = AF_INET;
    bufcpy(cfg->listen_addr, sizeof cfg->listen_addr, DEFAULT_LISTEN_ADDR);
    cfg->listen_port = DEFAULT_LISTEN_PORT;
  }
  cfg->expiry_s = have_e ? e_value : DEFAULT_EXPIRY_S;
  return ARGS_OK;
}
