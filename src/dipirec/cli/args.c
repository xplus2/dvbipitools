/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>

#include "priv.h"

static const char *const shortopts = "o:i:a:f:p:s:t:I:O:c:vh";

static const struct option longopts[] = {
  {"out", required_argument, 0, 'o'},
  {"in", required_argument, 0, 'i'},
  {"audio", required_argument, 0, 'a'},
  {"format", required_argument, 0, 'f'},
  {"pmt-pid", required_argument, 0, 'p'},
  {"subtitles", required_argument, 0, 's'},
  {"time", required_argument, 0, 't'},
  {"iface", required_argument, 0, 'I'},
  {"verbose", no_argument, 0, 'v'},
  {"sub-lead", required_argument, 0, OPT_SUB_LEAD},
  {"color", required_argument, 0, OPT_COLOR},
  {"ret", required_argument, 0, OPT_RET},
  {"no-ret-mc", no_argument, 0, OPT_NO_RET_MC},
  {"ret-mc-port", required_argument, 0, OPT_RET_MC_PORT},
  {"ret-pt", required_argument, 0, OPT_RET_PT},
  {"ret-wait", required_argument, 0, OPT_RET_WAIT},
  {"strip", required_argument, 0, OPT_STRIP},
  {"pace", no_argument, 0, OPT_PACE},
  {"out-iface", required_argument, 0, 'O'},
  {"ttl", required_argument, 0, OPT_TTL},
  {"al-fec", required_argument, 0, OPT_AL_FEC},
  {"al-fec-port", required_argument, 0, OPT_AL_FEC_PORT},
  {"rist-profile", required_argument, 0, OPT_PROFILE},
  {"rist-secret", required_argument, 0, OPT_SECRET},
  {"rist-encryption-type", required_argument, 0, OPT_ENCRYPTION_TYPE},
  {"rist-cname", required_argument, 0, OPT_CNAME},
  {"rist-buffer", required_argument, 0, OPT_BUFFER},
  {"insecure", no_argument, 0, OPT_INSECURE},
  {"metrics", required_argument, 0, OPT_METRICS},
  {"metrics-id", required_argument, 0, OPT_METRICS_ID},
  {"metrics-interval", required_argument, 0, OPT_METRICS_INTERVAL},
  {"metrics-inspect-ts", required_argument, 0, OPT_METRICS_INSPECT_TS},
  {"metrics-inspect-ts-pids", required_argument, 0, OPT_METRICS_INSPECT_TS_PIDS},
  {"rist-profile-in", required_argument, 0, OPT_PROFILE_IN},
  {"rist-encryption-type-in", required_argument, 0, OPT_ENCRYPTION_TYPE_IN},
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
      rec_print_help();
      opterr = 1;
      return ARGS_HELP;
    }
  }
  opterr = 1;
  return ARGS_OK;
}

static args_status_t dispatch(rec_opt_t *p, int c) {
  static args_status_t (*const groups[])(rec_opt_t *, int) = {rec_opt_general, rec_opt_stream, rec_opt_rist, rec_opt_srt};

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
  rec_opt_t p = {.cfg = cfg};
  args_status_t pst;
  int c;

  pst = prescan(argc, argv, &cfg_path, &configtest, &strict);
  if (pst != ARGS_OK) return pst;
  if (configtest) return rec_cfg_test(cfg_path, strict) ? ARGS_ERR : ARGS_HELP;
  rec_cfg_defaults(cfg);
  if (rec_cfg_load(cfg, cfg_path, strict)) return ARGS_ERR;

  optind = 1;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    args_status_t st;
    if (c == 'c' || c == OPT_CONFIG_STRICT || c == OPT_CONFIGTEST) continue;
    if (c == 'h') {
      rec_print_help();
      return ARGS_HELP;
    }
    st = dispatch(&p, c);
    if (st != ARGS_OK) return st;
  }
  if (optind < argc) {
    argerr("unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  return rec_cli_check(cfg);
}
