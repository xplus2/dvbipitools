/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "args.h"
#include "version.h"

static void argerr(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void argerr(const char *fmt, ...) {
  va_list ap;
  fputs(TOOL_NAME ": ", stderr);
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
}

static int port_parse(const char *p, unsigned *out) {
  char *end;
  unsigned long v;
  if (*p == '\0')
    return -1;
  v = strtoul(p, &end, 10);
  if (*end != '\0' || v == 0 || v > 65535)
    return -1;
  *out = (unsigned)v;
  return 0;
}

static int mcast_parse(const char *s, config_t *cfg) {
  char addr[64];

  if (*s == '[') {
    const char *close = strchr(s, ']');
    size_t len;
    if (!close)
      return -1;
    len = (size_t)(close - (s + 1));
    if (len == 0 || len >= sizeof addr)
      return -1;
    memcpy(addr, s + 1, len);
    addr[len] = '\0';
    if (close[1] != ':' || port_parse(close + 2, &cfg->mcast_port))
      return -1;
    cfg->family = AF_INET6;
  } else {
    const char *colon = strrchr(s, ':');
    size_t len;
    if (!colon)
      return -1;
    len = (size_t)(colon - s);
    if (len == 0 || len >= sizeof addr)
      return -1;
    memcpy(addr, s, len);
    addr[len] = '\0';
    if (port_parse(colon + 1, &cfg->mcast_port))
      return -1;
    cfg->family = AF_INET;
  }

  if (cfg->family == AF_INET) {
    struct in_addr a;
    if (inet_pton(AF_INET, addr, &a) != 1)
      return -1;
    if ((ntohl(a.s_addr) >> 28) != 0xE)
      return -1;
  } else {
    struct in6_addr a6;
    if (inet_pton(AF_INET6, addr, &a6) != 1)
      return -1;
    if (a6.s6_addr[0] != 0xFF)
      return -1;
  }

  strncpy(cfg->mcast_group, addr, sizeof cfg->mcast_group - 1);
  cfg->mcast_group[sizeof cfg->mcast_group - 1] = '\0';
  return 0;
}

void mcast_describe(const config_t *cfg, char *buf, size_t n) {
  if (cfg->family == AF_INET6)
    snprintf(buf, n, "[%s]:%u", cfg->mcast_group, cfg->mcast_port);
  else
    snprintf(buf, n, "%s:%u", cfg->mcast_group, cfg->mcast_port);
}

typedef struct {
  const char *name;
  int value;
} enum_map_t;

static int map_lookup(const enum_map_t *m, size_t n, const char *s, int *out) {
  size_t i;
  for (i = 0; i < n; i++)
    if (strcmp(s, m[i].name) == 0) {
      *out = m[i].value;
      return 0;
    }
  return -1;
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
      "  -v, --verbose          periodic stats on stderr\n"
      "      --color <when>     auto|always|never (default auto)\n"
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
      {"verbose", no_argument, 0, 'v'},
      {"color", required_argument, 0, 1000},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int have_a = 0, have_l = 0, have_mcast = 0, have_t = 0, have_w = 0;
  long t_value = 0, w_value = 0;
  int c;

  memset(cfg, 0, sizeof *cfg);
  optind = 1;
  while ((c = getopt_long(argc, argv, "ali:M:w:m:I:t:o:C:vh", longopts, NULL)) != -1) {
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
    case 'v':
      cfg->verbose = 1;
      break;
    case 1000: {
      static const enum_map_t map[] = {{"auto", 0}, {"always", 1}, {"never", 2}};
      int v;
      if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
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
  if (have_a == have_l) {
    argerr("exactly one of -a/--announce or -l/--listen is required");
    return ARGS_ERR;
  }
  if (!have_mcast) {
    argerr("missing -m multicast group:port");
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
    if (!cfg->output_path)
      cfg->output_path = "-";
    cfg->timeout_s = have_t ? t_value : 35;
  }
  return ARGS_OK;
}
