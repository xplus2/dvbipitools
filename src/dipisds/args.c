/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/argutil.h"
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

static int mcast_parse(const char *s, config_t *cfg) {
  if (argutil_addrport_parse(s, &cfg->family, cfg->mcast_group, sizeof cfg->mcast_group, &cfg->mcast_port))
    return -1;

  if (cfg->family == AF_INET) {
    struct in_addr a;
    inet_pton(AF_INET, cfg->mcast_group, &a);
    if ((ntohl(a.s_addr) >> 28) != 0xE)
      return -1;
  } else {
    struct in6_addr a6;
    inet_pton(AF_INET6, cfg->mcast_group, &a6);
    if (a6.s6_addr[0] != 0xFF)
      return -1;
  }
  return 0;
}

static int ret_addr_parse(const char *s, char *addr_out, size_t addr_cap, unsigned *port_out) {
  int family;
  return argutil_addrport_parse(s, &family, addr_out, addr_cap, port_out);
}

void mcast_describe(const config_t *cfg, char *buf, size_t n) {
  if (cfg->family == AF_INET6)
    snprintf(buf, n, "[%s]:%u", cfg->mcast_group, cfg->mcast_port);
  else
    snprintf(buf, n, "%s:%u", cfg->mcast_group, cfg->mcast_port);
}

static int has_suffix(const char *s, const char *sfx) {
  size_t ls = strlen(s), lx = strlen(sfx);
  return ls >= lx && !strcmp(s + ls - lx, sfx);
}

