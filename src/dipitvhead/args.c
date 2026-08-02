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

/* 32-bit Super_CAS_id, conventionally written in hex; decimal also accepted */
static int cas_super_id_parse(const char *s, unsigned *out) {
  char *end;
  unsigned long v = strtoul(s, &end, 0);
  if (*end != '\0' || v == 0 || v > 0xFFFFFFFFUL)
    return -1;
  *out = (unsigned)v;
  return 0;
}

/* tcp://host:port/ or host:port; brackets required for a literal IPv6 host, e.g. [::1]:2222 */
static int cas_endpoint_parse(const char *s, char *host_out, size_t host_out_sz, unsigned *port_out) {
  const char *p = s, *host, *colon;
  size_t hostlen;
  char *end;
  unsigned long port;

  if (!strncmp(p, "tcp://", 6))
    p += 6;
  if (*p == '[') {
    const char *close = strchr(p, ']');
    if (!close)
      return -1;
    host = p + 1;
    hostlen = (size_t)(close - host);
    if (close[1] != ':')
      return -1;
    colon = close + 1;
  } else {
    host = p;
    colon = strrchr(p, ':');
    if (!colon)
      return -1;
    hostlen = (size_t)(colon - host);
  }
  if (hostlen == 0 || hostlen >= host_out_sz)
    return -1;
  memcpy(host_out, host, hostlen);
  host_out[hostlen] = '\0';

  port = strtoul(colon + 1, &end, 10);
  if (end == colon + 1 || port == 0 || port > 65535)
    return -1;
  if (*end != '\0' && *end != '/')
    return -1;
  *port_out = (unsigned)port;
  return 0;
}

/* 2 or 3, ETSI TS 103 197 Simulcrypt protocol versions; no other value is valid */
static int cas_version_parse(const char *s, unsigned *out) {
  char *end;
  unsigned long v = strtoul(s, &end, 10);
  if (*end != '\0' || (v != 2 && v != 3))
    return -1;
  *out = (unsigned)v;
  return 0;
}

static int cas_pids_parse(const char *s, config_t *cfg) {
  char buf[ARGS_MAX_CAS_PIDS * 8];
  char *tok, *save = NULL;
  if (strlen(s) >= sizeof buf)
    return -1;
  strcpy(buf, s);
  cfg->cas_pid_count = 0;
  for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
    unsigned pid;
    if (cfg->cas_pid_count >= ARGS_MAX_CAS_PIDS)
      return -1;
    if (pid_parse(tok, &pid) || pid == 0)
      return -1;
    cfg->cas_pids[cfg->cas_pid_count++] = pid;
  }
  return cfg->cas_pid_count ? 0 : -1;
}

