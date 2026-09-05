/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/cas/biss/biss.h"
#include "lib/cas/cas_args.h"
#include "lib/cas/emmg_server/emmg_server.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/uriparse.h"

#include "args.h"
#include "mux/pmtbuild.h"
#include "version.h"

static void argerr(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void argerr(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  argutil_verr(TOOL_NAME, fmt, ap);
  va_end(ap);
}

/* args_parse()-only: cfg is that function's config_t* param */
#define REQUIRE_INPUT(opt) \
  do { \
    if (cfg->n_inputs == 0) { \
      argerr(opt " must follow the -i it names"); \
      return ARGS_ERR; \
    } \
  } while (0)

#define REQUIRE_CAS_VENDOR(opt) \
  do { \
    if (cfg->n_cas_vendors == 0) { \
      argerr(opt " must follow the --cas-ecmg it names"); \
      return ARGS_ERR; \
    } \
  } while (0)

/* [@]<addr>:<port> or [@][<addr6>]:<port>, multicast literal required */
static int mcast_group_parse(const char *s, int *family, char *addr_out, size_t addr_out_sz, unsigned *port_out) {
  if (*s == '@')
    s++;
  return uriparse_mcast_addrport(s, family, addr_out, addr_out_sz, port_out);
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
  if (strncmp(uri, "rist://", 7) == 0) {
    if (uri[7] != '@') /* rist:// as input always listens */
      return -1;
    if (strlen(uri) >= sizeof s->rist_uri)
      return -1;
    s->kind = SRC_RIST;
    bufcpy(s->rist_uri, sizeof s->rist_uri, uri);
    return 0;
  }
  if (strncmp(uri, "srt://", 6) == 0) {
    const char *rest = uri + 6;
    int listen = *rest == '@';
    if (listen)
      rest++;
    if (argutil_addrport_parse(rest, &s->srt_family, s->srt_host, sizeof s->srt_host, &s->srt_port))
      return -1;
    s->kind = SRC_SRT;
    s->srt_listen = listen;
    return 0;
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
    bufcpy(buf, n, "-");
    break;
  case SRC_RIST:
    bufcpy(buf, n, s->rist_uri);
    break;
  case SRC_SRT:
    if (s->srt_family == AF_INET6)
      snprintf(buf, n, "srt://%s[%s]:%u", s->srt_listen ? "@" : "", s->srt_host, s->srt_port);
    else
      snprintf(buf, n, "srt://%s%s:%u", s->srt_listen ? "@" : "", s->srt_host, s->srt_port);
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
  char *save = NULL;
  if (strlen(s) >= sizeof buf)
    return -1;
  bufcpy(buf, sizeof buf, s);
  cfg->cas_pid_count = 0;
  cfg->cas_pids_video = 0;
  cfg->cas_pids_audio = 0;
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
    if (cfg->cas_pid_count >= ARGS_MAX_CAS_PIDS)
      return -1;
    if (pid_parse(tok, &pid) || pid == 0)
      return -1;
    cfg->cas_pids[cfg->cas_pid_count++] = pid;
  }
  return (cfg->cas_pid_count || cfg->cas_pids_video || cfg->cas_pids_audio) ? 0 : -1;
}

/* comma-separated TVSTRIP_* tokens, or "none" (default) */
static int parse_strip(const char *s, unsigned *mask) {
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
    if (len == 0 || len >= sizeof tok)
      return -1;
    memcpy(tok, p, len);
    tok[len] = '\0';
    if (map_lookup(map, sizeof map / sizeof map[0], tok, &v))
      return -1;
    *mask |= (unsigned)v;
    p += len;
    if (*p == ',')
      p++;
  }
  return 0;
}

