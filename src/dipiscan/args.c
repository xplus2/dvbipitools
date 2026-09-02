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

#include "lib/helper/argutil.h"
#include "lib/helper/log.h"

#include "args.h"
#include "version.h"

static void argerr(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void argerr(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  argutil_verr(TOOL_NAME, fmt, ap);
  va_end(ap);
}

/* full multicast address, family from ':' presence */
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

void args_range_describe(const config_t *cfg, char *buf, size_t n) {
  char lo[64], hi[64];
  int af = cfg->family == AF_INET6 ? AF_INET6 : AF_INET;
  inet_ntop(af, cfg->start, lo, sizeof lo);
  inet_ntop(af, cfg->end, hi, sizeof hi);
  snprintf(buf, n, "%s-%s", lo, hi);
}

/* cap on swept addresses: do not sweep millions of candidates */
#define MAX_SWEEP_HOSTBITS 20
#define MAX_SWEEP_ADDRS ((1u << MAX_SWEEP_HOSTBITS) - 2u)

static void addr_incr1(unsigned char *a, int alen) {
  for (int i = alen - 1; i >= 0; i--) if (++a[i]) break;
}

static void addr_decr1(unsigned char *a, int alen) {
  for (int i = alen - 1; i >= 0; i--) if (a[i]--) break;
}

/* end-start, capped. -1 if end<start or range exceeds cap */
static int addr_diff_capped(const unsigned char *start, const unsigned char *end, int alen, unsigned cap, unsigned *out) {
  unsigned char diff[16] = {0};
  int borrow = 0;
  for (int i = alen - 1; i >= 0; i--) {
    int d = (int)end[i] - (int)start[i] - borrow;
    if (d < 0) {
      d += 256;
      borrow = 1;
    } else {
      borrow = 0;
    }
    diff[i] = (unsigned char)d;
  }
  if (borrow)
    return -1;
  for (int i = 0; i < alen - 4; i++)
    if (diff[i])
      return -1;
  {
    unsigned val = 0;
    for (int i = alen >= 4 ? alen - 4 : 0; i < alen; i++) val = (val << 8) | diff[i];
    if (val > cap) return -1;
    *out = val;
  }
  return 0;
}

/* addr/prefixlen. host range is net+1 .. broadcast-1 */
static int cidr_parse(const char *addrs, const char *prefixs, int *family, unsigned char *start, unsigned char *end, unsigned *total) {
  unsigned char addr[16], net[16], top[16];
  int fam, alen, maxprefix, hostbits, bit;
  char *pend;
  long prefix;

  if (base_parse(addrs, &fam, addr)) return -1;
  errno = 0;
  prefix = strtol(prefixs, &pend, 10);
  if (errno || pend == prefixs || *pend != '\0' || prefix < 0)
    return -1;

  alen = (fam == AF_INET6) ? 16 : 4;
  maxprefix = alen * 8;
  if (prefix > maxprefix - 2) /* need >= 2 host bits */
    return -1;
  hostbits = maxprefix - (int)prefix;
  if (hostbits > MAX_SWEEP_HOSTBITS)
    return -1;

  memcpy(net, addr, (size_t)alen);
  memcpy(top, addr, (size_t)alen);
  bit = 0;
  for (int i = alen - 1; i >= 0 && bit < hostbits; i--) {
    int bits_here = hostbits - bit < 8 ? hostbits - bit : 8;
    unsigned char mask = (unsigned char)((1u << bits_here) - 1);
    net[i] &= (unsigned char)~mask;
    top[i] |= mask;
    bit += bits_here;
  }

  memcpy(start, net, (size_t)alen);
  addr_incr1(start, alen);
  memcpy(end, top, (size_t)alen);
  addr_decr1(end, alen);
  *family = fam;
  *total = (1u << hostbits) - 2u;
  return 0;
}

/* startaddr-stopaddr (incl.) */
static int range_parse(const char *los, const char *his, int *family, unsigned char *start, unsigned char *end, unsigned *total) {
  int fam_lo, fam_hi, alen;
  unsigned char lo[16], hi[16];
  unsigned diff;

  if (base_parse(los, &fam_lo, lo) || base_parse(his, &fam_hi, hi))
    return -1;
  if (fam_lo != fam_hi)
    return -1;
  alen = (fam_lo == AF_INET6) ? 16 : 4;
  if (memcmp(lo, hi, (size_t)alen) > 0)
    return -1;
  if (addr_diff_capped(lo, hi, alen, MAX_SWEEP_ADDRS - 1u, &diff))
    return -1;

  *family = fam_lo;
  memcpy(start, lo, 16);
  memcpy(end, hi, 16);
  *total = diff + 1u;
  return 0;
}

/* default /24, last byte swept 1..254 */
static void plain_parse(const unsigned char *addr, int family, unsigned char *start, unsigned char *end, unsigned *total) {
  int alen = (family == AF_INET6) ? 16 : 4;
  memcpy(start, addr, 16);
  memcpy(end, addr, 16);
  start[alen - 1] = 1;
  end[alen - 1] = 254;
  *total = 254;
}

/* plain addr, CIDR or startaddr-stopaddr */
static int mcast_range_parse(const char *s, int *family, unsigned char *start, unsigned char *end, unsigned *total) {
  const char *slash = strchr(s, '/');
  const char *dash = strchr(s, '-');

  if (slash) {
    char addrbuf[64];
    size_t len = (size_t)(slash - s);
    if (len == 0 || len >= sizeof addrbuf)
      return -1;
    memcpy(addrbuf, s, len);
    addrbuf[len] = '\0';
    return cidr_parse(addrbuf, slash + 1, family, start, end, total);
  }
  if (dash) {
    char lobuf[64];
    size_t len = (size_t)(dash - s);
    if (len == 0 || len >= sizeof lobuf)
      return -1;
    memcpy(lobuf, s, len);
    lobuf[len] = '\0';
    return range_parse(lobuf, dash + 1, family, start, end, total);
  }
  {
    unsigned char addr[16];
    if (base_parse(s, family, addr))
      return -1;
    plain_parse(addr, *family, start, end, total);
    return 0;
  }
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
static int http_proxy_parse(const char *s, config_t *cfg) {
  const char *p = s;
  size_t len;

  if (*p == '[') {
    const char *close = strchr(p, ']');
    if (!close)
      return -1;
    len = (size_t)(close - (p + 1));
    if (len == 0 || len >= sizeof cfg->http_proxy_host)
      return -1;
    memcpy(cfg->http_proxy_host, p + 1, len);
    cfg->http_proxy_host[len] = '\0';
    p = close + 1;
  } else {
    const char *hp = p;
    while (*hp && *hp != ':')
      hp++;
    len = (size_t)(hp - p);
    if (len == 0 || len >= sizeof cfg->http_proxy_host)
      return -1;
    memcpy(cfg->http_proxy_host, p, len);
    cfg->http_proxy_host[len] = '\0';
    p = hp;
  }

  if (*p == ':')
    return port_num_parse(p + 1, &cfg->http_proxy_port);
  if (*p != '\0')
    return -1;
  cfg->http_proxy_port = 80;
  return 0;
}

/* %g (group), %p (port), %% (literal %). anything else after % is invalid */
static int http_path_tmpl_valid(const char *t) {
  while (*t) {
    size_t step = 1;
    if (*t == '%') {
      if (t[1] != 'g' && t[1] != 'p' && t[1] != '%')
        return -1;
      step = 2;
    }
    t += step;
  }
  return 0;
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
      "  -m, --mcast <addr>       base multicast group, v4 or v6; the last\n"
      "                           byte is swept 1..254                  [239.19.75.0]\n"
      "                           or <addr>/<prefixlen>, host range swept\n"
      "                           or <startaddr>-<stopaddr>, swept as given\n"
      "  -p, --port <port[-port]> port or inclusive port range          [8700]\n"
      "  -f, --format <fmt>       m3u|csv|xspf|xml|null                 [m3u]\n"
      "  -P, --provider <name>    DomainName (required on -f xml)\n"
      "  -o, --out <path>         output file, or \"-\" for stdout      [stdout]\n"
      "  -t, --timeout <secs>     wall-clock budget per candidate       [1]\n"
      "  -j, --jets <jets>        concurrent probing threads            [1]\n"
      "  -M, --mpts               report every program at an address,\n"
      "                           waits out the whole timeout budget per address\n"
      "  -u, --http-proxy <ip:port>  probe via an HTTP TS proxy instead of a\n"
      "                           direct IGMP/MLD join\n"
      "  -x, --http-path <tmpl>   proxy request path per candidate, -u only\n"
      "                           %%g=group %%p=port %%%%=literal %%    [/udp/%%g:%%p/]\n"
      "  -I, --iface <iface>      interface for the multicast join      [kernel default]\n"
      "  -v, --verbose            per-candidate diagnostics on stderr\n"
      "      --color <when>       auto|always|never                     [auto]\n"
      "  -h, --help               this help\n\n"
      "examples:\n"
      "  %s -m 239.19.75.0 -p 8700-8705 >hd.m3u\n"
      "  %s -v -f csv -o scan.csv\n"
      "  %s -u 127.0.0.1:8080 -m 239.19.75.0 -f xspf >playlist.xspf\n"
      "  %s -f xml -P example.org -o scan.xml    # feed straight into dipisds -a -i\n"
      "  %s -M -t 3 -f xml -P example.org -o scan.xml  # MPTS addresses too\n"
      "  %s -m 239.19.75.0/23 -p 8700-8705 >hd.m3u  # sweep a CIDR block\n"
      "  %s -m 239.19.75.10-239.19.75.20 >hd.m3u    # sweep an explicit range\n\n",
      TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME);
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  static const struct option longopts[] = {
      {"mcast", required_argument, 0, 'm'},
      {"port", required_argument, 0, 'p'},
      {"format", required_argument, 0, 'f'},
      {"provider", required_argument, 0, 'P'},
      {"out", required_argument, 0, 'o'},
      {"timeout", required_argument, 0, 't'},
      {"jets", required_argument, 0, 'j'},
      {"mpts", no_argument, 0, 'M'},
      {"http-proxy", required_argument, 0, 'u'},
      {"http-path", required_argument, 0, 'x'},
      {"iface", required_argument, 0, 'I'},
      {"verbose", no_argument, 0, 'v'},
      {"color", required_argument, 0, 1001},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int c;

  if (argc == 1)
    return ARGS_NOARGS;

  memset(cfg, 0, sizeof *cfg);
  {
    unsigned char def[16];
    base_parse("239.19.75.0", &cfg->family, def);
    plain_parse(def, cfg->family, cfg->start, cfg->end, &cfg->total);
  }
  cfg->port_lo = cfg->port_hi = 8700;
  cfg->format = OUT_M3U;
  cfg->timeout_ms = 1000;
  cfg->jets = 1;
  optind = 1;
  while ((c = getopt_long(argc, argv, "m:p:f:P:o:t:j:Mu:x:I:vh", longopts, NULL)) != -1) {
    switch (c) {
      case 'm':
        if (mcast_range_parse(optarg, &cfg->family, cfg->start, cfg->end, &cfg->total)) {
          argerr("invalid -m address: %s (addr, addr/prefixlen, or startaddr-stopaddr)", optarg);
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
      case 'j': {
        char *end;
        long v = strtol(optarg, &end, 10);
        if (*end != '\0' || v < 1 || v > DIPISCAN_MAX_JETS) {
          argerr("invalid -j jets: %s (1..%d)", optarg, DIPISCAN_MAX_JETS);
          return ARGS_ERR;
        }
        cfg->jets = (unsigned)v;
        break;
      }
      case 'M':
        cfg->mpts = 1;
        break;
      case 'u':
        if (http_proxy_parse(optarg, cfg)) {
          argerr("invalid -u http-proxy address: %s", optarg);
          return ARGS_ERR;
        }
        cfg->http_proxy = 1;
        break;
      case 'x':
        if (http_path_tmpl_valid(optarg)) {
          argerr("invalid -x path template: %s (%%g, %%p, %%%% only)", optarg);
          return ARGS_ERR;
        }
        cfg->http_path_tmpl = optarg;
        break;
      case 'I':
        cfg->iface = optarg;
        break;
      case 'v':
        cfg->verbose = 1;
        break;
      case 1001: {
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
  if (cfg->http_path_tmpl && !cfg->http_proxy)
    log_line(TOOL_NAME ": --http-path has no effect without -u/--http-proxy");
  return ARGS_OK;
}
