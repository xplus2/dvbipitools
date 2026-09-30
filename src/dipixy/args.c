/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "lib/helper/argutil.h"
#include "lib/mux/fec2022.h"
#include "lib/helper/base64.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"

#include "args.h"
#include "config.h"
#include "core/route.h"
#include "version.h"

#define OPT_TLS_CERT 1001
#define OPT_TLS_KEY 1002
#define OPT_MAX_CLIENTS 1057
#define OPT_MAX_CHANNELS 1051
#define OPT_IDLE_TIMEOUT 1052
#define OPT_CAPTURE_RING_SIZE 1048
#define OPT_SDS_TIMEOUT 1044
#define OPT_SDS_REFRESH_INTERVAL 1045
#define OPT_SEGMENT_SIZE 1008
#define OPT_SEGMENT_COUNT 1009
#define OPT_HLS_PART_SIZE 1010
#define OPT_DASH_PART_SIZE 1049
#define OPT_DASH_UTC_URL 1050
#define OPT_HLS_SEG_POOL 1039
#define OPT_METRICS 1012
#define OPT_METRICS_ID 1013
#define OPT_METRICS_INTERVAL 1014
#define OPT_METRICS_INSPECT_TS 1068
#define OPT_TS_STARTUP_TIMEOUT 1069
#define OPT_METRICS_HTTP 1015
#define OPT_METRICS_AUTH 1056
#define OPT_NO_URL_RTP 1020
#define OPT_NO_URL_UDP 1021
#define OPT_NO_URL_SRT 1026
#define OPT_NO_PID_FILTERS 1022
#define OPT_NO_LCEVC 1055
#define OPT_NO_HTTP2 1040
#define OPT_NO_HTTP3 1041
#define OPT_H3_ALTSVC_PORT 1060
#define OPT_H3_MAX_STREAMS 1061
#define OPT_H3_MAX_CONNS 1062
#define OPT_H3_IDLE_TIMEOUT 1063
#define OPT_H3_RETRY 1064
#define OPT_H3_MAX_UDP_PAYLOAD 1065
#define OPT_H3_WINDOW 1066
#define OPT_H3_CC 1067
#define OPT_NO_FCC 1042
#define OPT_NO_RET 1043
#define OPT_AL_FEC 1053
#define OPT_NO_AL_FEC 1054
#define OPT_NO_STATUS 1023
#define OPT_STATUS_TPL 1027
#define OPT_AUTH 1037
#define OPT_CORS_ORIGIN 1036
#define OPT_SSDP_TTL 1029
#define OPT_SSDP_IFACE 1030
#define OPT_SSDP_INTERVAL 1046
#define OPT_SSDP_MAX_AGE 1047
#define OPT_ENABLE_DLNA 1031
#define OPT_DLNA_HOST 1032
#define OPT_DLNA_NAME 1033
#define OPT_DLNA_KEEP_MULTICAST 1038
#define OPT_MEDIA_TYPE 1034
#define OPT_COLOR 1007
#define OPT_CONFIG_STRICT 1059
#define OPT_CONFIGTEST 1058

#define ARGS_AUTH_CREDS_MAX 128 /* max "user:password" length for --auth */

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

/* "all:<port>" (wildcard, both families) or "<addr>:<port>" / "[<addr6>]:<port>" */
int dixy_cfg_listen(listen_spec_t *out, const char *s) {
  if (strncmp(s, "all:", 4) == 0) {
    unsigned port;
    if (argutil_port_parse(s + 4, &port)) return -1;
    out->scope = LISTEN_ANY;
    out->addr[0] = '\0';
    out->port = port;
    return 0;
  }
  {
    int family;
    unsigned port;
    if (argutil_addrport_parse(s, &family, out->addr, sizeof out->addr, &port)) return -1;
    out->scope = family == AF_INET6 ? LISTEN_V6 : LISTEN_V4;
    out->port = port;
    return 0;
  }
}

/* -1/-2/-3 (that many x core count) or a positive absolute thread count */
int dixy_cfg_workers(int *out, const char *s) {
  char *end;
  long v = strtol(s, &end, 10);
  if (*end != '\0') return -1;
  if (v == -1 || v == -2 || v == -3) {
    *out = (int)v;
    return 0;
  }
  if (v >= 1 && v <= 1024) {
    *out = (int)v;
    return 0;
  }
  return -1;
}

/* -f/--format <list>: comma-separated list.
   listed formats 0, everything else 1. 0 ok, -1 empty or unknown token */
int dixy_cfg_format(config_t *cfg, const char *s) {
  struct {
    const char *name;
    int *flag;
  } items[] = {
      {"ts", &cfg->no_ts},   {"spts", &cfg->no_spts}, {"rawaudio", &cfg->no_rawaudio}, {"mp4", &cfg->no_mp4},
      {"hls", &cfg->no_hls}, {"llhls", &cfg->no_llhls}, {"dash", &cfg->no_dash}, {"lldash", &cfg->no_lldash},
  };
  size_t n_items = sizeof items / sizeof items[0];
  size_t i;
  const char *p = s;
  if (!*s) return -1;
  for (i = 0; i < n_items; i++) *items[i].flag = 1;
  while (*p) {
    const char *comma = strchr(p, ',');
    size_t len = comma ? (size_t)(comma - p) : strlen(p);
    int matched = 0;
    if (!len) return -1;
    for (i = 0; i < n_items; i++) if (strlen(items[i].name) == len && !strncmp(items[i].name, p, len)) {
      *items[i].flag = 0;
      matched = 1;
      break;
    }
    if (!matched) return -1;
    p = comma ? comma + 1 : p + len;
  }
  return 0;
}

