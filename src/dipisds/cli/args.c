/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>

#include "priv.h"

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
    if (c == OPT_CONFIGTEST) *configtest = 1;
    if (c == OPT_CONFIG_STRICT) *strict = 1;
    if (c == 'h') {
      sds_print_help();
      opterr = 1;
      return ARGS_HELP;
    }
  }
  opterr = 1;
  return ARGS_OK;
}

static args_status_t dispatch(sds_opt_t *p, int c) {
  static args_status_t (*const groups[])(sds_opt_t *, int) = {sds_opt_general, sds_opt_ret, sds_opt_rms};

  for (size_t i = 0; i < sizeof groups / sizeof groups[0]; i++) {
    args_status_t st = groups[i](p, c);
    if (st != OPT_UNHANDLED) return st;
  }
  return ARGS_ERR; /* getopt already reported */
}

args_status_t args_parse(int argc, char **argv, config_t *cfg) {
  const char *cfg_path = NULL;
  int configtest = 0;
  int strict = 0;
  sds_opt_t p = {.cfg = cfg};
  args_status_t pst;
  int c;

  pst = prescan(argc, argv, &cfg_path, &configtest, &strict);
  if (pst != ARGS_OK) return pst;
  if (configtest) return sds_cfg_test(cfg_path, strict) ? ARGS_ERR : ARGS_HELP;

  sds_cfg_defaults(cfg);
  if (sds_cfg_load(cfg, cfg_path, strict)) return ARGS_ERR;

  optind = 1;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    args_status_t st;
    if (c == 'c' || c == OPT_CONFIG_STRICT || c == OPT_CONFIGTEST) continue;
    if (c == 'h') {
      sds_print_help();
      return ARGS_HELP;
    }
    st = dispatch(&p, c);
    if (st != ARGS_OK) return st;
  }
  if (optind < argc) {
    argerr("unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  return sds_cli_check(cfg);
}
