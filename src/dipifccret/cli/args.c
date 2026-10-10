/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>
#include <unistd.h>

#include "priv.h"

static const char shortopts[] = "g:l:I:M:R:w:u:c:vdhB:F:G:C:X:D:";

static const struct option longopts[] = {
{"range", required_argument, 0, 'g'},
{"listen", required_argument, 0, 'l'},
{"iface", required_argument, 0, 'I'},
{"max-channels", required_argument, 0, 'M'},
{"channel-idle-timeout", required_argument, 0, OPT_CHANNEL_IDLE_TIMEOUT},
{"rtx-pt", required_argument, 0, 'R'},
{"workers", required_argument, 0, 'w'},
{"cpu-affinity", required_argument, 0, OPT_CPU_AFFINITY},
{"user", required_argument, 0, 'u'},
{"verbose", no_argument, 0, 'v'},
{"color", required_argument, 0, OPT_COLOR},
{"no-ret", no_argument, 0, OPT_NO_RET},
{"buffer", required_argument, 0, 'B'},
{"ff-port", required_argument, 0, 'F'},
{"no-mc-ret", no_argument, 0, OPT_NO_MC_RET},
{"max-ret-clients", required_argument, 0, OPT_MAX_RET_CLIENTS},
{"ret-client-idle-timeout", required_argument, 0, OPT_RET_CLIENT_IDLE_TIMEOUT},
{"ret-client-rate", required_argument, 0, OPT_RET_CLIENT_RATE},
{"ret-mc-dedup", required_argument, 0, OPT_RET_MC_DEDUP},
{"no-rsi", no_argument, 0, OPT_NO_RSI},
{"rsi-interval", required_argument, 0, OPT_RSI_INTERVAL},
{"rsi-mc-ret", no_argument, 0, OPT_RSI_MC_RET},
{"rsi-hostname", required_argument, 0, OPT_RSI_HOSTNAME},
{"no-fcc", no_argument, 0, OPT_NO_FCC},
{"gop-cap", required_argument, 0, 'G'},
{"max-bursts", required_argument, 0, 'C'},
{"burst-multiplier", required_argument, 0, 'X'},
{"burst-duration-cap", required_argument, 0, 'D'},
{"max-buffer-fill-bound", required_argument, 0, OPT_MAX_BUFFER_FILL_BOUND},
{"fcc-resolve-by-port", no_argument, 0, OPT_FCC_RESOLVE_BY_PORT},
{"fcc-resolve-base-port", required_argument, 0, OPT_FCC_RESOLVE_BASE_PORT},
{"congestion-nack-threshold", required_argument, 0, OPT_CONGESTION_NACK_THRESHOLD},
{"fcc-range", required_argument, 0, OPT_FCC_RANGE},
{"fcc-client-range", required_argument, 0, OPT_FCC_CLIENT_RANGE},
{"fcc-client-rate", required_argument, 0, OPT_FCC_CLIENT_RATE},
{"metrics", required_argument, 0, OPT_METRICS},
{"metrics-id", required_argument, 0, OPT_METRICS_ID},
{"metrics-interval", required_argument, 0, OPT_METRICS_INTERVAL},
{"metrics-inspect-ts", required_argument, 0, OPT_METRICS_INSPECT_TS},
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
      fccret_print_help();
      opterr = 1;
      return ARGS_HELP;
    }
  }
  opterr = 1;
  return ARGS_OK;
}

static args_status_t dispatch(fccret_opt_t *p, int c) {
  static args_status_t (*const groups[])(fccret_opt_t *, int) = {fccret_opt_general, fccret_opt_fcc};

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
  fccret_opt_t p = {.cfg = cfg};
  args_status_t pst;
  int c;

  pst = prescan(argc, argv, &cfg_path, &configtest, &strict);
  if (pst != ARGS_OK) return pst;
  if (configtest) return fccret_cfg_test(cfg_path, strict) ? ARGS_ERR : ARGS_HELP;

  fccret_cfg_defaults(cfg);
  if (fccret_cfg_load(cfg, cfg_path, strict)) return ARGS_ERR;

  optind = 1;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    args_status_t st;
    if (c == 'c' || c == OPT_CONFIG_STRICT || c == OPT_CONFIGTEST) continue;
    if (c == 'h') {
      fccret_print_help();
      return ARGS_HELP;
    }
    st = dispatch(&p, c);
    if (st != ARGS_OK) return st;
  }
  if (optind < argc) {
    argerr("unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  return fccret_cli_check(cfg);
}