static void print_help(void) {
  printf(
      "usage: %s -i <uri> -m <mcast>:<port> [options]\n\n"
      "re-package a transport stream (already-muxed, not raw ES) as a DVB-IPI multicast\n\n"
      "options:\n"
      "  -i, --input <uri>          udp://, rtp://, http(s)://, or \"-\" for stdin\n"
      "  -p, --pmt-pid <pid>        select program by PMT PID (dec or 0x-hex; default: first live one)\n"
      "  -m, --mcast <g>:<p>        output multicast group:port ([addr6]:port for v6)\n"
      "  -I, --iface <iface>        incoming multicast interface\n"
      "  -O, --out-iface <iface>    outgoing multicast interface\n"
      "  -u, --udp                  plain UDP output (default: RTP-wrapped)\n"
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
      "      --cas-algo <a>         enable CAS: cissa|csa2 (default: disabled)\n"
      "      --cas-ecmg <ep>        ECMG address, tcp://host:port (required with --cas-algo)\n"
      "      --cas-ecmg-version <n> ECMG protocol version 2|3 (default: auto-negotiate)\n"
      "      --cas-super-id <n>     Super_CAS_id sent to the ECMG, dec or 0x-hex (required with --cas-algo)\n"
      "      --cas-ecm-id <n>       ECM_id sent to the ECMG (required with --cas-algo)\n"
      "      --cas-ecm-pid <pid>    output PID for the ECM stream (default: 0x0020)\n"
      "      --cas-emmg-port <n>    our EMMG listener port (default: 8002)\n"
      "      --cas-emmg-version <n> EMMG protocol version 2|3 (default: accept client's proposal)\n"
      "      --cas-emm-pid <pid>    output PID for the EMM stream (default: 0x0021)\n"
      "      --cas-pids <list>      comma-separated PIDs to scramble (required with --cas-algo)\n"
      "      --cas-cp-duration <ms> crypto-period duration in ms (default: 10000)\n"
      "      --cas-resilience <r>   on ECMG loss: frozen|cycling|unscrambled (default: frozen)\n"
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
      {"out-iface", required_argument, 0, 'O'},
      {"udp", no_argument, 0, 'u'},
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
      {"cas-algo", required_argument, 0, 1008},
      {"cas-ecmg", required_argument, 0, 1009},
      {"cas-ecmg-version", required_argument, 0, 1010},
      {"cas-super-id", required_argument, 0, 1011},
      {"cas-ecm-id", required_argument, 0, 1012},
      {"cas-ecm-pid", required_argument, 0, 1013},
      {"cas-emmg-port", required_argument, 0, 1014},
      {"cas-emmg-version", required_argument, 0, 1015},
      {"cas-emm-pid", required_argument, 0, 1016},
      {"cas-pids", required_argument, 0, 1017},
      {"cas-cp-duration", required_argument, 0, 1018},
      {"cas-resilience", required_argument, 0, 1019},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int have_in = 0, have_mcast = 0, have_hbbtv_org = 0, have_hbbtv_app = 0;
  int have_cas_ecmg = 0, have_cas_super_id = 0, have_cas_ecm_id = 0, have_cas_pids = 0, any_cas_flag = 0;
  int c;

  memset(cfg, 0, sizeof *cfg);
  cfg->tsid = 1;
  cfg->onid = 1;
  cfg->sid = 1;
  cfg->rtp = 1;
  cfg->cas_ecm_pid = 0x0020;
  cfg->cas_emmg_port = 8002;
  cfg->cas_emm_pid = 0x0021;
  cfg->cas_cp_duration_ms = 10000;
  optind = 1;
  while ((c = getopt_long(argc, argv, "i:p:m:I:O:uT:n:s:b:SBe:kvh", longopts, NULL)) != -1) {
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
        cfg->iface_in = optarg;
        break;
      case 'O':
        cfg->iface_out = optarg;
        break;
      case 'u':
        cfg->rtp = 0;
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
      case 1008: {
        static const enum_map_t map[] = {{"cissa", CAS_ALGO_CISSA}, {"csa2", CAS_ALGO_CSA2}};
        int v;
        any_cas_flag = 1;
        if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
          argerr("invalid --cas-algo: %s (cissa|csa2)", optarg);
          return ARGS_ERR;
        }
        cfg->cas_algo = (cas_algo_t)v;
        break;
      }
      case 1009:
        any_cas_flag = 1;
        if (cas_endpoint_parse(optarg, cfg->cas_ecmg_host, sizeof cfg->cas_ecmg_host, &cfg->cas_ecmg_port)) {
          argerr("invalid --cas-ecmg endpoint: %s", optarg);
          return ARGS_ERR;
        }
        have_cas_ecmg = 1;
        break;
      case 1010:
        any_cas_flag = 1;
        if (cas_version_parse(optarg, &cfg->cas_ecmg_version)) {
          argerr("invalid --cas-ecmg-version: %s (2|3)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1011:
        any_cas_flag = 1;
        if (cas_super_id_parse(optarg, &cfg->cas_super_cas_id)) {
          argerr("invalid --cas-super-id: %s", optarg);
          return ARGS_ERR;
        }
        have_cas_super_id = 1;
        break;
      case 1012:
        any_cas_flag = 1;
        if (id_parse(optarg, &cfg->cas_ecm_id)) {
          argerr("invalid --cas-ecm-id: %s (1..65535)", optarg);
          return ARGS_ERR;
        }
        have_cas_ecm_id = 1;
        break;
      case 1013:
        any_cas_flag = 1;
        if (pid_parse(optarg, &cfg->cas_ecm_pid) || cfg->cas_ecm_pid == 0) {
          argerr("invalid --cas-ecm-pid: %s (0x0001..0x1FFE)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1014:
        any_cas_flag = 1;
        if (port_parse(optarg, &cfg->cas_emmg_port)) {
          argerr("invalid --cas-emmg-port: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 1015:
        any_cas_flag = 1;
        if (cas_version_parse(optarg, &cfg->cas_emmg_version)) {
          argerr("invalid --cas-emmg-version: %s (2|3)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1016:
        any_cas_flag = 1;
        if (pid_parse(optarg, &cfg->cas_emm_pid) || cfg->cas_emm_pid == 0) {
          argerr("invalid --cas-emm-pid: %s (0x0001..0x1FFE)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1017:
        any_cas_flag = 1;
        if (cas_pids_parse(optarg, cfg)) {
          argerr("invalid --cas-pids: %s", optarg);
          return ARGS_ERR;
        }
        have_cas_pids = 1;
        break;
      case 1018: {
        char *end;
        unsigned long v;
        any_cas_flag = 1;
        v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0 || v > 86400000UL) {
          argerr("invalid --cas-cp-duration: %s (ms, 1..86400000)", optarg);
          return ARGS_ERR;
        }
        cfg->cas_cp_duration_ms = (unsigned)v;
        break;
      }
      case 1019: {
        static const enum_map_t map[] = {{"frozen", CAS_RESILIENCE_FROZEN}, {"cycling", CAS_RESILIENCE_CYCLING}, {"unscrambled", CAS_RESILIENCE_UNSCRAMBLED}};
        int v;
        any_cas_flag = 1;
        if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
          argerr("invalid --cas-resilience: %s (frozen|cycling|unscrambled)", optarg);
          return ARGS_ERR;
        }
        cfg->cas_resilience = (cas_resilience_t)v;
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
  if (any_cas_flag && cfg->cas_algo == CAS_ALGO_NONE) {
    argerr("--cas-* options require --cas-algo");
    return ARGS_ERR;
  }
  if (cfg->cas_algo != CAS_ALGO_NONE) {
    if (!have_cas_ecmg) {
      argerr("--cas-algo requires --cas-ecmg");
      return ARGS_ERR;
    }
    if (!have_cas_super_id) {
      argerr("--cas-algo requires --cas-super-id");
      return ARGS_ERR;
    }
    if (!have_cas_ecm_id) {
      argerr("--cas-algo requires --cas-ecm-id");
      return ARGS_ERR;
    }
    if (!have_cas_pids) {
      argerr("--cas-algo requires --cas-pids");
      return ARGS_ERR;
    }
    if (cfg->cas_ecm_pid == cfg->cas_emm_pid) {
      argerr("--cas-ecm-pid and --cas-emm-pid must differ");
      return ARGS_ERR;
    }
  }
  return ARGS_OK;
}