static void print_help(void) {
  printf(
      "usage: %s -a -i <path> -m <mcast>:<port> [options]\n"
      "       %s -l -m <mcast>:<port> [options]\n\n"
      "DVBSTP / SD&S (ETSI TS 102 034) service discovery: announce a service list on\n"
      "multicast, or listen for one and write a playlist\n\n"
      "options:\n"
      "  -a, --announce          headend mode: read -i, transmit on -m\n"
      "  -l, --listen            client mode: receive on -m, write -o\n"
      "  -i, --input <path>      a: .csv/.m3u/.xspf playlist or raw SD&S .xml\n"
      "  -p, --provider <name>   a: DomainName (required unless -i is .xml)\n"
      "  -O, --offering <name>   a: display name (required unless -i is .xml)\n"
      "  -L, --lang <code>       a: ISO 639-2 for the display name (default deu)\n"
      "  -m, --mcast <g>:<p>     multicast group:port ([addr6]:port for v6)\n"
      "  -I, --iface <iface>     multicast interface\n"
      "  -t, --interval <s>      a: repeat interval (default 5)\n"
      "  -t, --timeout <s>       l: stop after N seconds (default 35)\n"
      "  -o, --output <path>     l: output path, - for stdout (default)\n"
      "  -f, --format <fmt>      l: m3u|csv|xspf|xml|null (default from -o suffix)\n"
      "  -v, --verbose           periodic stats on stderr\n"
      "      --color <when>      auto|always|never (default auto)\n"
      "      --ret-addr <a>:<p>  a: advertise a dipifccret RET server (its -l value);\n"
      "                          opt-in, adds RTPRetransmission to every announced service\n"
      "      --ret-rtx-time <ms> a: RET rtx-time, matches dipifccret -B (default 2000)\n"
      "      --ret-rtx-pt <n>    a: RET RTP payload type, matches dipifccret -R (default 99)\n"
      "      --ret-mc            a: also advertise multicast RET (dipifccret without --no-mc-ret)\n"
      "      --ret-mc-port <p>   a: multicast RET port, matches dipifccret -F (default: each\n"
      "                          service's own port)\n"
      "      --fcc-addr <a>:<p>  a: advertise a dipifccret FCC server (its -l value);\n"
      "                          opt-in, adds ServerBasedEnhancementServiceInfo to every service\n"
      "      --fcc-rtx-time <ms> a: FCC Retransmission_session rtx-time (default 2000)\n"
      "      --fcc-rtx-pt <n>    a: FCC RTP payload type, matches dipifccret -R (default 99)\n"
      "      --metrics <path>    a: Unix datagram socket for metrics (default: /run/dvbipitools/metrics.sock)\n"
      "      --metrics-id <name> a: stable instance id; metrics disabled unless set\n"
      "      --metrics-interval <s> a: snapshot interval in seconds (default: 5)\n"
      "  -h, --help              this help\n\n"
      "examples:\n"
      "  %s -a -i channels.csv -p example.org -O \"My Headend\" -m 239.255.0.1:3937\n"
      "  %s -l -m 239.255.0.1:3937 -o discovered.m3u\n",
      TOOL_NAME, TOOL_NAME, TOOL_NAME, TOOL_NAME);
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  static const struct option longopts[] = {
      {"announce", no_argument, 0, 'a'},
      {"listen", no_argument, 0, 'l'},
      {"input", required_argument, 0, 'i'},
      {"provider", required_argument, 0, 'p'},
      {"offering", required_argument, 0, 'O'},
      {"lang", required_argument, 0, 'L'},
      {"mcast", required_argument, 0, 'm'},
      {"iface", required_argument, 0, 'I'},
      {"interval", required_argument, 0, 't'},
      {"timeout", required_argument, 0, 't'},
      {"output", required_argument, 0, 'o'},
      {"format", required_argument, 0, 'f'},
      {"verbose", no_argument, 0, 'v'},
      {"color", required_argument, 0, 1000},
      {"ret-addr", required_argument, 0, 1001},
      {"ret-rtx-time", required_argument, 0, 1002},
      {"ret-rtx-pt", required_argument, 0, 1003},
      {"ret-mc", no_argument, 0, 1004},
      {"ret-mc-port", required_argument, 0, 1005},
      {"fcc-addr", required_argument, 0, 1006},
      {"fcc-rtx-time", required_argument, 0, 1007},
      {"fcc-rtx-pt", required_argument, 0, 1008},
      {"metrics", required_argument, 0, 1009},
      {"metrics-id", required_argument, 0, 1010},
      {"metrics-interval", required_argument, 0, 1011},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int have_a = 0, have_l = 0, have_mcast = 0, have_t = 0;
  int have_ret_rtx_time = 0, have_ret_rtx_pt = 0, have_ret_mc_port = 0;
  int have_fcc_rtx_time = 0, have_fcc_rtx_pt = 0;
  long t_value = 0;
  int c;

  memset(cfg, 0, sizeof *cfg);
  cfg->format = OUT_M3U;
  optind = 1;
  while ((c = getopt_long(argc, argv, "ali:p:O:L:m:I:t:o:f:vh", longopts, NULL)) != -1) {
    switch (c) {
    case 'a':
      have_a = 1;
      cfg->mode = MODE_ANNOUNCE;
      break;
    case 'l':
      have_l = 1;
      cfg->mode = MODE_LISTEN;
      break;
    case 'i':
      cfg->input_path = optarg;
      break;
    case 'p':
      cfg->provider = optarg;
      break;
    case 'O':
      cfg->offering = optarg;
      break;
    case 'L':
      if (strlen(optarg) != 3) {
        argerr("invalid -L lang: %s (3-letter ISO 639-2 code)", optarg);
        return ARGS_ERR;
      }
      memcpy(cfg->lang, optarg, 3);
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
    case 't': {
      char *end;
      long v = strtol(optarg, &end, 10);
      if (*end != '\0' || v < 0) {
        argerr("invalid -t seconds: %s", optarg);
        return ARGS_ERR;
      }
      t_value = v;
      have_t = 1;
      break;
    }
    case 'o':
      cfg->output_path = optarg;
      break;
    case 'f': {
      static const enum_map_t map[] = {{"m3u", OUT_M3U}, {"csv", OUT_CSV}, {"xspf", OUT_XSPF}, {"xml", OUT_XML}, {"null", OUT_NULL}};
      int v;
      if (map_lookup(map, sizeof map / sizeof map[0], optarg, &v)) {
        argerr("invalid --format: %s (m3u|csv|xspf|xml|null)", optarg);
        return ARGS_ERR;
      }
      cfg->format = (out_fmt_t)v;
      break;
    }
    case 'v':
      cfg->verbose = 1;
      break;
    case 1000: {
      log_color_t v;
      if (log_color_from_string(optarg, &v)) {
        argerr("invalid --color: %s (auto|always|never)", optarg);
        return ARGS_ERR;
      }
      cfg->color_mode = v;
      break;
    }
    case 1001:
      if (ret_addr_parse(optarg, cfg->ret_addr, sizeof cfg->ret_addr, &cfg->ret_port)) {
        argerr("invalid --ret-addr: %s", optarg);
        return ARGS_ERR;
      }
      cfg->ret_enabled = 1;
      break;
    case 1002: {
      char *end;
      unsigned long v = strtoul(optarg, &end, 10);
      if (*end != '\0' || v == 0) {
        argerr("invalid --ret-rtx-time: %s", optarg);
        return ARGS_ERR;
      }
      cfg->ret_rtx_time = (unsigned)v;
      have_ret_rtx_time = 1;
      break;
    }
    case 1003: {
      char *end;
      unsigned long v = strtoul(optarg, &end, 10);
      if (*end != '\0' || v > 127) {
        argerr("invalid --ret-rtx-pt: %s (0..127)", optarg);
        return ARGS_ERR;
      }
      cfg->ret_rtx_pt = (unsigned char)v;
      have_ret_rtx_pt = 1;
      break;
    }
    case 1004:
      cfg->ret_mc = 1;
      break;
    case 1005: {
      unsigned v;
      if (argutil_port_parse(optarg, &v)) {
        argerr("invalid --ret-mc-port: %s", optarg);
        return ARGS_ERR;
      }
      cfg->ret_mc_port = v;
      have_ret_mc_port = 1;
      break;
    }
    case 1006:
      if (ret_addr_parse(optarg, cfg->fcc_addr, sizeof cfg->fcc_addr, &cfg->fcc_port)) {
        argerr("invalid --fcc-addr: %s", optarg);
        return ARGS_ERR;
      }
      cfg->fcc_enabled = 1;
      break;
    case 1007: {
      char *end;
      unsigned long v = strtoul(optarg, &end, 10);
      if (*end != '\0' || v == 0) {
        argerr("invalid --fcc-rtx-time: %s", optarg);
        return ARGS_ERR;
      }
      cfg->fcc_rtx_time = (unsigned)v;
      have_fcc_rtx_time = 1;
      break;
    }
    case 1008: {
      char *end;
      unsigned long v = strtoul(optarg, &end, 10);
      if (*end != '\0' || v > 127) {
        argerr("invalid --fcc-rtx-pt: %s (0..127)", optarg);
        return ARGS_ERR;
      }
      cfg->fcc_rtx_pt = (unsigned char)v;
      have_fcc_rtx_pt = 1;
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
    case 'h':
      print_help();
      return ARGS_HELP;
    default:
      return ARGS_ERR;
    }
  }
  if (optind < argc) {
    argerr("unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  if (have_a == have_l) {
    argerr("exactly one of -a/--announce or -l/--listen is required");
    return ARGS_ERR;
  }
  if (!have_mcast) {
    argerr("missing -m multicast group:port");
    return ARGS_ERR;
  }
  if ((cfg->metrics_sock || cfg->metrics_interval_s) && !cfg->metrics_id) {
    argerr("--metrics/--metrics-interval require --metrics-id");
    return ARGS_ERR;
  }

  if (cfg->mode == MODE_ANNOUNCE) {
    if (!cfg->input_path) {
      argerr("missing -i input");
      return ARGS_ERR;
    }
    if (!has_suffix(cfg->input_path, ".xml")) {
      if (!cfg->provider) {
        argerr("missing -p provider (required unless -i is .xml)");
        return ARGS_ERR;
      }
      if (!cfg->offering) {
        argerr("missing -O offering (required unless -i is .xml)");
        return ARGS_ERR;
      }
    }
    if (!cfg->lang[0])
      memcpy(cfg->lang, "deu", 3);
    cfg->interval_s = have_t ? t_value : 5;
    if (cfg->ret_enabled && has_suffix(cfg->input_path, ".xml")) {
      argerr("--ret-addr has no effect with a raw .xml -i input (that path is sent through unparsed)");
      return ARGS_ERR;
    }
    if (!cfg->ret_enabled && (have_ret_rtx_time || have_ret_rtx_pt || cfg->ret_mc || have_ret_mc_port)) {
      argerr("--ret-rtx-time/--ret-rtx-pt/--ret-mc/--ret-mc-port require --ret-addr");
      return ARGS_ERR;
    }
    if (cfg->ret_enabled) {
      if (!have_ret_rtx_time)
        cfg->ret_rtx_time = 2000;
      if (!have_ret_rtx_pt)
        cfg->ret_rtx_pt = 99;
    }
    if (cfg->fcc_enabled && has_suffix(cfg->input_path, ".xml")) {
      argerr("--fcc-addr has no effect with a raw .xml -i input (that path is sent through unparsed)");
      return ARGS_ERR;
    }
    if (!cfg->fcc_enabled && (have_fcc_rtx_time || have_fcc_rtx_pt)) {
      argerr("--fcc-rtx-time/--fcc-rtx-pt require --fcc-addr");
      return ARGS_ERR;
    }
    if (cfg->fcc_enabled) {
      if (!have_fcc_rtx_time)
        cfg->fcc_rtx_time = 2000;
      if (!have_fcc_rtx_pt)
        cfg->fcc_rtx_pt = 99;
    }
  } else {
    if (cfg->ret_enabled || have_ret_rtx_time || have_ret_rtx_pt || cfg->ret_mc || have_ret_mc_port) {
      argerr("--ret-* options are announce-only");
      return ARGS_ERR;
    }
    if (cfg->fcc_enabled || have_fcc_rtx_time || have_fcc_rtx_pt) {
      argerr("--fcc-* options are announce-only");
      return ARGS_ERR;
    }
    if (cfg->metrics_id) {
      argerr("--metrics-id is announce-only");
      return ARGS_ERR;
    }
    if (!cfg->output_path)
      cfg->output_path = "-";
    if (have_t)
      cfg->timeout_s = t_value;
    else
      cfg->timeout_s = 35;
  }
  return ARGS_OK;
}
