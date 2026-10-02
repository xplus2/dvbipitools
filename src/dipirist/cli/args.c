/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <unistd.h>

#include "priv.h"

static const char *const shortopts = "i:o:I:c:kvdh";

static const struct option longopts[] = {
  {"in", required_argument, 0, 'i'},
  {"out", required_argument, 0, 'o'},
  {"iface", required_argument, 0, 'I'},
  {"insecure", no_argument, 0, 'k'},
  {"profile", required_argument, 0, OPT_PROFILE},
  {"secret", required_argument, 0, OPT_SECRET},
  {"encryption-type", required_argument, 0, OPT_ENCRYPTION_TYPE},
  {"cname", required_argument, 0, OPT_CNAME},
  {"buffer", required_argument, 0, OPT_BUFFER},
  {"color", required_argument, 0, OPT_COLOR},
  {"metrics", required_argument, 0, OPT_METRICS},
  {"metrics-id", required_argument, 0, OPT_METRICS_ID},
  {"metrics-interval", required_argument, 0, OPT_METRICS_INTERVAL},
  {"metrics-inspect-ts", required_argument, 0, OPT_METRICS_INSPECT_TS},
  {"al-fec", required_argument, 0, OPT_AL_FEC},
  {"al-fec-port", required_argument, 0, OPT_AL_FEC_PORT},
  {"verbose", no_argument, 0, 'v'},
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
      rist_print_help();
      opterr = 1;
      return ARGS_HELP;
    }
  }
  opterr = 1;
  return ARGS_OK;
}

static args_status_t dispatch(rist_opt_t *p, int c) {
  static args_status_t (*const groups[])(rist_opt_t *, int) = {rist_opt_general, rist_opt_rist};

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
  rist_opt_t p = {.cfg = cfg};
  args_status_t pst;
  int c;

  pst = prescan(argc, argv, &cfg_path, &configtest, &strict);
  if (pst != ARGS_OK) return pst;
  if (configtest) return rist_cfg_test(cfg_path, strict) ? ARGS_ERR : ARGS_HELP;
  rist_cfg_defaults(cfg);
  if (rist_cfg_load(cfg, cfg_path, strict)) return ARGS_ERR;
  optind = 1;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    args_status_t st;
    if (c == 'c' || c == OPT_CONFIG_STRICT || c == OPT_CONFIGTEST) continue;
    if (c == 'h') {
      rist_print_help();
      return ARGS_HELP;
    }
    st = dispatch(&p, c);
    if (st != ARGS_OK) return st;
  }
  if (optind < argc) {
    argerr("unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  return rist_cli_check(cfg);
}
