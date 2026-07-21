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

/* <addr>:<port> or [<addr6>]:<port>, multicast literal required */
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
    if ((ntohl(a.s_addr) >> 28) != 0xE) /* 224.0.0.0/4 */
      return -1;
  } else {
    struct in6_addr a6;
    if (inet_pton(AF_INET6, addr, &a6) != 1)
      return -1;
    if (a6.s6_addr[0] != 0xFF) /* ff00::/8 */
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

static int id_parse(const char *s, unsigned *out) {
  char *end;
  unsigned long v;
  v = strtoul(s, &end, 10);
  if (*end != '\0' || v == 0 || v > 0xFFFF)
    return -1;
  *out = (unsigned)v;
  return 0;
}

static void print_help(void) {
  printf(
      "usage: %s -i <uri> -m <mcast>:<port> [options]\n\n"
      "fetch an icecast/shoutcast stream and re-mux it as a DVB-IPI multicast\n\n"
      "options:\n"
      "  -i, --input <uri>      icecast/shoutcast source, http:// or https://\n"
      "  -m, --mcast <g>:<p>    output multicast group:port ([addr6]:port for v6)\n"
      "  -I, --iface <iface>    outgoing multicast interface\n"
      "  -r, --rtp              wrap output in RTP (default: plain UDP)\n"
      "  -n, --nit <text>       NIT network_name\n"
      "  -s, --sdt <text>       SDT service_name\n"
      "  -e, --error <seconds>  on input error, reconnect after N s (default: fail once)\n"
      "  -k, --insecure         skip TLS verification (self-signed, hostname, expiry)\n"
      "      --tsid <n>         transport_stream_id (default 1)\n"
      "      --onid <n>         original_network_id (default 1)\n"
      "      --sid <n>          service_id / program_number (default 1)\n"
      "  -v, --verbose          periodic stats on stderr\n"
      "      --color <when>     auto|always|never (default auto)\n"
      "  -h, --help             this help\n\n"
      "examples:\n"
      "  %s -i https://orf-live.ors-shoutcast.at/oe1-q2a.m3u -m 239.1.1.1:5000 -s \"OE1\"\n"
      "  %s -i http://radio886.at/streams/radio_88.6/aac -m 239.1.1.2:5000 -r -e 5\n",
      TOOL_NAME, TOOL_NAME, TOOL_NAME);
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  static const struct option longopts[] = {
      {"input", required_argument, 0, 'i'},
      {"mcast", required_argument, 0, 'm'},
      {"iface", required_argument, 0, 'I'},
      {"rtp", no_argument, 0, 'r'},
      {"nit", required_argument, 0, 'n'},
      {"sdt", required_argument, 0, 's'},
      {"error", required_argument, 0, 'e'},
      {"insecure", no_argument, 0, 'k'},
      {"tsid", required_argument, 0, 1000},
      {"onid", required_argument, 0, 1001},
      {"sid", required_argument, 0, 1002},
      {"verbose", no_argument, 0, 'v'},
      {"color", required_argument, 0, 1003},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int have_in = 0, have_mcast = 0;
  int c;

  memset(cfg, 0, sizeof *cfg);
  cfg->tsid = 1;
  cfg->onid = 1;
  cfg->sid = 1;
  optind = 1;
  while ((c = getopt_long(argc, argv, "i:m:I:rn:s:e:kvh", longopts, NULL)) != -1) {
    switch (c) {
      case 'i':
        cfg->input_uri = optarg;
        have_in = 1;
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
      case 'r':
        cfg->rtp = 1;
        break;
      case 'n':
        snprintf(cfg->nit_text, sizeof cfg->nit_text, "%s", optarg);
        break;
      case 's':
        snprintf(cfg->sdt_text, sizeof cfg->sdt_text, "%s", optarg);
        break;
      case 'e': {
        char *end;
        long v = strtol(optarg, &end, 10);
        if (*end != '\0' || v < 0) {
          argerr("invalid -e seconds: %s", optarg);
          return ARGS_ERR;
        }
        cfg->error_retry_s = v;
        break;
      }
      case 'k':
        cfg->insecure_tls = 1;
        break;
      case 1000:
        if (id_parse(optarg, &cfg->tsid)) {
          argerr("invalid --tsid: %s (1..65535)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1001:
        if (id_parse(optarg, &cfg->onid)) {
          argerr("invalid --onid: %s (1..65535)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1002:
        if (id_parse(optarg, &cfg->sid)) {
          argerr("invalid --sid: %s (1..65535)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1003: {
        static const enum_map_t map[] = {{"auto", 0}, {"always", 1}, {"never", 2}};
        int v;
        if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
          argerr("invalid --color: %s (auto|always|never)", optarg);
          return ARGS_ERR;
        }
        cfg->color_mode = v;
        break;
      }
      case 'v':
        cfg->verbose = 1;
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
  if (!have_in) {
    argerr("missing -i input");
    return ARGS_ERR;
  }
  if (!have_mcast) {
    argerr("missing -m output multicast");
    return ARGS_ERR;
  }
  if (!cfg->sdt_text[0])
    snprintf(cfg->sdt_text, sizeof cfg->sdt_text, "%s", TOOL_NAME);
  return ARGS_OK;
}
