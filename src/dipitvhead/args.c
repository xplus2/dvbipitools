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

/* [@]<addr>:<port> or [@][<addr6>]:<port>, multicast literal required */
static int mcast_group_parse(const char *s, int *family, char *addr_out, size_t addr_out_sz, unsigned *port_out) {
  char addr[64];

  if (*s == '@')
    s++;
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
    if ((ntohl(a.s_addr) >> 28) != 0xE) /* 224.0.0.0/4 */
      return -1;
  } else {
    struct in6_addr a6;
    if (inet_pton(AF_INET6, addr, &a6) != 1)
      return -1;
    if (a6.s6_addr[0] != 0xFF) /* ff00::/8 */
      return -1;
  }

  if (strlen(addr) >= addr_out_sz)
    return -1;
  strcpy(addr_out, addr);
  return 0;
}

static int mcast_parse(const char *s, config_t *cfg) {
  return mcast_group_parse(s, &cfg->family, cfg->mcast_group, sizeof cfg->mcast_group, &cfg->mcast_port);
}

static int source_parse(const char *uri, source_t *s) {
  memset(s, 0, sizeof *s);
  if (strcmp(uri, "-") == 0) {
    s->kind = SRC_STDIN;
    return 0;
  }
  if (strncmp(uri, "rtp://", 6) == 0) {
    s->kind = SRC_RTP;
    return mcast_group_parse(uri + 6, &s->family, s->group, sizeof s->group, &s->port);
  }
  if (strncmp(uri, "udp://", 6) == 0) {
    s->kind = SRC_UDP;
    return mcast_group_parse(uri + 6, &s->family, s->group, sizeof s->group, &s->port);
  }
  if (strncmp(uri, "http://", 7) == 0 || strncmp(uri, "https://", 8) == 0) {
    s->kind = SRC_HTTP;
    return http_url_parse(uri, &s->http);
  }
  return -1;
}

