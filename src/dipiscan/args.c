/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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

/* full multicast address, family from ':' presence. last byte becomes the
 * sweep counter in scan.c */
static int base_parse(const char *s, int *family, unsigned char *base) {
  if (strchr(s, ':')) {
    struct in6_addr a6;
    if (inet_pton(AF_INET6, s, &a6) != 1)
      return -1;
    if (a6.s6_addr[0] != 0xFF) /* ff00::/8 */
      return -1;
    memcpy(base, &a6, 16);
    *family = AF_INET6;
  } else {
    struct in_addr a;
    if (inet_pton(AF_INET, s, &a) != 1)
      return -1;
    if ((ntohl(a.s_addr) >> 28) != 0xE) /* 224.0.0.0/4 */
      return -1;
    memcpy(base, &a, 4);
    *family = AF_INET;
  }
  return 0;
}

void args_base_describe(const config_t *cfg, char *buf, size_t n) {
  inet_ntop( cfg->family == AF_INET6 ? AF_INET6 : AF_INET, cfg->base, buf, (socklen_t)n);
}

/* port 1..65535, digits only */
static int port_num_parse(const char *p, unsigned *out) {
  char *end;
  unsigned long v;
  if (*p == '\0')
    return -1;
  errno = 0;
  v = strtoul(p, &end, 10);
  if (errno || *end != '\0' || v == 0 || v > 65535)
    return -1;
  *out = (unsigned)v;
  return 0;
}

/* port or port-port, inclusive range */
static int port_range_parse(const char *s, unsigned *lo, unsigned *hi) {
  const char *dash = strchr(s, '-');
  if (!dash) {
    if (port_num_parse(s, lo))
      return -1;
    *hi = *lo;
    return 0;
  }
  {
    char buf[16];
    size_t len = (size_t)(dash - s);
    if (len == 0 || len >= sizeof buf)
      return -1;
    memcpy(buf, s, len);
    buf[len] = '\0';
    if (port_num_parse(buf, lo))
      return -1;
  }
  if (port_num_parse(dash + 1, hi))
    return -1;
  if (*lo > *hi)
    return -1;
  return 0;
}

/* host[:port], IPv6 host in brackets. port optional, default 80 */
static int udpxy_parse(const char *s, config_t *cfg) {
  const char *p = s;
  size_t len;

  if (*p == '[') {
    const char *close = strchr(p, ']');
    if (!close)
      return -1;
    len = (size_t)(close - (p + 1));
    if (len == 0 || len >= sizeof cfg->udpxy_host)
      return -1;
    memcpy(cfg->udpxy_host, p + 1, len);
    cfg->udpxy_host[len] = '\0';
    p = close + 1;
  } else {
    const char *hp = p;
    while (*hp && *hp != ':')
      hp++;
    len = (size_t)(hp - p);
    if (len == 0 || len >= sizeof cfg->udpxy_host)
      return -1;
    memcpy(cfg->udpxy_host, p, len);
    cfg->udpxy_host[len] = '\0';
    p = hp;
  }

  if (*p == ':')
    return port_num_parse(p + 1, &cfg->udpxy_port);
  if (*p != '\0')
    return -1;
  cfg->udpxy_port = 80;
  return 0;
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

static int fmt_from_name(const char *s, out_fmt_t *f) {
  static const enum_map_t map[] = {{"m3u", OUT_M3U}, {"csv", OUT_CSV}, {"xspf", OUT_XSPF}, {"xml", OUT_XML}, {"null", OUT_NULL}};
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], s, &v))
    return -1;
  *f = (out_fmt_t)v;
  return 0;
}

