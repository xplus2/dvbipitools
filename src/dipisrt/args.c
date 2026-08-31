/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/uriparse.h"

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
static int parse_direct(const char *rest, nonsrt_t *s) {
  if (*rest == '@')
    rest++;
  return uriparse_mcast_addrport(rest, &s->family, s->group, sizeof s->group, &s->port);
}

static int parse_nonsrt(const char *uri, nonsrt_t *s, int is_sink) {
  memset(s, 0, sizeof *s);
  if (strcmp(uri, "-") == 0) {
    s->kind = NONSRT_FILE; /* file_path[0] == '\0': stdin (source) / stdout (sink) */
    return 0;
  }
  if (strncmp(uri, "rtp://", 6) == 0) {
    s->kind = NONSRT_RTP;
    s->rtp_wrapped = 1;
    return parse_direct(uri + 6, s);
  }
  if (strncmp(uri, "udp://", 6) == 0) {
    s->kind = NONSRT_UDP;
    s->rtp_wrapped = 0;
    return parse_direct(uri + 6, s);
  }
  if (strncmp(uri, "http://", 7) == 0 || strncmp(uri, "https://", 8) == 0) {
    if (is_sink)
      return -1; /* an HTTP TS source makes no sense as an output */
    s->kind = NONSRT_HTTP;
    return http_url_parse(uri, &s->http);
  }
  if (strlen(uri) >= sizeof s->file_path)
    return -1;
  s->kind = NONSRT_FILE;
  bufcpy(s->file_path, sizeof s->file_path, uri);
  return 0;
}

/* count: prior calls for this -i/-o (caller's n_in/n_out). first call: decides is_srt.
   later calls: must match, or rejected. bonded srt:// peers: must agree on @ (listen) */
static int parse_endpoint_uri(const char *uri, endpoint_t *e, int is_sink, int *count) {
  int is_srt_uri = strncmp(uri, "srt://", 6) == 0;
  if (*count == 0) {
    e->is_srt = is_srt_uri;
    e->n_srt = 0;
  } else if (!e->is_srt || !is_srt_uri) {
    return -1;
  }

  if (e->is_srt) {
    const char *rest = uri + 6;
    int has_at = *rest == '@';
    int family;
    char host[64];
    unsigned port;

    if (has_at)
      rest++;
    if (e->n_srt >= SRTCOMMON_MAX_PEERS)
      return -1;
    if (argutil_addrport_parse(rest, &family, host, sizeof host, &port))
      return -1;
    if (e->n_srt == 0)
      e->listen = has_at;
    else if (e->listen != has_at)
      return -1; /* mixed caller/listener within one bonded endpoint makes no sense */
    e->family[e->n_srt] = family;
    bufcpy(e->srt_host[e->n_srt], sizeof e->srt_host[0], host);
    e->srt_port[e->n_srt] = port;
    e->n_srt++;
  } else if (parse_nonsrt(uri, &e->nonsrt, is_sink)) {
    return -1;
  }
  (*count)++;
  return 0;
}

int config_is_sender(const config_t *cfg) {
  return cfg->out.is_srt;
}

void endpoint_describe(const endpoint_t *e, char *buf, size_t n) {
  if (e->is_srt) {
    char first[96];
    if (e->family[0] == AF_INET6)
      snprintf(first, sizeof first, "srt://%s[%s]:%u", e->listen ? "@" : "", e->srt_host[0], e->srt_port[0]);
    else
      snprintf(first, sizeof first, "srt://%s%s:%u", e->listen ? "@" : "", e->srt_host[0], e->srt_port[0]);
    if (e->n_srt == 1)
      bufcpy(buf, n, first);
    else
      snprintf(buf, n, "%s +%d more", first, e->n_srt - 1);
    return;
  }
  switch (e->nonsrt.kind) {
  case NONSRT_RTP:
  case NONSRT_UDP: {
    const char *scheme = (e->nonsrt.kind == NONSRT_RTP) ? "rtp" : "udp";
    if (e->nonsrt.family == AF_INET6)
      snprintf(buf, n, "%s://@[%s]:%u", scheme, e->nonsrt.group, e->nonsrt.port);
    else
      snprintf(buf, n, "%s://@%s:%u", scheme, e->nonsrt.group, e->nonsrt.port);
    break;
  }
  case NONSRT_HTTP:
    snprintf(buf, n, "%s://%s:%u%s", e->nonsrt.http.tls ? "https" : "http", e->nonsrt.http.host, e->nonsrt.http.port,
              e->nonsrt.http.path);
    break;
  case NONSRT_FILE:
    bufcpy(buf, n, e->nonsrt.file_path[0] ? e->nonsrt.file_path : "- (stdin/stdout)");
    break;
  }
}

