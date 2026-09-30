/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/argutil.h"
#include "lib/helper/log.h"
#include "lib/helper/uriparse.h"
#include "lib/net/netconnect.h"
#include "args.h"
#include "config.h"
#include "version.h"

#define OPT_COLOR 1000
#define OPT_RET_ADDR 1001
#define OPT_RET_RTX_TIME 1002
#define OPT_RET_RTX_PT 1003
#define OPT_RET_MC 1004
#define OPT_RET_MC_PORT 1005
#define OPT_RET_RSI_MC_RET 1012
#define OPT_FCC_ADDR 1006
#define OPT_FCC_RTX_TIME 1007
#define OPT_FCC_RTX_PT 1008
#define OPT_FCC_RESOLVE_BY_PORT 1013
#define OPT_FCC_RESOLVE_BASE_PORT 1014
#define OPT_FCC_RESOLVE_MAX_CHANNELS 1015
#define OPT_AL_FEC_ADDR 1028
#define OPT_AL_FEC_PT 1029
#define OPT_METRICS 1009
#define OPT_METRICS_ID 1010
#define OPT_METRICS_INTERVAL 1011
#define OPT_PACKAGES 1016
#define OPT_CELLS 1017
#define OPT_RMS_NAME 1018
#define OPT_RMS_LANG 1019
#define OPT_RMS_LOCATION 1020
#define OPT_RMS_LOGO 1021
#define OPT_FUS_NAME 1022
#define OPT_FUS_LANG 1023
#define OPT_FUS_ID 1024
#define OPT_FUS_ANNOUNCE 1025
#define OPT_FUS_LOGO 1026
#define OPT_DSCP 1027
#define OPT_CONFIG_STRICT 1031
#define OPT_CONFIGTEST 1030

#define argerr(...) argutil_err(TOOL_NAME, __VA_ARGS__)

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
      "  -m, --mcast <g>:<p>     multicast group:port ([addr6]:port for v6)\n"
      "  -I, --iface <iface>     multicast interface\n"
      "  -v, --verbose           periodic stats on stderr\n"
      "      --color <when>      auto|always|never (default auto)\n"
      "  -d, --daemonize         fork to background after startup, detach from terminal\n"
      "  -c, --config <path>     YAML config file (default: %s, if present)\n"
      "      --config-strict     fail on config file issues instead of warnings\n"
      "      --configtest        check the config file, then exit\n"
      "  -h, --help              this help\n\n"
      "listen mode options:\n"
      "  -t, --timeout <s>       stop after N seconds (default 35)\n"
      "  -o, --output <path>     output path, - for stdout (default)\n"
      "  -f, --format <fmt>      m3u|csv|xspf|xml|null (default from -o suffix)\n\n"
      "announce mode options:\n"
      "  -i, --input <path>      .csv/.m3u/.xspf playlist or raw SD&S .xml\n"
      "  -p, --provider <name>   DomainName (required unless -i is .xml)\n"
      "  -O, --offering <name>   display name (required unless -i is .xml)\n"
      "  -L, --lang <code>       ISO 639-2 for the display name (default deu)\n"
      "      --dscp <v>          output DSCP marking: video-high|video-low|voice|\n"
      "                          signalling|best-effort|0..63 (default: signalling)\n"
      "  -t, --interval <s>      repeat interval (default 5)\n"
      "      --ret-addr <a>:<p>  advertise a dipifccret RET server (its -l value);\n"
      "                          opt-in, adds RTPRetransmission to every announced service\n"
      "      --ret-rtx-time <ms> RET rtx-time, matches dipifccret -B (default 2000)\n"
      "      --ret-rtx-pt <n>    RET RTP payload type, matches dipifccret -R (default 99)\n"
      "      --ret-mc            also advertise multicast RET (dipifccret without --no-mc-ret)\n"
      "      --ret-mc-port <p>   multicast RET port, matches dipifccret -F (default: service port)\n"
      "      --ret-rsi-mc-ret    RSI (F.5.3) rides the MC RET session, not the default\n"
      "                          session; requires --ret-mc, matches dipifccret --rsi-mc-ret\n"
      "      --fcc-addr <a>:<p>  advertise a dipifccret FCC server (its -l value);\n"
      "                          opt-in, adds ServerBasedEnhancementServiceInfo to every service\n"
      "      --fcc-rtx-time <ms> FCC Retransmission_session rtx-time (default 2000)\n"
      "      --fcc-rtx-pt <n>    FCC RTP payload type, matches dipifccret -R (default 99)\n"
      "      --fcc-resolve-by-port          per-service FCC port instead of --fcc-addr's port\n"
      "      --fcc-resolve-base-port <p>    matches dipifccret --fcc-resolve-base-port\n"
      "      --fcc-resolve-max-channels <n> port hash modulus for resolve-by-port (default 300)\n"
      "      --al-fec-addr <a>:<p>          advertise an Annex E Layer 1 FEC repair stream\n"
      "      --al-fec-pt <n>     FEC repair stream RTP payload type (default 96)\n"
      "      --metrics <path>    Unix datagram socket for metrics (default: /run/dvbipitools/metrics.sock)\n"
      "      --metrics-id <name> stable instance id; metrics disabled unless set\n"
      "      --metrics-interval <s> a: snapshot interval in seconds (default: 5)\n"
      "      --packages <path>   Package Discovery from id,name,lang,visible,svc1|svc2|... lines\n"
      "      --cells <path>      Regionalisation Discovery from id,country,type:value,... lines\n"
      "      --rms-name <name>   RMS Discovery display name; requires --rms-location\n"
      "      --rms-lang <code>   ISO 639-2 for --rms-name (default deu)\n"
      "      --rms-location <uri>          RMSType RMSLocation\n"
      "      --rms-logo <uri>    RMSType LogoURI\n"
      "      --fus-name <name>   FUS Discovery display name; requires --fus-id\n"
      "      --fus-lang <code>   ISO 639-2 for --fus-name (default deu)\n"
      "      --fus-id <n>        FUSID (decimal)\n"
      "      --fus-announce <a>:<p>        FUS MulticastAnnouncementAddress\n"
      "      --fus-logo <uri>    FUSType LogoURI\n\n"
      "examples:\n"
      "  %s -a -i channels.csv -p example.org -O \"My Headend\" -m 239.255.0.1:3937\n"
      "  %s -l -m 239.255.0.1:3937 -o discovered.m3u\n",
      TOOL_NAME, TOOL_NAME, DEFAULT_CONFIG_PATH, TOOL_NAME, TOOL_NAME);
}