static void print_help(void) {
  printf(
      "usage: %s -i <uri> [per-input options] [-i <uri> ...] {-m <mcast>:<port>|-R <uri>} [options]\n\n"
      "re-package one or more transport streams (already-muxed, not raw ES) as one DVB-IPI\n"
      "multicast. A single -i: normal SPTS. Multiple -i: MPTS, one program per input.\n\n"
      "options:\n"
      "  -i, --input <uri>          udp://, rtp://, http(s)://, rist://@host:port[?query]\n"
      "                             (single peer, requires librist; @ marks it listening; no\n"
      "                             bonding, use dipirist for that), srt://[@]host:port (single\n"
      "                             peer, requires libsrt; no bonding/rendezvous, use dipisrt\n"
      "                             for that), or \"-\" for stdin; repeatable.\n"
      "                             each RIST/SRT input costs an extra thread\n"
      "  -p, --pmt-pid <pid>        for -i right before: select program by PMT PID\n"
      "                             (dec or 0x-hex; default: first live one)\n"
      "      --sid <n>              for -i right before: service_id/program_number\n"
      "                             (default: auto)\n"
      "  -s, --sdt <text|->         for -i right before: SDT service_name - default\n"
      "                             passthrough source; \"-\" drops it; text = our own\n"
      "  -I, --iface <iface>        for -i right before: incoming multicast interface\n"
      "      --strip-eit            for -i right before: drop source EIT (default: passed through)\n"
      "      --strip <list>|none    for -i right before: comma list of DATA,ECM to drop (default: none)\n"
      "      --hbbtv <url>          for -i right before: inject an AIT signalling this\n"
      "                             HbbTV app (default: none)\n"
      "      --hbbtv-org-id <n>     for -i right before: HbbTV organisation_id\n"
      "                             (required with --hbbtv)\n"
      "      --hbbtv-app-id <n>     for -i right before: HbbTV application_id\n"
      "                             (required with --hbbtv)\n"
      "      --rist-profile-in <p>  for -i right before: simple|main; -i rist:// only\n"
      "                             (default: simple)\n"
      "      --srt-passphrase-in <pw>   for -i right before: passphrase, 10..79 chars;\n"
      "                             -i srt:// only\n"
      "      --srt-pbkeylen-in <n>  for -i right before: AES key length 16|24|32, requires\n"
      "                             --srt-passphrase-in\n"
      "      --srt-streamid-in <id> for -i right before: SRTO_STREAMID; -i srt:// only\n"
      "      --srt-packetfilter-in <c>  for -i right before: SRTO_PACKETFILTER; -i srt:// only\n"
      "      --srt-latency-in <ms>  for -i right before: SRTO_LATENCY; -i srt:// only\n"
      "  -m, --mcast <g>:<p>        output multicast group:port ([addr6]:port for v6)\n"
      "  -O, --out-iface <iface>    outgoing multicast interface\n"
      "  -u, --udp                  plain UDP output (default: RTP-wrapped; -m output only)\n"
      "  -T, --ttl <n>              multicast TTL / hop limit (default: 1)\n"
      "  -R, --rist <uri>           rist://host:port[?query] or srt://host:port output,\n"
      "                             bonded with any other -R of the same scheme given\n"
      "                             (requires librist/libsrt respectively; one scheme at a\n"
      "                             time, rist:// and srt:// don't mix)\n"
      "      --profile <p>          simple|main; -R rist:// peers only (default: simple)\n"
      "      --secret <psk>         -R rist:// pre-shared key; requires --profile main\n"
      "      --cname <name>         -R rist:// cname (default: library default)\n"
      "      --buffer <ms>          -R rist:// recovery buffer (default: library default)\n"
      "      --srt-group-mode <m>   broadcast|backup; required when bonding more than one\n"
      "                             -R srt:// peer\n"
      "      --srt-passphrase <pw>  passphrase for every -R srt:// peer, 10..79 chars\n"
      "      --srt-pbkeylen <n>     AES key length for --srt-passphrase: 16|24|32\n"
      "      --srt-streamid <id>    SRTO_STREAMID for every -R srt:// peer\n"
      "      --srt-packetfilter <c> SRTO_PACKETFILTER for every -R srt:// peer\n"
      "      --srt-latency <ms>     SRTO_LATENCY for every -R srt:// peer\n"
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
      "      --cas-emmg-max-conns <n> for the --cas-ecmg right before this: max concurrent EMMG\n"
      "                             client connections (default: 8, max: 64)\n"
      "      --cas-emmg-version <n> for the --cas-ecmg right before this: EMMG protocol version\n"
      "                             2|3 (default: accept client's proposal)\n"
      "      --cas-emm-pid <pid>    for the --cas-ecmg right before this: output PID for its EMM\n"
      "                             stream (default: 0x0021)\n"
      "      --cas-resilience <r>   for the --cas-ecmg right before this: on its own ECMG loss,\n"
      "                             frozen|cycling|silent (default: frozen)\n"
      "      --cas-required         for the --cas-ecmg right before this: its outage forces the\n"
      "                             global fallback regardless of other vendors\n"
      "      --cas-cwenc-algo <a>   for the --cas-ecmg right before this: encrypt CW_provision's\n"
      "                             CWs per Annex D, des56|aes128|aes256 (default: off)\n"
      "      --cas-cwenc-aes-mode <m> for the --cas-ecmg right before this: stream|ecb, aes* only\n"
      "                             (default: stream)\n"
      "      --cas-cwenc-fixed-key <hex> for the --cas-ecmg right before this: 14/32/64 hex chars\n"
      "                             for des56/aes128/aes256 (default: des56's Annex D ROM key)\n"
      "      --cas-cwenc-key-list-a <path> for the --cas-ecmg right before this: 2048-byte Annex D\n"
      "                             key list file\n"
      "      --cas-cwenc-key-list-b <path> for the --cas-ecmg right before this: same, second list\n"
      "      --cas-pids <list>      PIDs to scramble: comma-separated pids and/or video/audio keywords\n"
      "                             (default: video,audio - all video and audio streams)\n"
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
      "  -d, --daemonize            fork to background after startup, detach from terminal\n"
      "  -h, --help                 this help\n\n"
      "examples:\n"
      "  %s -i rtp://@239.19.75.1:8700 -m 239.1.1.1:5000 -s \"My Channel\"\n"
      "  %s -i https://host/live/x/y.ts -m 239.1.1.2:5000 -b 8000 -S -B\n"
      "  %s -i rtp://@239.19.75.1:8700 --sdt \"Channel A\" -i rtp://@239.19.75.2:8700 --sdt \"Channel B\" \\\n"
      "     -m 239.1.1.3:5000 -e 5\n",
      TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME);
}

