/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/config/yamlcfg.h"
#include "../config.h"
#include "../version.h"
#include "priv.h"

static const yamlcfg_key_t keys[] = {
{"input", rdh_apply_input, 0, YAMLCFG_LIST_KEYED},
{"input.sid", rdh_apply_input_sid, 0, 0},
{"input.sdt", rdh_apply_input_sdt, 0, 0},
{"input.provider", rdh_apply_input_provider, 0, 0},
{"mcast", rdh_apply_mcast, 0, 0},
{"out-iface", rdh_apply_out_iface, 0, 0},
{"rtp", rdh_apply_rtp, 0, 0},
{"ttl", rdh_apply_ttl, 0, 0},
{"dscp", rdh_apply_dscp, 0, 0},
{"al-fec", rdh_apply_al_fec, 0, 0},
{"al-fec-port", rdh_apply_al_fec_port, 0, 0},
{"nit", rdh_apply_nit, 0, 0},
{"default-provider", rdh_apply_default_provider, 0, 0},
{"remote", rdh_apply_rist, 0, 1},
{"rist.profile", rdh_apply_profile, 0, 0},
{"rist.secret", rdh_apply_secret, 0, 0},
{"rist.cname", rdh_apply_cname, 0, 0},
{"rist.buffer", rdh_apply_buffer, 0, 0},
{"srt.group-mode", rdh_apply_srt_group_mode, 0, 0},
{"srt.passphrase", rdh_apply_srt_passphrase, 0, 0},
{"srt.pbkeylen", rdh_apply_srt_pbkeylen, 0, 0},
{"srt.streamid", rdh_apply_srt_streamid, 0, 0},
{"srt.packetfilter", rdh_apply_srt_packetfilter, 0, 0},
{"srt.latency", rdh_apply_srt_latency, 0, 0},
{"error", rdh_apply_error, 0, 0},
{"insecure", rdh_apply_insecure, 0, 0},
{"tsid", rdh_apply_tsid, 0, 0},
{"onid", rdh_apply_onid, 0, 0},
{"verbose", rdh_apply_verbose, 0, 0},
{"daemonize", rdh_apply_daemonize, 0, 0},
{"color", rdh_apply_color, 0, 0},
{"metrics.sock", rdh_apply_metrics_sock, 0, 0},
{"metrics.id", rdh_apply_metrics_id, 0, 0},
{"metrics.interval", rdh_apply_metrics_interval, 0, 0},
{"metrics.inspect-ts", rdh_apply_metrics_inspect_ts, 0, 0},
{"metrics.inspect-ts-pids", rdh_apply_metrics_known_pids, 0, 0},
{"cas.algo", rdh_apply_cas_algo, 0, 0},
{"cas.cp-duration", rdh_apply_cas_cp_duration, 0, 0},
{"cas.fallback-clear", rdh_apply_cas_fallback_clear, 0, 0},
{"cas.ecmg", rdh_apply_cas_ecmg, 0, YAMLCFG_LIST_KEYED},
{"cas.ecmg.ecmg-version", rdh_apply_cas_ecmg_version, 0, 0},
{"cas.ecmg.super-id", rdh_apply_cas_super_id, 0, 0},
{"cas.ecmg.ecm-id", rdh_apply_cas_ecm_id, 0, 0},
{"cas.ecmg.ecm-pid", rdh_apply_cas_ecm_pid, 0, 0},
{"cas.ecmg.emmg-port", rdh_apply_cas_emmg_port, 0, 0},
{"cas.ecmg.emmg-max-conns", rdh_apply_cas_emmg_max_conns, 0, 0},
{"cas.ecmg.emmg-version", rdh_apply_cas_emmg_version, 0, 0},
{"cas.ecmg.emmg-reverse", rdh_apply_cas_emmg_reverse, 0, 0},
{"cas.ecmg.emm-pid", rdh_apply_cas_emm_pid, 0, 0},
{"cas.ecmg.resilience", rdh_apply_cas_resilience, 0, 0},
{"cas.ecmg.required", rdh_apply_cas_required, 0, 0},
{"cas.ecmg.cwenc-algo", rdh_apply_cas_cwenc_algo, 0, 0},
{"cas.ecmg.cwenc-aes-mode", rdh_apply_cas_cwenc_aes_mode, 0, 0},
{"cas.ecmg.cwenc-fixed-key", rdh_apply_cas_cwenc_fixed_key, 0, 0},
{"cas.ecmg.cwenc-key-list-a", rdh_apply_cas_cwenc_key_list_a, 1, 0},
{"cas.ecmg.cwenc-key-list-b", rdh_apply_cas_cwenc_key_list_b, 1, 0},
{"biss1.sw", rdh_apply_biss1_sw, 0, 0},
{"biss2.sw", rdh_apply_biss2_sw, 0, 0},
{"biss2.emit-esw", rdh_apply_biss2_emit_esw, 0, 0},
{"biss2.ca-receivers", rdh_apply_biss2_ca_receivers, 1, 0},
{"biss2.ca-session-id", rdh_apply_biss2_ca_session_id, 0, 0},
};