static args_status_t validate_mode_mcast(const config_t *cfg, const args_flags_t *fl) {
  if (fl->have_a == fl->have_l) {
    argerr("exactly one of -a/--announce or -l/--listen is required");
    return ARGS_ERR;
  }
  if (!fl->have_mcast) {
    argerr("missing -m multicast group:port");
    return ARGS_ERR;
  }
  if (argutil_metrics_opts_validate(TOOL_NAME, cfg->metrics_sock, cfg->metrics_id, cfg->metrics_interval_s)) return ARGS_ERR;
  return ARGS_OK;
}

static args_status_t validate_announce_input(config_t *cfg, const args_flags_t *fl) {
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
  if (!cfg->lang[0]) memcpy(cfg->lang, "deu", 3);
  if (fl->have_t) cfg->interval_s = fl->t_value;
  return ARGS_OK;
}

static args_status_t validate_announce_ret(config_t *cfg, const args_flags_t *fl) {
  if (cfg->ret_enabled && has_suffix(cfg->input_path, ".xml")) {
    argerr("--ret-addr has no effect with a raw .xml -i input (that path is sent through unparsed)");
    return ARGS_ERR;
  }
  if (!cfg->ret_enabled && (fl->have_ret_rtx_time || fl->have_ret_rtx_pt || cfg->ret_mc || fl->have_ret_mc_port || cfg->ret_rsi_mc_ret)) {
    argerr("--ret-rtx-time/--ret-rtx-pt/--ret-mc/--ret-mc-port/--ret-rsi-mc-ret require --ret-addr");
    return ARGS_ERR;
  }
  if (cfg->ret_rsi_mc_ret && !cfg->ret_mc) {
    argerr("--ret-rsi-mc-ret requires --ret-mc");
    return ARGS_ERR;
  }
  if (cfg->ret_enabled) {
    if (!fl->have_ret_rtx_time) cfg->ret_rtx_time = 2000;
    if (!fl->have_ret_rtx_pt) cfg->ret_rtx_pt = 99;
  }
  return ARGS_OK;
}

