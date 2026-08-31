/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "lib/helper/argutil.h"
#include "lib/helper/base64.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"

#include "args.h"
#include "core/route.h"
#include "version.h"

#define ARGS_AUTH_CREDS_MAX 128 /* max "user:password" length for --auth */

static void argerr(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void argerr(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  argutil_verr(TOOL_NAME, fmt, ap);
  va_end(ap);
}

/* "all:<port>" (wildcard, both families) or "<addr>:<port>" / "[<addr6>]:<port>" */
static int listen_parse(const char *s, listen_spec_t *out) {
  if (strncmp(s, "all:", 4) == 0) {
    unsigned port;
    if (argutil_port_parse(s + 4, &port))
      return -1;
    out->scope = LISTEN_ANY;
    out->addr[0] = '\0';
    out->port = port;
    return 0;
  }
  {
    int family;
    unsigned port;
    if (argutil_addrport_parse(s, &family, out->addr, sizeof out->addr, &port))
      return -1;
    out->scope = family == AF_INET6 ? LISTEN_V6 : LISTEN_V4;
    out->port = port;
    return 0;
  }
}

/* -1/-2/-3 (that many x core count) or a positive absolute thread count */
static int workers_parse(const char *s, int *out) {
  char *end;
  long v = strtol(s, &end, 10);
  if (*end != '\0')
    return -1;
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

/* -f/--format <list>: comma-separated whitelist, tokens ts,spts,rawaudio,hls,llhls,dash.
   sets cfg's no_* fields: listed formats 0, everything else 1. 0 ok, -1 empty or unknown token */
static int format_parse(const char *s, config_t *cfg) {
  struct {
    const char *name;
    int *flag;
  } items[] = {
      {"ts", &cfg->no_ts},   {"spts", &cfg->no_spts}, {"rawaudio", &cfg->no_rawaudio},
      {"hls", &cfg->no_hls}, {"llhls", &cfg->no_llhls}, {"dash", &cfg->no_dash},
  };
  size_t n_items = sizeof items / sizeof items[0];
  size_t i;
  const char *p = s;

  if (!*s)
    return -1;

  for (i = 0; i < n_items; i++)
    *items[i].flag = 1;

  while (*p) {
    const char *comma = strchr(p, ',');
    size_t len = comma ? (size_t)(comma - p) : strlen(p);
    int matched = 0;

    if (!len)
      return -1;
    for (i = 0; i < n_items; i++) {
      if (strlen(items[i].name) == len && !strncmp(items[i].name, p, len)) {
        *items[i].flag = 0;
        matched = 1;
        break;
      }
    }
    if (!matched)
      return -1;
    p = comma ? comma + 1 : p + len;
  }
  return 0;
}

/* case-insensitive suffix match against known playlist extensions */
static int playlist_kind_from_ext(const char *path, source_kind_t *out) {
  const char *dot = strrchr(path, '.');
  if (!dot)
    return -1;
  if (!strcasecmp(dot, ".m3u") || !strcasecmp(dot, ".m3u8"))
    *out = SRC_M3U;
  else if (!strcasecmp(dot, ".xspf"))
    *out = SRC_XSPF;
  else if (!strcasecmp(dot, ".csv"))
    *out = SRC_CSV;
  else if (!strcasecmp(dot, ".xml"))
    *out = SRC_XML;
  else
    return -1;
  return 0;
}

static int sources_append(config_t *cfg, source_kind_t kind, const char *value, int ordinal) {
  source_def_t *p = array_grow(cfg->sources, &cfg->sources_cap, cfg->n_sources + 1, sizeof *cfg->sources);
  if (!p)
    return -1;
  cfg->sources = p;
  memset(&cfg->sources[cfg->n_sources], 0, sizeof *cfg->sources);
  cfg->sources[cfg->n_sources].kind = kind;
  cfg->sources[cfg->n_sources].value = value;
  cfg->sources[cfg->n_sources].ordinal = ordinal;
  cfg->n_sources++;
  return 0;
}

static int name_in_use(const config_t *cfg, const char *name) {
  int i;
  if (cfg->stdin_name && !strcmp(cfg->stdin_name, name))
    return 1;
  if (cfg->rist_name && !strcmp(cfg->rist_name, name))
    return 1;
  for (i = 0; i < cfg->n_sources; i++)
    if (cfg->sources[i].name && !strcmp(cfg->sources[i].name, name))
      return 1;
  return 0;
}

void args_free(config_t *cfg) {
  free(cfg->sources);
  cfg->sources = NULL;
  cfg->n_sources = 0;
  cfg->sources_cap = 0;
}

static void print_help(void) {
  printf(
      "usage: %s [options]\n\n"
      "serve DVB-IPI multicast streams over HTTP as raw TS push, HLS,\n"
      "LL-HLS, or MPEG-DASH\n\n"
      "options:\n"
      "  -I, --iface <iface>         interface for multicast joins           [kernel]\n"
      "  -l, --listen <a>:<p>        HTTP listen address:port                [all:9080]\n"
      "  -L, --listen-tls <a>:<p>    HTTPS listen address:port               [all:9443]\n"
      "      --tls-cert <path>       certificate file (PEM)\n"
      "      --tls-key <path>        private key file (PEM)\n"
      "  -j, --workers <spec>        -1/-2/-3: that many x cpu cores,\n"
      "                              or <N>: an absolute thread count        [-1]\n"
      "  -c, --max-clients <n>       cap on concurrent streams               [256]\n"
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
      "  -k, --insecure              skip TLS verification on https:// input\n"
      "      --sds-timeout <s>       sds:// discovery wait at startup/reload [3]\n"
      "      --sds-refresh-interval <s>  sds:// re-poll period               [30]\n"
      "      --segment-size <s>      target segment duration, seconds        [3]\n"
      "                              (hls, hls-fmp4, llhls, dash)\n"
      "      --segment-count <n>     playlist/manifest sliding-window size   [4]\n"
      "      --hls-part-size <s>     LL-HLS target part duration, seconds    [0.35]\n"
      "      --hls-seg-pool <n>      segment buffer freelist cap per size    [8]\n"
      "      --metrics <path>        metrics sock [/run/dvbipitools/metrics.sock]\n"
      "      --metrics-id <name>     stable instance id, disabled if not set\n"
      "      --metrics-interval <s>  snapshot interval (default: 5 seconds)\n"
      "      --metrics-http          also serve /metrics ourselves           [off]\n"
      "  -f, --format <list>         comma-separated route whitelist, from\n"
      "                              ts,spts,rawaudio,hls,llhls,dash         [all]\n"
      "      --no-url-rtp            disable /rtp/... routes\n"
      "      --no-url-udp            disable /udp/... routes\n"
      "      --no-url-srt            disable /srt/... routes\n"
      "      --no-pid-filters        ignore ?filter= on every route\n"
      "      --no-http2              disable HTTP/2\n"
      "      --no-http3              disable HTTP/3\n"
      "      --no-fcc                ignore SDS fcc\n"
      "      --no-ret                ignore SDS ret\n"
      "      --no-status             disable /ui/status.js\n"
      "      --status-tpl <path>     use file instead of the built-in page\n"
      "      --auth <user:pass>      HTTP Basic Auth for /, /ui/status.js, /ui/ws/  [off]\n"
      "      --cors-origin <list>    comma-separated hls/llhls/dash origins  [\"*\"]\n"
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
      "  -h, --help                  this help\n"
      "\n"
      "Each -i flag's list index is its own position on the command line.\n"
      "any URL takes ?filter=<pids> to drop PIDs, e.g. ?filter=101,0x20.\n\n"
      "on an MPTS source, hls/llhls/dash demux the first arriving PMT.\n"
      "use ?pmt=<pid> (dec or 0x-hex) to pick a different one. ts\n"
      "passes the whole MPTS through.\n"
      "\n"
      "Examples:\n"
      "  %s -i sds://239.19.75.1:3937\n"
      "  %s -l 0.0.0.0:9080 -i channels.m3u\n"
      "  %s -L [::]:9443 --tls-cert server.crt --tls-key server.key -i ch.xspf\n\n",
      TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME);
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  static const struct option longopts[] = {
      {"iface", required_argument, 0, 'I'},
      {"listen", required_argument, 0, 'l'},
      {"listen-tls", required_argument, 0, 'L'},
      {"tls-cert", required_argument, 0, 1001},
      {"tls-key", required_argument, 0, 1002},
      {"workers", required_argument, 0, 'j'},
      {"max-clients", required_argument, 0, 'c'},
      {"capture-ring-size", required_argument, 0, 1048},
      {"sds-timeout", required_argument, 0, 1044},
      {"sds-refresh-interval", required_argument, 0, 1045},
      {"segment-size", required_argument, 0, 1008},
      {"segment-count", required_argument, 0, 1009},
      {"hls-part-size", required_argument, 0, 1010},
      {"hls-seg-pool", required_argument, 0, 1039},
      {"metrics", required_argument, 0, 1012},
      {"metrics-id", required_argument, 0, 1013},
      {"metrics-interval", required_argument, 0, 1014},
      {"metrics-http", no_argument, 0, 1015},
      {"format", required_argument, 0, 'f'},
      {"no-url-rtp", no_argument, 0, 1020},
      {"no-url-udp", no_argument, 0, 1021},
      {"no-url-srt", no_argument, 0, 1026},
      {"no-pid-filters", no_argument, 0, 1022},
      {"no-http2", no_argument, 0, 1040},
      {"no-http3", no_argument, 0, 1041},
      {"no-fcc", no_argument, 0, 1042},
      {"no-ret", no_argument, 0, 1043},
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
      {"insecure", no_argument, 0, 'k'},
      {"name", required_argument, 0, 'n'},
      {"daemonize", no_argument, 0, 'd'},
      {"verbose", no_argument, 0, 'v'},
      {"color", required_argument, 0, 1007},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int c;
  int i_ordinal = 0;
  const char *dlna_host_arg = NULL;
  enum { LAST_NONE, LAST_STDIN, LAST_RIST, LAST_SOURCE } last_input = LAST_NONE;
  int media_type_seen = 0;

  if (argc == 1)
    return ARGS_NOARGS;

  memset(cfg, 0, sizeof *cfg);
  listen_parse("all:9080", &cfg->listen);
  listen_parse("all:9443", &cfg->listen_tls);
  cfg->workers_spec = -1;
  cfg->max_clients = 256;
  cfg->capture_ring_kib = 4096;
  cfg->sds_timeout_s = 3.0;
  cfg->sds_refresh_interval_s = 30.0;
  cfg->segment_size = 3.0;
  cfg->segment_count = 4;
  cfg->hls_part_size = 0.35;
  cfg->hls_seg_pool = 8;
  cfg->ssdp_ttl = 3;
  cfg->ssdp_interval_s = 60.0;
  cfg->ssdp_max_age_s = 1800;
  optind = 1;
  while ((c = getopt_long(argc, argv, "I:i:kl:L:j:c:f:n:dvh", longopts, NULL)) != -1) {
    switch (c) {
      case 'I':
        cfg->iface = optarg;
        break;
      case 'l':
        if (listen_parse(optarg, &cfg->listen)) {
          argerr("invalid -l/--listen address: %s", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      case 'L':
        if (listen_parse(optarg, &cfg->listen_tls)) {
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
        if (workers_parse(optarg, &cfg->workers_spec)) {
          argerr("invalid -j/--workers: %s (-1/-2/-3, or a positive count)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        break;
      case 'c': {
        char *end;
        long v = strtol(optarg, &end, 10);
        if (*end != '\0' || v < 1 || v > 65536) {
          argerr("invalid -c/--max-clients: %s (1..65536)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->max_clients = (int)v;
        break;
      }
      case 1048: {
        char *end;
        long v = strtol(optarg, &end, 10);
        if (*end != '\0' || v < 1) {
          argerr("invalid --capture-ring-size: %s (KiB, min 1)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->capture_ring_kib = (unsigned)v;
        break;
      }
      case 1044: {
        char *end;
        double v = strtod(optarg, &end);
        if (*end != '\0' || v <= 0.0) {
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
        if (*end != '\0' || v <= 0.0) {
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
        if (*end != '\0' || v < 2.0) {
          argerr("invalid --segment-size: %s (seconds, min 2)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->segment_size = v;
        break;
      }
      case 1009: {
        char *end;
        long v = strtol(optarg, &end, 10);
        if (*end != '\0' || v < 3 || v > 1000) {
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
        if (*end != '\0' || v < 0.05 || v > 5.0) {
          argerr("invalid --hls-part-size: %s (seconds, 0.05-5.0)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->hls_part_size = v;
        break;
      }
      case 1039: {
        char *end;
        long v = strtol(optarg, &end, 10);
        if (*end != '\0' || v < 1) {
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
      case 1014: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0 || v > 86400UL) {
          argerr("invalid --metrics-interval: %s (seconds, 1..86400)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->metrics_interval_s = (unsigned)v;
        break;
      }
      case 1015:
        cfg->metrics_http = 1;
        break;
      case 'f':
        if (format_parse(optarg, cfg)) {
          argerr("invalid -f/--format: %s (comma-separated list of ts,spts,rawaudio,hls,llhls,dash)", optarg);
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
      case 1040:
        cfg->no_http2 = 1;
        break;
      case 1041:
        cfg->no_http3 = 1;
        break;
      case 1042:
        cfg->no_fcc = 1;
        break;
      case 1043:
        cfg->no_ret = 1;
        break;
      case 1023:
        cfg->no_status = 1;
        break;
      case 1027:
        cfg->status_template = optarg;
        break;
      case 1037: {
        const char *colon = strchr(optarg, ':');
        char b64[192];
        if (!colon || colon == optarg) {
          argerr("invalid --auth: %s (need user:password)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        if (strlen(optarg) >= ARGS_AUTH_CREDS_MAX) {
          argerr("--auth credentials too long: %s", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        base64_encode(optarg, strlen(optarg), b64);
        {
          size_t n = bufcpy(cfg->http_auth, sizeof cfg->http_auth, "Basic ");
          bufcpy(cfg->http_auth + n, sizeof cfg->http_auth - n, b64);
        }
        break;
      }
      case 1036:
        cfg->cors_origins = optarg;
        break;
      case 1029: {
        char *end;
        long v = strtol(optarg, &end, 10);
        if (*end != '\0' || v < 1 || v > 255) {
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
        if (*end != '\0' || v <= 0.0) {
          argerr("invalid --ssdp-interval: %s (seconds, > 0)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->ssdp_interval_s = v;
        break;
      }
      case 1047: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0) {
          argerr("invalid --ssdp-max-age: %s (seconds, > 0)", optarg);
          args_free(cfg);
          return ARGS_ERR;
        }
        cfg->ssdp_max_age_s = (unsigned)v;
        break;
      }
      case 1031:
        cfg->enable_dlna = 1;
        break;
      case 1032:
        dlna_host_arg = optarg;
        break;
      case 1033:
        cfg->dlna_name = optarg;
        break;
      case 1038:
        cfg->dlna_keep_multicast = 1;
        break;
      case 'i': {
        source_kind_t kind;
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
  if ((double)cfg->ssdp_max_age_s < 2.0 * cfg->ssdp_interval_s) {
    argerr("--ssdp-max-age (%u) must be at least 2x --ssdp-interval (%.2f)", cfg->ssdp_max_age_s, cfg->ssdp_interval_s);
    args_free(cfg);
    return ARGS_ERR;
  }
  if ((cfg->metrics_sock || cfg->metrics_interval_s) && !cfg->metrics_id) {
    argerr("--metrics/--metrics-interval require --metrics-id");
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
    if (dlna_host_arg) {
      if (strlen(dlna_host_arg) >= sizeof cfg->dlna_host) {
        argerr("--dlna-host too long: %s", dlna_host_arg);
        args_free(cfg);
        return ARGS_ERR;
      }
      bufcpy(cfg->dlna_host, sizeof cfg->dlna_host, dlna_host_arg);
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
