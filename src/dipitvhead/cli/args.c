/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <getopt.h>

#include "lib/helper/log.h"

#include "priv.h"

static const char shortopts[] = "+i:p:m:I:O:uT:n:s:b:SBe:kvdhR:c:";

static const struct option longopts[] = {
  {"input", required_argument, 0, 'i'},
  {"pmt-pid", required_argument, 0, 'p'},
  {"mcast", required_argument, 0, 'm'},
  {"iface", required_argument, 0, 'I'},
  {"out-iface", required_argument, 0, 'O'},
  {"udp", no_argument, 0, 'u'},
  {"ttl", required_argument, 0, 'T'},
  {"nit", required_argument, 0, 'n'},
  {"sdt", required_argument, 0, 's'},
  {"bitrate", required_argument, 0, 'b'},
  {"stuff", no_argument, 0, 'S'},
  {"burst-limit", no_argument, 0, 'B'},
  {"pcr-mode", required_argument, 0, OPT_PCR_MODE},
  {"pcr-lead-ms", required_argument, 0, OPT_PCR_LEAD_MS},
  {"strip-eit", no_argument, 0, OPT_STRIP_EIT},
  {"hbbtv", required_argument, 0, OPT_HBBTV},
  {"hbbtv-org-id", required_argument, 0, OPT_HBBTV_ORG_ID},
  {"hbbtv-app-id", required_argument, 0, OPT_HBBTV_APP_ID},
  {"error", required_argument, 0, 'e'},
  {"insecure", no_argument, 0, 'k'},
  {"tsid", required_argument, 0, OPT_TSID},
  {"onid", required_argument, 0, OPT_ONID},
  {"sid", required_argument, 0, OPT_SID},
  {"verbose", no_argument, 0, 'v'},
  {"color", required_argument, 0, OPT_COLOR},
  {"cas-algo", required_argument, 0, OPT_CAS_ALGO},
  {"cas-ecmg", required_argument, 0, OPT_CAS_ECMG},
  {"cas-ecmg-version", required_argument, 0, OPT_CAS_ECMG_VERSION},
  {"cas-super-id", required_argument, 0, OPT_CAS_SUPER_ID},
  {"cas-ecm-id", required_argument, 0, OPT_CAS_ECM_ID},
  {"cas-ecm-pid", required_argument, 0, OPT_CAS_ECM_PID},
  {"cas-emmg-listen", required_argument, 0, OPT_CAS_EMMG_LISTEN},
  {"cas-emmg-version", required_argument, 0, OPT_CAS_EMMG_VERSION},
  {"cas-emmg-max-conns", required_argument, 0, OPT_CAS_EMMG_MAX_CONNS},
  {"cas-emmg-reverse", required_argument, 0, OPT_CAS_EMMG_REVERSE},
  {"cas-emm-pid", required_argument, 0, OPT_CAS_EMM_PID},
  {"cas-pids", required_argument, 0, OPT_CAS_PIDS},
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
  {"daemonize", no_argument, 0, 'd'},
  {"strip", required_argument, 0, OPT_STRIP},
  {"rist-profile-in", required_argument, 0, OPT_RIST_PROFILE_IN},
  {"rist-encryption-type-in", required_argument, 0, OPT_RIST_ENCRYPTION_TYPE_IN},
  {"dscp", required_argument, 0, OPT_DSCP},
  {"al-fec", required_argument, 0, OPT_AL_FEC},
  {"al-fec-port", required_argument, 0, OPT_AL_FEC_PORT},
  {"srt-passphrase-in", required_argument, 0, OPT_SRT_PASSPHRASE_IN},
  {"srt-pbkeylen-in", required_argument, 0, OPT_SRT_PBKEYLEN_IN},
  {"srt-streamid-in", required_argument, 0, OPT_SRT_STREAMID_IN},
  {"srt-packetfilter-in", required_argument, 0, OPT_SRT_PACKETFILTER_IN},
  {"srt-latency-in", required_argument, 0, OPT_SRT_LATENCY_IN},
  {"jitter-ms", required_argument, 0, OPT_JITTER_MS},
  {"srt-group-mode", required_argument, 0, OPT_SRT_GROUP_MODE},
  {"srt-passphrase", required_argument, 0, OPT_SRT_PASSPHRASE},
  {"srt-pbkeylen", required_argument, 0, OPT_SRT_PBKEYLEN},
  {"srt-streamid", required_argument, 0, OPT_SRT_STREAMID},
  {"srt-packetfilter", required_argument, 0, OPT_SRT_PACKETFILTER},
  {"srt-latency", required_argument, 0, OPT_SRT_LATENCY},
  {"provider", required_argument, 0, OPT_PROVIDER},
  {"default-provider", required_argument, 0, OPT_DEFAULT_PROVIDER},
  {"config", required_argument, 0, 'c'},
  {"config-strict", no_argument, 0, OPT_CONFIG_STRICT},
  {"configtest", no_argument, 0, OPT_CONFIGTEST},
  {"help", no_argument, 0, 'h'},
  {0, 0, 0, 0}};

static void report_cli(void *ud, int fatal, const char *msg) {
  (void)ud;
  if (fatal) argerr("%s", msg);
  else       log_line(TOOL_NAME ": %s", msg);
}

static args_status_t prescan(int argc, char **argv, const char **cfg_path, int *configtest, int *strict) {
  int c;
  optind = 1;
  opterr = 0;
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    if (c == 'c') *cfg_path = optarg;
    if (c == OPT_CONFIGTEST) *configtest = 1;
    if (c == OPT_CONFIG_STRICT) *strict = 1;
    if (c == 'h') {
      tvh_print_help();
      opterr = 1;
      return ARGS_HELP;
    }
  }
  opterr = 1;
  return ARGS_OK;
}

static args_status_t dispatch(tvh_opt_t *p, int c) {
  static args_status_t (*const groups[])(tvh_opt_t *, int) = {tvh_opt_input, tvh_opt_general, tvh_opt_net, tvh_opt_cas};
  config_t *cfg = p->cfg;

  p->in = p->cli_inputs ? &cfg->inputs[cfg->n_inputs - 1] : NULL;
  p->vd = p->cli_vendors ? &cfg->cas_vendors[cfg->n_cas_vendors - 1] : NULL;
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
  tvh_opt_t p = {.cfg = cfg};
  args_status_t pst;
  int c;

  pst = prescan(argc, argv, &cfg_path, &configtest, &strict);
  if (pst != ARGS_OK) return pst;
  if (configtest) return tvh_cfg_test(cfg_path, strict) ? ARGS_ERR : ARGS_HELP;

  tvh_cfg_defaults(cfg);
  if (tvh_cfg_load(cfg, cfg_path, strict)) return ARGS_ERR;

  optind = 1;
  /* leading '+': disable GNU getopt argument permutation, so per-input options stay paired
     with whichever -i preceded them instead of being reordered */
  while ((c = getopt_long(argc, argv, shortopts, longopts, NULL)) != -1) {
    args_status_t st;
    if (c == 'c' || c == OPT_CONFIG_STRICT || c == OPT_CONFIGTEST) continue;
    if (c == 'h') {
      tvh_print_help();
      return ARGS_HELP;
    }
    st = dispatch(&p, c);
    if (st != ARGS_OK) return st;
  }
  if (optind < argc) {
    argerr("unexpected argument: %s", argv[optind]);
    return ARGS_ERR;
  }
  if (tvh_cfg_check(cfg, 0, report_cli, NULL)) return ARGS_ERR;
  tvh_finalize(cfg);
  return ARGS_OK;
}