static args_status_t validate_announce_fcc(config_t *cfg, const args_flags_t *fl) {
  if (cfg->fcc_enabled && has_suffix(cfg->input_path, ".xml")) {
    argerr("--fcc-addr has no effect with a raw .xml -i input (that path is sent through unparsed)");
    return ARGS_ERR;
  }
  if (!cfg->fcc_enabled && (fl->have_fcc_rtx_time || fl->have_fcc_rtx_pt || cfg->fcc_resolve_by_port || cfg->fcc_resolve_base_port || fl->have_fcc_resolve_max_channels)) {
    argerr("--fcc-rtx-time/--fcc-rtx-pt/--fcc-resolve-* require --fcc-addr");
    return ARGS_ERR;
  }
  if (cfg->fcc_enabled) {
    if (!fl->have_fcc_rtx_time) cfg->fcc_rtx_time = 2000;
    if (!fl->have_fcc_rtx_pt) cfg->fcc_rtx_pt = 99;
    if (!fl->have_fcc_resolve_max_channels) cfg->fcc_resolve_max_channels = 300;
  }
  return ARGS_OK;
}

static args_status_t validate_announce_al_fec(config_t *cfg, const args_flags_t *fl) {
  if (cfg->al_fec_enabled && has_suffix(cfg->input_path, ".xml")) {
    argerr("--al-fec-addr has no effect with a raw .xml -i input (that path is sent through unparsed)");
    return ARGS_ERR;
  }
  if (!cfg->al_fec_enabled && fl->have_al_fec_pt) {
    argerr("--al-fec-pt requires --al-fec-addr");
    return ARGS_ERR;
  }
  if (cfg->al_fec_enabled && !fl->have_al_fec_pt) cfg->al_fec_pt = 96;
  return ARGS_OK;
}

static args_status_t validate_announce_rms_fus(config_t *cfg, const args_flags_t *fl) {
  if ((cfg->packages_path || cfg->cells_path || cfg->rms_enabled || cfg->fus_enabled) && has_suffix(cfg->input_path, ".xml")) {
    argerr("--packages/--cells/--rms-name/--fus-name have no effect with a raw .xml -i input (that path is sent through unparsed)");
    return ARGS_ERR;
  }
  if (cfg->rms_enabled && cfg->fus_enabled) {
    argerr("--rms-name and --fus-name are mutually exclusive (RMSFUSDiscovery carries one or the other, never both)");
    return ARGS_ERR;
  }
  if (!cfg->rms_enabled && (fl->have_rms_lang || cfg->rms_location || cfg->rms_logo)) {
    argerr("--rms-lang/--rms-location/--rms-logo require --rms-name");
    return ARGS_ERR;
  }
  if (cfg->rms_enabled) {
    if (!cfg->rms_location) {
      argerr("--rms-name requires --rms-location");
      return ARGS_ERR;
    }
    if (!fl->have_rms_lang) memcpy(cfg->rms_lang, "deu", 3);
  }
  if (!cfg->fus_enabled && (fl->have_fus_lang || fl->have_fus_id || cfg->fus_announce_addr[0] || cfg->fus_logo)) {
    argerr("--fus-lang/--fus-id/--fus-announce/--fus-logo require --fus-name");
    return ARGS_ERR;
  }
  if (cfg->fus_enabled) {
    if (!fl->have_fus_id) {
      argerr("--fus-name requires --fus-id");
      return ARGS_ERR;
    }
    if (!fl->have_fus_lang) memcpy(cfg->fus_lang, "deu", 3);
  }
  return ARGS_OK;
}

static args_status_t validate_listen(config_t *cfg, const args_flags_t *fl) {
  if (cfg->ret_enabled || fl->have_ret_rtx_time || fl->have_ret_rtx_pt || cfg->ret_mc || fl->have_ret_mc_port || cfg->ret_rsi_mc_ret) {
    argerr("--ret-* options are announce-only");
    return ARGS_ERR;
  }
  if (cfg->fcc_enabled || fl->have_fcc_rtx_time || fl->have_fcc_rtx_pt || cfg->fcc_resolve_by_port || cfg->fcc_resolve_base_port || fl->have_fcc_resolve_max_channels) {
    argerr("--fcc-* options are announce-only");
    return ARGS_ERR;
  }
  if (cfg->metrics_id) {
    argerr("--metrics-id is announce-only");
    return ARGS_ERR;
  }
  if (cfg->packages_path || cfg->cells_path || cfg->rms_enabled || fl->have_rms_lang || cfg->rms_location || cfg->rms_logo ||
      cfg->fus_enabled || fl->have_fus_lang || fl->have_fus_id || cfg->fus_announce_addr[0] || cfg->fus_logo) {
    argerr("--packages/--cells/--rms-*/--fus-* options are announce-only");
    return ARGS_ERR;
  }
  if (!cfg->output_path) cfg->output_path = "-";
  if (!fl->have_format) {
    if (has_suffix(cfg->output_path, ".csv"))       cfg->format = OUT_CSV;
    else if (has_suffix(cfg->output_path, ".xspf")) cfg->format = OUT_XSPF;
    else if (has_suffix(cfg->output_path, ".xml"))  cfg->format = OUT_XML;
  }
  if (fl->have_t) cfg->timeout_s = fl->t_value;
  return ARGS_OK;
}