void source_describe(const source_t *s, char *buf, size_t n) {
  switch (s->kind) {
  case SRC_RTP:
  case SRC_UDP: {
    const char *scheme = (s->kind == SRC_RTP) ? "rtp" : "udp";
    if (s->family == AF_INET6)
      snprintf(buf, n, "%s://@[%s]:%u", scheme, s->group, s->port);
    else
      snprintf(buf, n, "%s://@%s:%u", scheme, s->group, s->port);
    break;
  }
  case SRC_HTTP:
    snprintf(buf, n, "%s://%s:%u%s", s->http.tls ? "https" : "http", s->http.host, s->http.port, s->http.path);
    break;
  case SRC_STDIN:
    snprintf(buf, n, "-");
    break;
  }
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

/* organisation_id is 32 bits per TS 102 809, unlike application_id's 16 */
static int org_id_parse(const char *s, unsigned *out) {
  char *end;
  unsigned long v;
  v = strtoul(s, &end, 10);
  if (*end != '\0' || v == 0 || v > 0xFFFFFFFFUL)
    return -1;
  *out = (unsigned)v;
  return 0;
}

/* decimal or 0x-hex, PMT pid range: 0x0010..0x1FFE (0 = auto, handled by caller) */
static int pid_parse(const char *s, unsigned *out) {
  char *end;
  unsigned long v = strtoul(s, &end, 0);
  if (*end != '\0' || v > 0x1FFE)
    return -1;
  *out = (unsigned)v;
  return 0;
}

static void print_help(void) {
  printf(
      "usage: %s -i <uri> -m <mcast>:<port> [options]\n\n"
      "re-package a transport stream (already-muxed, not raw ES) as a DVB-IPI multicast\n\n"
      "options:\n"
      "  -i, --input <uri>          udp://, rtp://, http(s)://, or \"-\" for stdin\n"
      "  -p, --pmt-pid <pid>        select program by PMT PID (dec or 0x-hex; default: first live one)\n"
      "  -m, --mcast <g>:<p>        output multicast group:port ([addr6]:port for v6)\n"
      "  -I, --iface <iface>        outgoing multicast interface\n"
      "  -r, --rtp                  wrap output in RTP (default: plain UDP)\n"
      "  -T, --ttl <n>              multicast TTL / hop limit (default: 1)\n"
      "  -n, --nit <text|->         NIT: default passthrough source; \"-\" drops it; text = our own\n"
      "  -s, --sdt <text|->         SDT: default passthrough source; \"-\" drops it; text = our own\n"
      "  -b, --bitrate <kbps>       target output bitrate (default: no shaping)\n"
      "  -S, --stuff                null-packet stuffing up to -b's target (needs -b)\n"
      "  -B, --burst-limit          cap output at -b's target, never above (needs -b)\n"
      "      --strip-eit            drop source EIT (default: passed through)\n"
      "      --hbbtv <url>          inject an AIT signalling this HbbTV app (default: none)\n"
      "      --hbbtv-org-id <n>     HbbTV organisation_id (required with --hbbtv)\n"
      "      --hbbtv-app-id <n>     HbbTV application_id (required with --hbbtv)\n"
      "  -e, --error <seconds>      on input error, reconnect after N s (default: fail once)\n"
      "  -k, --insecure             skip TLS verification (self-signed, hostname, expiry)\n"
      "      --tsid <n>             transport_stream_id (default 1)\n"
      "      --onid <n>             original_network_id (default 1)\n"
      "      --sid <n>              service_id / program_number (default 1)\n"
      "  -v, --verbose              periodic stats on stderr\n"
      "      --color <when>         auto|always|never (default auto)\n"
      "  -h, --help                 this help\n\n"
      "examples:\n"
      "  %s -i rtp://@239.2.24.1:8208 -m 239.1.1.1:5000 -s \"My Channel\"\n"
      "  %s -i https://host/live/x/y.ts -m 239.1.1.2:5000 -b 8000 -S -B\n",
      TOOL_NAME, TOOL_NAME, TOOL_NAME);
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  static const struct option longopts[] = {
      {"input", required_argument, 0, 'i'},
      {"pmt-pid", required_argument, 0, 'p'},
      {"mcast", required_argument, 0, 'm'},
      {"iface", required_argument, 0, 'I'},
      {"rtp", no_argument, 0, 'r'},
      {"ttl", required_argument, 0, 'T'},
      {"nit", required_argument, 0, 'n'},
      {"sdt", required_argument, 0, 's'},
      {"bitrate", required_argument, 0, 'b'},
      {"stuff", no_argument, 0, 'S'},
      {"burst-limit", no_argument, 0, 'B'},
      {"strip-eit", no_argument, 0, 1000},
      {"hbbtv", required_argument, 0, 1001},
      {"hbbtv-org-id", required_argument, 0, 1002},
      {"hbbtv-app-id", required_argument, 0, 1003},
      {"error", required_argument, 0, 'e'},
      {"insecure", no_argument, 0, 'k'},
      {"tsid", required_argument, 0, 1004},
      {"onid", required_argument, 0, 1005},
      {"sid", required_argument, 0, 1006},
      {"verbose", no_argument, 0, 'v'},
      {"color", required_argument, 0, 1007},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int have_in = 0, have_mcast = 0, have_hbbtv_org = 0, have_hbbtv_app = 0;
  int c;

  memset(cfg, 0, sizeof *cfg);
  cfg->tsid = 1;
  cfg->onid = 1;
  cfg->sid = 1;
  optind = 1;
  while ((c = getopt_long(argc, argv, "i:p:m:I:rT:n:s:b:SBe:kvh", longopts, NULL)) != -1) {
    switch (c) {
      case 'i':
        if (source_parse(optarg, &cfg->input)) {
          argerr("invalid -i uri: %s", optarg);
          return ARGS_ERR;
        }
        have_in = 1;
        break;
      case 'p':
        if (pid_parse(optarg, &cfg->pmt_pid) || cfg->pmt_pid == 0) {
          argerr("invalid -p pmt-pid: %s (0x0010..0x1FFE)", optarg);
          return ARGS_ERR;
        }
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
      case 'T': {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0 || v > 255) {
          argerr("invalid -T ttl: %s (1..255)", optarg);
          return ARGS_ERR;
        }
        cfg->ttl = (unsigned)v;
        break;
      }
      case 'n':
        if (strcmp(optarg, "-") == 0) {
          cfg->nit_mode = TABLE_DROP;
        } else {
          cfg->nit_mode = TABLE_OVERRIDE;
          snprintf(cfg->nit_text, sizeof cfg->nit_text, "%s", optarg);
        }
        break;
      case 's':
        if (strcmp(optarg, "-") == 0) {
          cfg->sdt_mode = TABLE_DROP;
        } else {
          cfg->sdt_mode = TABLE_OVERRIDE;
          snprintf(cfg->sdt_text, sizeof cfg->sdt_text, "%s", optarg);
        }
        break;
      case 'b': {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0 || v > 1000000) {
          argerr("invalid -b bitrate: %s (kbps)", optarg);
          return ARGS_ERR;
        }
        cfg->bitrate_kbps = (unsigned)v;
        break;
      }
      case 'S':
        cfg->stuff = 1;
        break;
      case 'B':
        cfg->burst_limit = 1;
        break;
      case 1000:
        cfg->strip_eit = 1;
        break;
      case 1001:
        cfg->hbbtv_url = optarg;
        break;
      case 1002:
        if (org_id_parse(optarg, &cfg->hbbtv_org_id)) {
          argerr("invalid --hbbtv-org-id: %s", optarg);
          return ARGS_ERR;
        }
        have_hbbtv_org = 1;
        break;
      case 1003:
        if (id_parse(optarg, &cfg->hbbtv_app_id)) {
          argerr("invalid --hbbtv-app-id: %s", optarg);
          return ARGS_ERR;
        }
        have_hbbtv_app = 1;
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
      case 1004:
        if (id_parse(optarg, &cfg->tsid)) {
          argerr("invalid --tsid: %s (1..65535)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1005:
        if (id_parse(optarg, &cfg->onid)) {
          argerr("invalid --onid: %s (1..65535)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1006:
        if (id_parse(optarg, &cfg->sid)) {
          argerr("invalid --sid: %s (1..65535)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1007: {
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
  if ((cfg->stuff || cfg->burst_limit) && !cfg->bitrate_kbps) {
    argerr("-S/--stuff and -B/--burst-limit need -b/--bitrate");
    return ARGS_ERR;
  }
  if (cfg->hbbtv_url && (!have_hbbtv_org || !have_hbbtv_app)) {
    argerr("--hbbtv requires --hbbtv-org-id and --hbbtv-app-id");
    return ARGS_ERR;
  }
  if ((have_hbbtv_org || have_hbbtv_app) && !cfg->hbbtv_url) {
    argerr("--hbbtv-org-id/--hbbtv-app-id need --hbbtv");
    return ARGS_ERR;
  }
  return ARGS_OK;
}
