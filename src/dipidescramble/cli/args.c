/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>

#include "priv.h"

static const char shortopts[] = "i:k:s:e:u:o:f:p:I:c:vdh";

static const struct option longopts[] = {
{"input", required_argument, 0, 'i'},
{"key", required_argument, 0, 'k'},
{"serial", required_argument, 0, 's'},
{"emm-file", required_argument, 0, 'e'},
{"unicast-emm", required_argument, 0, 'u'},
{"insecure", no_argument, 0, OPT_INSECURE},
{"token-header", required_argument, 0, OPT_TOKEN_HEADER},
{"output", required_argument, 0, 'o'},
{"format", required_argument, 0, 'f'},
{"pmt-pid", required_argument, 0, 'p'},
{"iface", required_argument, 0, 'I'},
{"verbose", no_argument, 0, 'v'},
{"color", required_argument, 0, OPT_COLOR},
{"biss2-sw", required_argument, 0, OPT_BISS2_SW},
{"biss2-esw", required_argument, 0, OPT_BISS2_ESW},
{"biss2-id", required_argument, 0, OPT_BISS2_ID},
{"biss1-sw", required_argument, 0, OPT_BISS1_SW},
{"biss2-ca-key", required_argument, 0, OPT_BISS2_CA_KEY},
{"ecm-profile", required_argument, 0, OPT_ECM_PROFILE},
{"metrics", required_argument, 0, OPT_METRICS},
{"metrics-id", required_argument, 0, OPT_METRICS_ID},
{"metrics-interval", required_argument, 0, OPT_METRICS_INTERVAL},
{"metrics-inspect-ts", required_argument, 0, OPT_METRICS_INSPECT_TS},
{"metrics-inspect-ts-pids", required_argument, 0, OPT_METRICS_INSPECT_TS_PIDS},
{"max-services", required_argument, 0, OPT_MAX_SERVICES},
{"rist-profile", required_argument, 0, OPT_PROFILE},
{"rist-encryption-type", required_argument, 0, OPT_ENCRYPTION_TYPE},
{"srt-passphrase-in", required_argument, 0, OPT_SRT_PASSPHRASE_IN},
{"srt-pbkeylen-in", required_argument, 0, OPT_SRT_PBKEYLEN_IN},
{"srt-streamid-in", required_argument, 0, OPT_SRT_STREAMID_IN},
{"srt-packetfilter-in", required_argument, 0, OPT_SRT_PACKETFILTER_IN},
{"srt-latency-in", required_argument, 0, OPT_SRT_LATENCY_IN},
{"srt-passphrase", required_argument, 0, OPT_SRT_PASSPHRASE},
{"srt-pbkeylen", required_argument, 0, OPT_SRT_PBKEYLEN},
{"srt-streamid", required_argument, 0, OPT_SRT_STREAMID},
{"srt-packetfilter", required_argument, 0, OPT_SRT_PACKETFILTER},
{"srt-latency", required_argument, 0, OPT_SRT_LATENCY},
{"strip-lcevc", no_argument, 0, OPT_STRIP_LCEVC},
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
      dscr_print_help();
      opterr = 1;
      return ARGS_HELP;
    }
  }
  opterr = 1;
  return ARGS_OK;
}

static args_status_t dispatch(dscr_opt_t *p, int c) {
  static args_status_t (*const groups[])(dscr_opt_t *, int) = {dscr_opt_general, dscr_opt_biss, dscr_opt_net};

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
  dscr_opt_t p = {.cfg = cfg};
  args_status_t pst;
  int c;

  pst = prescan(argc, argv, &cfg_path, &configtest, &strict);
  if (pst != ARGS_OK) return pst;
  if (configtest) return dscr_cfg_test(cfg_path, strict) ? ARGS_ERR : ARGS_HELP;

  dscr_cfg_defaults(cfg);
  if (dscr_cfg_load(cfg, cfg_path, strict)) return ARGS_ERR;

  optind = 1;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    args_status_t st;
    if (c == 'c' || c == OPT_CONFIG_STRICT || c == OPT_CONFIGTEST) continue;
    if (c == 'h') {
      dscr_print_help();
      return ARGS_HELP;
    }
    st = dispatch(&p, c);
    if (st != ARGS_OK) return st;
  }
  if (optind < argc) {
    argerr("unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  return dscr_cli_check(cfg);
}
