/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/cas/cas_args.h"
#include "lib/helper/describe.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/uriparse.h"
#include "lib/mux/fec2022.h"
#include "lib/net/netconnect.h"

#include "args.h"
#include "config.h"
#include "mux/pmtbuild.h"
#include "version.h"

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

/* [@]<addr>:<port> or [@][<addr6>]:<port>, multicast literal required */
static int mcast_group_parse(const char *s, int *family, char *addr_out, size_t addr_out_sz, unsigned *port_out) {
  if (*s == '@') s++;
  return uriparse_mcast_addrport(s, family, addr_out, addr_out_sz, port_out);
}

int tvh_cfg_mcast(config_t *cfg, const char *s) {
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
  if (strncmp(uri, "rist://", 7) == 0) {
    if (uri[7] != '@') return -1; /* rist:// as input always listens */
    if (strlen(uri) >= sizeof s->rist_uri) return -1;
    s->kind = SRC_RIST;
    bufcpy(s->rist_uri, sizeof s->rist_uri, uri);
    return 0;
  }
  if (strncmp(uri, "srt://", 6) == 0) {
    const char *rest = uri + 6;
    int listen = *rest == '@';
    if (listen) rest++;
    if (argutil_addrport_parse(rest, &s->srt_family, s->srt_host, sizeof s->srt_host, &s->srt_port)) return -1;
    s->kind = SRC_SRT;
    s->srt_listen = listen;
    return 0;
  }
  return -1;
}

void source_describe(const source_t *s, char *buf, size_t n) {
  switch (s->kind) {
    case SRC_RTP:
      describe_mcast_uri(buf, n, "rtp", s->family, s->group, s->port);
      break;
    case SRC_UDP:
      describe_mcast_uri(buf, n, "udp", s->family, s->group, s->port);
      break;
    case SRC_HTTP:
      describe_http_uri(buf, n, s->http.tls, s->http.host, s->http.port, s->http.path);
      break;
    case SRC_STDIN:
      bufcpy(buf, n, "-");
      break;
    case SRC_RIST:
      bufcpy(buf, n, s->rist_uri);
      break;
    case SRC_SRT:
      describe_srt_uri(buf, n, s->srt_family, s->srt_listen, s->srt_host, s->srt_port);
      break;
  }
}

void mcast_describe(const config_t *cfg, char *buf, size_t n) {
  if (cfg->family == AF_INET6) snprintf(buf, n, "[%s]:%u", cfg->mcast_group, cfg->mcast_port);
  else                         snprintf(buf, n, "%s:%u", cfg->mcast_group, cfg->mcast_port);
}

int tvh_cfg_source(source_t *dst, const char *uri, char *err, size_t errsz) {
  if (source_parse(uri, dst)) {
    snprintf(err, errsz, "invalid uri '%s'", uri);
    return -1;
  }
  return 0;
}

/* uri NULL: blank input, source given later */
int tvh_cfg_add_input(config_t *cfg, const char *uri, char *err, size_t errsz) {
  dipitvhead_input_t *in;
  if (cfg->n_inputs >= ARGS_MAX_INPUTS) {
    snprintf(err, errsz, "too many inputs (max %d)", ARGS_MAX_INPUTS);
    return -1;
  }
  in = &cfg->inputs[cfg->n_inputs];
  memset(in, 0, sizeof *in);
  if (uri && tvh_cfg_source(&in->input, uri, err, errsz)) return -1;
  cfg->n_inputs++;
  return 0;
}

int tvh_cfg_add_peer(config_t *cfg, const char *uri, char *err, size_t errsz) {
  if (strncmp(uri, "rist://", 7) == 0) {
    if (cfg->n_srt > 0) {
      snprintf(err, errsz, "rist:// and srt:// peers cannot mix in one run");
      return -1;
    }
    if (cfg->n_rist >= ARGS_MAX_RIST_PEERS) {
      snprintf(err, errsz, "too many peers (max %d)", ARGS_MAX_RIST_PEERS);
      return -1;
    }
    if (strlen(uri) >= sizeof cfg->rist_uri[0]) {
      snprintf(err, errsz, "uri too long");
      return -1;
    }
    memcpy(cfg->rist_uri[cfg->n_rist], uri, strlen(uri) + 1);
    cfg->n_rist++;
    return 0;
  }
  if (strncmp(uri, "srt://", 6) == 0) {
    if (cfg->n_rist > 0) {
      snprintf(err, errsz, "rist:// and srt:// peers cannot mix in one run");
      return -1;
    }
    if (cfg->n_srt >= ARGS_MAX_SRT_PEERS) {
      snprintf(err, errsz, "too many srt:// peers (max %d)", ARGS_MAX_SRT_PEERS);
      return -1;
    }
    if (uri[6] == '@') {
      snprintf(err, errsz, "srt:// output always calls out, no listener mode");
      return -1;
    }
    if (argutil_addrport_parse(uri + 6, &cfg->srt_family[cfg->n_srt], cfg->srt_host[cfg->n_srt], sizeof cfg->srt_host[0], &cfg->srt_port[cfg->n_srt])) {
      snprintf(err, errsz, "invalid srt uri '%s'", uri);
      return -1;
    }
    cfg->n_srt++;
    return 0;
  }
  snprintf(err, errsz, "invalid uri '%s' (must start with rist:// or srt://)", uri);
  return -1;
}

int tvh_cfg_profile(config_t *cfg, const char *val, char *err, size_t errsz) {
  static const enum_map_t map[] = {{"simple", RIST_PROF_SIMPLE}, {"main", RIST_PROF_MAIN}};
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], val, &v)) {
    snprintf(err, errsz, "invalid '%s' (simple|main)", val);
    return -1;
  }
  cfg->rist_profile = (rist_profile_sel_t)v;
  cfg->rist_profile_given = 1;
  return 0;
}