/* case-insensitive suffix match against known playlist extensions */
static int playlist_kind_from_ext(const char *path, source_kind_t *out) {
  static const struct { const char *ext; source_kind_t kind; } map[] = {
    {".m3u", SRC_M3U}, {".m3u8", SRC_M3U}, {".xspf", SRC_XSPF}, {".csv", SRC_CSV}, {".xml", SRC_XML},
  };
  const char *dot = strrchr(path, '.');
  if (!dot) return -1;
  for (size_t i = 0; i < sizeof map / sizeof map[0]; i++) {
    if (!strcasecmp(dot, map[i].ext)) {
      *out = map[i].kind;
      return 0;
    }
  }
  return -1;
}

static int sources_append(config_t *cfg, source_kind_t kind, const char *value, int ordinal) {
  source_def_t *p = array_grow(cfg->sources, &cfg->sources_cap, cfg->n_sources + 1, sizeof *cfg->sources);
  if (!p) return -1;
  cfg->sources = p;
  memset(&cfg->sources[cfg->n_sources], 0, sizeof *cfg->sources);
  cfg->sources[cfg->n_sources].kind = kind;
  cfg->sources[cfg->n_sources].value = value;
  cfg->sources[cfg->n_sources].ordinal = ordinal;
  cfg->n_sources++;
  return 0;
}

static int name_in_use(const config_t *cfg, const char *name) {
  if (cfg->stdin_name && !strcmp(cfg->stdin_name, name)) return 1;
  if (cfg->rist_name && !strcmp(cfg->rist_name, name))   return 1;
  for (int i = 0; i < cfg->n_sources; i++) if (cfg->sources[i].name && !strcmp(cfg->sources[i].name, name)) return 1;
  return 0;
}

void args_free(config_t *cfg) {
  free(cfg->sources);
  cfg->sources = NULL;
  cfg->n_sources = 0;
  cfg->sources_cap = 0;
}

void dixy_cfg_reset_inputs(config_t *cfg) {
  args_free(cfg);
  cfg->stdin_path = NULL;
  cfg->stdin_name = NULL;
  cfg->stdin_ordinal = 0;
  cfg->stdin_media_type = MEDIA_TV;
  cfg->rist_uri = NULL;
  cfg->rist_name = NULL;
  cfg->rist_ordinal = 0;
  cfg->rist_media_type = MEDIA_TV;
  cfg->input_ordinal = 0;
  cfg->last_input = LAST_NONE;
  cfg->media_type_seen = 0;
}

int dixy_cfg_add_input(config_t *cfg, const char *val, char *err, size_t errsz) {
  source_kind_t kind;
  int ordinal = cfg->input_ordinal + 1;
  if (strcmp(val, "-") == 0) {
    cfg->stdin_path = val;
    cfg->stdin_ordinal = ordinal;
    cfg->last_input = LAST_STDIN;
  } else if (strncmp(val, "rist://", 7) == 0) {
    if (cfg->rist_uri) {
      bufcpy(err, errsz, "at most one rist:// input");
      return -1;
    }
    if (val[7] != '@') {
      snprintf(err, errsz, "invalid '%s' (rist:// needs rist://@host:port)", val);
      return -1;
    }
    cfg->rist_uri = val;
    cfg->rist_ordinal = ordinal;
    cfg->last_input = LAST_RIST;
  } else {
    const char *value = val;
    if (strncmp(val, "sds://", 6) == 0) {
      int family;
      char addr[64];
      unsigned port;
      value = val + 6;
      if (argutil_addrport_parse(value, &family, addr, sizeof addr, &port)) {
        snprintf(err, errsz, "invalid '%s' (sds:// needs sds://addr:port)", val);
        return -1;
      }
      kind = SRC_SDS;
    } else if (strncmp(val, "http://", 7) == 0 || strncmp(val, "https://", 8) == 0) {
      kind = SRC_HTTP;
    } else if (strncmp(val, "rtp://", 6) == 0 || strncmp(val, "udp://", 6) == 0) {
      const char *scheme = val[0] == 'r' ? "rtp" : "udp";
      int family;
      int rtp;
      char addr[64];
      unsigned port;
      if (route_resolve_channel_uri(val, &family, addr, sizeof addr, &port, &rtp)) {
        snprintf(err, errsz, "invalid '%s' (%s:// needs %s://addr:port, multicast)", val, scheme, scheme);
        return -1;
      }
      kind = SRC_MCAST;
    } else if (playlist_kind_from_ext(val, &kind)) {
      snprintf(err, errsz, "unidentified '%s' (expected -, sds://, rist://, rtp://, udp://, http(s)://, or a .m3u/.xspf/.csv/.xml path)", val);
      return -1;
    }
    if (sources_append(cfg, kind, value, ordinal)) {
      bufcpy(err, errsz, "out of memory");
      return -1;
    }
    cfg->last_input = LAST_SOURCE;
  }
  cfg->input_ordinal = ordinal;
  cfg->media_type_seen = 0;
  return 0;
}

int dixy_cfg_set_name(config_t *cfg, const char *name, char *err, size_t errsz) {
  const char **slot;
  if (!route_name_valid(name)) {
    snprintf(err, errsz, "invalid name '%s' (no '/', not starting with '.', not a reserved word, max %d chars)", name, ROUTE_NAME_MAX);
    return -1;
  }
  if (name_in_use(cfg, name)) {
    snprintf(err, errsz, "duplicate name '%s'", name);
    return -1;
  }
  switch (cfg->last_input) {
    case LAST_STDIN:
      slot = &cfg->stdin_name;
      break;
    case LAST_RIST:
      slot = &cfg->rist_name;
      break;
    case LAST_SOURCE:
      slot = &cfg->sources[cfg->n_sources - 1].name;
      break;
    default:
      bufcpy(err, errsz, "name must directly follow the input it names");
      return -1;
  }
  if (*slot) {
    bufcpy(err, errsz, "name given twice for the same input");
    return -1;
  }
  *slot = name;
  return 0;
}