int rdh_cfg_load(config_t *cfg, const char *path, int strict) {
  yamlcfg_t y;
  int mode = strict ? YAMLCFG_STRICT : 0;
  int rc = yamlcfg_load_items(&y, TOOL_NAME, mode, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], cfg, rdh_item_hook);
  return rc == YAMLCFG_ERROR ? -1 : 0;
}

static void warn_if(yamlcfg_t *y, int cond, const char *msg) {
  if (cond) yamlcfg_warn(y, "%s", msg);
}

int rdh_cfg_test(const char *path, int strict) {
  yamlcfg_t y;
  config_t cfg;
  size_t pwlen;

  rdh_cfg_defaults(&cfg);
  if (yamlcfg_load_items(&y, TOOL_NAME, strict ? YAMLCFG_CHECK | YAMLCFG_STRICT : YAMLCFG_CHECK, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], &cfg, rdh_item_hook) != YAMLCFG_LOADED) return -1;
  pwlen = strlen(cfg.srt_passphrase);

  warn_if(&y, cfg.n_inputs == 0, "input not set (required unless given on the command line)");
  warn_if(&y, !cfg.mcast_port && cfg.n_rist == 0 && cfg.n_srt == 0, "mcast or rist not set (one is required unless given on the command line)");
  warn_if(&y, (cfg.metrics_sock || cfg.metrics_interval_s) && !cfg.metrics_id, "metrics.sock and metrics.interval require metrics.id");
  warn_if(&y, cfg.metrics_inspect_ts != METRICS_INSPECT_TS_OFF && !cfg.metrics_id, "metrics.inspect-ts requires metrics.id");
  warn_if(&y, cfg.al_fec_l && !cfg.al_fec_port, "al-fec requires al-fec-port");
  warn_if(&y, !cfg.al_fec_l && cfg.al_fec_port, "al-fec-port has no effect without al-fec");
  warn_if(&y, cfg.al_fec_l && !cfg.rtp, "al-fec requires rtp");
  warn_if(&y, cfg.al_fec_l && !cfg.mcast_port, "al-fec has no effect without mcast");
  warn_if(&y, cfg.n_rist == 0 && (cfg.rist_profile_given || cfg.rist_secret[0] || cfg.rist_cname[0] || cfg.rist_buffer_ms), "profile/secret/cname/buffer have no effect without rist");
  warn_if(&y, cfg.n_rist > 0 && cfg.rist_secret[0] && cfg.rist_profile != RIST_PROF_MAIN, "rist-secret requires rist-profile main");
  warn_if(&y, cfg.n_srt > 1 && cfg.srt_group_mode == SRT_BOND_NONE, "bonding several srt:// peers requires srt.group-mode");
  warn_if(&y, cfg.n_srt == 1 && cfg.srt_group_mode != SRT_BOND_NONE, "srt.group-mode has no effect with a single srt:// peer");
  warn_if(&y, cfg.n_srt == 0 && (cfg.srt_group_mode != SRT_BOND_NONE || cfg.srt_passphrase[0] || cfg.srt_pbkeylen || cfg.srt_streamid[0] || cfg.srt_packetfilter[0] || cfg.srt_latency_ms), "srt.* settings need an srt:// peer");
  warn_if(&y, pwlen && (pwlen < 10 || pwlen > 79), "srt.passphrase must be 10..79 characters");
  warn_if(&y, cfg.srt_pbkeylen && !pwlen, "srt.pbkeylen requires srt.passphrase");
  warn_if(&y, cfg.any_cas_flag && cfg.cas_algo == CAS_ALGO_NONE, "cas.* options require cas.algo");
  if (cas_args_validate(TOOL_NAME, cfg.cas_algo, cfg.cas_vendors, cfg.n_cas_vendors, cfg.biss2_enabled, cfg.biss1_enabled, cfg.biss2_ca_enabled, cfg.biss2_emit_esw, cfg.biss2_ca_session_id_given, cfg.cas_cp_duration_ms) != 0)
    y.warnings++;
  return yamlcfg_report(&y);
}
