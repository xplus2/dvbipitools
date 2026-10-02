/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>

#include "priv.h"

static const char shortopts[] = "+i:m:O:rT:n:s:e:kvdhR:c:";

static const struct option longopts[] = {
{"input", required_argument, 0, 'i'},
{"mcast", required_argument, 0, 'm'},
{"out-iface", required_argument, 0, 'O'},
{"rtp", no_argument, 0, 'r'},
{"ttl", required_argument, 0, 'T'},
{"nit", required_argument, 0, 'n'},
{"sdt", required_argument, 0, 's'},
{"error", required_argument, 0, 'e'},
{"insecure", no_argument, 0, 'k'},
{"tsid", required_argument, 0, OPT_TSID},
{"onid", required_argument, 0, OPT_ONID},
{"sid", required_argument, 0, OPT_SID},
{"jitter-ms", required_argument, 0, OPT_JITTER_MS},
{"verbose", no_argument, 0, 'v'},
{"color", required_argument, 0, OPT_COLOR},
{"cas-algo", required_argument, 0, OPT_CAS_ALGO},
{"cas-ecmg", required_argument, 0, OPT_CAS_ECMG},
{"cas-ecmg-version", required_argument, 0, OPT_CAS_ECMG_VERSION},
{"cas-super-id", required_argument, 0, OPT_CAS_SUPER_ID},
{"cas-ecm-id", required_argument, 0, OPT_CAS_ECM_ID},
{"cas-ecm-pid", required_argument, 0, OPT_CAS_ECM_PID},
{"cas-emmg-port", required_argument, 0, OPT_CAS_EMMG_PORT},
{"cas-emmg-version", required_argument, 0, OPT_CAS_EMMG_VERSION},
{"cas-emmg-max-conns", required_argument, 0, OPT_CAS_EMMG_MAX_CONNS},
{"cas-emmg-reverse", required_argument, 0, OPT_CAS_EMMG_REVERSE},
{"cas-emm-pid", required_argument, 0, OPT_CAS_EMM_PID},
{"cas-cp-duration", required_argument, 0, OPT_CAS_CP_DURATION},
{"cas-resilience", required_argument, 0, OPT_CAS_RESILIENCE},
{"metrics", required_argument, 0, OPT_METRICS},
{"metrics-id", required_argument, 0, OPT_METRICS_ID},
{"metrics-interval", required_argument, 0, OPT_METRICS_INTERVAL},
{"metrics-inspect-ts", required_argument, 0, OPT_METRICS_INSPECT_TS},
{"metrics-inspect-ts-pids", required_argument, 0, OPT_METRICS_INSPECT_TS_PIDS},
{"cas-required", no_argument, 0, OPT_CAS_REQUIRED},
{"cas-cwenc-algo", required_argument, 0, OPT_CAS_CWENC_ALGO},
{"cas-cwenc-aes-mode", required_argument, 0, OPT_CAS_CWENC_AES_MODE},
{"cas-cwenc-fixed-key", required_argument, 0, OPT_CAS_CWENC_FIXED_KEY},
{"cas-cwenc-key-list-a", required_argument, 0, OPT_CAS_CWENC_KEY_LIST_A},
{"cas-cwenc-key-list-b", required_argument, 0, OPT_CAS_CWENC_KEY_LIST_B},
{"cas-fallback-clear", no_argument, 0, OPT_CAS_FALLBACK_CLEAR},
{"biss2-sw", required_argument, 0, OPT_BISS2_SW},
{"biss2-emit-esw", required_argument, 0, OPT_BISS2_EMIT_ESW},
{"biss1-sw", required_argument, 0, OPT_BISS1_SW},
{"biss2-ca-receivers", required_argument, 0, OPT_BISS2_CA_RECEIVERS},
{"biss2-ca-session-id", required_argument, 0, OPT_BISS2_CA_SESSION_ID},
{"remote", required_argument, 0, 'R'},
{"rist-profile", required_argument, 0, OPT_RIST_PROFILE},
{"rist-secret", required_argument, 0, OPT_RIST_SECRET},
{"rist-encryption-type", required_argument, 0, OPT_RIST_ENCRYPTION_TYPE},
{"rist-cname", required_argument, 0, OPT_RIST_CNAME},
{"rist-buffer", required_argument, 0, OPT_RIST_BUFFER},
{"srt-group-mode", required_argument, 0, OPT_SRT_GROUP_MODE},
{"srt-passphrase", required_argument, 0, OPT_SRT_PASSPHRASE},
{"srt-pbkeylen", required_argument, 0, OPT_SRT_PBKEYLEN},
{"srt-streamid", required_argument, 0, OPT_SRT_STREAMID},
{"srt-packetfilter", required_argument, 0, OPT_SRT_PACKETFILTER},
{"srt-latency", required_argument, 0, OPT_SRT_LATENCY},
{"dscp", required_argument, 0, OPT_DSCP},
{"al-fec", required_argument, 0, OPT_AL_FEC},
{"al-fec-port", required_argument, 0, OPT_AL_FEC_PORT},
{"provider", required_argument, 0, OPT_PROVIDER},
{"default-provider", required_argument, 0, OPT_DEFAULT_PROVIDER},
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
      rdh_print_help();
      opterr = 1;
      return ARGS_HELP;
    }
  }
  opterr = 1;
  return ARGS_OK;
}

static args_status_t dispatch(rdh_opt_t *p, int c) {
  static args_status_t (*const groups[])(rdh_opt_t *, int) = {rdh_opt_general, rdh_opt_cas, rdh_opt_net};

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
  rdh_opt_t p = {.cfg = cfg};
  args_status_t pst;
  int c;

  pst = prescan(argc, argv, &cfg_path, &configtest, &strict);
  if (pst != ARGS_OK) return pst;
  if (configtest) return rdh_cfg_test(cfg_path, strict) ? ARGS_ERR : ARGS_HELP;

  rdh_cfg_defaults(cfg);
  if (rdh_cfg_load(cfg, cfg_path, strict)) return ARGS_ERR;

  optind = 1;
  /* leading '+': disable GNU getopt argument permutation, so --sid/--sdt stay paired with
     whichever -i preceded them on the command line instead of being reordered */
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    args_status_t st;
    if (c == 'c' || c == OPT_CONFIG_STRICT || c == OPT_CONFIGTEST) continue;
    if (c == 'h') {
      rdh_print_help();
      return ARGS_HELP;
    }
    st = dispatch(&p, c);
    if (st != ARGS_OK) return st;
  }
  if (optind < argc) {
    argerr("unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  return rdh_cli_check(cfg, &p);
}
