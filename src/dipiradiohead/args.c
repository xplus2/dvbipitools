/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/argutil.h"
#include "lib/cas/biss/biss.h"
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

/* <addr>:<port> or [<addr6>]:<port>, multicast literal required */
static int mcast_parse(const char *s, config_t *cfg) {
  if (argutil_addrport_parse(s, &cfg->family, cfg->mcast_group, sizeof cfg->mcast_group, &cfg->mcast_port))
    return -1;

  if (cfg->family == AF_INET) {
    struct in_addr a;
    inet_pton(AF_INET, cfg->mcast_group, &a);
    if ((ntohl(a.s_addr) >> 28) != 0xE) /* 224.0.0.0/4 */
      return -1;
  } else {
    struct in6_addr a6;
    inet_pton(AF_INET6, cfg->mcast_group, &a6);
    if (a6.s6_addr[0] != 0xFF) /* ff00::/8 */
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

static int id_parse(const char *s, unsigned *out) {
  char *end;
  unsigned long v;
  v = strtoul(s, &end, 10);
  if (*end != '\0' || v == 0 || v > 0xFFFF)
    return -1;
  *out = (unsigned)v;
  return 0;
}

/* decimal or 0x-hex pid, 0x0001..0x1FFE */
static int pid_parse(const char *s, unsigned *out) {
  char *end;
  unsigned long v = strtoul(s, &end, 0);
  if (*end != '\0' || v == 0 || v > 0x1FFE)
    return -1;
  *out = (unsigned)v;
  return 0;
}

static void print_help(void) {
  printf(
      "usage: %s -i <uri> [--sid <n>] [--sdt <name>] [-i <uri> ...] -m <mcast>:<port> [options]\n\n"
      "fetch one or more icecast/shoutcast streams and re-mux them as one DVB-IPI multicast\n"
      "(a single -i: normal SPTS. multiple -i: MPTS, one program per input)\n\n"
      "options:\n"
      "  -i, --input <uri>          icecast/shoutcast source, http:// or https://; repeatable\n"
      "      --sid <n>              service_id/program_number for the -i right before this (default: auto)\n"
      "      --sdt <name>           SDT service_name for the -i right before this (default: auto)\n"
      "  -m, --mcast <g>:<p>        output multicast group:port ([addr6]:port for v6)\n"
      "  -I, --iface <iface>        outgoing multicast interface\n"
      "  -r, --rtp                  wrap output in RTP (default: plain UDP)\n"
      "  -T, --ttl <n>              multicast TTL / hop limit (default: 1)\n"
      "  -n, --nit <text>           NIT network_name\n"
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
      "      --cas-algo <a>         enable CAS: cissa|csa2|csa1 (default: disabled)\n"
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
      "      --cas-cp-duration <ms> crypto-period duration in ms, shared by every vendor (default: 10000)\n"
      "      --cas-fallback-clear   on total outage (or a --cas-required vendor down): clear\n"
      "                             instead of staying scrambled on the last known-good CW\n"
      "      --biss2-sw <hex32>      enable BISS2 Mode 1/E: 32 hex char Session Word, scrambles\n"
      "                             with CISSA. No ECMG/EMMG. Mutually exclusive with --cas-algo\n"
      "      --biss2-emit-esw <id>   with --biss2-sw: log the AES-128-ECB Encrypted Session Word\n"
      "                             for this 32 hex char receiver ID, for out-of-band distribution\n"
      "      --biss1-sw <hex12>     enable legacy BISS1 Mode 1: 12 hex char Session Word,\n"
      "                             scrambles with CSA1. Mutually exclusive with --biss2-sw/--cas-algo\n"
      "      --biss2-ca-receivers <dir> enable BISS2 Mode CA: directory of PEM public keys, one\n"
      "                             per entitled receiver/group. Rescanned on SIGHUP; a receiver\n"
      "                             removed from the directory is revoked (forces a Session Key\n"
      "                             change). Mutually exclusive with --biss1-sw/--biss2-sw/--cas-algo\n"
      "      --biss2-ca-session-id <n> administratively unique entitlement_session_id, dec or\n"
      "                             0x-hex, 16 bit (default: random at startup)\n"
      "  -h, --help                 this help\n\n"
      "examples:\n"
      "  %s -i https://example.com/radio.m3u --sdt \"Channel 1\" -m 239.1.1.1:5000\n"
      "  %s -i http://example.com/somechannel/aac --sdt \"Some Channel\" -i https://example.com/radio.m3u --sdt \"Channel 1\" -m 239.1.1.2:5000 -r -e 5\n",
      TOOL_NAME, TOOL_NAME, TOOL_NAME);
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  static const struct option longopts[] = {
      {"input", required_argument, 0, 'i'},
      {"mcast", required_argument, 0, 'm'},
      {"iface", required_argument, 0, 'I'},
      {"rtp", no_argument, 0, 'r'},
      {"ttl", required_argument, 0, 'T'},
      {"nit", required_argument, 0, 'n'},
      {"sdt", required_argument, 0, 's'},
      {"error", required_argument, 0, 'e'},
      {"insecure", no_argument, 0, 'k'},
      {"tsid", required_argument, 0, 1000},
      {"onid", required_argument, 0, 1001},
      {"sid", required_argument, 0, 1002},
      {"verbose", no_argument, 0, 'v'},
      {"color", required_argument, 0, 1003},
      {"cas-algo", required_argument, 0, 1004},
      {"cas-ecmg", required_argument, 0, 1005},
      {"cas-ecmg-version", required_argument, 0, 1006},
      {"cas-super-id", required_argument, 0, 1007},
      {"cas-ecm-id", required_argument, 0, 1008},
      {"cas-ecm-pid", required_argument, 0, 1009},
      {"cas-emmg-port", required_argument, 0, 1010},
      {"cas-emmg-version", required_argument, 0, 1011},
      {"cas-emm-pid", required_argument, 0, 1012},
      {"cas-cp-duration", required_argument, 0, 1013},
      {"cas-resilience", required_argument, 0, 1014},
      {"metrics", required_argument, 0, 1015},
      {"metrics-id", required_argument, 0, 1016},
      {"metrics-interval", required_argument, 0, 1017},
      {"cas-required", no_argument, 0, 1018},
      {"cas-fallback-clear", no_argument, 0, 1019},
      {"biss2-sw", required_argument, 0, 1020},
      {"biss2-emit-esw", required_argument, 0, 1021},
      {"biss1-sw", required_argument, 0, 1022},
      {"biss2-ca-receivers", required_argument, 0, 1023},
      {"biss2-ca-session-id", required_argument, 0, 1024},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int have_mcast = 0;
  int any_cas_flag = 0;
  int c;
  unsigned i;

  memset(cfg, 0, sizeof *cfg);
  cfg->tsid = 1;
  cfg->onid = 1;
  cfg->cas_cp_duration_ms = 10000;
  optind = 1;
  /* leading '+': disable GNU getopt argument permutation, so --sid/--sdt stay paired with
     whichever -i preceded them on the command line instead of being reordered */
  while ((c = getopt_long(argc, argv, "+i:m:I:rT:n:s:e:kvh", longopts, NULL)) != -1) {
    switch (c) {
      case 'i':
        if (cfg->n_inputs >= RADIOHEAD_MAX_INPUTS) {
          argerr("too many -i inputs (max %d)", RADIOHEAD_MAX_INPUTS);
          return ARGS_ERR;
        }
        cfg->inputs[cfg->n_inputs].uri = optarg;
        cfg->n_inputs++;
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
        snprintf(cfg->nit_text, sizeof cfg->nit_text, "%s", optarg);
        break;
      case 's':
        if (cfg->n_inputs == 0) {
          argerr("--sdt/-s must follow the -i it names");
          return ARGS_ERR;
        }
        snprintf(cfg->inputs[cfg->n_inputs - 1].sdt_text, sizeof cfg->inputs[0].sdt_text, "%s", optarg);
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
        if (cfg->n_inputs == 0) {
          argerr("--sid must follow the -i it names");
          return ARGS_ERR;
        }
        if (id_parse(optarg, &cfg->inputs[cfg->n_inputs - 1].sid)) {
          argerr("invalid --sid: %s (1..65535)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1003: {
        log_color_t v;
        if (log_color_from_string(optarg, &v)) {
          argerr("invalid --color: %s (auto|always|never)", optarg);
          return ARGS_ERR;
        }
        cfg->color_mode = v;
        break;
      }
      case 1004: {
        static const enum_map_t map[] = {{"cissa", CAS_ALGO_CISSA}, {"csa2", CAS_ALGO_CSA2}, {"csa1", CAS_ALGO_CSA1}};
        int v;
        any_cas_flag = 1;
        if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
          argerr("invalid --cas-algo: %s (cissa|csa2|csa1)", optarg);
          return ARGS_ERR;
        }
        cfg->cas_algo = (cas_algo_t)v;
        break;
      }
      case 1005: {
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
      case 1006:
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
      case 1007:
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
      case 1008:
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
      case 1009:
        any_cas_flag = 1;
        if (cfg->n_cas_vendors == 0) {
          argerr("--cas-ecm-pid must follow the --cas-ecmg it names");
          return ARGS_ERR;
        }
        if (pid_parse(optarg, &cfg->cas_vendors[cfg->n_cas_vendors - 1].ecm_pid)) {
          argerr("invalid --cas-ecm-pid: %s (0x0001..0x1FFE)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1010:
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
      case 1011:
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
      case 1012:
        any_cas_flag = 1;
        if (cfg->n_cas_vendors == 0) {
          argerr("--cas-emm-pid must follow the --cas-ecmg it names");
          return ARGS_ERR;
        }
        if (pid_parse(optarg, &cfg->cas_vendors[cfg->n_cas_vendors - 1].emm_pid)) {
          argerr("invalid --cas-emm-pid: %s (0x0001..0x1FFE)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1013: {
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
      case 1014: {
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
      case 1015:
        cfg->metrics_sock = optarg;
        break;
      case 1016:
        cfg->metrics_id = optarg;
        break;
      case 1017: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0 || v > 86400UL) {
          argerr("invalid --metrics-interval: %s (seconds, 1..86400)", optarg);
          return ARGS_ERR;
        }
        cfg->metrics_interval_s = (unsigned)v;
        break;
      }
      case 1018:
        any_cas_flag = 1;
        if (cfg->n_cas_vendors == 0) {
          argerr("--cas-required must follow the --cas-ecmg it names");
          return ARGS_ERR;
        }
        cfg->cas_vendors[cfg->n_cas_vendors - 1].required = 1;
        break;
      case 1019:
        any_cas_flag = 1;
        cfg->cas_fallback_clear = 1;
        break;
      case 1020:
        if (biss_parse_hex16(optarg, cfg->biss2_sw)) {
          argerr("invalid --biss2-sw: %s (32 hex chars)", optarg);
          return ARGS_ERR;
        }
        cfg->biss2_enabled = 1;
        break;
      case 1021:
        if (biss_parse_hex16(optarg, cfg->biss2_esw_id)) {
          argerr("invalid --biss2-emit-esw: %s (32 hex chars)", optarg);
          return ARGS_ERR;
        }
        cfg->biss2_emit_esw = 1;
        break;
      case 1022:
        if (biss1_parse_sw(optarg, cfg->biss1_cw)) {
          argerr("invalid --biss1-sw: %s (12 hex chars)", optarg);
          return ARGS_ERR;
        }
        cfg->biss1_enabled = 1;
        break;
      case 1023:
        cfg->biss2_ca_receivers_dir = optarg;
        cfg->biss2_ca_enabled = 1;
        break;
      case 1024: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 0);
        if (*end != '\0' || v > 0xFFFFUL) {
          argerr("invalid --biss2-ca-session-id: %s (16 bit, dec or 0x-hex)", optarg);
          return ARGS_ERR;
        }
        cfg->biss2_ca_session_id = (unsigned)v;
        cfg->biss2_ca_session_id_given = 1;
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
  if (cfg->n_inputs == 0) {
    argerr("missing -i input");
    return ARGS_ERR;
  }
  if (!have_mcast) {
    argerr("missing -m output multicast");
    return ARGS_ERR;
  }
  if ((cfg->metrics_sock || cfg->metrics_interval_s) && !cfg->metrics_id) {
    argerr("--metrics/--metrics-interval require --metrics-id");
    return ARGS_ERR;
  }
  if (any_cas_flag && cfg->cas_algo == CAS_ALGO_NONE) {
    argerr("--cas-* options require --cas-algo");
    return ARGS_ERR;
  }
  if (cfg->biss2_enabled && (cfg->cas_algo != CAS_ALGO_NONE || cfg->n_cas_vendors > 0)) {
    argerr("--biss2-sw is mutually exclusive with --cas-algo/--cas-ecmg");
    return ARGS_ERR;
  }
  if (cfg->biss2_emit_esw && !cfg->biss2_enabled) {
    argerr("--biss2-emit-esw requires --biss2-sw");
    return ARGS_ERR;
  }
  if (cfg->biss1_enabled && (cfg->cas_algo != CAS_ALGO_NONE || cfg->n_cas_vendors > 0)) {
    argerr("--biss1-sw is mutually exclusive with --cas-algo/--cas-ecmg");
    return ARGS_ERR;
  }
  if (cfg->biss2_enabled && cfg->biss1_enabled) {
    argerr("--biss2-sw and --biss1-sw are mutually exclusive");
    return ARGS_ERR;
  }
  if (cfg->biss2_ca_enabled && (cfg->cas_algo != CAS_ALGO_NONE || cfg->n_cas_vendors > 0)) {
    argerr("--biss2-ca-receivers is mutually exclusive with --cas-algo/--cas-ecmg");
    return ARGS_ERR;
  }
  if (cfg->biss2_ca_enabled && (cfg->biss2_enabled || cfg->biss1_enabled)) {
    argerr("--biss2-ca-receivers is mutually exclusive with --biss2-sw/--biss1-sw");
    return ARGS_ERR;
  }
  if (cfg->biss2_ca_session_id_given && !cfg->biss2_ca_enabled) {
    argerr("--biss2-ca-session-id requires --biss2-ca-receivers");
    return ARGS_ERR;
  }
  if (cfg->biss2_ca_enabled && cfg->cas_cp_duration_ms < 1000) {
    argerr("--biss2-ca-receivers needs --cas-cp-duration >= 1000 (Tech 3292-s1 T_ECM_change_min)");
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
  }
  {
    unsigned used[RADIOHEAD_MAX_INPUTS];
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
  for (i = 0; i < cfg->n_inputs; i++) {
    if (cfg->inputs[i].sdt_text[0])
      continue;
    if (cfg->n_inputs == 1)
      snprintf(cfg->inputs[i].sdt_text, sizeof cfg->inputs[i].sdt_text, "%s", TOOL_NAME);
    else
      snprintf(cfg->inputs[i].sdt_text, sizeof cfg->inputs[i].sdt_text, "%s %u", TOOL_NAME, i + 1);
  }
  return ARGS_OK;
}
