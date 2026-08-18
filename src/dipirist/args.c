/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <ctype.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/argutil.h"
#include "lib/ioutil.h"
#include "lib/log.h"
#include "lib/uriparse.h"

#include "args.h"
#include "version.h"

static void argerr(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

static void argerr(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  argutil_verr(TOOL_NAME, fmt, ap);
  va_end(ap);
}

/* rest: [@]addr:port, multicast literal required */
static int parse_direct(const char *rest, nonrist_t *s) {
  if (*rest == '@')
    rest++;
  return uriparse_mcast_addrport(rest, &s->family, s->group, sizeof s->group, &s->port);
}

static int parse_nonrist(const char *uri, nonrist_t *s, int is_sink) {
  memset(s, 0, sizeof *s);
  if (strcmp(uri, "-") == 0) {
    s->kind = NONRIST_FILE; /* file_path[0] == '\0': stdin (source) / stdout (sink) */
    return 0;
  }
  if (strncmp(uri, "rtp://", 6) == 0) {
    s->kind = NONRIST_RTP;
    s->rtp_wrapped = 1;
    return parse_direct(uri + 6, s);
  }
  if (strncmp(uri, "udp://", 6) == 0) {
    s->kind = NONRIST_UDP;
    s->rtp_wrapped = 0;
    return parse_direct(uri + 6, s);
  }
  if (strncmp(uri, "http://", 7) == 0 || strncmp(uri, "https://", 8) == 0) {
    if (is_sink)
      return -1; /* an HTTP TS source makes no sense as an output */
    s->kind = NONRIST_HTTP;
    return http_url_parse(uri, &s->http);
  }
  if (strlen(uri) >= sizeof s->file_path)
    return -1;
  s->kind = NONRIST_FILE;
  bufcpy(s->file_path, sizeof s->file_path, uri);
  return 0;
}

/* *count tracks with this flag: 0 = decides is_rist, further rist:// bond onto same endpoint, others (mixed, or a repeated non-RIST endpoint) rejected */
static int parse_endpoint_uri(const char *uri, endpoint_t *e, int is_sink, int *count) {
  int is_rist_uri = strncmp(uri, "rist://", 7) == 0;
  if (*count == 0) {
    e->is_rist = is_rist_uri;
    e->n_rist = 0;
  } else if (!e->is_rist || !is_rist_uri) {
    return -1;
  }

  if (e->is_rist) {
    int has_at = uri[7] == '@'; /* librist: '@' right after rist:// binds/listens, else it calls out */

    if (e->n_rist >= DIPIRIST_MAX_PEERS)
      return -1;
    if (strlen(uri) >= sizeof e->rist_uri[0])
      return -1;
    /* -i rist:// listens for sender, -o rist:// calls out to receiver */
    if (is_sink == has_at)
      return -1;
    bufcpy(e->rist_uri[e->n_rist++], sizeof e->rist_uri[0], uri);
  } else if (parse_nonrist(uri, &e->nonrist, is_sink)) {
    return -1;
  }
  (*count)++;
  return 0;
}

int config_is_sender(const config_t *cfg) {
  return cfg->out.is_rist;
}

void endpoint_describe(const endpoint_t *e, char *buf, size_t n) {
  if (e->is_rist) {
    if (e->n_rist == 1)
      snprintf(buf, n, "%s", e->rist_uri[0]);
    else
      snprintf(buf, n, "%s +%d more", e->rist_uri[0], e->n_rist - 1);
    return;
  }
  switch (e->nonrist.kind) {
  case NONRIST_RTP:
  case NONRIST_UDP: {
    const char *scheme = (e->nonrist.kind == NONRIST_RTP) ? "rtp" : "udp";
    if (e->nonrist.family == AF_INET6)
      snprintf(buf, n, "%s://@[%s]:%u", scheme, e->nonrist.group, e->nonrist.port);
    else
      snprintf(buf, n, "%s://@%s:%u", scheme, e->nonrist.group, e->nonrist.port);
    break;
  }
  case NONRIST_HTTP:
    snprintf(buf, n, "%s://%s:%u%s", e->nonrist.http.tls ? "https" : "http", e->nonrist.http.host, e->nonrist.http.port,
              e->nonrist.http.path);
    break;
  case NONRIST_FILE:
    snprintf(buf, n, "%s", e->nonrist.file_path[0] ? e->nonrist.file_path : "- (stdin/stdout)");
    break;
  }
}

static void print_help(void) {
  printf(
      "usage: %s -i <uri> -o <uri> [options]\n\n"
      "bridge a DVB-IPI stream between plain RTP/UDP/file and a RIST link (VSF TR-06),\n"
      "either direction: exactly one of -i/-o must be rist://, the other is a regular\n"
      "dipirec-style endpoint\n\n"
      "endpoints:\n"
      "  rist://<host>:<port>[?params]  RIST peer, calls out; -o only\n"
      "  rist://@<host>:<port>[?params] RIST peer, listens; -i only\n"
      "                              repeat -i/-o to bond several links\n"
      "                              e.g. ?buffer=1000&secret=... - see --buffer/\n"
      "                              --secret below for the equivalent flags)\n"
      "  rtp://@<group>:<port>      RTP wrapped SPTS multicast (@ optional)\n"
      "  udp://@<group>:<port>      raw SPTS multicast (@ optional)\n"
      "  http://<host>:<port>/<path>   HTTP TS stream, -i only\n"
      "  https://<host>:<port>/<path>  same, TLS (-k skips verification), -i only\n"
      "  -                          stdin (-i) or stdout (-o)\n"
      "  <path>                     a file\n"
      "  IPv6 groups in brackets, e.g. rtp://@[ff3e::1]:8700\n\n"
      "options:\n"
      "  -i, --in <uri>             input (see above), repeatable if rist://\n"
      "  -o, --out <uri>            output (see above), repeatable if rist://\n"
      "  -I, --iface <iface>        interface for the non-RIST side's multicast join/send\n"
      "  -k, --insecure             skip TLS verification, -i https:// only\n"
      "      --profile <name>       simple|main (default simple); main adds encryption\n"
      "      --secret <psk>         pre-shared key; requires --profile main\n"
      "      --cname <name>         RTCP cname; default library-generated\n"
      "      --buffer <ms>          RIST recovery buffer (min=max=<ms>); default library\n"
      "      --color <when>         auto|always|never (default auto)\n"
      "      --metrics <path>       Unix datagram socket for metrics (default: /run/dvbipitools/metrics.sock)\n"
      "      --metrics-id <name>    stable instance id; metrics disabled unless set\n"
      "      --metrics-interval <s> snapshot interval in seconds (default: 5)\n"
      "  -v, --verbose              periodic bridge stats on stderr\n"
      "  -d, --daemonize            fork to background after startup, detach from terminal\n"
      "  -h, --help                 this help\n\n"
      "examples:\n"
      "  %s -i rtp://@239.1.1.1:5000 -o rist://1.2.3.4:6000 --buffer 1000\n"
      "  %s -i rist://@0.0.0.0:6000 -o rtp://@239.1.1.1:5000 --buffer 1000\n"
      "  %s -i rtp://@239.1.1.1:5000 -o rist://1.2.3.4:6000 -o rist://5.6.7.8:6000\n",
      TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME);
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  static const struct option longopts[] = {
      {"in", required_argument, 0, 'i'},
      {"out", required_argument, 0, 'o'},
      {"iface", required_argument, 0, 'I'},
      {"insecure", no_argument, 0, 'k'},
      {"profile", required_argument, 0, 1000},
      {"secret", required_argument, 0, 1001},
      {"cname", required_argument, 0, 1002},
      {"buffer", required_argument, 0, 1003},
      {"color", required_argument, 0, 1004},
      {"metrics", required_argument, 0, 1005},
      {"metrics-id", required_argument, 0, 1006},
      {"metrics-interval", required_argument, 0, 1007},
      {"verbose", no_argument, 0, 'v'},
      {"daemonize", no_argument, 0, 'd'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int n_in = 0, n_out = 0;
  int c;

  memset(cfg, 0, sizeof *cfg);
  cfg->profile = RIST_PROF_SIMPLE;
  optind = 1;
  while ((c = getopt_long(argc, argv, "i:o:I:kvdh", longopts, NULL)) != -1) {
    switch (c) {
      case 'i':
        if (parse_endpoint_uri(optarg, &cfg->in, 0, &n_in)) {
          argerr("invalid -i: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 'o':
        if (parse_endpoint_uri(optarg, &cfg->out, 1, &n_out)) {
          argerr("invalid -o: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case 'I':
        cfg->iface = optarg;
        break;
      case 'k':
        cfg->insecure_tls = 1;
        break;
      case 1000: {
        static const enum_map_t map[] = {{"simple", RIST_PROF_SIMPLE}, {"main", RIST_PROF_MAIN}};
        int v;
        if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
          argerr("invalid --profile: %s (simple|main)", optarg);
          return ARGS_ERR;
        }
        cfg->profile = (rist_profile_sel_t)v;
        break;
      }
      case 1001:
        if (bufcpy(cfg->secret, sizeof cfg->secret, optarg) >= sizeof cfg->secret) {
          argerr("--secret too long");
          return ARGS_ERR;
        }
        break;
      case 1002:
        if (bufcpy(cfg->cname, sizeof cfg->cname, optarg) >= sizeof cfg->cname) {
          argerr("--cname too long");
          return ARGS_ERR;
        }
        break;
      case 1003: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0 || v > 60000) {
          argerr("invalid --buffer: %s (1..60000 ms)", optarg);
          return ARGS_ERR;
        }
        cfg->buffer_ms = (unsigned)v;
        break;
      }
      case 1004: {
        log_color_t v;
        if (log_color_from_string(optarg, &v)) {
          argerr("invalid --color: %s (auto|always|never)", optarg);
          return ARGS_ERR;
        }
        cfg->color_mode = v;
        break;
      }
      case 1005:
        cfg->metrics_sock = optarg;
        break;
      case 1006:
        cfg->metrics_id = optarg;
        break;
      case 1007: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0 || v > 86400UL) {
          argerr("invalid --metrics-interval: %s (seconds, 1..86400)", optarg);
          return ARGS_ERR;
        }
        cfg->metrics_interval_s = (unsigned)v;
        break;
      }
      case 'v':
        cfg->verbose = 1;
        break;
      case 'd':
        cfg->daemonize = 1;
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
  if (!n_in) {
    argerr("missing -i input");
    return ARGS_ERR;
  }
  if (!n_out) {
    argerr("missing -o output");
    return ARGS_ERR;
  }
  if (cfg->in.is_rist == cfg->out.is_rist) {
    argerr("exactly one of -i/-o must be rist://, the other a plain endpoint");
    return ARGS_ERR;
  }
  if (cfg->secret[0] && cfg->profile != RIST_PROF_MAIN) {
    argerr("--secret requires --profile main");
    return ARGS_ERR;
  }
  if ((cfg->metrics_sock || cfg->metrics_interval_s) && !cfg->metrics_id) {
    argerr("--metrics/--metrics-interval require --metrics-id");
    return ARGS_ERR;
  }
  if (cfg->insecure_tls && !(cfg->in.nonrist.kind == NONRIST_HTTP && cfg->in.nonrist.http.tls))
    log_line(TOOL_NAME ": --insecure has no effect, no -i https:// source");
  return ARGS_OK;
}