int dixy_cfg_set_h3_retry(config_t *cfg, const char *val, char *err, size_t errsz) {
  static const enum_map_t map[] = {{"auto", H3_RETRY_CFG_AUTO}, {"off", H3_RETRY_CFG_OFF}, {"always", H3_RETRY_CFG_ALWAYS}};
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], val, &v)) {
    snprintf(err, errsz, "invalid '%s' (off, auto or always)", val);
    return -1;
  }
  cfg->h3_retry = v;
  return 0;
}

int dixy_cfg_set_h3_cc(config_t *cfg, const char *val, char *err, size_t errsz) {
  static const enum_map_t map[] = {{"cubic", H3_CC_CFG_CUBIC}, {"bbr", H3_CC_CFG_BBR}, {"reno", H3_CC_CFG_RENO}};
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], val, &v)) {
    snprintf(err, errsz, "invalid '%s' (cubic, bbr or reno)", val);
    return -1;
  }
  cfg->h3_cc = v;
  return 0;
}

int dixy_cfg_set_media_type(config_t *cfg, const char *val, char *err, size_t errsz) {
  static const enum_map_t map[] = {{"tv", MEDIA_TV}, {"radio", MEDIA_RADIO}};
  media_type_t mt;
  int v;
  if (map_lookup(map, sizeof map / sizeof map[0], val, &v)) {
    snprintf(err, errsz, "invalid '%s' (radio or tv)", val);
    return -1;
  }
  mt = (media_type_t)v;
  if (cfg->media_type_seen) {
    bufcpy(err, errsz, "media type given twice for the same input");
    return -1;
  }
  switch (cfg->last_input) {
    case LAST_STDIN:
      cfg->stdin_media_type = mt;
      break;
    case LAST_RIST:
      cfg->rist_media_type = mt;
      break;
    case LAST_SOURCE:
      cfg->sources[cfg->n_sources - 1].media_type = mt;
      break;
    default:
      bufcpy(err, errsz, "media type must directly follow the input it applies to");
      return -1;
  }
  cfg->media_type_seen = 1;
  return 0;
}

int dixy_cfg_auth(const char *val, char *out, size_t outsz, char *err, size_t errsz) {
  const char *colon = strchr(val, ':');
  char b64[192];
  size_t n;
  if (!colon || colon == val) {
    snprintf(err, errsz, "invalid '%s' (need user:password)", val);
    return -1;
  }
  if (strlen(val) >= ARGS_AUTH_CREDS_MAX) {
    bufcpy(err, errsz, "credentials too long");
    return -1;
  }
  base64_encode(val, strlen(val), b64);
  n = bufcpy(out, outsz, "Basic ");
  bufcpy(out + n, outsz - n, b64);
  return 0;
}

static int basic_auth_parse(const char *flag, const char *val, char *out, size_t outsz) {
  const char *colon = strchr(val, ':');
  char b64[192];
  size_t n;
  if (!colon || colon == val) {
    argerr("invalid %s: %s (need user:password)", flag, val);
    return -1;
  }
  if (strlen(val) >= ARGS_AUTH_CREDS_MAX) {
    argerr("%s credentials too long: %s", flag, val);
    return -1;
  }
  base64_encode(val, strlen(val), b64);
  n = bufcpy(out, outsz, "Basic ");
  bufcpy(out + n, outsz - n, b64);
  return 0;
}

