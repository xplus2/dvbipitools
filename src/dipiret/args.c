/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

/* [addr]:<port> or <addr4>:<port>, unicast, no multicast restriction (unlike dipitvhead's mcast_group_parse) */
static int addr_port_parse(const char *s, int *family, char *addr_out, size_t addr_out_sz, unsigned *port_out) {
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
    if (close[1] != ':' || port_parse(close + 2, port_out))
      return -1;
    *family = AF_INET6;
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
    if (port_parse(colon + 1, port_out))
      return -1;
    *family = AF_INET;
  }

  if (*family == AF_INET) {
    struct in_addr a;
    if (inet_pton(AF_INET, addr, &a) != 1)
      return -1;
  } else {
    struct in6_addr a6;
    if (inet_pton(AF_INET6, addr, &a6) != 1)
      return -1;
  }

  if (strlen(addr) >= addr_out_sz)
    return -1;
  strcpy(addr_out, addr);
  return 0;
}

/* comma-separated CIDR list (IPv4 or IPv6); light validation here, capture.c re-validates at BPF-build time */
static int ranges_parse(const char *s, config_t *cfg) {
  char buf[ARGS_MAX_RANGES * 64];
  char *tok, *save = NULL;
  if (strlen(s) >= sizeof buf)
    return -1;
  strcpy(buf, s);
  cfg->range_count = 0;
  for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
    char *slash = strchr(tok, '/');
    int is_v6 = strchr(tok, ':') != NULL;
    struct in_addr a4;
    struct in6_addr a6;
    long prefix;
    char *end;
    if (cfg->range_count >= ARGS_MAX_RANGES)
      return -1;
    if (!slash)
      return -1;
    *slash = '\0';
    if (is_v6 ? inet_pton(AF_INET6, tok, &a6) != 1 : inet_pton(AF_INET, tok, &a4) != 1)
      return -1;
    prefix = strtol(slash + 1, &end, 10);
    if (*end != '\0' || prefix < 0 || prefix > (is_v6 ? 128 : 32))
      return -1;
    *slash = '/';
    if (strlen(tok) >= sizeof cfg->ranges[0])
      return -1;
    strcpy(cfg->ranges[cfg->range_count], tok);
    cfg->range_ptrs[cfg->range_count] = cfg->ranges[cfg->range_count];
    cfg->range_count++;
  }
  return cfg->range_count ? 0 : -1;
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
      "usage: %s -g <range> -l <addr>:<port> [options]\n\n"
      "RTP retransmission (RET) server, DVB-IPI Annex F\n\n"
      "options:\n"
      "  -g, --range <cidr>[,<cidr>...]  multicast destination range(s) to buffer, IPv4 or IPv6\n"
      "  -l, --listen <addr>:<port>      unicast bind for client NACK requests / RTX replies\n"
      "  -I, --iface <iface>             capture interface (default: \"any\")\n"
      "      --bpf <expr>                raw BPF capture filter, overrides the -g auto-build\n"
      "  -B, --buffer <ms>                per-channel retransmission buffer depth (default: 2000)\n"
      "  -M, --max-channels <n>           preallocated channel slots (default: 0 = 384)\n"
      "  -R, --rtx-pt <n>                 RTP payload type for retransmitted packets (default: 99)\n"
      "  -F, --ff-port <port>             multicast RET session port (default: 0 = original channel's port)\n"
      "      --no-mc-ret                  disable the multicast RET session, unicast-only repair\n"
      "  -w, --workers <n>                -l socket worker threads (default: 0 = online CPU count)\n"
      "  -u, --user <user>                drop privileges to this user after opening the capture handle\n"
      "  -v, --verbose                    periodic stats on stderr\n"
      "      --color <when>               auto|always|never (default auto)\n"
      "  -h, --help                       this help\n\n"
      "example:\n"
      "  %s -g 239.0.0.0/8 -l 10.0.0.1:6000\n",
      TOOL_NAME, TOOL_NAME);
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  static const struct option longopts[] = {
      {"range", required_argument, 0, 'g'},
      {"listen", required_argument, 0, 'l'},
      {"iface", required_argument, 0, 'I'},
      {"bpf", required_argument, 0, 1000},
      {"buffer", required_argument, 0, 'B'},
      {"max-channels", required_argument, 0, 'M'},
      {"rtx-pt", required_argument, 0, 'R'},
      {"ff-port", required_argument, 0, 'F'},
      {"no-mc-ret", no_argument, 0, 1001},
      {"workers", required_argument, 0, 'w'},
      {"user", required_argument, 0, 'u'},
      {"verbose", no_argument, 0, 'v'},
      {"color", required_argument, 0, 1002},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int have_range = 0, have_listen = 0;
  int c;

  memset(cfg, 0, sizeof *cfg);
  cfg->buffer_ms = 2000;
  cfg->rtx_pt = 99;
  optind = 1;
  while ((c = getopt_long(argc, argv, "g:l:I:B:M:R:F:w:u:vh", longopts, NULL)) != -1) {
    switch (c) {
      case 'g':
        if (ranges_parse(optarg, cfg)) {
          argerr("invalid -g range: %s", optarg);
          return ARGS_ERR;
        }
        have_range = 1;
        break;
      case 'l':
        if (addr_port_parse(optarg, &cfg->listen_family, cfg->listen_addr, sizeof cfg->listen_addr, &cfg->listen_port)) {
          argerr("invalid -l addr:port: %s", optarg);
          return ARGS_ERR;
        }
        have_listen = 1;
        break;
      case 'I':
        cfg->iface = optarg;
        break;
      case 1000:
        cfg->bpf_expr = optarg;
        break;
      case 'B': {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0) {
          argerr("invalid -B buffer: %s (ms)", optarg);
          return ARGS_ERR;
        }
        cfg->buffer_ms = (unsigned)v;
        break;
      }
      case 'M': {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0') {
          argerr("invalid -M max-channels: %s", optarg);
          return ARGS_ERR;
        }
        cfg->max_channels = (size_t)v;
        break;
      }
      case 'R': {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v > 127) {
          argerr("invalid -R rtx-pt: %s (0..127)", optarg);
          return ARGS_ERR;
        }
        cfg->rtx_pt = (unsigned char)v;
        break;
      }
      case 'F': {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v > 65535) {
          argerr("invalid -F ff-port: %s", optarg);
          return ARGS_ERR;
        }
        cfg->ff_port = (unsigned)v;
        break;
      }
      case 1001:
        cfg->no_mc_ret = 1;
        break;
      case 'w': {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0') {
          argerr("invalid -w workers: %s", optarg);
          return ARGS_ERR;
        }
        cfg->workers = (unsigned)v;
        break;
      }
      case 'u':
        cfg->user = optarg;
        break;
      case 'v':
        cfg->verbose = 1;
        break;
      case 1002: {
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
        return ARGS_ERR; /* getopt already reported */
    }
  }
  if (optind < argc) {
    argerr("unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  if (!have_range) {
    argerr("missing -g range");
    return ARGS_ERR;
  }
  if (!have_listen) {
    argerr("missing -l listen");
    return ARGS_ERR;
  }
  if (cfg->workers == 0) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    cfg->workers = n > 0 ? (unsigned)n : 1;
  }
  return ARGS_OK;
}