static void print_help(void) {
  printf(
      "usage: %s [options] 1>playlist 2>log\n\n"
      "sweep a multicast /24 (or the analogous IPv6 range) for DVB-IPI\n"
      "services and write a playlist of what answered\n\n"
      "options:\n"
      "  -m, --mcast <addr>     base multicast group, v4 or v6; the last\n"
      "                         byte is swept 1..254               [239.2.16.0]\n"
      "  -p, --port <port[-port]> port or inclusive port range      [8208]\n"
      "  -f, --format <fmt>     m3u|csv|xspf|xml|null               [m3u]\n"
      "  -P, --provider <name>  DomainName for -f xml (required if xml)\n"
      "  -o, --out <path>       output file, or \"-\" for stdout      [stdout]\n"
      "  -t, --timeout <secs>   wall-clock budget per candidate     [1]\n"
      "  -u, --udpxy <ip:port>  use udpxy instead of a direct IGMP/MLD join\n"
      "  -I, --iface <iface>    interface for the multicast join   [kernel default]\n"
      "  -v, --verbose          per-candidate diagnostics on stderr\n"
      "      --color <when>     auto|always|never                  [auto]\n"
      "  -h, --help             this help\n\n"
      "examples:\n"
      "  %s -m 239.2.24.0 -p 8208-8229 >hd.m3u\n"
      "  %s -v -f csv -o scan.csv\n"
      "  %s -u 127.0.0.1:8080 -m 239.2.16.0 -f xspf >playlist.xspf\n"
      "  %s -f xml -P example.org -o scan.xml    # feed straight into dipisds -a -i\n\n",
      TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME);
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  static const struct option longopts[] = {
      {"mcast", required_argument, 0, 'm'},
      {"port", required_argument, 0, 'p'},
      {"format", required_argument, 0, 'f'},
      {"provider", required_argument, 0, 'P'},
      {"out", required_argument, 0, 'o'},
      {"timeout", required_argument, 0, 't'},
      {"udpxy", required_argument, 0, 'u'},
      {"iface", required_argument, 0, 'I'},
      {"verbose", no_argument, 0, 'v'},
      {"color", required_argument, 0, 1001},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int c;

  if (argc == 1)
    return ARGS_NOARGS;

  memset(cfg, 0, sizeof *cfg);
  base_parse("239.2.16.0", &cfg->family, cfg->base);
  cfg->port_lo = cfg->port_hi = 8208;
  cfg->format = OUT_M3U;
  cfg->timeout_ms = 1000;
  optind = 1;
  while ((c = getopt_long(argc, argv, "m:p:f:P:o:t:u:I:vh", longopts, NULL)) != -1) {
    switch (c) {
      case 'm':
        if (base_parse(optarg, &cfg->family, cfg->base)) {
          argerr("invalid -m address: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 'p':
        if (port_range_parse(optarg, &cfg->port_lo, &cfg->port_hi)) {
          argerr("invalid -p port range: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 'f':
        if (fmt_from_name(optarg, &cfg->format)) {
          argerr("invalid -f format: %s (m3u|csv|xspf|xml|null)", optarg);
          return ARGS_ERR;
        }
        break;
      case 'P':
        cfg->provider = optarg;
        break;
      case 'o':
        cfg->out_path = optarg;
        break;
      case 't': {
        char *end;
        long v = strtol(optarg, &end, 10);
        if (*end != '\0' || v <= 0 || v > 3600) {
          argerr("invalid -t timeout: %s (1..3600 seconds)", optarg);
          return ARGS_ERR;
        }
        cfg->timeout_ms = (int)(v * 1000);
        break;
      }
      case 'u':
        if (udpxy_parse(optarg, cfg)) {
          argerr("invalid -u udpxy address: %s", optarg);
          return ARGS_ERR;
        }
        cfg->udpxy = 1;
        break;
      case 'I':
        cfg->iface = optarg;
        break;
      case 'v':
        cfg->verbose = 1;
        break;
      case 1001: {
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
  if (cfg->format == OUT_XML && !cfg->provider) {
    argerr("missing -P provider (required for -f xml)");
    return ARGS_ERR;
  }
  return ARGS_OK;
}