static void print_help(void) {
  printf(
    "usage: %s [options]\n\n"
    "serve streams over HTTP(S) as raw TS push, HLS, LL-HLS, MPEG-DASH or LL-DASH.\n"
    "It includes clients for FCC, RET(RAMS) and AL-FEC, also acts as a DLNA MediaServer.\n\n"
    "options:\n"
    "  -I, --iface <iface>            interface name for multicast joins      [kernel]\n"
    "  -l, --listen <a>:<p>           HTTP listen address:port                [all:9080]\n"
    "  -L, --listen-tls <a>:<p>       HTTPS listen address:port               [all:9443]\n"
    "      --tls-cert <path>          certificate file (PEM)\n"
    "      --tls-key <path>           private key file (PEM)\n"
    "  -j, --workers <spec>           -1/-2/-3: that many x cpu cores,\n"
    "                                 or <N>: an absolute thread count        [-1]\n"
    "      --max-clients <n>          cap on concurrent streams               [256]\n"
    "      --max-channels <n>         cap on concurrent\n"
    "                                 (source,filter,pmt,container)           [32]\n"
    "      --idle-timeout <s>         close a conn idle this long, 0 = off    [0]\n"
    "      --capture-ring-size <n>    per-source ingress ring buffer, KiB     [4096]\n"
    "      --ts-startup-timeout <s>   ts/spts/rawaudio: wait this long for a source's\n"
    "                                 first packet before 504, 0 = off          [5]\n"
    "  -i, --input <source>           add an input, repeatable, by form:\n"
    "                                 -                      stdin, /stdin/<fmt>\n"
    "                                 rist://@host:port      RIST, /rist/<fmt>\n"
    "                                 sds://addr:port        live SD&S/DVBSTP\n"
    "                                 rtp://addr:port        direct multicast, RTP\n"
    "                                 udp://addr:port        direct multicast, plain TS\n"
    "                                 http(s)://url          raw TS/RTP source\n"
    "                                 *.m3u/.xspf/.csv/.xml  playlist file\n"
    "                                 list index = position among all -i flags, so\n"
    "                                 a -/rist:// slot leaves that number unused\n"
    "  -n, --name <name>              name the -i right before it; that name can then\n"
    "                                 be used in URLs instead of /list/<n>/ or /rist//stdin\n"
    "                                 no '/', no leading '.', not a reserved word, unique\n"
    "      --media-type <t>           radio|tv, DLNA upnp:class               [tv]\n"
    "  -J, --join-all                 join all inputs at startup, never leave [off]\n"
    "  -k, --insecure                 skip TLS verification on https:// input\n"
    "      --sds-timeout <s>          sds:// discovery wait at startup/reload [3]\n"
    "      --sds-refresh-interval <s> sds:// re-poll period               [30]\n"
    "      --segment-size <s>         target segment duration, seconds        [3]\n"
    "                                 (hls, hls-fmp4, llhls, dash, lldash)\n"
    "      --segment-count <n>        playlist/manifest sliding-window size   [4]\n"
    "      --hls-part-size <s>        LL-HLS target part duration, seconds    [0.35]\n"
    "      --dash-part-size <s>       LL-DASH target chunk duration, seconds  [0.333]\n"
    "      --dash-utc-url <url>       LL-DASH MPD UTCTiming source, http-xsiso\n"
    "                                 [http://time.akamai.com/?iso&ms]\n"
    "      --hls-seg-pool <n>         segment buffer freelist cap per size    [8]\n"
    "      --metrics <path>           metrics sock [/run/dvbipitools/metrics.sock]\n"
    "      --metrics-id <name>        stable instance id, disabled if not set\n"
    "      --metrics-interval <s>     snapshot interval (default: 5 seconds)\n"
    "      --metrics-inspect-ts <lvl> TS health metrics: off|basic|medium|full (default: off)\n"
    "      --metrics-http             also serve /metrics ourselves           [off]\n"
    "      --metrics-auth <u>:<p>     HTTP Basic Auth for GET /metrics        [off]\n"
    "  -f, --format <list>            comma-separated route whitelist, from\n"
    "                                 ts,spts,rawaudio,hls,llhls,dash,lldash  [all]\n"
    "      --no-url-rtp               disable /rtp/... routes\n"
    "      --no-url-udp               disable /udp/... routes\n"
    "      --no-url-srt               disable /srt/... routes\n"
    "      --no-pid-filters           ignore ?filter= on every route\n"
    "      --no-lcevc                 ignore ?lcevc= on every route\n"
    "      --no-http2                 disable HTTP/2\n"
    "      --no-http3                 disable HTTP/3\n"
    "      --no-fcc                   ignore SDS fcc\n"
    "      --no-ret                   ignore SDS ret\n"
    "      --al-fec <L>:<D>           Annex E Layer 1 FEC (SMPTE 2022-1) matrix size for any\n"
    "                                 SDS-advertised repair stream, L*D<=400, L<=40\n"
    "      --no-al-fec                ignore SDS FECBaseLayer\n"
    "      --no-status                disable /ui/status.js\n"
    "      --h3-altsvc-port <n>       port announced in Alt-Svc               [TLS port]\n"
    "      --h3-max-streams <n>       concurrent requests per HTTP/3 conn     [100]\n"
    "      --h3-max-conns <n>         HTTP/3 conns per worker, upper bound    [256]\n"
    "      --h3-idle-timeout <s>      HTTP/3 connection idle timeout          [30]\n"
    "      --h3-retry <mode>          HTTP/3 addr validation: off|auto|always [auto]\n"
    "      --h3-max-udp-payload <n>   largest HTTP/3 UDP datagram, 1200..65507 [1452]\n"
    "      --h3-window <KiB>          HTTP/3 receive window per stream        [256]\n"
    "      --h3-cc <algo>             HTTP/3 congestion ctrl: cubic|bbr|reno  [cubic]\n"
    "      --status-tpl <path>        use file instead of the built-in page\n"
    "      --auth <user:pass>         HTTP Basic Auth for /, /ui/status.js, /ui/ws/  [off]\n"
    "      --cors-origin <list>       comma-separated hls/hls-fmp4/llhls/dash/lldash\n"
    "                                 origins  [\"*\"]\n"
    "      --ssdp-ttl <n>             SSDP multicast TTL                      [3]\n"
    "      --ssdp-iface <iface>       interface for SSDP announce/reply       [kernel]\n"
    "      --ssdp-interval <s>        SSDP NOTIFY re-announce period          [60]\n"
    "      --ssdp-max-age <s>         CACHE-CONTROL max-age, >= 2x interval   [1800]\n"
    "      --enable-dlna              serve SSDP + a UPnP MediaServer (DLNA)  [off]\n"
    "      --dlna-host <h>[:<p>]      host[:port] advertised in SSDP/DIDL     [-l/--listen]\n"
    "      --dlna-name <name>         DLNA friendlyName                       [%s (host)]\n"
    "      --dlna-keep-multicast      rtp/udp items: dvb-igmp/dvb-mld, mgroup [off]\n"
    "  -d, --daemonize                fork to background after startup\n"
    "  -v, --verbose                  per-connection diagnostics on stderr\n"
    "      --color <when>             auto|always|never                       [auto]\n"
    "  -c, --config <path>            YAML config file                        [%s, if present]\n"
    "      --config-strict            fail on config file issues instead of warnings\n"
    "      --configtest               check the config file, then exit\n"
    "  -h, --help                     this help\n"
    "\n"
    "Each -i's list index is its own position on the command line.\n"
    "any URL takes ?filter=<pids> to drop PIDs, e.g. ?filter=101,0x20.\n\n"
    "/export/<fmt>/<type> (type: m3u|xspf) lists every channel as one\n"
    "playlist. ?host=, ?input=1,3,4, ?filter=, ?keep_multicast, ?plain.\n\n"
    "on an MPTS source, hls/hls-fmp4/llhls/dash/lldash demux the first\n"
    "arriving PMT.\n"
    "Use ?pmt=<pid> (dec or 0x-hex) to pick a different one. ts\n"
    "passes the whole MPTS.\n"
    "\n"
    "On a program carrying LCEVC, hls/hls-fmp4/llhls/dash/lldash accept\n"
    "?lcevc=base|full|all|<n>: strip it, keep it (default), list every\n"
    "alternative in the manifest, or pick one by index/PID.\n"
    "disable this feature with --no-lcevc.\n"
    "\n"
    "Examples:\n"
    "  %s -i sds://239.19.75.1:3937\n"
    "  %s -l 0.0.0.0:9080 -i channels.m3u\n"
    "  %s -L [::]:9443 --tls-cert server.crt --tls-key server.key -i ch.xspf\n\n",
    TOOL_NAME, TOOL_NAME, DEFAULT_CONFIG_PATH, TOOL_NAME, TOOL_NAME, TOOL_NAME);
}

