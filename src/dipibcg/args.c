/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/argutil.h"
#include "lib/log.h"

#include "args.h"
#include "version.h"

static void argerr(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void argerr(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  argutil_verr(TOOL_NAME, fmt, ap);
  va_end(ap);
}

static int mcast_parse(const char *s, config_t *cfg) {
  if (argutil_addrport_parse(s, &cfg->family, cfg->mcast_group, sizeof cfg->mcast_group, &cfg->mcast_port))
    return -1;

  if (cfg->family == AF_INET) {
    struct in_addr a;
    inet_pton(AF_INET, cfg->mcast_group, &a);
    if ((ntohl(a.s_addr) >> 28) != 0xE)
      return -1;
  } else {
    struct in6_addr a6;
    inet_pton(AF_INET6, cfg->mcast_group, &a6);
    if (a6.s6_addr[0] != 0xFF)
      return -1;
  }
  return 0;
}

void mcast_describe(const config_t *cfg, char *buf, size_t n) {
  if (cfg->family == AF_INET6)
    snprintf(buf, n, "[%s]:%u", cfg->mcast_group, cfg->mcast_port);
  else
    snprintf(buf, n, "%s:%u", cfg->mcast_group, cfg->mcast_port);
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
      "  -h, --help             this help\n\n"
      "examples:\n"
      "  %s -a -i guide.xml -M mapping.csv -m 239.255.0.2:3938\n"
      "  %s -l -m 239.255.0.2:3938 -o guide.xml -C mapping.csv\n",
      TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME);
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
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
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int have_a = 0, have_l = 0, have_mcast = 0, have_t = 0, have_w = 0;
  long t_value = 0, w_value = 0;
  int c;

  memset(cfg, 0, sizeof *cfg);
  optind = 1;
  while ((c = getopt_long(argc, argv, "ali:M:w:m:I:t:o:C:Zvh", longopts, NULL)) != -1) {
    switch (c) {
    case 'a':
      have_a = 1;
      cfg->mode = MODE_ANNOUNCE;
      break;
    case 'l':
      have_l = 1;
      cfg->mode = MODE_LISTEN;
      break;
    case 'i':
      cfg->input_path = optarg;
      break;
    case 'M':
      cfg->map_path = optarg;
      break;
    case 'w': {
      char *end;
      long v = strtol(optarg, &end, 10);
      if (*end != '\0' || v <= 0) {
        argerr("invalid -w window hours: %s", optarg);
        return ARGS_ERR;
      }
      w_value = v;
      have_w = 1;
      break;
    }
    case 'm':
      if (mcast_parse(optarg, cfg)) {
        argerr("invalid -m group:port: %s", optarg);
        return ARGS_ERR;
      }
      have_mcast = 1;
      break;
    case 'I':
      cfg->iface = optarg;
      break;
    case 't': {
      char *end;
      long v = strtol(optarg, &end, 10);
      if (*end != '\0' || v < 0) {
        argerr("invalid -t seconds: %s", optarg);
        return ARGS_ERR;
      }
      t_value = v;
      have_t = 1;
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
    case 1003: {
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
      return ARGS_ERR;
    }
  }
  if (optind < argc) {
    argerr("unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  if (have_a == have_l) {
    argerr("exactly one of -a/--announce or -l/--listen is required");
    return ARGS_ERR;
  }
  if (!have_mcast) {
    argerr("missing -m multicast group:port");
    return ARGS_ERR;
  }
  if ((cfg->metrics_sock || cfg->metrics_interval_s) && !cfg->metrics_id) {
    argerr("--metrics/--metrics-interval require --metrics-id");
    return ARGS_ERR;
  }

  if (cfg->mode == MODE_ANNOUNCE) {
    if (!cfg->input_path) {
      argerr("missing -i input");
      return ARGS_ERR;
    }
    if (!cfg->map_path) {
      argerr("missing -M map");
      return ARGS_ERR;
    }
    cfg->window_hours = have_w ? w_value : 24;
    cfg->interval_s = have_t ? t_value : 5;
  } else {
    if (cfg->metrics_id) {
      argerr("--metrics-id is announce-only");
      return ARGS_ERR;
    }
    if (cfg->compress) {
      argerr("-Z/--compress is announce-only");
      return ARGS_ERR;
    }
    if (!cfg->output_path)
      cfg->output_path = "-";
    cfg->timeout_s = have_t ? t_value : 35;
  }
  return ARGS_OK;
}
