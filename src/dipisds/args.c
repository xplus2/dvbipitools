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

static int ret_addr_parse(const char *s, char *addr_out, size_t addr_cap, unsigned *port_out) {
  char addr[64];

  if (*s == '[') {
    const char *close = strchr(s, ']');
    struct in6_addr a6;
    size_t len;
    if (!close)
      return -1;
    len = (size_t)(close - (s + 1));
    if (len == 0 || len >= sizeof addr)
      return -1;
    memcpy(addr, s + 1, len);
    addr[len] = '\0';
    if (close[1] != ':' || port_parse(close + 2, port_out))
      return -1;
    if (inet_pton(AF_INET6, addr, &a6) != 1)
      return -1;
  } else {
    const char *colon = strrchr(s, ':');
    struct in_addr a4;
    size_t len;
    if (!colon)
      return -1;
    len = (size_t)(colon - s);
    if (len == 0 || len >= sizeof addr)
      return -1;
    memcpy(addr, s, len);
    addr[len] = '\0';
    if (port_parse(colon + 1, port_out))
      return -1;
    if (inet_pton(AF_INET, addr, &a4) != 1)
      return -1;
  }
  if (strlen(addr) >= addr_cap)
    return -1;
  strcpy(addr_out, addr);
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

static int has_suffix(const char *s, const char *sfx) {
  size_t ls = strlen(s), lx = strlen(sfx);
  return ls >= lx && !strcmp(s + ls - lx, sfx);
}

static void print_help(void) {
  printf(
      "usage: %s -a -i <path> -m <mcast>:<port> [options]\n"
      "       %s -l -m <mcast>:<port> [options]\n\n"
      "DVBSTP / SD&S (ETSI TS 102 034) service discovery: announce a service list on\n"
      "multicast, or listen for one and write a playlist\n\n"
      "options:\n"
      "  -a, --announce         headend mode: read -i, transmit on -m\n"
      "  -l, --listen           client mode: receive on -m, write -o\n"
      "  -i, --input <path>     announce: .csv/.m3u/.xspf playlist or raw SD&S .xml\n"
      "  -p, --provider <name>  announce: DomainName (required unless -i is .xml)\n"
      "  -O, --offering <name>  announce: display name (required unless -i is .xml)\n"
      "  -L, --lang <code>      announce: ISO 639-2 for the display name (default deu)\n"
      "  -m, --mcast <g>:<p>    multicast group:port ([addr6]:port for v6)\n"
      "  -I, --iface <iface>    multicast interface\n"
      "  -t, --interval <s>     announce: repeat interval (default 5)\n"
      "  -t, --timeout <s>      listen: stop after N seconds (default 35)\n"
      "  -o, --output <path>    listen: output path, - for stdout (default)\n"
      "  -f, --format <fmt>     listen: m3u|csv|xspf|xml|null (default from -o suffix)\n"
      "  -v, --verbose          periodic stats on stderr\n"
      "      --color <when>     auto|always|never (default auto)\n"
      "      --ret-addr <a>:<p> announce: advertise a dipiret RET server (its -l value);\n"
      "                         opt-in, adds RTPRetransmission to every announced service\n"
      "      --ret-rtx-time <ms> announce: RET rtx-time, matches dipiret -B (default 2000)\n"
      "      --ret-rtx-pt <n>   announce: RET RTP payload type, matches dipiret -R (default 99)\n"
      "      --ret-mc           announce: also advertise multicast RET (dipiret without --no-mc-ret)\n"
      "      --ret-mc-port <p>  announce: multicast RET port, matches dipiret -F (default: each\n"
      "                         service's own port)\n"
      "  -h, --help             this help\n\n"
      "examples:\n"
      "  %s -a -i channels.csv -p example.org -O \"My Headend\" -m 239.255.0.1:3937\n"
      "  %s -l -m 239.255.0.1:3937 -o discovered.m3u\n",
      TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME);
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  static const struct option longopts[] = {
      {"announce", no_argument, 0, 'a'},
      {"listen", no_argument, 0, 'l'},
      {"input", required_argument, 0, 'i'},
      {"provider", required_argument, 0, 'p'},
      {"offering", required_argument, 0, 'O'},
      {"lang", required_argument, 0, 'L'},
      {"mcast", required_argument, 0, 'm'},
      {"iface", required_argument, 0, 'I'},
      {"interval", required_argument, 0, 't'},
      {"timeout", required_argument, 0, 't'},
      {"output", required_argument, 0, 'o'},
      {"format", required_argument, 0, 'f'},
      {"verbose", no_argument, 0, 'v'},
      {"color", required_argument, 0, 1000},
      {"ret-addr", required_argument, 0, 1001},
      {"ret-rtx-time", required_argument, 0, 1002},
      {"ret-rtx-pt", required_argument, 0, 1003},
      {"ret-mc", no_argument, 0, 1004},
      {"ret-mc-port", required_argument, 0, 1005},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int have_a = 0, have_l = 0, have_mcast = 0, have_t = 0;
  int have_ret_rtx_time = 0, have_ret_rtx_pt = 0, have_ret_mc_port = 0;
  long t_value = 0;
  int c;

  memset(cfg, 0, sizeof *cfg);
  cfg->format = OUT_M3U;
  optind = 1;
  while ((c = getopt_long(argc, argv, "ali:p:O:L:m:I:t:o:f:vh", longopts, NULL)) != -1) {
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
    case 'p':
      cfg->provider = optarg;
      break;
    case 'O':
      cfg->offering = optarg;
      break;
    case 'L':
      if (strlen(optarg) != 3) {
        argerr("invalid -L lang: %s (3-letter ISO 639-2 code)", optarg);
        return ARGS_ERR;
      }
      memcpy(cfg->lang, optarg, 3);
      break;
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
    case 'f': {
      static const enum_map_t map[] = {{"m3u", OUT_M3U}, {"csv", OUT_CSV}, {"xspf", OUT_XSPF}, {"xml", OUT_XML}, {"null", OUT_NULL}};
      int v;
      if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
        argerr("invalid --format: %s (m3u|csv|xspf|xml|null)", optarg);
        return ARGS_ERR;
      }
      cfg->format = (out_fmt_t)v;
      break;
    }
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
    case 1001:
      if (ret_addr_parse(optarg, cfg->ret_addr, sizeof cfg->ret_addr, &cfg->ret_port)) {
        argerr("invalid --ret-addr: %s", optarg);
        return ARGS_ERR;
      }
      cfg->ret_enabled = 1;
      break;
    case 1002: {
      char *end;
      unsigned long v = strtoul(optarg, &end, 10);
      if (*end != '\0' || v == 0) {
        argerr("invalid --ret-rtx-time: %s", optarg);
        return ARGS_ERR;
      }
      cfg->ret_rtx_time = (unsigned)v;
      have_ret_rtx_time = 1;
      break;
    }
    case 1003: {
      char *end;
      unsigned long v = strtoul(optarg, &end, 10);
      if (*end != '\0' || v > 127) {
        argerr("invalid --ret-rtx-pt: %s (0..127)", optarg);
        return ARGS_ERR;
      }
      cfg->ret_rtx_pt = (unsigned char)v;
      have_ret_rtx_pt = 1;
      break;
    }
    case 1004:
      cfg->ret_mc = 1;
      break;
    case 1005: {
      unsigned v;
      if (port_parse(optarg, &v)) {
        argerr("invalid --ret-mc-port: %s", optarg);
        return ARGS_ERR;
      }
      cfg->ret_mc_port = v;
      have_ret_mc_port = 1;
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
    if (!has_suffix(cfg->input_path, ".xml")) {
      if (!cfg->provider) {
        argerr("missing -p provider (required unless -i is .xml)");
        return ARGS_ERR;
      }
      if (!cfg->offering) {
        argerr("missing -O offering (required unless -i is .xml)");
        return ARGS_ERR;
      }
    }
    if (!cfg->lang[0])
      memcpy(cfg->lang, "deu", 3);
    cfg->interval_s = have_t ? t_value : 5;
    if (cfg->ret_enabled && has_suffix(cfg->input_path, ".xml")) {
      argerr("--ret-addr has no effect with a raw .xml -i input (that path is sent through unparsed)");
      return ARGS_ERR;
    }
    if (!cfg->ret_enabled && (have_ret_rtx_time || have_ret_rtx_pt || cfg->ret_mc || have_ret_mc_port)) {
      argerr("--ret-rtx-time/--ret-rtx-pt/--ret-mc/--ret-mc-port require --ret-addr");
      return ARGS_ERR;
    }
    if (cfg->ret_enabled) {
      if (!have_ret_rtx_time)
        cfg->ret_rtx_time = 2000;
      if (!have_ret_rtx_pt)
        cfg->ret_rtx_pt = 99;
    }
  } else {
    if (cfg->ret_enabled || have_ret_rtx_time || have_ret_rtx_pt || cfg->ret_mc || have_ret_mc_port) {
      argerr("--ret-* options are announce-only");
      return ARGS_ERR;
    }
    if (!cfg->output_path)
      cfg->output_path = "-";
    if (have_t)
      cfg->timeout_s = t_value;
    else
      cfg->timeout_s = 35;
  }
  return ARGS_OK;
}