static void print_help(void) {
  printf(
      "usage: %s -i <uri> -o <uri> [options]\n\n"
      "bridge a DVB-IPI stream between plain RTP/UDP/file and an SRT link, either\n"
      "direction: exactly one of -i/-o must be srt://, the other a regular\n"
      "dipirec-style endpoint\n\n"
      "endpoints:\n"
      "  srt://<host>:<port>           SRT peer, calls out (caller)\n"
      "  srt://@<host>:<port>          SRT peer, binds/listens/accepts (listener)\n"
      "                                repeat -i/-o to bond several links (--group-mode)\n"
      "                                <host> is a numeric IP, not a hostname\n"
      "  rtp://@<group>:<port>         RTP wrapped SPTS multicast (@ optional)\n"
      "  udp://@<group>:<port>         raw SPTS multicast (@ optional)\n"
      "  http://<host>:<port>/<path>   HTTP TS stream, -i only\n"
      "  https://<host>:<port>/<path>  same, TLS (-k skips verification), -i only\n"
      "  -                             stdin (-i) or stdout (-o)\n"
      "  <path>                        a file\n"
      "  IPv6 addrs/groups in brackets, e.g. srt://@[::1]:9000, rtp://@[ff3e::1]:8700\n\n"
      "options:\n"
      "  -i, --in <uri>            input (see above), repeatable if srt://\n"
      "  -o, --out <uri>           output (see above), repeatable if srt://\n"
      "  -I, --iface <iface>       interface for the non-SRT side's multicast join/send\n"
      "  -k, --insecure            skip TLS verification, -i https:// only\n"
      "      --group-mode <mode>   broadcast|backup; required when bonding (repeated srt://)\n"
      "      --rendezvous          srt_rendezvous() instead of connect/listen; needs --local,\n"
      "                            not combinable with @ or --group-mode\n"
      "      --local <host:port>   local bind address for --rendezvous\n"
      "      --passphrase <pw>     encryption passphrase, 10..79 chars\n"
      "      --pbkeylen <n>        16|24|32 (AES key length); default 16 if --passphrase set\n"
      "      --streamid <id>       SRTO_STREAMID, passed to a listening peer on accept\n"
      "      --packetfilter <cfg>  SRTO_PACKETFILTER config string, e.g. fec,cols:10,rows:5\n"
      "      --latency <ms>        SRTO_LATENCY; default library\n"
      "      --send-buffer-mult <n> sender queue depth in latency windows, 1..32; default 4\n"
      "      --color <when>        auto|always|never (default auto)\n"
      "      --metrics <path>       Unix datagram socket for metrics (default: /run/dvbipitools/metrics.sock)\n"
      "      --metrics-id <name>    stable instance id; metrics disabled unless set\n"
      "      --metrics-interval <s> snapshot interval in seconds (default: 5)\n"
      "  -v, --verbose              periodic bridge stats on stderr\n"
      "  -d, --daemonize            fork to background after startup, detach from terminal\n"
      "  -h, --help                 this help\n\n"
      "examples:\n"
      "  %s -i rtp://@239.1.1.1:5000 -o srt://1.2.3.4:9000 --latency 200\n"
      "  %s -i srt://@0.0.0.0:9000 -o rtp://@239.1.1.1:5000\n"
      "  %s -i rtp://@239.1.1.1:5000 -o srt://1.2.3.4:9000 -o srt://5.6.7.8:9000 --group-mode broadcast\n",
      TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME);
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  static const struct option longopts[] = {
      {"in", required_argument, 0, 'i'},
      {"out", required_argument, 0, 'o'},
      {"iface", required_argument, 0, 'I'},
      {"insecure", no_argument, 0, 'k'},
      {"group-mode", required_argument, 0, 1000},
      {"rendezvous", no_argument, 0, 1001},
      {"local", required_argument, 0, 1002},
      {"passphrase", required_argument, 0, 1003},
      {"pbkeylen", required_argument, 0, 1004},
      {"streamid", required_argument, 0, 1005},
      {"packetfilter", required_argument, 0, 1006},
      {"latency", required_argument, 0, 1007},
      {"send-buffer-mult", required_argument, 0, 1012},
      {"color", required_argument, 0, 1008},
      {"metrics", required_argument, 0, 1009},
      {"metrics-id", required_argument, 0, 1010},
      {"metrics-interval", required_argument, 0, 1011},
      {"verbose", no_argument, 0, 'v'},
      {"daemonize", no_argument, 0, 'd'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int n_in = 0;
  int n_out = 0;
  int c;

  memset(cfg, 0, sizeof *cfg);
  cfg->group_mode = SRTGROUP_NONE;
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
#ifndef DIPISRT_HAVE_BONDING
        argerr("--group-mode needs a libsrt built with bonding support (ENABLE_BONDING=ON)");
        return ARGS_ERR;
#else
        static const enum_map_t map[] = {{"broadcast", SRTGROUP_BROADCAST}, {"backup", SRTGROUP_BACKUP}};
        int v;
        if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
          argerr("invalid --group-mode: %s (broadcast|backup)", optarg);
          return ARGS_ERR;
        }
        cfg->group_mode = (srtgroup_mode_t)v;
        break;
#endif
      }
      case 1001:
        cfg->rendezvous = 1;
        break;
      case 1002: {
        int family_unused;
        if (argutil_addrport_parse(optarg, &family_unused, cfg->local_host, sizeof cfg->local_host, &cfg->local_port)) {
          argerr("invalid --local: %s", optarg);
          return ARGS_ERR;
        }
        break;
      }
      case 1003:
        if (bufcpy(cfg->passphrase, sizeof cfg->passphrase, optarg) >= sizeof cfg->passphrase) {
          argerr("--passphrase too long");
          return ARGS_ERR;
        }
        break;
      case 1004: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || (v != 16 && v != 24 && v != 32)) {
          argerr("invalid --pbkeylen: %s (16|24|32)", optarg);
          return ARGS_ERR;
        }
        cfg->pbkeylen = (int)v;
        break;
      }
      case 1005:
        if (bufcpy(cfg->streamid, sizeof cfg->streamid, optarg) >= sizeof cfg->streamid) {
          argerr("--streamid too long");
          return ARGS_ERR;
        }
        break;
      case 1006:
        if (bufcpy(cfg->packetfilter, sizeof cfg->packetfilter, optarg) >= sizeof cfg->packetfilter) {
          argerr("--packetfilter too long");
          return ARGS_ERR;
        }
        break;
      case 1007: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0 || v > 60000) {
          argerr("invalid --latency: %s (1..60000 ms)", optarg);
          return ARGS_ERR;
        }
        cfg->latency_ms = (unsigned)v;
        break;
      }
      case 1012: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0' || v == 0 || v > 32) {
          argerr("invalid --send-buffer-mult: %s (1..32)", optarg);
          return ARGS_ERR;
        }
        cfg->send_buffer_mult = (unsigned)v;
        break;
      }
      case 1008: {
        log_color_t v;
        if (log_color_from_string(optarg, &v)) {
          argerr("invalid --color: %s (auto|always|never)", optarg);
          return ARGS_ERR;
        }
        cfg->color_mode = v;
        break;
      }
      case 1009:
        cfg->metrics_sock = optarg;
        break;
      case 1010:
        cfg->metrics_id = optarg;
        break;
      case 1011: {
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
  if (cfg->in.is_srt == cfg->out.is_srt) {
    argerr("exactly one of -i/-o must be srt://, the other a plain endpoint");
    return ARGS_ERR;
  }
  {
    const endpoint_t *srt_ep = cfg->in.is_srt ? &cfg->in : &cfg->out;
    if (srt_ep->n_srt > 1 && cfg->group_mode == SRTGROUP_NONE) {
      argerr("bonding several srt:// peers requires --group-mode");
      return ARGS_ERR;
    }
    if (srt_ep->n_srt == 1 && cfg->group_mode != SRTGROUP_NONE) {
      argerr("--group-mode has no effect with a single srt:// peer");
      return ARGS_ERR;
    }
    if (cfg->rendezvous) {
      if (srt_ep->listen) {
        argerr("--rendezvous is not combinable with srt://@ (listener)");
        return ARGS_ERR;
      }
      if (cfg->group_mode != SRTGROUP_NONE) {
        argerr("--rendezvous is not combinable with --group-mode");
        return ARGS_ERR;
      }
      if (!cfg->local_host[0] || !cfg->local_port) {
        argerr("--rendezvous requires --local <host:port>");
        return ARGS_ERR;
      }
    }
  }
  if (cfg->passphrase[0] && (strlen(cfg->passphrase) < 10 || strlen(cfg->passphrase) > 79)) {
    argerr("--passphrase must be 10..79 characters");
    return ARGS_ERR;
  }
  if (cfg->pbkeylen && !cfg->passphrase[0]) {
    argerr("--pbkeylen requires --passphrase");
    return ARGS_ERR;
  }
  if ((cfg->metrics_sock || cfg->metrics_interval_s) && !cfg->metrics_id) {
    argerr("--metrics/--metrics-interval require --metrics-id");
    return ARGS_ERR;
  }
  if (cfg->insecure_tls && !(cfg->in.nonsrt.kind == NONSRT_HTTP && cfg->in.nonsrt.http.tls))
    log_line(TOOL_NAME ": --insecure has no effect, no -i https:// source");
  if (cfg->send_buffer_mult && !config_is_sender(cfg))
    log_line(TOOL_NAME ": --send-buffer-mult has no effect, no -o srt:// sender side");
  return ARGS_OK;
}
