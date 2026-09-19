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
  const char *dot = strrchr(path, '.');
  if (!dot) return -1;
  if (!strcasecmp(dot, ".m3u") || !strcasecmp(dot, ".m3u8")) *out = SRC_M3U;
  else if (!strcasecmp(dot, ".xspf"))                        *out = SRC_XSPF;
  else if (!strcasecmp(dot, ".csv"))                         *out = SRC_CSV;
  else if (!strcasecmp(dot, ".xml"))                         *out = SRC_XML;
  else return -1;
  return 0;
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
      snprintf(err, errsz, "at most one rist:// input");
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
    } else if (playlist_kind_from_ext(val, &kind)) {
      snprintf(err, errsz, "can't tell what '%s' is (expected -, sds://, rist://, http(s)://, or a .m3u/.xspf/.csv/.xml path)", val);
      return -1;
    }
    if (sources_append(cfg, kind, value, ordinal)) {
      snprintf(err, errsz, "out of memory");
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
      snprintf(err, errsz, "name must directly follow the input it names");
      return -1;
  }
  if (*slot) {
    snprintf(err, errsz, "name given twice for the same input");
    return -1;
  }
  *slot = name;
  return 0;
}

int dixy_cfg_set_h3_retry(config_t *cfg, const char *val, char *err, size_t errsz) {
  if (!strcmp(val, "auto"))        cfg->h3_retry = H3_RETRY_CFG_AUTO;
  else if (!strcmp(val, "off"))    cfg->h3_retry = H3_RETRY_CFG_OFF;
  else if (!strcmp(val, "always")) cfg->h3_retry = H3_RETRY_CFG_ALWAYS;
  else {
    snprintf(err, errsz, "invalid '%s' (off, auto or always)", val);
    return -1;
  }
  return 0;
}

int dixy_cfg_set_h3_cc(config_t *cfg, const char *val, char *err, size_t errsz) {
  if (!strcmp(val, "cubic"))     cfg->h3_cc = H3_CC_CFG_CUBIC;
  else if (!strcmp(val, "bbr"))  cfg->h3_cc = H3_CC_CFG_BBR;
  else if (!strcmp(val, "reno")) cfg->h3_cc = H3_CC_CFG_RENO;
  else {
    snprintf(err, errsz, "invalid '%s' (cubic, bbr or reno)", val);
    return -1;
  }
  return 0;
}

