/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/argutil.h"
#include "lib/cas/cas_args.h"
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

/* [@]<addr>:<port> or [@][<addr6>]:<port>, multicast literal required */
static int mcast_group_parse(const char *s, int *family, char *addr_out, size_t addr_out_sz, unsigned *port_out) {
  if (*s == '@')
    s++;
  if (argutil_addrport_parse(s, family, addr_out, addr_out_sz, port_out))
    return -1;

  if (*family == AF_INET) {
    struct in_addr a;
    inet_pton(AF_INET, addr_out, &a);
    if ((ntohl(a.s_addr) >> 28) != 0xE) /* 224.0.0.0/4 */
      return -1;
  } else {
    struct in6_addr a6;
    inet_pton(AF_INET6, addr_out, &a6);
    if (a6.s6_addr[0] != 0xFF) /* ff00::/8 */
      return -1;
  }
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

/* comma-separated PIDs and/or the "video"/"audio" keywords, e.g. "0x0103,video" or "audio,0x0104,0x0106" */
static int cas_pids_parse(const char *s, config_t *cfg) {
  char buf[512];
  char *tok, *save = NULL;
  if (strlen(s) >= sizeof buf)
    return -1;
  strcpy(buf, s);
  cfg->cas_pid_count = 0;
  cfg->cas_pids_video = 0;
  cfg->cas_pids_audio = 0;
  for (tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
    unsigned pid;
    if (strcmp(tok, "video") == 0) {
      cfg->cas_pids_video = 1;
      continue;
    }
    if (strcmp(tok, "audio") == 0) {
      cfg->cas_pids_audio = 1;
      continue;
    }
    if (cfg->cas_pid_count >= ARGS_MAX_CAS_PIDS)
      return -1;
    if (pid_parse(tok, &pid) || pid == 0)
      return -1;
    cfg->cas_pids[cfg->cas_pid_count++] = pid;
  }
  return (cfg->cas_pid_count || cfg->cas_pids_video || cfg->cas_pids_audio) ? 0 : -1;
}

static void print_help(void) {
  printf(
      "usage: %s -i <uri> [per-input options] [-i <uri> ...] -m <mcast>:<port> [options]\n\n"
      "re-package one or more transport streams (already-muxed, not raw ES) as one DVB-IPI\n"
      "multicast. A single -i: normal SPTS. Multiple -i: MPTS, one program per input.\n\n"
      "options:\n"
      "  -i, --input <uri>          udp://, rtp://, http(s)://, or \"-\" for stdin; repeatable\n"
      "  -p, --pmt-pid <pid>        for the -i right before this: select program by PMT PID\n"
      "                             (dec or 0x-hex; default: first live one)\n"
      "      --sid <n>              for the -i right before this: service_id/program_number\n"
      "                             (default: auto)\n"
      "  -s, --sdt <text|->         for the -i right before this: SDT service_name - default\n"
      "                             passthrough source; \"-\" drops it; text = our own\n"
      "  -I, --iface <iface>        for the -i right before this: incoming multicast interface\n"
      "      --strip-eit            for the -i right before this: drop source EIT (default: passed through)\n"
      "      --hbbtv <url>          for the -i right before this: inject an AIT signalling this\n"
      "                             HbbTV app (default: none)\n"
      "      --hbbtv-org-id <n>     for the -i right before this: HbbTV organisation_id\n"
      "                             (required with --hbbtv)\n"
      "      --hbbtv-app-id <n>     for the -i right before this: HbbTV application_id\n"
      "                             (required with --hbbtv)\n"
      "  -m, --mcast <g>:<p>        output multicast group:port ([addr6]:port for v6)\n"
      "  -O, --out-iface <iface>    outgoing multicast interface\n"
      "  -u, --udp                  plain UDP output (default: RTP-wrapped)\n"
      "  -T, --ttl <n>              multicast TTL / hop limit (default: 1)\n"
      "  -n, --nit <text|->         NIT (whole output): default passthrough source; \"-\" drops\n"
      "                             it; text = our own\n"
      "  -b, --bitrate <kbps>       target output bitrate, shared across all inputs (default: no shaping)\n"
      "  -S, --stuff                null-packet stuffing up to -b's target (needs -b)\n"
      "  -B, --burst-limit          cap output at -b's target, never above (needs -b)\n"
      "  -e, --error <seconds>      on input error, reconnect after N s (default: fail once;\n"
      "                             always retries when more than one -i is given)\n"
      "  -k, --insecure             skip TLS verification (self-signed, hostname, expiry)\n"
      "      --tsid <n>             transport_stream_id (default 1)\n"
      "      --onid <n>             original_network_id (default 1)\n"
      "  -v, --verbose              periodic stats on stderr\n"
      "      --color <when>         auto|always|never (default auto)\n"
      "      --metrics <path>       Unix datagram socket for metrics (default: /run/dvbipitools/metrics.sock)\n"
      "      --metrics-id <name>    stable instance id; metrics disabled unless set\n"
      "      --metrics-interval <s> snapshot interval in seconds (default: 5)\n"
      "      --cas-algo <a>         enable CAS: cissa|csa2 (default: disabled)\n"
      "      --cas-ecmg <ep>        ECMG address, tcp://host:port; repeatable, one CAS vendor\n"
      "                             per --cas-ecmg (required with --cas-algo)\n"
      "      --cas-ecmg-version <n> for the --cas-ecmg right before this: protocol version 2|3\n"
      "                             (default: auto-negotiate)\n"
      "      --cas-super-id <n>     for the --cas-ecmg right before this: Super_CAS_id, dec or\n"
      "                             0x-hex (required per vendor)\n"
      "      --cas-ecm-id <n>       for the --cas-ecmg right before this: ECM_id (required per vendor)\n"
      "      --cas-ecm-pid <pid>    for the --cas-ecmg right before this: output PID for its ECM\n"
      "                             stream (default: 0x0020)\n"
      "      --cas-emmg-port <n>    for the --cas-ecmg right before this: our EMMG listener port\n"
      "                             (default: 8002)\n"
      "      --cas-emmg-version <n> for the --cas-ecmg right before this: EMMG protocol version\n"
      "                             2|3 (default: accept client's proposal)\n"
      "      --cas-emm-pid <pid>    for the --cas-ecmg right before this: output PID for its EMM\n"
      "                             stream (default: 0x0021)\n"
      "      --cas-resilience <r>   for the --cas-ecmg right before this: on its own ECMG loss,\n"
      "                             frozen|cycling|silent (default: frozen)\n"
      "      --cas-required         for the --cas-ecmg right before this: its outage forces the\n"
      "                             global fallback regardless of other vendors\n"
      "      --cas-pids <list>      PIDs to scramble: comma-separated pids and/or video/audio keywords\n"
      "                             (default: video,audio - all video and audio streams)\n"
      "      --cas-cp-duration <ms> crypto-period duration in ms, shared by every vendor (default: 10000)\n"
      "      --cas-fallback-clear   on total outage (or a --cas-required vendor down): clear\n"
      "                             instead of staying scrambled on the last known-good CW\n"
      "  -h, --help                 this help\n\n"
      "examples:\n"
      "  %s -i rtp://@239.19.75.1:8700 -m 239.1.1.1:5000 -s \"My Channel\"\n"
      "  %s -i https://host/live/x/y.ts -m 239.1.1.2:5000 -b 8000 -S -B\n"
      "  %s -i rtp://@239.19.75.1:8700 --sdt \"Channel A\" -i rtp://@239.19.75.2:8700 --sdt \"Channel B\" \\\n"
      "     -m 239.1.1.3:5000 -e 5\n",
      TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME);
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
      {"metrics", required_argument, 0, 1020},
      {"metrics-id", required_argument, 0, 1021},
      {"metrics-interval", required_argument, 0, 1022},
      {"cas-required", no_argument, 0, 1023},
      {"cas-fallback-clear", no_argument, 0, 1024},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int have_mcast = 0;
  int have_cas_pids = 0, any_cas_flag = 0;
  int c;
  unsigned i;

  memset(cfg, 0, sizeof *cfg);
  cfg->tsid = 1;
  cfg->onid = 1;
  cfg->rtp = 1;
  cfg->cas_cp_duration_ms = 10000;
  optind = 1;
  /* leading '+': disable GNU getopt argument permutation, so per-input options stay paired
     with whichever -i preceded them instead of being reordered */
  while ((c = getopt_long(argc, argv, "+i:p:m:I:O:uT:n:s:b:SBe:kvh", longopts, NULL)) != -1) {
    switch (c) {
      case 'i': {
        source_t parsed;
        if (source_parse(optarg, &parsed)) {
          argerr("invalid -i uri: %s", optarg);
          return ARGS_ERR;
        }
        if (cfg->n_inputs >= ARGS_MAX_INPUTS) {
          argerr("too many -i inputs (max %d)", ARGS_MAX_INPUTS);
          return ARGS_ERR;
        }
        cfg->inputs[cfg->n_inputs].input = parsed;
        cfg->n_inputs++;
        break;
      }
      case 'p':
        if (cfg->n_inputs == 0) {
          argerr("-p/--pmt-pid must follow the -i it names");
          return ARGS_ERR;
        }
        if (pid_parse(optarg, &cfg->inputs[cfg->n_inputs - 1].pmt_pid) || cfg->inputs[cfg->n_inputs - 1].pmt_pid == 0) {
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
        if (cfg->n_inputs == 0) {
          argerr("-I/--iface must follow the -i it names");
          return ARGS_ERR;
        }
        cfg->inputs[cfg->n_inputs - 1].iface_in = optarg;
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
        if (cfg->n_inputs == 0) {
          argerr("-s/--sdt must follow the -i it names");
          return ARGS_ERR;
        }
        if (strcmp(optarg, "-") == 0) {
          cfg->inputs[cfg->n_inputs - 1].sdt_mode = TABLE_DROP;
        } else {
          cfg->inputs[cfg->n_inputs - 1].sdt_mode = TABLE_OVERRIDE;
          snprintf(cfg->inputs[cfg->n_inputs - 1].sdt_text, sizeof cfg->inputs[0].sdt_text, "%s", optarg);
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
        if (cfg->n_inputs == 0) {
          argerr("--strip-eit must follow the -i it names");
          return ARGS_ERR;
        }
        cfg->inputs[cfg->n_inputs - 1].strip_eit = 1;
        break;
      case 1001:
        if (cfg->n_inputs == 0) {
          argerr("--hbbtv must follow the -i it names");
          return ARGS_ERR;
        }
        cfg->inputs[cfg->n_inputs - 1].hbbtv_url = optarg;
        break;
      case 1002:
        if (cfg->n_inputs == 0) {
          argerr("--hbbtv-org-id must follow the -i it names");
          return ARGS_ERR;
        }
        if (org_id_parse(optarg, &cfg->inputs[cfg->n_inputs - 1].hbbtv_org_id)) {
          argerr("invalid --hbbtv-org-id: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 1003:
        if (cfg->n_inputs == 0) {
          argerr("--hbbtv-app-id must follow the -i it names");
          return ARGS_ERR;
        }
        if (id_parse(optarg, &cfg->inputs[cfg->n_inputs - 1].hbbtv_app_id)) {
          argerr("invalid --hbbtv-app-id: %s", optarg);
          return ARGS_ERR;
        }
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
        if (cfg->n_inputs == 0) {
          argerr("--sid must follow the -i it names");
          return ARGS_ERR;
        }
        if (id_parse(optarg, &cfg->inputs[cfg->n_inputs - 1].sid)) {
          argerr("invalid --sid: %s (1..65535)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1007: {
        log_color_t v;
        if (log_color_from_string(optarg, &v)) {
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
      case 1009: {
        cas_vendor_t *vend;
        any_cas_flag = 1;
        if (cfg->n_cas_vendors >= ARGS_MAX_CAS_VENDORS) {
          argerr("too many --cas-ecmg vendors (max %d)", ARGS_MAX_CAS_VENDORS);
          return ARGS_ERR;
        }
        vend = &cfg->cas_vendors[cfg->n_cas_vendors];
        memset(vend, 0, sizeof *vend);
        vend->ecm_pid = 0x0020;
        vend->emmg_port = 8002;
        vend->emm_pid = 0x0021;
        if (cas_endpoint_parse(optarg, vend->ecmg_host, sizeof vend->ecmg_host, &vend->ecmg_port)) {
          argerr("invalid --cas-ecmg endpoint: %s", optarg);
          return ARGS_ERR;
        }
        cfg->n_cas_vendors++;
        break;
      }
      case 1010:
        any_cas_flag = 1;
        if (cfg->n_cas_vendors == 0) {
          argerr("--cas-ecmg-version must follow the --cas-ecmg it names");
          return ARGS_ERR;
        }
        if (cas_version_parse(optarg, &cfg->cas_vendors[cfg->n_cas_vendors - 1].ecmg_version)) {
          argerr("invalid --cas-ecmg-version: %s (2|3)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1011:
        any_cas_flag = 1;
        if (cfg->n_cas_vendors == 0) {
          argerr("--cas-super-id must follow the --cas-ecmg it names");
          return ARGS_ERR;
        }
        if (cas_super_id_parse(optarg, &cfg->cas_vendors[cfg->n_cas_vendors - 1].super_cas_id)) {
          argerr("invalid --cas-super-id: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 1012:
        any_cas_flag = 1;
        if (cfg->n_cas_vendors == 0) {
          argerr("--cas-ecm-id must follow the --cas-ecmg it names");
          return ARGS_ERR;
        }
        if (id_parse(optarg, &cfg->cas_vendors[cfg->n_cas_vendors - 1].ecm_id)) {
          argerr("invalid --cas-ecm-id: %s (1..65535)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1013:
        any_cas_flag = 1;
        if (cfg->n_cas_vendors == 0) {
          argerr("--cas-ecm-pid must follow the --cas-ecmg it names");
          return ARGS_ERR;
        }
        if (pid_parse(optarg, &cfg->cas_vendors[cfg->n_cas_vendors - 1].ecm_pid) || cfg->cas_vendors[cfg->n_cas_vendors - 1].ecm_pid == 0) {
          argerr("invalid --cas-ecm-pid: %s (0x0001..0x1FFE)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1014:
        any_cas_flag = 1;
        if (cfg->n_cas_vendors == 0) {
          argerr("--cas-emmg-port must follow the --cas-ecmg it names");
          return ARGS_ERR;
        }
        if (argutil_port_parse(optarg, &cfg->cas_vendors[cfg->n_cas_vendors - 1].emmg_port)) {
          argerr("invalid --cas-emmg-port: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 1015:
        any_cas_flag = 1;
        if (cfg->n_cas_vendors == 0) {
          argerr("--cas-emmg-version must follow the --cas-ecmg it names");
          return ARGS_ERR;
        }
        if (cas_version_parse(optarg, &cfg->cas_vendors[cfg->n_cas_vendors - 1].emmg_version)) {
          argerr("invalid --cas-emmg-version: %s (2|3)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1016:
        any_cas_flag = 1;
        if (cfg->n_cas_vendors == 0) {
          argerr("--cas-emm-pid must follow the --cas-ecmg it names");
          return ARGS_ERR;
        }
        if (pid_parse(optarg, &cfg->cas_vendors[cfg->n_cas_vendors - 1].emm_pid) || cfg->cas_vendors[cfg->n_cas_vendors - 1].emm_pid == 0) {
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
        static const enum_map_t map[] = {{"frozen", CAS_OUTAGE_FROZEN}, {"cycling", CAS_OUTAGE_CYCLING}, {"silent", CAS_OUTAGE_SILENT}};
        int v;
        any_cas_flag = 1;
        if (cfg->n_cas_vendors == 0) {
          argerr("--cas-resilience must follow the --cas-ecmg it names");
          return ARGS_ERR;
        }
        if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
          argerr("invalid --cas-resilience: %s (frozen|cycling|silent)", optarg);
          return ARGS_ERR;
        }
        cfg->cas_vendors[cfg->n_cas_vendors - 1].resilience = (cas_outage_mode_t)v;
        break;
      }
      case 1020:
        cfg->metrics_sock = optarg;
        break;
      case 1021:
        cfg->metrics_id = optarg;
        break;
      case 1022: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0 || v > 86400UL) {
          argerr("invalid --metrics-interval: %s (seconds, 1..86400)", optarg);
          return ARGS_ERR;
        }
        cfg->metrics_interval_s = (unsigned)v;
        break;
      }
      case 1023:
        any_cas_flag = 1;
        if (cfg->n_cas_vendors == 0) {
          argerr("--cas-required must follow the --cas-ecmg it names");
          return ARGS_ERR;
        }
        cfg->cas_vendors[cfg->n_cas_vendors - 1].required = 1;
        break;
      case 1024:
        any_cas_flag = 1;
        cfg->cas_fallback_clear = 1;
        break;
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
  if (cfg->n_inputs == 0) {
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
  if ((cfg->metrics_sock || cfg->metrics_interval_s) && !cfg->metrics_id) {
    argerr("--metrics/--metrics-interval require --metrics-id");
    return ARGS_ERR;
  }
  for (i = 0; i < cfg->n_inputs; i++) {
    dipitvhead_input_t *in = &cfg->inputs[i];
    if (in->hbbtv_url && (!in->hbbtv_org_id || !in->hbbtv_app_id)) {
      argerr("--hbbtv requires --hbbtv-org-id and --hbbtv-app-id");
      return ARGS_ERR;
    }
    if ((in->hbbtv_org_id || in->hbbtv_app_id) && !in->hbbtv_url) {
      argerr("--hbbtv-org-id/--hbbtv-app-id need --hbbtv");
      return ARGS_ERR;
    }
  }
  if (any_cas_flag && cfg->cas_algo == CAS_ALGO_NONE) {
    argerr("--cas-* options require --cas-algo");
    return ARGS_ERR;
  }
  if (cfg->cas_algo != CAS_ALGO_NONE) {
    unsigned vi, vj;
    if (cfg->n_cas_vendors == 0) {
      argerr("--cas-algo requires --cas-ecmg");
      return ARGS_ERR;
    }
    for (vi = 0; vi < cfg->n_cas_vendors; vi++) {
      const cas_vendor_t *v = &cfg->cas_vendors[vi];
      if (!v->super_cas_id) {
        argerr("--cas-ecmg %s:%u requires --cas-super-id", v->ecmg_host, v->ecmg_port);
        return ARGS_ERR;
      }
      if (!v->ecm_id) {
        argerr("--cas-ecmg %s:%u requires --cas-ecm-id", v->ecmg_host, v->ecmg_port);
        return ARGS_ERR;
      }
      if (v->ecm_pid == v->emm_pid) {
        argerr("--cas-ecm-pid and --cas-emm-pid must differ (--cas-ecmg %s:%u)", v->ecmg_host, v->ecmg_port);
        return ARGS_ERR;
      }
      for (vj = vi + 1; vj < cfg->n_cas_vendors; vj++) {
        const cas_vendor_t *o = &cfg->cas_vendors[vj];
        if (v->ecm_pid == o->ecm_pid || v->ecm_pid == o->emm_pid || v->emm_pid == o->ecm_pid || v->emm_pid == o->emm_pid) {
          argerr("--cas-ecm-pid/--cas-emm-pid collide across --cas-ecmg vendors");
          return ARGS_ERR;
        }
        if (v->emmg_port == o->emmg_port) {
          argerr("--cas-emmg-port %u used by more than one --cas-ecmg vendor (each needs its own EMMG listener)", v->emmg_port);
          return ARGS_ERR;
        }
      }
    }
    if (!have_cas_pids) {
      /* default: scramble all video and audio elementary streams */
      cfg->cas_pids_video = 1;
      cfg->cas_pids_audio = 1;
    }
  }

  {
    unsigned used[ARGS_MAX_INPUTS];
    unsigned n_used = 0;
    unsigned next = 1;
    unsigned j;

    for (i = 0; i < cfg->n_inputs; i++) {
      if (cfg->inputs[i].sid == 0)
        continue;
      for (j = 0; j < n_used; j++)
        if (used[j] == cfg->inputs[i].sid) {
          argerr("duplicate --sid %u", cfg->inputs[i].sid);
          return ARGS_ERR;
        }
      used[n_used++] = cfg->inputs[i].sid;
    }
    for (i = 0; i < cfg->n_inputs; i++) {
      if (cfg->inputs[i].sid != 0)
        continue;
      for (;;) {
        int taken = 0;
        for (j = 0; j < n_used; j++)
          if (used[j] == next) {
            taken = 1;
            break;
          }
        if (!taken)
          break;
        next++;
      }
      cfg->inputs[i].sid = next;
      used[n_used++] = next;
      next++;
    }
  }
  return ARGS_OK;
}