int tvh_cfg_group_mode(config_t *cfg, const char *val, char *err, size_t errsz) {
  static const enum_map_t map[] = {{"broadcast", SRT_BOND_BROADCAST}, {"backup", SRT_BOND_BACKUP}};
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], val, &v)) {
    snprintf(err, errsz, "invalid '%s' (broadcast|backup)", val);
    return -1;
  }
  cfg->srt_group_mode = (srt_bond_mode_t)v;
  return 0;
}

static int id_parse(const char *s, unsigned *out) {
  return argutil_uint_range(s, 1, 0xFFFF, out);
}

/* organisation_id is 32 bits per TS 102 809, unlike application_id's 16 */
static int org_id_parse(const char *s, unsigned *out) {
  return argutil_uint_range(s, 1, 0xFFFFFFFFUL, out);
}

/* decimal or 0x-hex, PMT pid range: 0x0010..0x1FFE (0 = auto, handled by caller) */
int tvh_cfg_pid(const char *s, unsigned *out) {
  char *end;
  unsigned long v = strtoul(s, &end, 0);
  if (*end != '\0' || v > 0x1FFE) return -1;
  *out = (unsigned)v;
  return 0;
}

/* pids and/or video/audio/lcevc keywords, e.g. 0x0103,video */
int tvh_cfg_cas_pids(config_t *cfg, const char *s) {
  char buf[512];
  char *save = NULL;
  if (strlen(s) >= sizeof buf) return -1;
  bufcpy(buf, sizeof buf, s);
  cfg->cas_pid_count = 0;
  cfg->cas_pids_video = 0;
  cfg->cas_pids_audio = 0;
  cfg->cas_pids_lcevc = 0;
  for (const char *tok = strtok_r(buf, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
    unsigned pid;
    if (strcmp(tok, "video") == 0) {
      cfg->cas_pids_video = 1;
      continue;
    }
    if (strcmp(tok, "audio") == 0) {
      cfg->cas_pids_audio = 1;
      continue;
    }
    if (strcmp(tok, "lcevc") == 0) {
      cfg->cas_pids_lcevc = 1;
      continue;
    }
    if (cfg->cas_pid_count >= ARGS_MAX_CAS_PIDS) return -1;
    if (tvh_cfg_pid(tok, &pid) || pid == 0) return -1;
    cfg->cas_pids[cfg->cas_pid_count++] = pid;
  }
  return (cfg->cas_pid_count || cfg->cas_pids_video || cfg->cas_pids_audio || cfg->cas_pids_lcevc) ? 0 : -1;
}

/* comma-separated TVSTRIP_* tokens, or "none" (default) */
int tvh_cfg_strip(unsigned *mask, const char *s) {
  static const enum_map_t map[] = {{"DATA", TVSTRIP_DATA}, {"ECM", TVSTRIP_ECM}};
  const char *p = s;

  if (strcmp(s, "none") == 0) {
    *mask = 0;
    return 0;
  }
  *mask = 0;
  while (*p) {
    const char *comma = strchr(p, ',');
    size_t len = comma ? (size_t)(comma - p) : strlen(p);
    char tok[8];
    int v;
    if (len == 0 || len >= sizeof tok) return -1;
    memcpy(tok, p, len);
    tok[len] = '\0';
    if (map_lookup(map, sizeof map / sizeof map[0], tok, &v)) return -1;
    *mask |= (unsigned)v;
    p += len;
    if (*p == ',') p++;
  }
  return 0;
}

static void print_help(void) {
  printf(
    "usage: %s -i <uri> [per-input options] [-i <uri> ...] {-m <mcast>:<port>|-R <uri>} [options]\n\n"
    "re-package one or more transport streams (already-muxed, not raw ES) as one multicast.\n"
    "A single -i: normal SPTS. Multiple -i: MPTS, one program per input.\n\n"
    "options:\n"
    "  -i, --input <uri>          udp://, rtp://, http(s)://, rist://@host:port[?query]\n"
    "                             (single peer, @ marks it listening, srt://[@]host:port\n"
    "                             (single peer, or \"-\" for stdin; repeatable.\n"
    "                             each RIST/SRT input is an extra thread.\n"
    "                             see options scoped to this below.\n"
    "  -p, --pmt-pid <pid>        -i before: select program by PMT PID\n"
    "                             (dec or 0x-hex; default: first live one)\n"
    "  -m, --mcast <g>:<p>        output multicast group:port ([addr6]:port for IPv6)\n"
    "  -O, --out-iface <iface>    outgoing multicast interface name\n"
    "  -u, --udp                  plain UDP output (default: RTP-wrapped; -m output only)\n"
    "  -T, --ttl <n>              multicast TTL / hop limit (default: 1)\n"
    "      --dscp <v>             output DSCP marking: video-high|video-low|voice|\n"
    "                             signalling|best-effort|0..63 (default: video-high)\n"
    "      --al-fec <L>:<D>       Annex E Layer 1 FEC (SMPTE 2022-1), L*D<=400, L<=40\n"
    "      --al-fec-port <port>   AL-FEC stream UDP port, requires --al-fec\n"
    "  -R, --remote <uri>         rist://host:port[?query] or srt://host:port output,\n"
    "                             bonded with any other -R of the same scheme given.\n"
    "                             one scheme at a time, rist:// and srt:// don't mix\n"
    "      --rist-profile <p>     simple|main; -R rist:// peers only (default: simple)\n"
    "      --rist-secret <psk>    -R rist:// pre-shared key; requires --rist-profile main\n"
    "      --rist-cname <name>    -R rist:// cname (default: library default)\n"
    "      --rist-buffer <ms>     -R rist:// recovery buffer (default: library default)\n"
    "      --srt-group-mode <m>   broadcast|backup. required to bond >1 -R srt:// peer\n"
    "      --srt-passphrase <pw>  passphrase for every -R srt:// peer, 10..79 chars\n"
    "      --srt-pbkeylen <n>     AES key length for --srt-passphrase: 16|24|32\n"
    "      --srt-streamid <id>    SRTO_STREAMID for every -R srt:// peer\n"
    "      --srt-packetfilter <c> SRTO_PACKETFILTER for every -R srt:// peer\n"
    "      --srt-latency <ms>     SRTO_LATENCY for every -R srt:// peer\n"
    "  -n, --nit <text|->         NIT (whole output): default passthrough; \"-\" drops\n"
    "      --default-provider <p> SDT default provider name, if not overridden\n"
    "                             by --provider at programme level (default: " TOOL_NAME ")\n"
    "  -b, --bitrate <kbps>       target output bitrate across all inputs (default: no shaping)\n"
    "  -S, --stuff                null-packet stuffing up to -b's target (needs -b)\n"
    "  -B, --burst-limit          cap output at -b's target, never above (needs -b)\n"
    "  -e, --error <seconds>      on input error, reconnect after N s (default: fail once,\n"
    "                             always retries when more than one -i is given)\n"
    "  -k, --insecure             skip TLS verification\n"
    "      --tsid <n>             transport_stream_id (default 1)\n"
    "      --onid <n>             original_network_id (default 1)\n"
    "  -v, --verbose              periodic stats on stderr\n"
    "      --color <when>         auto|always|never (default auto)\n"
    "      --metrics <path>       socket for metrics (default: /run/dvbipitools/metrics.sock)\n"
    "      --metrics-id <name>    stable instance id. metrics are disabled unless set\n"
    "      --metrics-interval <s> snapshot interval in seconds (default: 5)\n"
    "      --cas-algo <a>         enable CAS: cissa|csa2|csa1 (default: disabled)\n"
    "      --cas-ecmg <ep>        ECMG address, tcp://host:port. repeatable, one CAS vendor\n"
    "                             per --cas-ecmg (required with --cas-algo)\n"
    "                             see options scoped to this below.\n"
    "      --cas-pids <list>      PIDs to scramble: comma-separated pids and/or video/audio/lcevc\n"
    "                             keywords (default: video,audio - lcevc never implied by either)\n"
    "      --cas-cp-duration <ms> crypto-period duration in ms, shared by every vendor (default: 10000)\n"
    "      --cas-fallback-clear   on total outage (or a --cas-required vendor down): clear\n"
    "                             instead of staying scrambled on the last known-good CW\n"
    "      --biss2-sw <hex32>     enable BISS2 Mode 1/E: 32 hex char Session Word, scrambles\n"
    "                             with CISSA. No ECMG/EMMG. Mutually exclusive with --cas-algo\n"
    "      --biss2-emit-esw <id>  with --biss2-sw: log the AES-128-ECB Encrypted Session Word\n"
    "                             for this 32 hex char receiver ID, for out-of-band distribution\n"
    "      --biss1-sw <hex12>     enable legacy BISS1 Mode 1: 12 hex char Session Word,\n"
    "                             scrambles with CSA1. Mutually exclusive with --biss2-sw/--cas-algo\n"
    "      --biss2-ca-receivers <dir> enable BISS2 Mode CA: directory of PEM public keys, one\n"
    "                             per entitled receiver/group. Rescanned on SIGHUP. a receiver\n"
    "                             removed from the directory is revoked (forces a Session Key\n"
    "                             change). Mutually exclusive with --biss1-sw/--biss2-sw/--cas-algo\n"
    "      --biss2-ca-session-id <n> administratively unique entitlement_session_id, dec or\n"
    "                             0x-hex, 16 bit (default: random at startup)\n"
    "  -d, --daemonize            fork to background after startup, detach from terminal\n"
    "  -c, --config <path>        YAML config file (default: %s, if present)\n"
    "      --configtest           check the config file, then exit\n"
    "  -h, --help                 this help\n\n"
    "scoped to the -i input right before:\n"
    "      --sid <n>                  service_id/program_number (default: auto)\n"
    "  -s, --sdt <text|->             SDT service_name. default=passthrough, \"-\" drops\n"
    "      --provider <text>          SDT service_provider_name.\n"
    "                                 default=passthrough (or --default-provider with -s)\n"
    "  -I, --iface <iface>            incoming multicast interface name\n"
    "      --strip-eit                drop source EIT (default: passed through)\n"
    "      --strip <list>|none        comma list of DATA,ECM to drop (default: none)\n"
    "      --hbbtv <url>              inject an AIT (default: none)\n"
    "      --hbbtv-org-id <n>         HbbTV organisation_id (required with --hbbtv)\n"
    "      --hbbtv-app-id <n>         HbbTV application_id (required with --hbbtv)\n"
    "      --rist-profile-in <p>      simple|main; -i rist:// only (default: simple)\n"
    "      --srt-passphrase-in <p>    passphrase, 10..79 chars.  -i srt:// only\n"
    "      --srt-pbkeylen-in <n>      AES key length 16|24|32, requires --srt-passphrase-in\n"
    "      --srt-streamid-in <id>     SRTO_STREAMID; -i srt:// only\n"
    "      --srt-packetfilter-in <c>  SRTO_PACKETFILTER; -i srt:// only\n"
    "      --srt-latency-in <ms>      SRTO_LATENCY; -i srt:// only\n\n"
    "scoped to --cas-ecmg right before:\n"
    "      --cas-ecmg-version <n>     protocol version 2|3 (default: auto-negotiate)\n"
    "      --cas-super-id <n>         Super_CAS_id, dec or 0x-hex (required per vendor)\n"
    "      --cas-ecm-id <n>           ECM_id (required per vendor)\n"
    "      --cas-ecm-pid <pid>        ECM stream PID (default: 0x0020)\n"
    "      --cas-emmg-port <n>        our EMMG listener port (default: 8002)\n"
    "      --cas-emmg-max-conns <n>   max concurrent EMMG client conns (default: 8, max: 64)\n"
    "      --cas-emmg-version <n>     EMMG protocol version 2|3 (default: client proposal)\n"
    "      --cas-emmg-reverse <ep>    reverse EMMG, dial out to tcp://host:port\n"
    "      --cas-emm-pid <pid>        output PID for its EMM stream (default: 0x0021)\n"
    "      --cas-resilience <r>       ECMG loss handling, frozen|cycling|silent (default: frozen)\n"
    "      --cas-required             its outage forces the global fallback regardless of others\n"
    "      --cas-cwenc-algo <a>       encrypt CW_provision's CWs per Annex D, des56|aes128|aes256 (default: off)\n"
    "      --cas-cwenc-aes-mode <m>   stream|ecb, aes* only (default: stream)\n"
    "      --cas-cwenc-fixed-key <hx> 14/32/64 hex chars for des56/aes128/aes256 (default: Annex D ROM)\n"
    "      --cas-cwenc-key-list-a <p> 2048-byte Annex D key list file\n"
    "      --cas-cwenc-key-list-b <p> same, second list\n\n"

    "examples:\n"
    "  %s -i rtp://@239.19.75.1:8700 -m 239.1.1.1:5000 -s \"My Channel\"\n"
    "  %s -i https://host/live/x/y.ts -m 239.1.1.2:5000 -b 8000 -S -B\n"
    "  %s -i rtp://@239.19.75.1:8700 --sdt \"Channel A\" -i rtp://@239.19.75.2:8700 --sdt \"Channel B\" \\\n"
    "     -m 239.1.1.3:5000 -e 5\n",
    TOOL_NAME, DEFAULT_CONFIG_PATH, TOOL_NAME, TOOL_NAME, TOOL_NAME);
}

static int is_sid_used(const unsigned *used, unsigned n_used, unsigned sid) {
  for (unsigned j = 0; j < n_used; j++) if (used[j] == sid) return 1;
  return 0;
}

static int has_duplicate_sid(const config_t *cfg) {
  unsigned used[ARGS_MAX_INPUTS] = {0};
  unsigned n_used = 0;
  for (unsigned i = 0; i < cfg->n_inputs; i++) {
    if (cfg->inputs[i].sid == 0) continue;
    if (is_sid_used(used, n_used, cfg->inputs[i].sid)) return 1;
    used[n_used++] = cfg->inputs[i].sid;
  }
  return 0;
}

int tvh_cfg_check(const config_t *cfg, int partial, tvh_report_fn rep, void *ud) {
  int fatal = 0;
  unsigned n_rist_in = 0;
  size_t pwlen = strlen(cfg->srt_passphrase);
  char msg[192];
#define FATAL(...) do { snprintf(msg, sizeof msg, __VA_ARGS__); rep(ud, 1, msg); fatal++; } while (0)
#define NOTE(...) do { snprintf(msg, sizeof msg, __VA_ARGS__); rep(ud, 0, msg); } while (0)

  if (!partial && cfg->n_inputs == 0) FATAL("missing -i input");
  for (unsigned i = 0; i < cfg->n_inputs; i++) if (cfg->inputs[i].input.kind == SRC_RIST) n_rist_in++;
  if (n_rist_in > 1) FATAL("at most one -i rist:// input: librist isn't safe with more than one context per process");
  if (n_rist_in && cfg->n_rist) FATAL("-i rist:// and -R rist:// cannot combine: librist isn't safe with more than one context per process");
  if (!partial && !cfg->mcast_port && cfg->n_rist == 0 && cfg->n_srt == 0) FATAL("need -m output multicast or at least one -R peer");
  if ((cfg->stuff || cfg->burst_limit) && !cfg->bitrate_kbps) FATAL("-S/--stuff and -B/--burst-limit need -b/--bitrate");
  if ((cfg->metrics_sock || cfg->metrics_interval_s) && !cfg->metrics_id) FATAL("--metrics/--metrics-interval require --metrics-id");
  if (cfg->al_fec_l && !cfg->al_fec_port) FATAL("--al-fec requires --al-fec-port");
  if (!cfg->al_fec_l && cfg->al_fec_port) NOTE("--al-fec-port has no effect without --al-fec");
  if (cfg->al_fec_l && !cfg->rtp) FATAL("--al-fec requires RTP output, not -u/--udp");
  if (cfg->al_fec_l && !cfg->mcast_port) NOTE("--al-fec has no effect without -m");
  if (cfg->n_rist == 0 && (cfg->rist_profile_given || cfg->rist_secret[0] || cfg->rist_cname[0] || cfg->rist_buffer_ms))
    NOTE("--rist-profile/--rist-secret/--rist-cname/--rist-buffer have no effect without -R");
  if (cfg->n_rist > 0 && cfg->rist_secret[0] && cfg->rist_profile != RIST_PROF_MAIN) FATAL("--rist-secret requires --rist-profile main");
  if (cfg->n_srt > 1 && cfg->srt_group_mode == SRT_BOND_NONE) FATAL("bonding several -R srt:// peers requires --srt-group-mode");
  if (cfg->n_srt == 1 && cfg->srt_group_mode != SRT_BOND_NONE) FATAL("--srt-group-mode has no effect with a single -R srt:// peer");
  if (cfg->n_srt == 0 && (cfg->srt_group_mode != SRT_BOND_NONE || cfg->srt_passphrase[0] || cfg->srt_pbkeylen ||
                          cfg->srt_streamid[0] || cfg->srt_packetfilter[0] || cfg->srt_latency_ms))
    NOTE("--srt-* has no effect without an -R srt:// peer");
  if (pwlen && (pwlen < 10 || pwlen > 79)) FATAL("--srt-passphrase must be 10..79 characters");
  if (cfg->srt_pbkeylen && !pwlen) FATAL("--srt-pbkeylen requires --srt-passphrase");
  for (unsigned i = 0; i < cfg->n_inputs; i++) {
    const dipitvhead_input_t *in = &cfg->inputs[i];
    size_t inlen = strlen(in->srt_passphrase_in);
    if (inlen && (inlen < 10 || inlen > 79)) FATAL("--srt-passphrase-in must be 10..79 characters");
    if (in->srt_pbkeylen_in && !inlen) FATAL("--srt-pbkeylen-in requires --srt-passphrase-in");
    if (in->input.kind != SRC_SRT && (inlen || in->srt_pbkeylen_in || in->srt_streamid_in[0] || in->srt_packetfilter_in[0] || in->srt_latency_in_ms))
      NOTE("--srt-*-in has no effect, that -i isn't srt://");
    if (in->hbbtv_url && (!in->hbbtv_org_id || !in->hbbtv_app_id)) FATAL("--hbbtv requires --hbbtv-org-id and --hbbtv-app-id");
    if ((in->hbbtv_org_id || in->hbbtv_app_id) && !in->hbbtv_url) FATAL("--hbbtv-org-id/--hbbtv-app-id need --hbbtv");
  }
  if (cfg->any_cas_flag && cfg->cas_algo == CAS_ALGO_NONE && !cfg->biss2_enabled && !cfg->biss1_enabled && !cfg->biss2_ca_enabled)
    FATAL("--cas-* options require --cas-algo (or --cas-pids alone under --biss2-sw/--biss1-sw/--biss2-ca-receivers)");
  if (cas_args_validate(TOOL_NAME, cfg->cas_algo, cfg->cas_vendors, cfg->n_cas_vendors, cfg->biss2_enabled, cfg->biss1_enabled,
                        cfg->biss2_ca_enabled, cfg->biss2_emit_esw, cfg->biss2_ca_session_id_given, cfg->cas_cp_duration_ms) != 0)
    fatal++;
  if (has_duplicate_sid(cfg)) FATAL("duplicate --sid");
#undef FATAL
#undef NOTE
  return fatal;
}

static void report_cli(void *ud, int fatal, const char *msg) {
  (void)ud;
  if (fatal) argerr("%s", msg);
  else       log_line(TOOL_NAME ": %s", msg);
}

static void finalize(config_t *cfg) {
  unsigned used[ARGS_MAX_INPUTS];
  unsigned n_used = 0;
  unsigned next = 1;
  int have_cas_pids = cfg->cas_pid_count || cfg->cas_pids_video || cfg->cas_pids_audio || cfg->cas_pids_lcevc;

  if (!have_cas_pids && (cfg->cas_algo != CAS_ALGO_NONE || cfg->biss1_enabled || cfg->biss2_enabled || cfg->biss2_ca_enabled)) {
    /* default: scramble all video and audio elementary streams */
    cfg->cas_pids_video = 1;
    cfg->cas_pids_audio = 1;
  }
  if (cfg->cas_algo != CAS_ALGO_NONE || cfg->biss1_enabled || cfg->biss2_enabled || cfg->biss2_ca_enabled) {
    for (unsigned i = 0; i < cfg->n_inputs; i++) if (!(cfg->inputs[i].strip_mask & TVSTRIP_ECM)) {
      log_line(TOOL_NAME ": source CA/ECM passthrough disabled: --cas-algo/--biss* already scrambling this mux");
      break;
    }
  }
  for (unsigned i = 0; i < cfg->n_inputs; i++) if (cfg->inputs[i].sid) used[n_used++] = cfg->inputs[i].sid;
  for (unsigned i = 0; i < cfg->n_inputs; i++) {
    if (cfg->inputs[i].sid != 0) continue;
    while (is_sid_used(used, n_used, next)) next++;
    cfg->inputs[i].sid = next;
    used[n_used++] = next;
    next++;
  }
}

static const char shortopts[] = "+i:p:m:I:O:uT:n:s:b:SBe:kvdhR:c:";

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
  {"cas-emmg-max-conns", required_argument, 0, 1035},
  {"cas-emmg-reverse", required_argument, 0, 1057},
  {"cas-emm-pid", required_argument, 0, 1016},
  {"cas-pids", required_argument, 0, 1017},
  {"cas-cp-duration", required_argument, 0, 1018},
  {"cas-resilience", required_argument, 0, 1019},
  {"metrics", required_argument, 0, 1020},
  {"metrics-id", required_argument, 0, 1021},
  {"metrics-interval", required_argument, 0, 1022},
  {"cas-required", no_argument, 0, 1023},
  {"cas-cwenc-algo", required_argument, 0, 1048},
  {"cas-cwenc-aes-mode", required_argument, 0, 1049},
  {"cas-cwenc-fixed-key", required_argument, 0, 1050},
  {"cas-cwenc-key-list-a", required_argument, 0, 1051},
  {"cas-cwenc-key-list-b", required_argument, 0, 1052},
  {"cas-fallback-clear", no_argument, 0, 1024},
  {"biss2-sw", required_argument, 0, 1025},
  {"biss2-emit-esw", required_argument, 0, 1026},
  {"biss1-sw", required_argument, 0, 1027},
  {"biss2-ca-receivers", required_argument, 0, 1028},
  {"biss2-ca-session-id", required_argument, 0, 1029},
  {"remote", required_argument, 0, 'R'},
  {"rist-profile", required_argument, 0, 1030},
  {"rist-secret", required_argument, 0, 1031},
  {"rist-cname", required_argument, 0, 1032},
  {"rist-buffer", required_argument, 0, 1033},
  {"daemonize", no_argument, 0, 'd'},
  {"strip", required_argument, 0, 1034},
  {"rist-profile-in", required_argument, 0, 1036},
  {"dscp", required_argument, 0, 1053},
  {"al-fec", required_argument, 0, 1054},
  {"al-fec-port", required_argument, 0, 1055},
  {"srt-passphrase-in", required_argument, 0, 1037},
  {"srt-pbkeylen-in", required_argument, 0, 1038},
  {"srt-streamid-in", required_argument, 0, 1039},
  {"srt-packetfilter-in", required_argument, 0, 1040},
  {"srt-latency-in", required_argument, 0, 1041},
  {"srt-group-mode", required_argument, 0, 1042},
  {"srt-passphrase", required_argument, 0, 1043},
  {"srt-pbkeylen", required_argument, 0, 1044},
  {"srt-streamid", required_argument, 0, 1045},
  {"srt-packetfilter", required_argument, 0, 1046},
  {"srt-latency", required_argument, 0, 1047},
  {"provider", required_argument, 0, 1056},
  {"default-provider", required_argument, 0, 1058},
  {"config", required_argument, 0, 'c'},
  {"configtest", no_argument, 0, 1059},
  {"help", no_argument, 0, 'h'},
  {0, 0, 0, 0}};