static const char *const shortopts = "ali:p:O:L:m:I:t:o:f:c:vdh";

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
    {"color", required_argument, 0, OPT_COLOR},
    {"ret-addr", required_argument, 0, OPT_RET_ADDR},
    {"ret-rtx-time", required_argument, 0, OPT_RET_RTX_TIME},
    {"ret-rtx-pt", required_argument, 0, OPT_RET_RTX_PT},
    {"ret-mc", no_argument, 0, OPT_RET_MC},
    {"ret-mc-port", required_argument, 0, OPT_RET_MC_PORT},
    {"ret-rsi-mc-ret", no_argument, 0, OPT_RET_RSI_MC_RET},
    {"fcc-addr", required_argument, 0, OPT_FCC_ADDR},
    {"fcc-rtx-time", required_argument, 0, OPT_FCC_RTX_TIME},
    {"fcc-rtx-pt", required_argument, 0, OPT_FCC_RTX_PT},
    {"fcc-resolve-by-port", no_argument, 0, OPT_FCC_RESOLVE_BY_PORT},
    {"fcc-resolve-base-port", required_argument, 0, OPT_FCC_RESOLVE_BASE_PORT},
    {"fcc-resolve-max-channels", required_argument, 0, OPT_FCC_RESOLVE_MAX_CHANNELS},
    {"al-fec-addr", required_argument, 0, OPT_AL_FEC_ADDR},
    {"al-fec-pt", required_argument, 0, OPT_AL_FEC_PT},
    {"metrics", required_argument, 0, OPT_METRICS},
    {"metrics-id", required_argument, 0, OPT_METRICS_ID},
    {"metrics-interval", required_argument, 0, OPT_METRICS_INTERVAL},
    {"packages", required_argument, 0, OPT_PACKAGES},
    {"cells", required_argument, 0, OPT_CELLS},
    {"rms-name", required_argument, 0, OPT_RMS_NAME},
    {"rms-lang", required_argument, 0, OPT_RMS_LANG},
    {"rms-location", required_argument, 0, OPT_RMS_LOCATION},
    {"rms-logo", required_argument, 0, OPT_RMS_LOGO},
    {"fus-name", required_argument, 0, OPT_FUS_NAME},
    {"fus-lang", required_argument, 0, OPT_FUS_LANG},
    {"fus-id", required_argument, 0, OPT_FUS_ID},
    {"fus-announce", required_argument, 0, OPT_FUS_ANNOUNCE},
    {"fus-logo", required_argument, 0, OPT_FUS_LOGO},
    {"dscp", required_argument, 0, OPT_DSCP},
    {"daemonize", no_argument, 0, 'd'},
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
    if (c == 1030) *configtest = 1;
    if (c == 1031) *strict = 1;
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
  args_status_t pst;
  int cli_mode = 0;
  int c;
  args_status_t st;

  pst = prescan(argc, argv, &cfg_path, &configtest, &strict);
  if (pst != ARGS_OK) return pst;
  if (configtest) return sds_cfg_test(cfg_path, strict) ? ARGS_ERR : ARGS_HELP;

  sds_cfg_defaults(cfg);
  if (sds_cfg_load(cfg, cfg_path, strict)) return ARGS_ERR;

  optind = 1;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    switch (c) {
      case 'a':
      case 'l':
        if (!cli_mode) {
          cfg->fl.have_a = 0;
          cfg->fl.have_l = 0;
        }
        cli_mode = 1;
        if (c == 'a')
          cfg->fl.have_a = 1;
        else
          cfg->fl.have_l = 1;
        break;
      case OPT_CONFIG_STRICT:
      case OPT_CONFIGTEST:
      case 'c':
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
        cfg->fl.have_mcast = 1;
        break;
      case 'I':
        cfg->iface = optarg;
        break;
      case OPT_DSCP:
        if (net_dscp_parse(optarg, &cfg->dscp)) {
          argerr("invalid --dscp: %s (video-high|video-low|voice|signalling|best-effort|0..63)", optarg);
          return ARGS_ERR;
        }
        break;
      case 't': {
        unsigned v;
        if (argutil_uint_range(optarg, 0, UINT_MAX, &v)) {
          argerr("invalid -t seconds: %s", optarg);
          return ARGS_ERR;
        }
        cfg->fl.t_value = v;
        cfg->fl.have_t = 1;
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
        cfg->fl.have_format = 1;
        break;
      }
      case 'v':
        cfg->verbose = 1;
        break;
      case 'd':
        cfg->daemonize = 1;
        break;
      case OPT_COLOR: {
        log_color_t v;
        if (log_color_from_string(optarg, &v)) {
          argerr("invalid --color: %s (auto|always|never)", optarg);
          return ARGS_ERR;
        }
        cfg->color_mode = v;
        break;
      }
      case OPT_RET_ADDR:
        if (ret_addr_parse(optarg, cfg->ret_addr, sizeof cfg->ret_addr, &cfg->ret_port)) {
          argerr("invalid --ret-addr: %s", optarg);
          return ARGS_ERR;
        }
        cfg->ret_enabled = 1;
        break;
      case OPT_RET_RTX_TIME: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, UINT_MAX, &v)) {
          argerr("invalid --ret-rtx-time: %s", optarg);
          return ARGS_ERR;
        }
        cfg->ret_rtx_time = v;
        cfg->fl.have_ret_rtx_time = 1;
        break;
      }
      case OPT_RET_RTX_PT: {
        unsigned v;
        if (argutil_uint_range(optarg, 0, 127, &v)) {
          argerr("invalid --ret-rtx-pt: %s (0..127)", optarg);
          return ARGS_ERR;
        }
        cfg->ret_rtx_pt = (unsigned char)v;
        cfg->fl.have_ret_rtx_pt = 1;
        break;
      }
      case OPT_RET_MC:
        cfg->ret_mc = 1;
        break;
      case OPT_RET_MC_PORT: {
        unsigned v;
        if (argutil_port_parse(optarg, &v)) {
          argerr("invalid --ret-mc-port: %s", optarg);
          return ARGS_ERR;
        }
        cfg->ret_mc_port = v;
        cfg->fl.have_ret_mc_port = 1;
        break;
      }
      case OPT_RET_RSI_MC_RET:
        cfg->ret_rsi_mc_ret = 1;
        break;
      case OPT_FCC_ADDR:
        if (ret_addr_parse(optarg, cfg->fcc_addr, sizeof cfg->fcc_addr, &cfg->fcc_port)) {
          argerr("invalid --fcc-addr: %s", optarg);
          return ARGS_ERR;
        }
        cfg->fcc_enabled = 1;
        break;
      case OPT_AL_FEC_ADDR:
        if (ret_addr_parse(optarg, cfg->al_fec_addr, sizeof cfg->al_fec_addr, &cfg->al_fec_port)) {
          argerr("invalid --al-fec-addr: %s", optarg);
          return ARGS_ERR;
        }
        cfg->al_fec_enabled = 1;
        break;
      case OPT_AL_FEC_PT: {
        unsigned v;
        if (argutil_uint_range(optarg, 0, 127, &v)) {
          argerr("invalid --al-fec-pt: %s (0..127)", optarg);
          return ARGS_ERR;
        }
        cfg->al_fec_pt = (unsigned char)v;
        cfg->fl.have_al_fec_pt = 1;
        break;
      }
      case OPT_FCC_RTX_TIME: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, UINT_MAX, &v)) {
          argerr("invalid --fcc-rtx-time: %s", optarg);
          return ARGS_ERR;
        }
        cfg->fcc_rtx_time = v;
        cfg->fl.have_fcc_rtx_time = 1;
        break;
      }
      case OPT_FCC_RTX_PT: {
        unsigned v;
        if (argutil_uint_range(optarg, 0, 127, &v)) {
          argerr("invalid --fcc-rtx-pt: %s (0..127)", optarg);
          return ARGS_ERR;
        }
        cfg->fcc_rtx_pt = (unsigned char)v;
        cfg->fl.have_fcc_rtx_pt = 1;
        break;
      }
      case OPT_FCC_RESOLVE_BY_PORT:
        cfg->fcc_resolve_by_port = 1;
        break;
      case OPT_FCC_RESOLVE_BASE_PORT: {
        unsigned v;
        if (argutil_port_parse(optarg, &v)) {
          argerr("invalid --fcc-resolve-base-port: %s", optarg);
          return ARGS_ERR;
        }
        cfg->fcc_resolve_base_port = v;
        break;
      }
      case OPT_FCC_RESOLVE_MAX_CHANNELS: {
        unsigned v;
        if (argutil_uint_range(optarg, 1, UINT_MAX, &v)) {
          argerr("invalid --fcc-resolve-max-channels: %s", optarg);
          return ARGS_ERR;
        }
        cfg->fcc_resolve_max_channels = (size_t)v;
        cfg->fl.have_fcc_resolve_max_channels = 1;
        break;
      }
      case OPT_METRICS:
        cfg->metrics_sock = optarg;
        break;
      case OPT_METRICS_ID:
        cfg->metrics_id = optarg;
        break;
      case OPT_METRICS_INTERVAL:
        if (argutil_metrics_interval_opt(TOOL_NAME, optarg, &cfg->metrics_interval_s)) return ARGS_ERR;
        break;
      case OPT_PACKAGES:
        cfg->packages_path = optarg;
        break;
      case OPT_CELLS:
        cfg->cells_path = optarg;
        break;
      case OPT_RMS_NAME:
        cfg->rms_name = optarg;
        cfg->rms_enabled = 1;
        break;
      case OPT_RMS_LANG:
        if (strlen(optarg) != 3) {
          argerr("invalid --rms-lang: %s (3-letter ISO 639-2 code)", optarg);
          return ARGS_ERR;
        }
        memcpy(cfg->rms_lang, optarg, 3);
        cfg->fl.have_rms_lang = 1;
        break;
      case OPT_RMS_LOCATION:
        cfg->rms_location = optarg;
        break;
      case OPT_RMS_LOGO:
        cfg->rms_logo = optarg;
        break;
      case OPT_FUS_NAME:
        cfg->fus_name = optarg;
        cfg->fus_enabled = 1;
        break;
      case OPT_FUS_LANG:
        if (strlen(optarg) != 3) {
          argerr("invalid --fus-lang: %s (3-letter ISO 639-2 code)", optarg);
          return ARGS_ERR;
        }
        memcpy(cfg->fus_lang, optarg, 3);
        cfg->fl.have_fus_lang = 1;
        break;
      case OPT_FUS_ID: {
        char *end;
        unsigned long v = strtoul(optarg, &end, 10);
        if (*end != '\0') {
          argerr("invalid --fus-id: %s", optarg);
          return ARGS_ERR;
        }
        cfg->fus_id = v;
        cfg->fl.have_fus_id = 1;
        break;
      }
      case OPT_FUS_ANNOUNCE:
        if (ret_addr_parse(optarg, cfg->fus_announce_addr, sizeof cfg->fus_announce_addr, &cfg->fus_announce_port)) {
          argerr("invalid --fus-announce: %s", optarg);
          return ARGS_ERR;
        }
        break;
      case OPT_FUS_LOGO:
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
  cfg->mode = cfg->fl.have_l ? MODE_LISTEN : MODE_ANNOUNCE;
  if ((st = validate_mode_mcast(cfg, &cfg->fl)) != ARGS_OK) return st;
  if (cfg->mode == MODE_ANNOUNCE) {
    if ((st = validate_announce_input(cfg, &cfg->fl)) != ARGS_OK) return st;
    if ((st = validate_announce_ret(cfg, &cfg->fl)) != ARGS_OK) return st;
    if ((st = validate_announce_fcc(cfg, &cfg->fl)) != ARGS_OK) return st;
    if ((st = validate_announce_al_fec(cfg, &cfg->fl)) != ARGS_OK) return st;
    if ((st = validate_announce_rms_fus(cfg, &cfg->fl)) != ARGS_OK) return st;
  } else {
    if ((st = validate_listen(cfg, &cfg->fl)) != ARGS_OK) return st;
  }
  return ARGS_OK;
}