int dixy_cfg_set_media_type(config_t *cfg, const char *val, char *err, size_t errsz) {
  media_type_t mt;
  if (!strcmp(val, "tv"))
    mt = MEDIA_TV;
  else if (!strcmp(val, "radio"))
    mt = MEDIA_RADIO;
  else {
    snprintf(err, errsz, "invalid '%s' (radio or tv)", val);
    return -1;
  }
  if (cfg->media_type_seen) {
    snprintf(err, errsz, "media type given twice for the same input");
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
      snprintf(err, errsz, "media type must directly follow the input it applies to");
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
    snprintf(err, errsz, "credentials too long");
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
    "serve DVB-IPI multicast streams over HTTP as raw TS push, HLS,\n"
    "LL-HLS, or MPEG-DASH\n\n"
    "options:\n"
    "  -I, --iface <iface>         interface name for multicast joins      [kernel]\n"
    "  -l, --listen <a>:<p>        HTTP listen address:port                [all:9080]\n"
    "  -L, --listen-tls <a>:<p>    HTTPS listen address:port               [all:9443]\n"
    "      --tls-cert <path>       certificate file (PEM)\n"
    "      --tls-key <path>        private key file (PEM)\n"
    "  -j, --workers <spec>        -1/-2/-3: that many x cpu cores,\n"
    "                              or <N>: an absolute thread count        [-1]\n"
    "      --max-clients <n>       cap on concurrent streams               [256]\n"
    "      --max-channels <n>      cap on concurrent\n"
    "                              (source,filter,pmt,container)           [32]\n"
    "      --idle-timeout <s>      close a conn idle this long, 0 = off    [0]\n"
    "      --capture-ring-size <n> per-source ingress ring buffer, KiB     [4096]\n"
    "  -i, --input <source>        add an input, repeatable, by form:\n"
    "                              -                      stdin, /stdin/<fmt>\n"
    "                              rist://@host:port      RIST, /rist/<fmt>\n"
    "                              sds://addr:port        live SD&S/DVBSTP\n"
    "                              http(s)://url          raw TS/RTP source\n"
    "                              *.m3u/.xspf/.csv/.xml  playlist file\n"
    "                              list index = position among all -i flags, so\n"
    "                              a -/rist:// slot leaves that number unused\n"
    "  -n, --name <name>           name the -i right before it; that name can then\n"
    "                              be used in URLs instead of /list/<n>/ or /rist//stdin\n"
    "                              no '/', no leading '.', not a reserved word, unique\n"
    "      --media-type <t>        radio|tv, DLNA upnp:class               [tv]\n"
    "  -J, --join-all              join all inputs at startup, never leave [off]\n"
    "  -k, --insecure              skip TLS verification on https:// input\n"
    "      --sds-timeout <s>       sds:// discovery wait at startup/reload [3]\n"
    "      --sds-refresh-interval <s>  sds:// re-poll period               [30]\n"
    "      --segment-size <s>      target segment duration, seconds        [3]\n"
    "                              (hls, hls-fmp4, llhls, dash, lldash)\n"
    "      --segment-count <n>     playlist/manifest sliding-window size   [4]\n"
    "      --hls-part-size <s>     LL-HLS target part duration, seconds    [0.35]\n"
    "      --dash-part-size <s>    LL-DASH target chunk duration, seconds  [0.333]\n"
    "      --dash-utc-url <url>    LL-DASH MPD UTCTiming source, http-xsiso\n"
    "                              [http://time.akamai.com/?iso&ms]\n"
    "      --hls-seg-pool <n>      segment buffer freelist cap per size    [8]\n"
    "      --metrics <path>        metrics sock [/run/dvbipitools/metrics.sock]\n"
    "      --metrics-id <name>     stable instance id, disabled if not set\n"
    "      --metrics-interval <s>  snapshot interval (default: 5 seconds)\n"
    "      --metrics-http          also serve /metrics ourselves           [off]\n"
    "      --metrics-auth <u>:<p>  HTTP Basic Auth for GET /metrics        [off]\n"
    "  -f, --format <list>         comma-separated route whitelist, from\n"
    "                              ts,spts,rawaudio,hls,llhls,dash,lldash  [all]\n"
    "      --no-url-rtp            disable /rtp/... routes\n"
    "      --no-url-udp            disable /udp/... routes\n"
    "      --no-url-srt            disable /srt/... routes\n"
    "      --no-pid-filters        ignore ?filter= on every route\n"
    "      --no-lcevc              ignore ?lcevc= on every route\n"
    "      --no-http2              disable HTTP/2\n"
    "      --no-http3              disable HTTP/3\n"
    "      --no-fcc                ignore SDS fcc\n"
    "      --no-ret                ignore SDS ret\n"
    "      --al-fec <L>:<D>        Annex E Layer 1 FEC (SMPTE 2022-1) matrix size for any\n"
    "                              SDS-advertised repair stream, L*D<=400, L<=40\n"
    "      --no-al-fec             ignore SDS FECBaseLayer\n"
    "      --no-status             disable /ui/status.js\n"
    "      --h3-altsvc-port <n>    port announced in Alt-Svc               [TLS port]\n"
    "      --h3-max-streams <n>    concurrent requests per HTTP/3 conn     [100]\n"
    "      --h3-max-conns <n>      HTTP/3 conns per worker, upper bound    [256]\n"
    "      --h3-idle-timeout <s>   HTTP/3 connection idle timeout          [30]\n"
    "      --h3-retry <mode>       HTTP/3 addr validation: off|auto|always [auto]\n"
    "      --h3-max-udp-payload <n> largest HTTP/3 UDP datagram, 1200..65507 [1452]\n"
    "      --h3-window <KiB>       HTTP/3 receive window per stream        [256]\n"
    "      --h3-cc <algo>          HTTP/3 congestion ctrl: cubic|bbr|reno  [cubic]\n"
    "      --status-tpl <path>     use file instead of the built-in page\n"
    "      --auth <user:pass>      HTTP Basic Auth for /, /ui/status.js, /ui/ws/  [off]\n"
    "      --cors-origin <list>    comma-separated hls/hls-fmp4/llhls/dash/lldash\n"
    "                              origins  [\"*\"]\n"
    "      --ssdp-ttl <n>          SSDP multicast TTL                      [3]\n"
    "      --ssdp-iface <iface>    interface for SSDP announce/reply       [kernel]\n"
    "      --ssdp-interval <s>     SSDP NOTIFY re-announce period          [60]\n"
    "      --ssdp-max-age <s>      CACHE-CONTROL max-age, >= 2x interval   [1800]\n"
    "      --enable-dlna           serve SSDP + a UPnP MediaServer (DLNA)  [off]\n"
    "      --dlna-host <h>[:<p>]   host[:port] advertised in SSDP/DIDL     [-l/--listen]\n"
    "      --dlna-name <name>      DLNA friendlyName                       [%s (host)]\n"
    "      --dlna-keep-multicast   rtp/udp items: dvb-igmp/dvb-mld, mgroup [off]\n"
    "  -d, --daemonize             fork to background after startup\n"
    "  -v, --verbose               per-connection diagnostics on stderr\n"
    "      --color <when>          auto|always|never                       [auto]\n"
    "  -c, --config <path>         YAML config file                        [%s, if present]\n"
    "      --config-strict         fail on config file issues instead of warnings\n"
    "      --configtest            check the config file, then exit\n"
    "  -h, --help                  this help\n"
    "\n"
    "Each -i's list index is its own position on the command line.\n"
    "any URL takes ?filter=<pids> to drop PIDs, e.g. ?filter=101,0x20.\n\n"
    "/export/<fmt>/<type> (type: m3u|xspf) lists every channel as one\n"
    "playlist. ?host=, ?input=1,3,4, ?filter=, ?keep_multicast, ?plain.\n\n"
    "on an MPTS source, hls/hls-fmp4/llhls/dash/lldash demux the first\n"
    "arriving PMT.\n"
    "Úse ?pmt=<pid> (dec or 0x-hex) to pick a different one. ts\n"
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
  {"tls-cert", required_argument, 0, 1001},
  {"tls-key", required_argument, 0, 1002},
  {"workers", required_argument, 0, 'j'},
  {"max-clients", required_argument, 0, 1057},
  {"max-channels", required_argument, 0, 1051},
  {"idle-timeout", required_argument, 0, 1052},
  {"capture-ring-size", required_argument, 0, 1048},
  {"sds-timeout", required_argument, 0, 1044},
  {"sds-refresh-interval", required_argument, 0, 1045},
  {"segment-size", required_argument, 0, 1008},
  {"segment-count", required_argument, 0, 1009},
  {"hls-part-size", required_argument, 0, 1010},
  {"dash-part-size", required_argument, 0, 1049},
  {"dash-utc-url", required_argument, 0, 1050},
  {"hls-seg-pool", required_argument, 0, 1039},
  {"metrics", required_argument, 0, 1012},
  {"metrics-id", required_argument, 0, 1013},
  {"metrics-interval", required_argument, 0, 1014},
  {"metrics-http", no_argument, 0, 1015},
  {"metrics-auth", required_argument, 0, 1056},
  {"format", required_argument, 0, 'f'},
  {"no-url-rtp", no_argument, 0, 1020},
  {"no-url-udp", no_argument, 0, 1021},
  {"no-url-srt", no_argument, 0, 1026},
  {"no-pid-filters", no_argument, 0, 1022},
  {"no-lcevc", no_argument, 0, 1055},
  {"no-http2", no_argument, 0, 1040},
  {"no-http3", no_argument, 0, 1041},
  {"h3-altsvc-port", required_argument, 0, 1060},
  {"h3-max-streams", required_argument, 0, 1061},
  {"h3-max-conns", required_argument, 0, 1062},
  {"h3-idle-timeout", required_argument, 0, 1063},
  {"h3-retry", required_argument, 0, 1064},
  {"h3-max-udp-payload", required_argument, 0, 1065},
  {"h3-window", required_argument, 0, 1066},
  {"h3-cc", required_argument, 0, 1067},
  {"no-fcc", no_argument, 0, 1042},
  {"no-ret", no_argument, 0, 1043},
  {"al-fec", required_argument, 0, 1053},
  {"no-al-fec", no_argument, 0, 1054},
  {"no-status", no_argument, 0, 1023},
  {"status-tpl", required_argument, 0, 1027},
  {"auth", required_argument, 0, 1037},
  {"cors-origin", required_argument, 0, 1036},
  {"ssdp-ttl", required_argument, 0, 1029},
  {"ssdp-iface", required_argument, 0, 1030},
  {"ssdp-interval", required_argument, 0, 1046},
  {"ssdp-max-age", required_argument, 0, 1047},
  {"enable-dlna", no_argument, 0, 1031},
  {"dlna-host", required_argument, 0, 1032},
  {"dlna-name", required_argument, 0, 1033},
  {"dlna-keep-multicast", no_argument, 0, 1038},
  {"media-type", required_argument, 0, 1034},
  {"input", required_argument, 0, 'i'},
  {"join-all", no_argument, 0, 'J'},
  {"insecure", no_argument, 0, 'k'},
  {"name", required_argument, 0, 'n'},
  {"daemonize", no_argument, 0, 'd'},
  {"verbose", no_argument, 0, 'v'},
  {"color", required_argument, 0, 1007},
  {"config", required_argument, 0, 'c'},
  {"config-strict", no_argument, 0, 1059},
  {"configtest", no_argument, 0, 1058},
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
  int i_ordinal = 0;
  int media_type_seen = 0;
  last_input_t last_input = LAST_NONE;
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
      case 1001:
        cfg->tls_cert = optarg;
        break;
      case 1002:
        cfg->tls_key = optarg;
        break;
      case 'j':
        if (dixy_cfg_workers(&cfg->workers_spec, optarg)) {
          argerr("invalid -j/--workers: %s (-1/-2/-3, or a positive count)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      case 1057: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, 65536, &v)) {
          argerr("invalid --max-clients: %s (1..65536)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->max_clients = (int)v;
        break;
      }
      case 1051: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, 1024, &v)) {
          argerr("invalid --max-channels: %s (1..1024)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->max_channels = (int)v;
        break;
      }
      case 1052: {
        unsigned v;
        if (argutil_uint_range(optarg, 0, 86400, &v)) {
          argerr("invalid --idle-timeout: %s (seconds, 0..86400, 0 = off)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->idle_timeout_s = v;
        break;
      }
      case 1048: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, UINT_MAX, &v)) {
          argerr("invalid --capture-ring-size: %s (KiB, min 1)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->capture_ring_kib = v;
        break;
      }
      case 1044: {
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
      case 1045: {
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
      case 1008: {
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
      case 1009: {
        unsigned v;
        if (argutil_uint_range(optarg, 3, 1000, &v)) {
          argerr("invalid --segment-count: %s (min 3)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->segment_count = (int)v;
        break;
      }
      case 1010: {
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
      case 1049: {
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
      case 1050:
        if (strlen(optarg) > 256) {
          argerr("invalid --dash-utc-url: too long (max 256 chars)");
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->dash_utc_url = optarg;
        break;
      case 1039: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, INT_MAX, &v)) {
          argerr("invalid --hls-seg-pool: %s (min 1)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->hls_seg_pool = (int)v;
        break;
      }
      case 1012:
        cfg->metrics_sock = optarg;
        break;
      case 1013:
        cfg->metrics_id = optarg;
        break;
      case 1014:
        if (argutil_metrics_interval_opt(TOOL_NAME, optarg, &cfg->metrics_interval_s)) {
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      case 1015:
        cfg->metrics_http = 1;
        break;
      case 'f':
        if (dixy_cfg_format(cfg, optarg)) {
          argerr("invalid -f/--format: %s (comma-separated list of ts,spts,rawaudio,hls,llhls,dash,lldash)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      case 1020:
        cfg->no_url_rtp = 1;
        break;
      case 1021:
        cfg->no_url_udp = 1;
        break;
      case 1026:
        cfg->no_url_srt = 1;
        break;
      case 1022:
        cfg->no_pid_filters = 1;
        break;
      case 1055:
        cfg->no_lcevc = 1;
        break;
      case 1040:
        cfg->no_http2 = 1;
        break;
      case 1041:
        cfg->no_http3 = 1;
        break;
      case 1060: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, 65535, &v)) {
          argerr("invalid --h3-altsvc-port: %s (1..65535)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->h3_altsvc_port = v;
        break;
      }
      case 1061: {
        unsigned v;
        if (argutil_uint_range(optarg, 4, 1000, &v)) {
          argerr("invalid --h3-max-streams: %s (4..1000)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->h3_max_streams = v;
        break;
      }
      case 1062: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, 65536, &v)) {
          argerr("invalid --h3-max-conns: %s (1..65536)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->h3_max_conns = v;
        break;
      }
      case 1063: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, 86400, &v)) {
          argerr("invalid --h3-idle-timeout: %s (seconds, 1..86400)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->h3_idle_s = v;
        break;
      }
      case 1064: {
        char err[96];
        if (dixy_cfg_set_h3_retry(cfg, optarg, err, sizeof err)) {
          argerr("invalid --h3-retry: %s", err);
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      }
      case 1065: {
        unsigned v;
        if (argutil_uint_range(optarg, 1200, 65507, &v)) {
          argerr("invalid --h3-max-udp-payload: %s (1200..65507)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->h3_max_udp = v;
        break;
      }
      case 1066: {
        unsigned v;
        if (argutil_uint_range(optarg, 16, 1048576, &v)) {
          argerr("invalid --h3-window: %s (KiB, 16..1048576)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->h3_window_kib = v;
        break;
      }
      case 1067: {
        char err[96];
        if (dixy_cfg_set_h3_cc(cfg, optarg, err, sizeof err)) {
          argerr("invalid --h3-cc: %s", err);
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      }
      case 1042:
        cfg->no_fcc = 1;
        break;
      case 1043:
        cfg->no_ret = 1;
        break;
      case 1053:
        if (fec2022_parse_ld(optarg, &cfg->al_fec_l, &cfg->al_fec_d)) {
          argerr("invalid --al-fec: %s (want L:D, L*D<=400, L<=40)", optarg);
          return ARGS_ERR;
        }
        break;
      case 1054:
        cfg->no_al_fec = 1;
        break;
      case 1023:
        cfg->no_status = 1;
        break;
      case 1027:
        cfg->status_template = optarg;
        break;
      case 1037:
        if (basic_auth_parse("--auth", optarg, cfg->http_auth, sizeof cfg->http_auth)) {
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      case 1056:
        if (basic_auth_parse("--metrics-auth", optarg, cfg->http_metrics_auth, sizeof cfg->http_metrics_auth)) {
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      case 1036:
        cfg->cors_origins = optarg;
        break;
      case 1029: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, 255, &v)) {
          argerr("invalid --ssdp-ttl: %s (1..255)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->ssdp_ttl = (int)v;
        break;
      }
      case 1030:
        cfg->ssdp_iface = optarg;
        break;
      case 1046: {
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
      case 1047: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, UINT_MAX, &v)) {
          argerr("invalid --ssdp-max-age: %s (seconds, > 0)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->ssdp_max_age_s = v;
        break;
      }
      case 1031:
        cfg->enable_dlna = 1;
        break;
      case 1032:
        cfg->dlna_host_opt = optarg;
        break;
      case 1033:
        cfg->dlna_name = optarg;
        break;
      case 1038:
        cfg->dlna_keep_multicast = 1;
        break;
      case 'i': {
        source_kind_t kind;
        if (!cli_input) {
          dixy_cfg_reset_inputs(cfg);
          cli_input = 1;
        }
        i_ordinal++;
        media_type_seen = 0;
        if (strcmp(optarg, "-") == 0) {
          cfg->stdin_path = optarg;
          cfg->stdin_ordinal = i_ordinal;
          last_input = LAST_STDIN;
          break;
        }
        if (strncmp(optarg, "rist://", 7) == 0) {
          if (cfg->rist_uri) {
            argerr("at most one rist:// input");
            args_free(cfg);
            return ARGS_ERR;
          }
          if (optarg[7] != '@') {
            argerr("invalid -i %s (rist:// needs rist://@host:port)", optarg);
            args_free(cfg);
            return ARGS_ERR;
          }
          cfg->rist_uri = optarg;
          cfg->rist_ordinal = i_ordinal;
          last_input = LAST_RIST;
          break;
        }
        if (strncmp(optarg, "sds://", 6) == 0) {
          int family;
          char addr[64];
          unsigned port;
          const char *hostport = optarg + 6;
          if (argutil_addrport_parse(hostport, &family, addr, sizeof addr, &port)) {
            argerr("invalid -i %s (sds:// needs sds://addr:port)", optarg);
            args_free(cfg);
            return ARGS_ERR;
          }
          if (sources_append(cfg, SRC_SDS, hostport, i_ordinal)) {
            argerr("out of memory");
            args_free(cfg);
            return ARGS_ERR;
          }
          last_input = LAST_SOURCE;
          break;
        }
        if (strncmp(optarg, "http://", 7) == 0 || strncmp(optarg, "https://", 8) == 0) {
          if (sources_append(cfg, SRC_HTTP, optarg, i_ordinal)) {
            argerr("out of memory");
            args_free(cfg);
            return ARGS_ERR;
          }
          last_input = LAST_SOURCE;
          break;
        }
        if (playlist_kind_from_ext(optarg, &kind) == 0) {
          if (sources_append(cfg, kind, optarg, i_ordinal)) {
            argerr("out of memory");
            args_free(cfg);
            return ARGS_ERR;
          }
          last_input = LAST_SOURCE;
          break;
        }
        argerr("can't tell what -i %s is (expected -, sds://, rist://, http(s)://, "
               "or a .m3u/.xspf/.csv/.xml path)",
               optarg);
        args_free(cfg);
        return ARGS_ERR;
      }
      case 'n': {
        if (!route_name_valid(optarg)) {
          argerr("invalid -n/--name: %s (no '/', not starting with '.', not a reserved word, max %d chars)", optarg,
                 ROUTE_NAME_MAX);
          args_free(cfg);
          return ARGS_ERR;
        }
        if (name_in_use(cfg, optarg)) {
          argerr("duplicate -n/--name: %s", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        switch (last_input) {
          case LAST_STDIN:
            if (cfg->stdin_name) {
              argerr("-n/--name given twice for -i -");
              args_free(cfg);
              return ARGS_ERR;
            }
            cfg->stdin_name = optarg;
            break;
          case LAST_RIST:
            if (cfg->rist_name) {
              argerr("-n/--name given twice for -i rist://...");
              args_free(cfg);
              return ARGS_ERR;
            }
            cfg->rist_name = optarg;
            break;
          case LAST_SOURCE:
            if (cfg->sources[cfg->n_sources - 1].name) {
              argerr("-n/--name given twice for the same -i");
              args_free(cfg);
              return ARGS_ERR;
            }
            cfg->sources[cfg->n_sources - 1].name = optarg;
            break;
          default:
            argerr("-n/--name must directly follow the -i it names");
            args_free(cfg);
            return ARGS_ERR;
        }
        break;
      }
      case 1034: {
        media_type_t mt;
        if (!strcmp(optarg, "tv"))
          mt = MEDIA_TV;
        else if (!strcmp(optarg, "radio"))
          mt = MEDIA_RADIO;
        else {
          argerr("invalid --media-type: %s (radio or tv)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        if (media_type_seen) {
          argerr("--media-type given twice for the same -i");
          args_free(cfg);
          return ARGS_ERR;
        }
        media_type_seen = 1;
        switch (last_input) {
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
            argerr("--media-type must directly follow the -i it applies to");
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
      case 1007: {
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
      case 1059:
      case 1058:
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