static int is_sid_used(const unsigned *used, unsigned n_used, unsigned sid) {
  for (unsigned j = 0; j < n_used; j++)
    if (used[j] == sid)
      return 1;
  return 0;
}

/* assigns the smallest unused positive sid to inputs that didn't get an explicit --sid.
   -1: duplicate --sid given explicitly, 0 ok */
static int assign_missing_sids(config_t *cfg) {
  unsigned used[ARGS_MAX_INPUTS];
  unsigned n_used = 0;
  unsigned next = 1;

  for (unsigned i = 0; i < cfg->n_inputs; i++) {
    if (cfg->inputs[i].sid == 0)
      continue;
    if (is_sid_used(used, n_used, cfg->inputs[i].sid)) {
      argerr("duplicate --sid %u", cfg->inputs[i].sid);
      return -1;
    }
    used[n_used++] = cfg->inputs[i].sid;
  }
  for (unsigned i = 0; i < cfg->n_inputs; i++) {
    if (cfg->inputs[i].sid != 0)
      continue;
    while (is_sid_used(used, n_used, next))
      next++;
    cfg->inputs[i].sid = next;
    used[n_used++] = next;
    next++;
  }
  return 0;
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
      {"cas-emmg-max-conns", required_argument, 0, 1035},
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
      {"rist", required_argument, 0, 'R'},
      {"profile", required_argument, 0, 1030},
      {"secret", required_argument, 0, 1031},
      {"cname", required_argument, 0, 1032},
      {"buffer", required_argument, 0, 1033},
      {"daemonize", no_argument, 0, 'd'},
      {"strip", required_argument, 0, 1034},
      {"rist-profile-in", required_argument, 0, 1036},
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
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int have_mcast = 0;
  int have_cas_pids = 0, any_cas_flag = 0;
  const char *profile_arg = NULL;
  int have_secret = 0;
  const char *srt_group_mode_arg = NULL;
  int c;

  memset(cfg, 0, sizeof *cfg);
  cfg->tsid = 1;
  cfg->onid = 1;
  cfg->rtp = 1;
  cfg->cas_cp_duration_ms = 10000;
  optind = 1;
  /* leading '+': disable GNU getopt argument permutation, so per-input options stay paired
     with whichever -i preceded them instead of being reordered */
  while ((c = getopt_long(argc, argv, "+i:p:m:I:O:uT:n:s:b:SBe:kvdhR:", longopts, NULL)) != -1) {
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
        REQUIRE_INPUT("-p/--pmt-pid");
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
        REQUIRE_INPUT("-I/--iface");
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
          bufcpy(cfg->nit_text, sizeof cfg->nit_text, optarg);
        }
        break;
      case 's':
        REQUIRE_INPUT("-s/--sdt");
        if (strcmp(optarg, "-") == 0) {
          cfg->inputs[cfg->n_inputs - 1].sdt_mode = TABLE_DROP;
        } else {
          cfg->inputs[cfg->n_inputs - 1].sdt_mode = TABLE_OVERRIDE;
          bufcpy(cfg->inputs[cfg->n_inputs - 1].sdt_text, sizeof cfg->inputs[0].sdt_text, optarg);
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
        REQUIRE_INPUT("--strip-eit");
        cfg->inputs[cfg->n_inputs - 1].strip_eit = 1;
        break;
      case 1034:
        REQUIRE_INPUT("--strip");
        if (parse_strip(optarg, &cfg->inputs[cfg->n_inputs - 1].strip_mask)) {
          argerr("invalid --strip: %s (comma list of DATA,ECM, or \"none\")", optarg);
          return ARGS_ERR;
        }
        break;
      case 1001:
        REQUIRE_INPUT("--hbbtv");
        cfg->inputs[cfg->n_inputs - 1].hbbtv_url = optarg;
        break;
      case 1002:
        REQUIRE_INPUT("--hbbtv-org-id");
        if (org_id_parse(optarg, &cfg->inputs[cfg->n_inputs - 1].hbbtv_org_id)) {
          argerr("invalid --hbbtv-org-id: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 1003:
        REQUIRE_INPUT("--hbbtv-app-id");
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
        REQUIRE_INPUT("--sid");
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
        REQUIRE_CAS_VENDOR("--cas-ecmg-version");
        if (cas_version_parse(optarg, &cfg->cas_vendors[cfg->n_cas_vendors - 1].ecmg_version)) {
          argerr("invalid --cas-ecmg-version: %s (2|3)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1011:
        any_cas_flag = 1;
        REQUIRE_CAS_VENDOR("--cas-super-id");
        if (cas_super_id_parse(optarg, &cfg->cas_vendors[cfg->n_cas_vendors - 1].super_cas_id)) {
          argerr("invalid --cas-super-id: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 1012:
        any_cas_flag = 1;
        REQUIRE_CAS_VENDOR("--cas-ecm-id");
        if (id_parse(optarg, &cfg->cas_vendors[cfg->n_cas_vendors - 1].ecm_id)) {
          argerr("invalid --cas-ecm-id: %s (1..65535)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1013:
        any_cas_flag = 1;
        REQUIRE_CAS_VENDOR("--cas-ecm-pid");
        if (pid_parse(optarg, &cfg->cas_vendors[cfg->n_cas_vendors - 1].ecm_pid) || cfg->cas_vendors[cfg->n_cas_vendors - 1].ecm_pid == 0) {
          argerr("invalid --cas-ecm-pid: %s (0x0001..0x1FFE)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1014:
        any_cas_flag = 1;
        REQUIRE_CAS_VENDOR("--cas-emmg-port");
        if (argutil_port_parse(optarg, &cfg->cas_vendors[cfg->n_cas_vendors - 1].emmg_port)) {
          argerr("invalid --cas-emmg-port: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 1015:
        any_cas_flag = 1;
        REQUIRE_CAS_VENDOR("--cas-emmg-version");
        if (cas_version_parse(optarg, &cfg->cas_vendors[cfg->n_cas_vendors - 1].emmg_version)) {
          argerr("invalid --cas-emmg-version: %s (2|3)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1035: {
        char *end;
        unsigned long v;
        any_cas_flag = 1;
        REQUIRE_CAS_VENDOR("--cas-emmg-max-conns");
        v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0 || v > EMMG_MAX_CONNS_CEILING) {
          argerr("invalid --cas-emmg-max-conns: %s (1..%u)", optarg, EMMG_MAX_CONNS_CEILING);
          return ARGS_ERR;
        }
        cfg->cas_vendors[cfg->n_cas_vendors - 1].emmg_max_conns = (unsigned)v;
        break;
      }
      case 1016:
        any_cas_flag = 1;
        REQUIRE_CAS_VENDOR("--cas-emm-pid");
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
        REQUIRE_CAS_VENDOR("--cas-resilience");
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
        REQUIRE_CAS_VENDOR("--cas-required");
        cfg->cas_vendors[cfg->n_cas_vendors - 1].required = 1;
        break;
      case 1048:
        any_cas_flag = 1;
        REQUIRE_CAS_VENDOR("--cas-cwenc-algo");
        if (strcmp(optarg, "des56") && strcmp(optarg, "aes128") && strcmp(optarg, "aes256")) {
          argerr("invalid --cas-cwenc-algo: %s (des56|aes128|aes256)", optarg);
          return ARGS_ERR;
        }
        bufcpy(cfg->cas_vendors[cfg->n_cas_vendors - 1].cwenc_algorithm, sizeof cfg->cas_vendors[0].cwenc_algorithm, optarg);
        break;
      case 1049:
        any_cas_flag = 1;
        REQUIRE_CAS_VENDOR("--cas-cwenc-aes-mode");
        if (strcmp(optarg, "stream") && strcmp(optarg, "ecb")) {
          argerr("invalid --cas-cwenc-aes-mode: %s (stream|ecb)", optarg);
          return ARGS_ERR;
        }
        bufcpy(cfg->cas_vendors[cfg->n_cas_vendors - 1].cwenc_aes_mode, sizeof cfg->cas_vendors[0].cwenc_aes_mode, optarg);
        break;
      case 1050:
        any_cas_flag = 1;
        REQUIRE_CAS_VENDOR("--cas-cwenc-fixed-key");
        if (bufcpy(cfg->cas_vendors[cfg->n_cas_vendors - 1].cwenc_fixed_key_hex, sizeof cfg->cas_vendors[0].cwenc_fixed_key_hex, optarg) >=
            sizeof cfg->cas_vendors[0].cwenc_fixed_key_hex) {
          argerr("--cas-cwenc-fixed-key too long");
          return ARGS_ERR;
        }
        break;
      case 1051:
        any_cas_flag = 1;
        REQUIRE_CAS_VENDOR("--cas-cwenc-key-list-a");
        if (bufcpy(cfg->cas_vendors[cfg->n_cas_vendors - 1].cwenc_key_list_a_path, sizeof cfg->cas_vendors[0].cwenc_key_list_a_path, optarg) >=
            sizeof cfg->cas_vendors[0].cwenc_key_list_a_path) {
          argerr("--cas-cwenc-key-list-a too long");
          return ARGS_ERR;
        }
        break;
      case 1052:
        any_cas_flag = 1;
        REQUIRE_CAS_VENDOR("--cas-cwenc-key-list-b");
        if (bufcpy(cfg->cas_vendors[cfg->n_cas_vendors - 1].cwenc_key_list_b_path, sizeof cfg->cas_vendors[0].cwenc_key_list_b_path, optarg) >=
            sizeof cfg->cas_vendors[0].cwenc_key_list_b_path) {
          argerr("--cas-cwenc-key-list-b too long");
          return ARGS_ERR;
        }
        break;
      case 1024:
        any_cas_flag = 1;
        cfg->cas_fallback_clear = 1;
        break;
      case 1025:
        if (biss_parse_hex16(optarg, cfg->biss2_sw)) {
          argerr("invalid --biss2-sw: %s (32 hex chars)", optarg);
          return ARGS_ERR;
        }
        cfg->biss2_enabled = 1;
        break;
      case 1026:
        if (biss_parse_hex16(optarg, cfg->biss2_esw_id)) {
          argerr("invalid --biss2-emit-esw: %s (32 hex chars)", optarg);
          return ARGS_ERR;
        }
        cfg->biss2_emit_esw = 1;
        break;
      case 1027:
        if (biss1_parse_sw(optarg, cfg->biss1_cw)) {
          argerr("invalid --biss1-sw: %s (12 hex chars)", optarg);
          return ARGS_ERR;
        }
        cfg->biss1_enabled = 1;
        break;
      case 1028:
        cfg->biss2_ca_receivers_dir = optarg;
        cfg->biss2_ca_enabled = 1;
        break;
      case 1029: {
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
      case 'd':
        cfg->daemonize = 1;
        break;
      case 'R':
        if (strncmp(optarg, "rist://", 7) == 0) {
          if (cfg->n_srt > 0) {
            argerr("-R: rist:// and srt:// peers cannot mix in one run");
            return ARGS_ERR;
          }
          if (cfg->n_rist >= ARGS_MAX_RIST_PEERS) {
            argerr("too many -R peers (max %d)", ARGS_MAX_RIST_PEERS);
            return ARGS_ERR;
          }
          if (bufcpy(cfg->rist_uri[cfg->n_rist], sizeof cfg->rist_uri[0], optarg) >= sizeof cfg->rist_uri[0]) {
            argerr("-R rist uri too long: %s", optarg);
            return ARGS_ERR;
          }
          cfg->n_rist++;
        } else if (strncmp(optarg, "srt://", 6) == 0) {
          if (cfg->n_rist > 0) {
            argerr("-R: rist:// and srt:// peers cannot mix in one run");
            return ARGS_ERR;
          }
          if (cfg->n_srt >= ARGS_MAX_SRT_PEERS) {
            argerr("too many -R srt:// peers (max %d)", ARGS_MAX_SRT_PEERS);
            return ARGS_ERR;
          }
          if (optarg[6] == '@') {
            argerr("-R srt:// output always calls out, no listener mode");
            return ARGS_ERR;
          }
          if (argutil_addrport_parse(optarg + 6, &cfg->srt_family[cfg->n_srt], cfg->srt_host[cfg->n_srt],
                                      sizeof cfg->srt_host[0], &cfg->srt_port[cfg->n_srt])) {
            argerr("invalid -R srt uri: %s", optarg);
            return ARGS_ERR;
          }
          cfg->n_srt++;
        } else {
          argerr("invalid -R uri: %s (must start with rist:// or srt://)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1030:
        profile_arg = optarg;
        break;
      case 1031:
        if (bufcpy(cfg->rist_secret, sizeof cfg->rist_secret, optarg) >= sizeof cfg->rist_secret) {
          argerr("--secret too long");
          return ARGS_ERR;
        }
        have_secret = 1;
        break;
      case 1032:
        if (bufcpy(cfg->rist_cname, sizeof cfg->rist_cname, optarg) >= sizeof cfg->rist_cname) {
          argerr("--cname too long");
          return ARGS_ERR;
        }
        break;
      case 1033: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0) {
          argerr("invalid --buffer: %s (ms)", optarg);
          return ARGS_ERR;
        }
        cfg->rist_buffer_ms = (unsigned)v;
        break;
      }
      case 1036: {
        static const enum_map_t map[] = {{"simple", 0}, {"main", 1}};
        int v;
        REQUIRE_INPUT("--rist-profile-in");
        if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
          argerr("invalid --rist-profile-in: %s (simple|main)", optarg);
          return ARGS_ERR;
        }
        cfg->inputs[cfg->n_inputs - 1].rist_profile_main = v;
        break;
      }
      case 1037:
        REQUIRE_INPUT("--srt-passphrase-in");
        if (bufcpy(cfg->inputs[cfg->n_inputs - 1].srt_passphrase_in, sizeof cfg->inputs[0].srt_passphrase_in, optarg) >=
            sizeof cfg->inputs[0].srt_passphrase_in) {
          argerr("--srt-passphrase-in too long");
          return ARGS_ERR;
        }
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
        cfg->inputs[cfg->n_inputs - 1].srt_pbkeylen_in = (int)v;
        break;
      }
      case 1039:
        REQUIRE_INPUT("--srt-streamid-in");
        if (bufcpy(cfg->inputs[cfg->n_inputs - 1].srt_streamid_in, sizeof cfg->inputs[0].srt_streamid_in, optarg) >=
            sizeof cfg->inputs[0].srt_streamid_in) {
          argerr("--srt-streamid-in too long");
          return ARGS_ERR;
        }
        break;
      case 1040:
        REQUIRE_INPUT("--srt-packetfilter-in");
        if (bufcpy(cfg->inputs[cfg->n_inputs - 1].srt_packetfilter_in, sizeof cfg->inputs[0].srt_packetfilter_in, optarg) >=
            sizeof cfg->inputs[0].srt_packetfilter_in) {
          argerr("--srt-packetfilter-in too long");
          return ARGS_ERR;
        }
        break;
      case 1041: {
        char *end;
        unsigned long v;
        REQUIRE_INPUT("--srt-latency-in");
        v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0 || v > 60000) {
          argerr("invalid --srt-latency-in: %s (1..60000 ms)", optarg);
          return ARGS_ERR;
        }
        cfg->inputs[cfg->n_inputs - 1].srt_latency_in_ms = (unsigned)v;
        break;
      }
      case 1042:
        srt_group_mode_arg = optarg;
        break;
      case 1043:
        if (bufcpy(cfg->srt_passphrase, sizeof cfg->srt_passphrase, optarg) >= sizeof cfg->srt_passphrase) {
          argerr("--srt-passphrase too long");
          return ARGS_ERR;
        }
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
        if (bufcpy(cfg->srt_streamid, sizeof cfg->srt_streamid, optarg) >= sizeof cfg->srt_streamid) {
          argerr("--srt-streamid too long");
          return ARGS_ERR;
        }
        break;
      case 1046:
        if (bufcpy(cfg->srt_packetfilter, sizeof cfg->srt_packetfilter, optarg) >= sizeof cfg->srt_packetfilter) {
          argerr("--srt-packetfilter too long");
          return ARGS_ERR;
        }
        break;
      case 1047: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0 || v > 60000) {
          argerr("invalid --srt-latency: %s (1..60000 ms)", optarg);
          return ARGS_ERR;
        }
        cfg->srt_latency_ms = (unsigned)v;
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
  if (cfg->n_inputs == 0) {
    argerr("missing -i input");
    return ARGS_ERR;
  }
  {
    unsigned n_rist_in = 0;
    for (unsigned i = 0; i < cfg->n_inputs; i++)
      if (cfg->inputs[i].input.kind == SRC_RIST)
        n_rist_in++;
    if (n_rist_in > 1) {
      argerr("at most one -i rist:// input: librist isn't safe with more than one context per process");
      return ARGS_ERR;
    }
    if (n_rist_in && cfg->n_rist) {
      argerr("-i rist:// and -R rist:// cannot combine: librist isn't safe with more than one context per process");
      return ARGS_ERR;
    }
  }
  if (!have_mcast && cfg->n_rist == 0 && cfg->n_srt == 0) {
    argerr("need -m output multicast or at least one -R peer");
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
  if (profile_arg) {
    static const enum_map_t map[] = {{"simple", RIST_PROF_SIMPLE}, {"main", RIST_PROF_MAIN}};
    int v;
    if (map_lookup(map, sizeof map / sizeof map[0], profile_arg, &v)) {
      argerr("invalid --profile: %s (simple|main)", profile_arg);
      return ARGS_ERR;
    }
    cfg->rist_profile = (rist_profile_sel_t)v;
  }
  if (cfg->n_rist == 0 && (profile_arg || have_secret || cfg->rist_cname[0] || cfg->rist_buffer_ms))
    log_line(TOOL_NAME ": --profile/--secret/--cname/--buffer have no effect without -R");
  if (cfg->n_rist > 0 && have_secret && cfg->rist_profile != RIST_PROF_MAIN) {
    argerr("--secret requires --profile main");
    return ARGS_ERR;
  }
  if (srt_group_mode_arg) {
    static const enum_map_t map[] = {{"broadcast", SRT_BOND_BROADCAST}, {"backup", SRT_BOND_BACKUP}};
    int v;
    if (map_lookup(map, sizeof map / sizeof map[0], srt_group_mode_arg, &v)) {
      argerr("invalid --srt-group-mode: %s (broadcast|backup)", srt_group_mode_arg);
      return ARGS_ERR;
    }
    cfg->srt_group_mode = (srt_bond_mode_t)v;
  }
  if (cfg->n_srt > 1 && cfg->srt_group_mode == SRT_BOND_NONE) {
    argerr("bonding several -R srt:// peers requires --srt-group-mode");
    return ARGS_ERR;
  }
  if (cfg->n_srt == 1 && cfg->srt_group_mode != SRT_BOND_NONE) {
    argerr("--srt-group-mode has no effect with a single -R srt:// peer");
    return ARGS_ERR;
  }
  if (cfg->n_srt == 0 && (srt_group_mode_arg || cfg->srt_passphrase[0] || cfg->srt_pbkeylen ||
                          cfg->srt_streamid[0] || cfg->srt_packetfilter[0] || cfg->srt_latency_ms))
    log_line(TOOL_NAME ": --srt-* has no effect without an -R srt:// peer");
  if (cfg->srt_passphrase[0] && (strlen(cfg->srt_passphrase) < 10 || strlen(cfg->srt_passphrase) > 79)) {
    argerr("--srt-passphrase must be 10..79 characters");
    return ARGS_ERR;
  }
  if (cfg->srt_pbkeylen && !cfg->srt_passphrase[0]) {
    argerr("--srt-pbkeylen requires --srt-passphrase");
    return ARGS_ERR;
  }
  for (unsigned i = 0; i < cfg->n_inputs; i++) {
    dipitvhead_input_t *in = &cfg->inputs[i];
    if (in->srt_passphrase_in[0] && (strlen(in->srt_passphrase_in) < 10 || strlen(in->srt_passphrase_in) > 79)) {
      argerr("--srt-passphrase-in must be 10..79 characters");
      return ARGS_ERR;
    }
    if (in->srt_pbkeylen_in && !in->srt_passphrase_in[0]) {
      argerr("--srt-pbkeylen-in requires --srt-passphrase-in");
      return ARGS_ERR;
    }
    if (in->input.kind != SRC_SRT && (in->srt_passphrase_in[0] || in->srt_pbkeylen_in || in->srt_streamid_in[0] ||
                                       in->srt_packetfilter_in[0] || in->srt_latency_in_ms))
      log_line(TOOL_NAME ": --srt-*-in has no effect, that -i isn't srt://");
    if (in->hbbtv_url && (!in->hbbtv_org_id || !in->hbbtv_app_id)) {
      argerr("--hbbtv requires --hbbtv-org-id and --hbbtv-app-id");
      return ARGS_ERR;
    }
    if ((in->hbbtv_org_id || in->hbbtv_app_id) && !in->hbbtv_url) {
      argerr("--hbbtv-org-id/--hbbtv-app-id need --hbbtv");
      return ARGS_ERR;
    }
  }
  if (any_cas_flag && cfg->cas_algo == CAS_ALGO_NONE && !cfg->biss2_enabled && !cfg->biss1_enabled && !cfg->biss2_ca_enabled) {
    argerr("--cas-* options require --cas-algo (or --cas-pids alone under --biss2-sw/--biss1-sw/--biss2-ca-receivers)");
    return ARGS_ERR;
  }
  if (cas_args_validate(TOOL_NAME, cfg->cas_algo, cfg->cas_vendors, cfg->n_cas_vendors, cfg->biss2_enabled, cfg->biss1_enabled,
                         cfg->biss2_ca_enabled, cfg->biss2_emit_esw, cfg->biss2_ca_session_id_given, cfg->cas_cp_duration_ms) != 0)
    return ARGS_ERR;
  if (cfg->cas_algo != CAS_ALGO_NONE && !have_cas_pids) {
    /* default: scramble all video and audio elementary streams */
    cfg->cas_pids_video = 1;
    cfg->cas_pids_audio = 1;
  }
  if ((cfg->biss2_enabled || cfg->biss1_enabled || cfg->biss2_ca_enabled) && !have_cas_pids) {
    /* same default as --cas-algo's own pid selection above */
    cfg->cas_pids_video = 1;
    cfg->cas_pids_audio = 1;
  }

  if (cfg->cas_algo != CAS_ALGO_NONE || cfg->biss1_enabled || cfg->biss2_enabled || cfg->biss2_ca_enabled) {
    for (unsigned i = 0; i < cfg->n_inputs; i++)
      if (!(cfg->inputs[i].strip_mask & TVSTRIP_ECM)) {
        log_line(TOOL_NAME ": source CA/ECM passthrough disabled: --cas-algo/--biss* already scrambling this mux");
        break;
      }
  }

  if (assign_missing_sids(cfg) != 0)
    return ARGS_ERR;
  return ARGS_OK;
}