static const char shortopts[] = "I:i:Jkl:L:j:c:f:n:dvh";

static const struct option longopts[] = {
  {"iface", required_argument, 0, 'I'},
  {"listen", required_argument, 0, 'l'},
  {"listen-tls", required_argument, 0, 'L'},
  {"tls-cert", required_argument, 0, OPT_TLS_CERT},
  {"tls-key", required_argument, 0, OPT_TLS_KEY},
  {"workers", required_argument, 0, 'j'},
  {"max-clients", required_argument, 0, OPT_MAX_CLIENTS},
  {"max-channels", required_argument, 0, OPT_MAX_CHANNELS},
  {"idle-timeout", required_argument, 0, OPT_IDLE_TIMEOUT},
  {"capture-ring-size", required_argument, 0, OPT_CAPTURE_RING_SIZE},
  {"ts-startup-timeout", required_argument, 0, OPT_TS_STARTUP_TIMEOUT},
  {"sds-timeout", required_argument, 0, OPT_SDS_TIMEOUT},
  {"sds-refresh-interval", required_argument, 0, OPT_SDS_REFRESH_INTERVAL},
  {"segment-size", required_argument, 0, OPT_SEGMENT_SIZE},
  {"segment-count", required_argument, 0, OPT_SEGMENT_COUNT},
  {"hls-part-size", required_argument, 0, OPT_HLS_PART_SIZE},
  {"dash-part-size", required_argument, 0, OPT_DASH_PART_SIZE},
  {"dash-utc-url", required_argument, 0, OPT_DASH_UTC_URL},
  {"hls-seg-pool", required_argument, 0, OPT_HLS_SEG_POOL},
  {"metrics", required_argument, 0, OPT_METRICS},
  {"metrics-id", required_argument, 0, OPT_METRICS_ID},
  {"metrics-interval", required_argument, 0, OPT_METRICS_INTERVAL},
  {"metrics-inspect-ts", required_argument, 0, OPT_METRICS_INSPECT_TS},
  {"metrics-http", no_argument, 0, OPT_METRICS_HTTP},
  {"metrics-auth", required_argument, 0, OPT_METRICS_AUTH},
  {"format", required_argument, 0, 'f'},
  {"no-url-rtp", no_argument, 0, OPT_NO_URL_RTP},
  {"no-url-udp", no_argument, 0, OPT_NO_URL_UDP},
  {"no-url-srt", no_argument, 0, OPT_NO_URL_SRT},
  {"no-pid-filters", no_argument, 0, OPT_NO_PID_FILTERS},
  {"no-lcevc", no_argument, 0, OPT_NO_LCEVC},
  {"no-http2", no_argument, 0, OPT_NO_HTTP2},
  {"no-http3", no_argument, 0, OPT_NO_HTTP3},
  {"h3-altsvc-port", required_argument, 0, OPT_H3_ALTSVC_PORT},
  {"h3-max-streams", required_argument, 0, OPT_H3_MAX_STREAMS},
  {"h3-max-conns", required_argument, 0, OPT_H3_MAX_CONNS},
  {"h3-idle-timeout", required_argument, 0, OPT_H3_IDLE_TIMEOUT},
  {"h3-retry", required_argument, 0, OPT_H3_RETRY},
  {"h3-max-udp-payload", required_argument, 0, OPT_H3_MAX_UDP_PAYLOAD},
  {"h3-window", required_argument, 0, OPT_H3_WINDOW},
  {"h3-cc", required_argument, 0, OPT_H3_CC},
  {"no-fcc", no_argument, 0, OPT_NO_FCC},
  {"no-ret", no_argument, 0, OPT_NO_RET},
  {"al-fec", required_argument, 0, OPT_AL_FEC},
  {"no-al-fec", no_argument, 0, OPT_NO_AL_FEC},
  {"no-status", no_argument, 0, OPT_NO_STATUS},
  {"status-tpl", required_argument, 0, OPT_STATUS_TPL},
  {"auth", required_argument, 0, OPT_AUTH},
  {"cors-origin", required_argument, 0, OPT_CORS_ORIGIN},
  {"ssdp-ttl", required_argument, 0, OPT_SSDP_TTL},
  {"ssdp-iface", required_argument, 0, OPT_SSDP_IFACE},
  {"ssdp-interval", required_argument, 0, OPT_SSDP_INTERVAL},
  {"ssdp-max-age", required_argument, 0, OPT_SSDP_MAX_AGE},
  {"enable-dlna", no_argument, 0, OPT_ENABLE_DLNA},
  {"dlna-host", required_argument, 0, OPT_DLNA_HOST},
  {"dlna-name", required_argument, 0, OPT_DLNA_NAME},
  {"dlna-keep-multicast", no_argument, 0, OPT_DLNA_KEEP_MULTICAST},
  {"media-type", required_argument, 0, OPT_MEDIA_TYPE},
  {"input", required_argument, 0, 'i'},
  {"join-all", no_argument, 0, 'J'},
  {"insecure", no_argument, 0, 'k'},
  {"name", required_argument, 0, 'n'},
  {"daemonize", no_argument, 0, 'd'},
  {"verbose", no_argument, 0, 'v'},
  {"color", required_argument, 0, OPT_COLOR},
  {"config", required_argument, 0, 'c'},
  {"config-strict", no_argument, 0, OPT_CONFIG_STRICT},
  {"configtest", no_argument, 0, OPT_CONFIGTEST},
  {"help", no_argument, 0, 'h'},
  {0, 0, 0, 0}};