static args_status_t prescan(int argc, char **argv, const char **cfg_path, int *configtest) {
  int c;
  optind = 1;
  opterr = 0;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    if (c == 'c') *cfg_path = optarg;
    if (c == 1059) *configtest = 1;
    if (c == 'h') {
      print_help();
      opterr = 1;
      return ARGS_HELP;
    }
  }
  opterr = 1;
  return ARGS_OK;
}

#define REQUIRE_INPUT(opt) \
  do { \
    if (!cli_inputs) { \
      argerr(opt " must follow the -i it names"); \
      return ARGS_ERR; \
    } \
  } while (0)

#define CAS_VENDOR(opt) \
  do { \
    if (!cli_vendors) { \
      argerr(opt " must follow the --cas-ecmg it names"); \
      return ARGS_ERR; \
    } \
  } while (0)

#define CHECK(call, opt) \
  do { \
    if (call) { \
      argerr(opt ": %s", err); \
      return ARGS_ERR; \
    } \
  } while (0)

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  const char *cfg_path = NULL;
  int configtest = 0;
  int cli_inputs = 0;
  int cli_vendors = 0;
  int cli_peers = 0;
  args_status_t pst;
  char err[192];
  int c;

  pst = prescan(argc, argv, &cfg_path, &configtest);
  if (pst != ARGS_OK) return pst;
  if (configtest) return tvh_cfg_test(cfg_path) ? ARGS_ERR : ARGS_HELP;

  tvh_cfg_defaults(cfg);
  if (tvh_cfg_load(cfg, cfg_path)) return ARGS_ERR;

  optind = 1;
  /* leading '+': disable GNU getopt argument permutation, so per-input options stay paired
     with whichever -i preceded them instead of being reordered */
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    dipitvhead_input_t *in = cli_inputs ? &cfg->inputs[cfg->n_inputs - 1] : NULL;
    cas_vendor_t *vd = cli_vendors ? &cfg->cas_vendors[cfg->n_cas_vendors - 1] : NULL;
    switch (c) {
      case 'i':
        if (!cli_inputs) {
          cfg->n_inputs = 0;
          cli_inputs = 1;
        }
        CHECK(tvh_cfg_add_input(cfg, optarg, err, sizeof err), "-i");
        break;
      case 'p':
        REQUIRE_INPUT("-p/--pmt-pid");
        if (tvh_cfg_pid(optarg, &in->pmt_pid) || in->pmt_pid == 0) {
          argerr("invalid -p pmt-pid: %s (0x0010..0x1FFE)", optarg);
          return ARGS_ERR;
        }
        break;
      case 'm':
        if (tvh_cfg_mcast(cfg, optarg)) {
          argerr("invalid -m group:port: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 'I':
        REQUIRE_INPUT("-I/--iface");
        in->iface_in = optarg;
        break;
      case 'O':
        cfg->iface_out = optarg;
        break;
      case 'u':
        cfg->rtp = 0;
        break;
      case 'T': {
        unsigned v;
        if (argutil_uint_range(optarg, 1, 255, &v)) {
          argerr("invalid -T ttl: %s (1..255)", optarg);
          return ARGS_ERR;
        }
        cfg->ttl = v;
        break;
      }
      case 'n':
        if (strcmp(optarg, "-") == 0) {
          cfg->nit_mode = TABLE_DROP;
        } else {
          cfg->nit_mode = TABLE_OVERRIDE;
          if (argutil_bufcpy_opt(TOOL_NAME, cfg->nit_text, sizeof cfg->nit_text, optarg, "-n nit-text"))
            return ARGS_ERR;
        }
        break;
      case 's':
        REQUIRE_INPUT("-s/--sdt");
        if (strcmp(optarg, "-") == 0) {
          in->sdt_mode = TABLE_DROP;
        } else {
          in->sdt_mode = TABLE_OVERRIDE;
          if (argutil_bufcpy_opt(TOOL_NAME, in->sdt_text, sizeof in->sdt_text, optarg, "-s sdt-text"))
            return ARGS_ERR;
        }
        break;
      case 1056:
        REQUIRE_INPUT("--provider");
        if (argutil_bufcpy_opt(TOOL_NAME, in->provider_text, sizeof in->provider_text, optarg, "--provider text"))
          return ARGS_ERR;
        break;
      case 1058:
        if (argutil_bufcpy_opt(TOOL_NAME, cfg->default_provider_text, sizeof cfg->default_provider_text, optarg, "--default-provider text"))
          return ARGS_ERR;
        break;
      case 'b':
        if (argutil_uint_range(optarg, 1, 1000000, &cfg->bitrate_kbps)) {
          argerr("invalid -b bitrate: %s (kbps)", optarg);
          return ARGS_ERR;
        }
        break;
      case 'S':
        cfg->stuff = 1;
        break;
      case 'B':
        cfg->burst_limit = 1;
        break;
      case 1000:
        REQUIRE_INPUT("--strip-eit");
        in->strip_eit = 1;
        break;
      case 1034:
        REQUIRE_INPUT("--strip");
        if (tvh_cfg_strip(&in->strip_mask, optarg)) {
          argerr("invalid --strip: %s (comma list of DATA,ECM, or \"none\")", optarg);
          return ARGS_ERR;
        }
        break;
      case 1001:
        REQUIRE_INPUT("--hbbtv");
        in->hbbtv_url = optarg;
        break;
      case 1002:
        REQUIRE_INPUT("--hbbtv-org-id");
        if (org_id_parse(optarg, &in->hbbtv_org_id)) {
          argerr("invalid --hbbtv-org-id: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 1003:
        REQUIRE_INPUT("--hbbtv-app-id");
        if (id_parse(optarg, &in->hbbtv_app_id)) {
          argerr("invalid --hbbtv-app-id: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 'e': {
        unsigned v;
        if (argutil_uint_range(optarg, 0, UINT_MAX, &v)) {
          argerr("invalid -e seconds: %s", optarg);
          return ARGS_ERR;
        }
        cfg->error_retry_s = (long)v;
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
        REQUIRE_INPUT("--sid");
        if (id_parse(optarg, &in->sid)) {
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
      case 1008:
        cfg->any_cas_flag = 1;
        CHECK(cas_set_algo(&cfg->cas_algo, optarg, err, sizeof err), "--cas-algo");
        break;
      case 1009:
        if (!cli_vendors) {
          cfg->n_cas_vendors = 0;
          cli_vendors = 1;
        }
        cfg->any_cas_flag = 1;
        CHECK(cas_vendor_add(cfg->cas_vendors, &cfg->n_cas_vendors, optarg, err, sizeof err), "--cas-ecmg");
        break;
      case 1010:
        CAS_VENDOR("--cas-ecmg-version");
        cfg->any_cas_flag = 1;
        CHECK(cas_vendor_set_ecmg_version(vd, optarg, err, sizeof err), "--cas-ecmg-version");
        break;
      case 1011:
        CAS_VENDOR("--cas-super-id");
        cfg->any_cas_flag = 1;
        CHECK(cas_vendor_set_super_id(vd, optarg, err, sizeof err), "--cas-super-id");
        break;
      case 1012:
        CAS_VENDOR("--cas-ecm-id");
        cfg->any_cas_flag = 1;
        CHECK(cas_vendor_set_ecm_id(vd, optarg, err, sizeof err), "--cas-ecm-id");
        break;
      case 1013:
        CAS_VENDOR("--cas-ecm-pid");
        cfg->any_cas_flag = 1;
        CHECK(cas_vendor_set_ecm_pid(vd, optarg, err, sizeof err), "--cas-ecm-pid");
        break;
      case 1014:
        CAS_VENDOR("--cas-emmg-port");
        cfg->any_cas_flag = 1;
        CHECK(cas_vendor_set_emmg_port(vd, optarg, err, sizeof err), "--cas-emmg-port");
        break;
      case 1057:
        CAS_VENDOR("--cas-emmg-reverse");
        cfg->any_cas_flag = 1;
        CHECK(cas_vendor_set_emmg_reverse(vd, optarg, err, sizeof err), "--cas-emmg-reverse");
        break;
      case 1015:
        CAS_VENDOR("--cas-emmg-version");
        cfg->any_cas_flag = 1;
        CHECK(cas_vendor_set_emmg_version(vd, optarg, err, sizeof err), "--cas-emmg-version");
        break;
      case 1035:
        CAS_VENDOR("--cas-emmg-max-conns");
        cfg->any_cas_flag = 1;
        CHECK(cas_vendor_set_emmg_max_conns(vd, optarg, err, sizeof err), "--cas-emmg-max-conns");
        break;
      case 1016:
        CAS_VENDOR("--cas-emm-pid");
        cfg->any_cas_flag = 1;
        CHECK(cas_vendor_set_emm_pid(vd, optarg, err, sizeof err), "--cas-emm-pid");
        break;
      case 1017:
        cfg->any_cas_flag = 1;
        if (tvh_cfg_cas_pids(cfg, optarg)) {
          argerr("invalid --cas-pids: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 1018:
        cfg->any_cas_flag = 1;
        CHECK(cas_set_cp_duration(&cfg->cas_cp_duration_ms, optarg, err, sizeof err), "--cas-cp-duration");
        break;
      case 1019:
        CAS_VENDOR("--cas-resilience");
        cfg->any_cas_flag = 1;
        CHECK(cas_vendor_set_resilience(vd, optarg, err, sizeof err), "--cas-resilience");
        break;
      case 1020:
        cfg->metrics_sock = optarg;
        break;
      case 1021:
        cfg->metrics_id = optarg;
        break;
      case 1022:
        if (argutil_metrics_interval_opt(TOOL_NAME, optarg, &cfg->metrics_interval_s)) return ARGS_ERR;
        break;
      case 1023:
        CAS_VENDOR("--cas-required");
        cfg->any_cas_flag = 1;
        vd->required = 1;
        break;
      case 1048:
        CAS_VENDOR("--cas-cwenc-algo");
        cfg->any_cas_flag = 1;
        CHECK(cas_vendor_set_cwenc_algo(vd, optarg, err, sizeof err), "--cas-cwenc-algo");
        break;
      case 1049:
        CAS_VENDOR("--cas-cwenc-aes-mode");
        cfg->any_cas_flag = 1;
        CHECK(cas_vendor_set_cwenc_aes_mode(vd, optarg, err, sizeof err), "--cas-cwenc-aes-mode");
        break;
      case 1050:
        CAS_VENDOR("--cas-cwenc-fixed-key");
        cfg->any_cas_flag = 1;
        CHECK(cas_vendor_set_cwenc_fixed_key(vd, optarg, err, sizeof err), "--cas-cwenc-fixed-key");
        break;
      case 1051:
        CAS_VENDOR("--cas-cwenc-key-list-a");
        cfg->any_cas_flag = 1;
        CHECK(cas_vendor_set_cwenc_key_list_a(vd, optarg, err, sizeof err), "--cas-cwenc-key-list-a");
        break;
      case 1052:
        CAS_VENDOR("--cas-cwenc-key-list-b");
        cfg->any_cas_flag = 1;
        CHECK(cas_vendor_set_cwenc_key_list_b(vd, optarg, err, sizeof err), "--cas-cwenc-key-list-b");
        break;
      case 1024:
        cfg->any_cas_flag = 1;
        cfg->cas_fallback_clear = 1;
        break;
      case 1053:
        if (net_dscp_parse(optarg, &cfg->dscp)) {
          argerr("invalid --dscp: %s (video-high|video-low|voice|signalling|best-effort|0..63)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1054:
        if (fec2022_parse_ld(optarg, &cfg->al_fec_l, &cfg->al_fec_d)) {
          argerr("invalid --al-fec: %s (want L:D, L*D<=400, L<=40)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1055:
        if (argutil_port_parse(optarg, &cfg->al_fec_port)) {
          argerr("invalid --al-fec-port: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 1025:
        CHECK(cas_set_biss2_sw(&cfg->biss2_enabled, cfg->biss2_sw, optarg, err, sizeof err), "--biss2-sw");
        break;
      case 1026:
        CHECK(cas_set_biss2_emit_esw(&cfg->biss2_emit_esw, cfg->biss2_esw_id, optarg, err, sizeof err), "--biss2-emit-esw");
        break;
      case 1027:
        CHECK(cas_set_biss1_sw(&cfg->biss1_enabled, cfg->biss1_cw, optarg, err, sizeof err), "--biss1-sw");
        break;
      case 1028:
        cfg->biss2_ca_receivers_dir = optarg;
        cfg->biss2_ca_enabled = 1;
        break;
      case 1029:
        CHECK(cas_set_biss2_ca_session_id(&cfg->biss2_ca_session_id, &cfg->biss2_ca_session_id_given, optarg, err, sizeof err), "--biss2-ca-session-id");
        break;
      case 'v':
        cfg->verbose = 1;
        break;
      case 'd':
        cfg->daemonize = 1;
        break;
      case 'R':
        if (!cli_peers) {
          cfg->n_rist = 0;
          cfg->n_srt = 0;
          cli_peers = 1;
        }
        CHECK(tvh_cfg_add_peer(cfg, optarg, err, sizeof err), "-R");
        break;
      case 1030:
        CHECK(tvh_cfg_profile(cfg, optarg, err, sizeof err), "--rist-profile");
        break;
      case 1031:
        if (argutil_bufcpy_opt(TOOL_NAME, cfg->rist_secret, sizeof cfg->rist_secret, optarg, "--rist-secret"))
          return ARGS_ERR;
        break;
      case 1032:
        if (argutil_bufcpy_opt(TOOL_NAME, cfg->rist_cname, sizeof cfg->rist_cname, optarg, "--rist-cname"))
          return ARGS_ERR;
        break;
      case 1033:
        if (argutil_uint_range(optarg, 1, UINT_MAX, &cfg->rist_buffer_ms)) {
          argerr("invalid --rist-buffer: %s (ms)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1036: {
        static const enum_map_t map[] = {{"simple", 0}, {"main", 1}};
        int v;
        REQUIRE_INPUT("--rist-profile-in");
        if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
          argerr("invalid --rist-profile-in: %s (simple|main)", optarg);
          return ARGS_ERR;
        }
        in->rist_profile_main = v;
        break;
      }
      case 1037:
        REQUIRE_INPUT("--srt-passphrase-in");
        if (argutil_bufcpy_opt(TOOL_NAME, in->srt_passphrase_in, sizeof in->srt_passphrase_in, optarg, "--srt-passphrase-in"))
          return ARGS_ERR;
        break;
      case 1038: {
        char *end;
        unsigned long v;
        REQUIRE_INPUT("--srt-pbkeylen-in");
        v = strtoul(optarg, &end, 10);
        if (*end != '\0' || (v != 16 && v != 24 && v != 32)) {
          argerr("invalid --srt-pbkeylen-in: %s (16|24|32)", optarg);
          return ARGS_ERR;
        }
        in->srt_pbkeylen_in = (int)v;
        break;
      }
      case 1039:
        REQUIRE_INPUT("--srt-streamid-in");
        if (argutil_bufcpy_opt(TOOL_NAME, in->srt_streamid_in, sizeof in->srt_streamid_in, optarg, "--srt-streamid-in"))
          return ARGS_ERR;
        break;
      case 1040:
        REQUIRE_INPUT("--srt-packetfilter-in");
        if (argutil_bufcpy_opt(TOOL_NAME, in->srt_packetfilter_in, sizeof in->srt_packetfilter_in, optarg, "--srt-packetfilter-in"))
          return ARGS_ERR;
        break;
      case 1041:
        REQUIRE_INPUT("--srt-latency-in");
        if (argutil_uint_range(optarg, 1, 60000, &in->srt_latency_in_ms)) {
          argerr("invalid --srt-latency-in: %s (1..60000 ms)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1042:
        CHECK(tvh_cfg_group_mode(cfg, optarg, err, sizeof err), "--srt-group-mode");
        break;
      case 1043:
        if (argutil_bufcpy_opt(TOOL_NAME, cfg->srt_passphrase, sizeof cfg->srt_passphrase, optarg, "--srt-passphrase"))
          return ARGS_ERR;
        break;
      case 1044: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || (v != 16 && v != 24 && v != 32)) {
          argerr("invalid --srt-pbkeylen: %s (16|24|32)", optarg);
          return ARGS_ERR;
        }
        cfg->srt_pbkeylen = (int)v;
        break;
      }
      case 1045:
        if (argutil_bufcpy_opt(TOOL_NAME, cfg->srt_streamid, sizeof cfg->srt_streamid, optarg, "--srt-streamid"))
          return ARGS_ERR;
        break;
      case 1046:
        if (argutil_bufcpy_opt(TOOL_NAME, cfg->srt_packetfilter, sizeof cfg->srt_packetfilter, optarg, "--srt-packetfilter"))
          return ARGS_ERR;
        break;
      case 1047:
        if (argutil_uint_range(optarg, 1, 60000, &cfg->srt_latency_ms)) {
          argerr("invalid --srt-latency: %s (1..60000 ms)", optarg);
          return ARGS_ERR;
        }
        break;
      case 'c':
      case 1059:
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
  if (tvh_cfg_check(cfg, 0, report_cli, NULL)) return ARGS_ERR;
  finalize(cfg);
  return ARGS_OK;
}
