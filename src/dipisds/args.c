/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
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

static int mcast_parse(const char *s, config_t *cfg) {
  return uriparse_mcast_addrport(s, &cfg->family, cfg->mcast_group, sizeof cfg->mcast_group, &cfg->mcast_port);
}

static int ret_addr_parse(const char *s, char *addr_out, size_t addr_cap, unsigned *port_out) {
  int family;
  return argutil_addrport_parse(s, &family, addr_out, addr_cap, port_out);
}

void mcast_describe(const config_t *cfg, char *buf, size_t n) {
  uriparse_mcast_describe(cfg->family, cfg->mcast_group, cfg->mcast_port, buf, n);
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
      "      --ret-rsi-mc-ret    a: RSI (F.5.3) rides the MC RET session, not the default\n"
      "                          session; requires --ret-mc, matches dipifccret --rsi-mc-ret\n"
      "      --fcc-addr <a>:<p>  a: advertise a dipifccret FCC server (its -l value);\n"
      "                          opt-in, adds ServerBasedEnhancementServiceInfo to every service\n"
      "      --fcc-rtx-time <ms> a: FCC Retransmission_session rtx-time (default 2000)\n"
      "      --fcc-rtx-pt <n>    a: FCC RTP payload type, matches dipifccret -R (default 99)\n"
      "      --fcc-resolve-by-port     a: per-service FCC port instead of --fcc-addr's port,\n"
      "                          matches dipifccret --fcc-resolve-by-port\n"
      "      --fcc-resolve-base-port <p> a: matches dipifccret --fcc-resolve-base-port\n"
      "      --fcc-resolve-max-channels <n> a: port hash modulus for resolve-by-port (default 300)\n"
      "      --metrics <path>    a: Unix datagram socket for metrics (default: /run/dvbipitools/metrics.sock)\n"
      "      --metrics-id <name> a: stable instance id; metrics disabled unless set\n"
      "      --metrics-interval <s> a: snapshot interval in seconds (default: 5)\n"
      "      --packages <path>   a: Package Discovery from id,name,lang,visible,svc1|svc2|... lines\n"
      "      --cells <path>      a: Regionalisation Discovery from id,country,type:value,... lines\n"
      "      --rms-name <name>   a: RMS Discovery display name; requires --rms-location\n"
      "      --rms-lang <code>   a: ISO 639-2 for --rms-name (default deu)\n"
      "      --rms-location <uri> a: RMSType RMSLocation\n"
      "      --rms-logo <uri>    a: RMSType LogoURI\n"
      "      --fus-name <name>   a: FUS Discovery display name; requires --fus-id\n"
      "      --fus-lang <code>   a: ISO 639-2 for --fus-name (default deu)\n"
      "      --fus-id <n>        a: FUSID (decimal)\n"
      "      --fus-announce <a>:<p> a: FUS MulticastAnnouncementAddress\n"
      "      --fus-logo <uri>    a: FUSType LogoURI\n"
      "  -d, --daemonize         fork to background after startup, detach from terminal\n"
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
      {"ret-rsi-mc-ret", no_argument, 0, 1012},
      {"fcc-addr", required_argument, 0, 1006},
      {"fcc-rtx-time", required_argument, 0, 1007},
      {"fcc-rtx-pt", required_argument, 0, 1008},
      {"fcc-resolve-by-port", no_argument, 0, 1013},
      {"fcc-resolve-base-port", required_argument, 0, 1014},
      {"fcc-resolve-max-channels", required_argument, 0, 1015},
      {"metrics", required_argument, 0, 1009},
      {"metrics-id", required_argument, 0, 1010},
      {"metrics-interval", required_argument, 0, 1011},
      {"packages", required_argument, 0, 1016},
      {"cells", required_argument, 0, 1017},
      {"rms-name", required_argument, 0, 1018},
      {"rms-lang", required_argument, 0, 1019},
      {"rms-location", required_argument, 0, 1020},
      {"rms-logo", required_argument, 0, 1021},
      {"fus-name", required_argument, 0, 1022},
      {"fus-lang", required_argument, 0, 1023},
      {"fus-id", required_argument, 0, 1024},
      {"fus-announce", required_argument, 0, 1025},
      {"fus-logo", required_argument, 0, 1026},
      {"daemonize", no_argument, 0, 'd'},
      {"help", no_argument, 0, 'h'},
      {0, 0, 0, 0}};
  int have_a = 0, have_l = 0, have_mcast = 0, have_t = 0;
  int have_ret_rtx_time = 0, have_ret_rtx_pt = 0, have_ret_mc_port = 0;
  int have_fcc_rtx_time = 0, have_fcc_rtx_pt = 0, have_fcc_resolve_max_channels = 0;
  int have_rms_lang = 0, have_fus_lang = 0, have_fus_id = 0;
  long t_value = 0;
  int c;

  memset(cfg, 0, sizeof *cfg);
  cfg->format = OUT_M3U;
  optind = 1;
  while ((c = getopt_long(argc, argv, "ali:p:O:L:m:I:t:o:f:vdh", longopts, NULL)) != -1) {
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
    case 'd':
      cfg->daemonize = 1;
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
    case 1012:
      cfg->ret_rsi_mc_ret = 1;
      break;
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
    case 1013:
      cfg->fcc_resolve_by_port = 1;
      break;
    case 1014: {
      unsigned v;
      if (argutil_port_parse(optarg, &v)) {
        argerr("invalid --fcc-resolve-base-port: %s", optarg);
        return ARGS_ERR;
      }
      cfg->fcc_resolve_base_port = v;
      break;
    }
    case 1015: {
      char *end;
      unsigned long v = strtoul(optarg, &end, 10);
      if (*end != '\0' || v == 0) {
        argerr("invalid --fcc-resolve-max-channels: %s", optarg);
        return ARGS_ERR;
      }
      cfg->fcc_resolve_max_channels = (size_t)v;
      have_fcc_resolve_max_channels = 1;
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
    case 1016:
      cfg->packages_path = optarg;
      break;
    case 1017:
      cfg->cells_path = optarg;
      break;
    case 1018:
      cfg->rms_name = optarg;
      cfg->rms_enabled = 1;
      break;
    case 1019:
      if (strlen(optarg) != 3) {
        argerr("invalid --rms-lang: %s (3-letter ISO 639-2 code)", optarg);
        return ARGS_ERR;
      }
      memcpy(cfg->rms_lang, optarg, 3);
      have_rms_lang = 1;
      break;
    case 1020:
      cfg->rms_location = optarg;
      break;
    case 1021:
      cfg->rms_logo = optarg;
      break;
    case 1022:
      cfg->fus_name = optarg;
      cfg->fus_enabled = 1;
      break;
    case 1023:
      if (strlen(optarg) != 3) {
        argerr("invalid --fus-lang: %s (3-letter ISO 639-2 code)", optarg);
        return ARGS_ERR;
      }
      memcpy(cfg->fus_lang, optarg, 3);
      have_fus_lang = 1;
      break;
    case 1024: {
      char *end;
      unsigned long v = strtoul(optarg, &end, 10);
      if (*end != '\0') {
        argerr("invalid --fus-id: %s", optarg);
        return ARGS_ERR;
      }
      cfg->fus_id = v;
      have_fus_id = 1;
      break;
    }
    case 1025:
      if (ret_addr_parse(optarg, cfg->fus_announce_addr, sizeof cfg->fus_announce_addr, &cfg->fus_announce_port)) {
        argerr("invalid --fus-announce: %s", optarg);
        return ARGS_ERR;
      }
      break;
    case 1026:
      cfg->fus_logo = optarg;
      break;
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
    if (!cfg->ret_enabled && (have_ret_rtx_time || have_ret_rtx_pt || cfg->ret_mc || have_ret_mc_port || cfg->ret_rsi_mc_ret)) {
      argerr("--ret-rtx-time/--ret-rtx-pt/--ret-mc/--ret-mc-port/--ret-rsi-mc-ret require --ret-addr");
      return ARGS_ERR;
    }
    if (cfg->ret_rsi_mc_ret && !cfg->ret_mc) {
      argerr("--ret-rsi-mc-ret requires --ret-mc");
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
    if (!cfg->fcc_enabled && (have_fcc_rtx_time || have_fcc_rtx_pt || cfg->fcc_resolve_by_port || cfg->fcc_resolve_base_port || have_fcc_resolve_max_channels)) {
      argerr("--fcc-rtx-time/--fcc-rtx-pt/--fcc-resolve-* require --fcc-addr");
      return ARGS_ERR;
    }
    if (cfg->fcc_enabled) {
      if (!have_fcc_rtx_time)
        cfg->fcc_rtx_time = 2000;
      if (!have_fcc_rtx_pt)
        cfg->fcc_rtx_pt = 99;
      if (!have_fcc_resolve_max_channels)
        cfg->fcc_resolve_max_channels = 300;
    }
    if ((cfg->packages_path || cfg->cells_path || cfg->rms_enabled || cfg->fus_enabled) && has_suffix(cfg->input_path, ".xml")) {
      argerr("--packages/--cells/--rms-name/--fus-name have no effect with a raw .xml -i input (that path is sent through unparsed)");
      return ARGS_ERR;
    }
    if (cfg->rms_enabled && cfg->fus_enabled) {
      argerr("--rms-name and --fus-name are mutually exclusive (RMSFUSDiscovery carries one or the other, never both)");
      return ARGS_ERR;
    }
    if (!cfg->rms_enabled && (have_rms_lang || cfg->rms_location || cfg->rms_logo)) {
      argerr("--rms-lang/--rms-location/--rms-logo require --rms-name");
      return ARGS_ERR;
    }
    if (cfg->rms_enabled) {
      if (!cfg->rms_location) {
        argerr("--rms-name requires --rms-location");
        return ARGS_ERR;
      }
      if (!have_rms_lang)
        memcpy(cfg->rms_lang, "deu", 3);
    }
    if (!cfg->fus_enabled && (have_fus_lang || have_fus_id || cfg->fus_announce_addr[0] || cfg->fus_logo)) {
      argerr("--fus-lang/--fus-id/--fus-announce/--fus-logo require --fus-name");
      return ARGS_ERR;
    }
    if (cfg->fus_enabled) {
      if (!have_fus_id) {
        argerr("--fus-name requires --fus-id");
        return ARGS_ERR;
      }
      if (!have_fus_lang)
        memcpy(cfg->fus_lang, "deu", 3);
    }
  } else {
    if (cfg->ret_enabled || have_ret_rtx_time || have_ret_rtx_pt || cfg->ret_mc || have_ret_mc_port || cfg->ret_rsi_mc_ret) {
      argerr("--ret-* options are announce-only");
      return ARGS_ERR;
    }
    if (cfg->fcc_enabled || have_fcc_rtx_time || have_fcc_rtx_pt || cfg->fcc_resolve_by_port || cfg->fcc_resolve_base_port || have_fcc_resolve_max_channels) {
      argerr("--fcc-* options are announce-only");
      return ARGS_ERR;
    }
    if (cfg->metrics_id) {
      argerr("--metrics-id is announce-only");
      return ARGS_ERR;
    }
    if (cfg->packages_path || cfg->cells_path || cfg->rms_enabled || have_rms_lang || cfg->rms_location || cfg->rms_logo ||
        cfg->fus_enabled || have_fus_lang || have_fus_id || cfg->fus_announce_addr[0] || cfg->fus_logo) {
      argerr("--packages/--cells/--rms-*/--fus-* options are announce-only");
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