static args_status_t prescan(int argc, char **argv, const char **cfg_path, int *configtest, int *strict) {
  int c;
  optind = 1;
  opterr = 0;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    if (c == 'c') *cfg_path = optarg;
    if (c == 1058) *configtest = 1;
    if (c == 1059) *strict = 1;
    if (c == 'h') {
      print_help();
      opterr = 1;
      return ARGS_HELP;
    }
  }
  opterr = 1;
  return ARGS_OK;
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  const char *cfg_path = NULL;
  int configtest = 0;
  int strict = 0;
  int cli_input = 0;
  args_status_t pst;
  int c;

  if (argc == 1 && access(DEFAULT_CONFIG_PATH, R_OK))
    return ARGS_NOARGS;

  pst = prescan(argc, argv, &cfg_path, &configtest, &strict);
  if (pst != ARGS_OK) return pst;
  if (configtest) return dixy_cfg_test(cfg_path, strict) ? ARGS_ERR : ARGS_HELP;

  dixy_cfg_defaults(cfg);
  if (dixy_cfg_load(cfg, cfg_path, strict)) {
    args_free(cfg);
    return ARGS_ERR;
  }

  optind = 1;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    switch (c) {
      case 'I':
        cfg->iface = optarg;
        break;
      case 'l':
        if (dixy_cfg_listen(&cfg->listen, optarg)) {
          argerr("invalid -l/--listen address: %s", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      case 'L':
        if (dixy_cfg_listen(&cfg->listen_tls, optarg)) {
          argerr("invalid -L/--listen-tls address: %s", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      case OPT_TLS_CERT:
        cfg->tls_cert = optarg;
        break;
      case OPT_TLS_KEY:
        cfg->tls_key = optarg;
        break;
      case 'j':
        if (dixy_cfg_workers(&cfg->workers_spec, optarg)) {
          argerr("invalid -j/--workers: %s (-1/-2/-3, or a positive count)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      case OPT_MAX_CLIENTS: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, 65536, &v)) {
          argerr("invalid --max-clients: %s (1..65536)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->max_clients = (int)v;
        break;
      }
      case OPT_MAX_CHANNELS: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, 1024, &v)) {
          argerr("invalid --max-channels: %s (1..1024)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->max_channels = (int)v;
        break;
      }
      case OPT_IDLE_TIMEOUT: {
        unsigned v;
        if (argutil_uint_range(optarg, 0, 86400, &v)) {
          argerr("invalid --idle-timeout: %s (seconds, 0..86400, 0 = off)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->idle_timeout_s = v;
        break;
      }
      case OPT_CAPTURE_RING_SIZE: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, UINT_MAX, &v)) {
          argerr("invalid --capture-ring-size: %s (KiB, min 1)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->capture_ring_kib = v;
        break;
      }
      case OPT_TS_STARTUP_TIMEOUT: {
        char *end;
        double v = strtod(optarg, &end);
        if (*end != '\0' || !isfinite(v) || v < 0.0 || v > 1e9) {
          argerr("invalid --ts-startup-timeout: %s (seconds, 0=off)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->ts_startup_timeout_s = v;
        break;
      }
      case OPT_SDS_TIMEOUT: {
        char *end;
        double v = strtod(optarg, &end);
        if (*end != '\0' || !isfinite(v) || v <= 0.0 || v > 1e9) {
          argerr("invalid --sds-timeout: %s (seconds, > 0)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->sds_timeout_s = v;
        break;
      }
      case OPT_SDS_REFRESH_INTERVAL: {
        char *end;
        double v = strtod(optarg, &end);
        if (*end != '\0' || !isfinite(v) || v <= 0.0 || v > 1e9) {
          argerr("invalid --sds-refresh-interval: %s (seconds, > 0)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->sds_refresh_interval_s = v;
        break;
      }
      case OPT_SEGMENT_SIZE: {
        char *end;
        double v = strtod(optarg, &end);
        if (*end != '\0' || !isfinite(v) || v < 2.0 || v > 1e9) {
          argerr("invalid --segment-size: %s (seconds, min 2)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->segment_size = v;
        break;
      }
      case OPT_SEGMENT_COUNT: {
        unsigned v;
        if (argutil_uint_range(optarg, 3, 1000, &v)) {
          argerr("invalid --segment-count: %s (min 3)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->segment_count = (int)v;
        break;
      }
      case OPT_HLS_PART_SIZE: {
        char *end;
        double v = strtod(optarg, &end);
        if (*end != '\0' || !isfinite(v) || v < 0.05 || v > 5.0) {
          argerr("invalid --hls-part-size: %s (seconds, 0.05-5.0)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->hls_part_size = v;
        break;
      }
      case OPT_DASH_PART_SIZE: {
        char *end;
        double v = strtod(optarg, &end);
        if (*end != '\0' || !isfinite(v) || v < 0.05 || v > 5.0) {
          argerr("invalid --dash-part-size: %s (seconds, 0.05-5.0)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->dash_part_size = v;
        break;
      }
      case OPT_DASH_UTC_URL:
        if (strlen(optarg) > 256) {
          argerr("invalid --dash-utc-url: too long (max 256 chars)");
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->dash_utc_url = optarg;
        break;
      case OPT_HLS_SEG_POOL: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, INT_MAX, &v)) {
          argerr("invalid --hls-seg-pool: %s (min 1)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->hls_seg_pool = (int)v;
        break;
      }
      case OPT_METRICS:
        cfg->metrics_sock = optarg;
        break;
      case OPT_METRICS_ID:
        cfg->metrics_id = optarg;
        break;
      case OPT_METRICS_INTERVAL:
        if (argutil_metrics_interval_opt(TOOL_NAME, optarg, &cfg->metrics_interval_s)) {
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      case OPT_METRICS_INSPECT_TS:
        if (argutil_metrics_inspect_ts_opt(TOOL_NAME, optarg, &cfg->metrics_inspect_ts)) return ARGS_ERR;
        break;
      case OPT_METRICS_HTTP:
        cfg->metrics_http = 1;
        break;
      case 'f':
        if (dixy_cfg_format(cfg, optarg)) {
          argerr("invalid -f/--format: %s (comma-separated list of ts,spts,rawaudio,hls,llhls,dash,lldash)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      case OPT_NO_URL_RTP:
        cfg->no_url_rtp = 1;
        break;
      case OPT_NO_URL_UDP:
        cfg->no_url_udp = 1;
        break;
      case OPT_NO_URL_SRT:
        cfg->no_url_srt = 1;
        break;
      case OPT_NO_PID_FILTERS:
        cfg->no_pid_filters = 1;
        break;
      case OPT_NO_LCEVC:
        cfg->no_lcevc = 1;
        break;
      case OPT_NO_HTTP2:
        cfg->no_http2 = 1;
        break;
      case OPT_NO_HTTP3:
        cfg->no_http3 = 1;
        break;
      case OPT_H3_ALTSVC_PORT: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, 65535, &v)) {
          argerr("invalid --h3-altsvc-port: %s (1..65535)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->h3_altsvc_port = v;
        break;
      }
      case OPT_H3_MAX_STREAMS: {
        unsigned v;
        if (argutil_uint_range(optarg, 4, 1000, &v)) {
          argerr("invalid --h3-max-streams: %s (4..1000)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->h3_max_streams = v;
        break;
      }
      case OPT_H3_MAX_CONNS: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, 65536, &v)) {
          argerr("invalid --h3-max-conns: %s (1..65536)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->h3_max_conns = v;
        break;
      }
      case OPT_H3_IDLE_TIMEOUT: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, 86400, &v)) {
          argerr("invalid --h3-idle-timeout: %s (seconds, 1..86400)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->h3_idle_s = v;
        break;
      }
      case OPT_H3_RETRY: {
        char err[96];
        if (dixy_cfg_set_h3_retry(cfg, optarg, err, sizeof err)) {
          argerr("invalid --h3-retry: %s", err);
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      }
      case OPT_H3_MAX_UDP_PAYLOAD: {
        unsigned v;
        if (argutil_uint_range(optarg, 1200, 65507, &v)) {
          argerr("invalid --h3-max-udp-payload: %s (1200..65507)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->h3_max_udp = v;
        break;
      }
      case OPT_H3_WINDOW: {
        unsigned v;
        if (argutil_uint_range(optarg, 16, 1048576, &v)) {
          argerr("invalid --h3-window: %s (KiB, 16..1048576)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->h3_window_kib = v;
        break;
      }
      case OPT_H3_CC: {
        char err[96];
        if (dixy_cfg_set_h3_cc(cfg, optarg, err, sizeof err)) {
          argerr("invalid --h3-cc: %s", err);
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      }
      case OPT_NO_FCC:
        cfg->no_fcc = 1;
        break;
      case OPT_NO_RET:
        cfg->no_ret = 1;
        break;
      case OPT_AL_FEC:
        if (fec2022_parse_ld(optarg, &cfg->al_fec_l, &cfg->al_fec_d)) {
          argerr("invalid --al-fec: %s (want L:D, L*D<=400, L<=40)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      case OPT_NO_AL_FEC:
        cfg->no_al_fec = 1;
        break;
      case OPT_NO_STATUS:
        cfg->no_status = 1;
        break;
      case OPT_STATUS_TPL:
        cfg->status_template = optarg;
        break;
      case OPT_AUTH:
        if (basic_auth_parse("--auth", optarg, cfg->http_auth, sizeof cfg->http_auth)) {
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      case OPT_METRICS_AUTH:
        if (basic_auth_parse("--metrics-auth", optarg, cfg->http_metrics_auth, sizeof cfg->http_metrics_auth)) {
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      case OPT_CORS_ORIGIN:
        cfg->cors_origins = optarg;
        break;
      case OPT_SSDP_TTL: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, 255, &v)) {
          argerr("invalid --ssdp-ttl: %s (1..255)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->ssdp_ttl = (int)v;
        break;
      }
      case OPT_SSDP_IFACE:
        cfg->ssdp_iface = optarg;
        break;
      case OPT_SSDP_INTERVAL: {
        char *end;
        double v = strtod(optarg, &end);
        if (*end != '\0' || !isfinite(v) || v <= 0.0 || v > 1e9) {
          argerr("invalid --ssdp-interval: %s (seconds, > 0)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->ssdp_interval_s = v;
        break;
      }
      case OPT_SSDP_MAX_AGE: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, UINT_MAX, &v)) {
          argerr("invalid --ssdp-max-age: %s (seconds, > 0)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->ssdp_max_age_s = v;
        break;
      }
      case OPT_ENABLE_DLNA:
        cfg->enable_dlna = 1;
        break;
      case OPT_DLNA_HOST:
        cfg->dlna_host_opt = optarg;
        break;
      case OPT_DLNA_NAME:
        cfg->dlna_name = optarg;
        break;
      case OPT_DLNA_KEEP_MULTICAST:
        cfg->dlna_keep_multicast = 1;
        break;
      case 'i': {
        char err[200];
        if (!cli_input) {
          dixy_cfg_reset_inputs(cfg);
          cli_input = 1;
        }
        if (dixy_cfg_add_input(cfg, optarg, err, sizeof err)) {
          argerr("-i %s: %s", optarg, err);
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      }
      case 'n': {
        char err[200];
        if (dixy_cfg_set_name(cfg, optarg, err, sizeof err)) {
          argerr("-n/--name %s: %s", optarg, err);
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      }
      case OPT_MEDIA_TYPE: {
        char err[200];
        if (dixy_cfg_set_media_type(cfg, optarg, err, sizeof err)) {
          argerr("--media-type %s: %s", optarg, err);
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      }
      case 'J':
        cfg->join_all = 1;
        break;
      case 'k':
        cfg->insecure_tls = 1;
        break;
      case 'd':
        cfg->daemonize = 1;
        break;
      case 'v':
        cfg->verbose = 1;
        break;
      case OPT_COLOR: {
        log_color_t v;
        if (log_color_from_string(optarg, &v)) {
          argerr("invalid --color: %s (auto|always|never)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->color_mode = v;
        break;
      }
      case 'c':
      case OPT_CONFIG_STRICT:
      case OPT_CONFIGTEST:
        break;
      case 'h':
        print_help();
        return ARGS_HELP;
      default:
        args_free(cfg);
        return ARGS_ERR; /* getopt already reported */
    }
  }
  if (optind < argc) {
    argerr("unexpected argument: %s", argv[optind]);
    args_free(cfg);
    return ARGS_ERR;
  }
  if (cfg->tls_cert && !cfg->tls_key) {
    argerr("--tls-cert given without --tls-key");
    args_free(cfg);
    return ARGS_ERR;
  }
  if (cfg->tls_key && !cfg->tls_cert) {
    argerr("--tls-key given without --tls-cert");
    args_free(cfg);
    return ARGS_ERR;
  }
  if (cfg->hls_part_size >= cfg->segment_size) {
    argerr("--hls-part-size (%.2f) must be smaller than --segment-size (%.2f)", cfg->hls_part_size, cfg->segment_size);
    args_free(cfg);
    return ARGS_ERR;
  }
  if (cfg->dash_part_size >= cfg->segment_size) {
    argerr("--dash-part-size (%.2f) must be smaller than --segment-size (%.2f)", cfg->dash_part_size, cfg->segment_size);
    args_free(cfg);
    return ARGS_ERR;
  }
  if ((double)cfg->ssdp_max_age_s < 2.0 * cfg->ssdp_interval_s) {
    argerr("--ssdp-max-age (%u) must be at least 2x --ssdp-interval (%.2f)", cfg->ssdp_max_age_s, cfg->ssdp_interval_s);
    args_free(cfg);
    return ARGS_ERR;
  }
  if (argutil_metrics_opts_validate(TOOL_NAME, cfg->metrics_sock, cfg->metrics_id, cfg->metrics_interval_s)) {
    args_free(cfg);
    return ARGS_ERR;
  }
  if (argutil_metrics_inspect_ts_validate(TOOL_NAME, cfg->metrics_id, cfg->metrics_inspect_ts)) {
    args_free(cfg);
    return ARGS_ERR;
  }
  if (cfg->enable_dlna && cfg->no_spts) {
    argerr("--enable-dlna requires spts in -f/--format (DLNA playback always uses /spts)");
    args_free(cfg);
    return ARGS_ERR;
  }
  if (cfg->enable_dlna && cfg->no_rawaudio) {
    argerr("--enable-dlna requires rawaudio in -f/--format (DLNA radio items use /rawaudio)");
    args_free(cfg);
    return ARGS_ERR;
  }
  if (cfg->enable_dlna) {
    if (cfg->dlna_host_opt) {
      if (argutil_bufcpy_opt(TOOL_NAME, cfg->dlna_host, sizeof cfg->dlna_host, cfg->dlna_host_opt, "--dlna-host")) {
        args_free(cfg);
        return ARGS_ERR;
      }
    } else if (cfg->listen.scope != LISTEN_ANY) {
      char portbuf[12];
      size_t off;
      uint_to_str(portbuf, cfg->listen.port);
      if (cfg->listen.scope == LISTEN_V6) {
        off = bufcpy(cfg->dlna_host, sizeof cfg->dlna_host, "[");
        off += bufcpy(cfg->dlna_host + off, sizeof cfg->dlna_host - off, cfg->listen.addr);
        off += bufcpy(cfg->dlna_host + off, sizeof cfg->dlna_host - off, "]:");
      } else {
        off = bufcpy(cfg->dlna_host, sizeof cfg->dlna_host, cfg->listen.addr);
        off += bufcpy(cfg->dlna_host + off, sizeof cfg->dlna_host - off, ":");
      }
      bufcpy(cfg->dlna_host + off, sizeof cfg->dlna_host - off, portbuf);
    } else {
      argerr("--enable-dlna needs --dlna-host (or a concrete -l/--listen address, not 'all')");
      args_free(cfg);
      return ARGS_ERR;
    }
  }
  return ARGS_OK;
}
