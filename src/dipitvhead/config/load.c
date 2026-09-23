/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/config/yamlcfg.h"
#include "../config.h"
#include "../version.h"
#include "priv.h"

static const yamlcfg_key_t keys[] = {
{"input", tvh_apply_input, 0, YAMLCFG_LIST_KEYED},
{"input.pmt-pid", tvh_apply_input_pmt_pid, 0, 0},
{"input.sid", tvh_apply_input_sid, 0, 0},
{"input.sdt", tvh_apply_input_sdt, 0, 0},
{"input.provider", tvh_apply_input_provider, 0, 0},
{"input.iface", tvh_apply_input_iface, 0, 0},
{"input.strip-eit", tvh_apply_input_strip_eit, 0, 0},
{"input.strip", tvh_apply_input_strip, 0, 0},
{"input.hbbtv", tvh_apply_input_hbbtv, 0, 0},
{"input.hbbtv-org-id", tvh_apply_input_hbbtv_org_id, 0, 0},
{"input.hbbtv-app-id", tvh_apply_input_hbbtv_app_id, 0, 0},
{"input.rist-profile-in", tvh_apply_input_rist_profile, 0, 0},
{"input.srt-passphrase-in", tvh_apply_input_srt_passphrase, 0, 0},
{"input.srt-pbkeylen-in", tvh_apply_input_srt_pbkeylen, 0, 0},
{"input.srt-streamid-in", tvh_apply_input_srt_streamid, 0, 0},
{"input.srt-packetfilter-in", tvh_apply_input_srt_packetfilter, 0, 0},
{"input.srt-latency-in", tvh_apply_input_srt_latency, 0, 0},
{"mcast", tvh_apply_mcast, 0, 0},
{"out-iface", tvh_apply_out_iface, 0, 0},
{"udp", tvh_apply_udp, 0, 0},
{"ttl", tvh_apply_ttl, 0, 0},
{"dscp", tvh_apply_dscp, 0, 0},
{"al-fec", tvh_apply_al_fec, 0, 0},
{"al-fec-port", tvh_apply_al_fec_port, 0, 0},
{"remote", tvh_apply_rist, 0, 1},
{"rist.profile", tvh_apply_profile, 0, 0},
{"rist.secret", tvh_apply_secret, 0, 0},
{"rist.cname", tvh_apply_cname, 0, 0},
{"rist.buffer", tvh_apply_buffer, 0, 0},
{"srt.group-mode", tvh_apply_srt_group_mode, 0, 0},
{"srt.passphrase", tvh_apply_srt_passphrase, 0, 0},
{"srt.pbkeylen", tvh_apply_srt_pbkeylen, 0, 0},
{"srt.streamid", tvh_apply_srt_streamid, 0, 0},
{"srt.packetfilter", tvh_apply_srt_packetfilter, 0, 0},
{"srt.latency", tvh_apply_srt_latency, 0, 0},
{"nit", tvh_apply_nit, 0, 0},
{"default-provider", tvh_apply_default_provider, 0, 0},
{"bitrate", tvh_apply_bitrate, 0, 0},
{"stuff", tvh_apply_stuff, 0, 0},
{"burst-limit", tvh_apply_burst_limit, 0, 0},
{"error", tvh_apply_error, 0, 0},
{"insecure", tvh_apply_insecure, 0, 0},
{"tsid", tvh_apply_tsid, 0, 0},
{"onid", tvh_apply_onid, 0, 0},
{"verbose", tvh_apply_verbose, 0, 0},
{"daemonize", tvh_apply_daemonize, 0, 0},
{"color", tvh_apply_color, 0, 0},
{"metrics.sock", tvh_apply_metrics_sock, 0, 0},
{"metrics.id", tvh_apply_metrics_id, 0, 0},
{"metrics.interval", tvh_apply_metrics_interval, 0, 0},
{"metrics.inspect-ts", tvh_apply_metrics_inspect_ts, 0, 0},
{"metrics.inspect-ts-pids", tvh_apply_metrics_known_pids, 0, 0},
{"cas.algo", tvh_apply_cas_algo, 0, 0},
{"cas.pids", tvh_apply_cas_pids, 0, 0},
{"cas.cp-duration", tvh_apply_cas_cp_duration, 0, 0},
{"cas.fallback-clear", tvh_apply_cas_fallback_clear, 0, 0},
{"cas.ecmg", tvh_apply_cas_ecmg, 0, YAMLCFG_LIST_KEYED},
{"cas.ecmg.ecmg-version", tvh_apply_cas_ecmg_version, 0, 0},
{"cas.ecmg.super-id", tvh_apply_cas_super_id, 0, 0},
{"cas.ecmg.ecm-id", tvh_apply_cas_ecm_id, 0, 0},
{"cas.ecmg.ecm-pid", tvh_apply_cas_ecm_pid, 0, 0},
{"cas.ecmg.emmg-port", tvh_apply_cas_emmg_port, 0, 0},
{"cas.ecmg.emmg-max-conns", tvh_apply_cas_emmg_max_conns, 0, 0},
{"cas.ecmg.emmg-version", tvh_apply_cas_emmg_version, 0, 0},
{"cas.ecmg.emmg-reverse", tvh_apply_cas_emmg_reverse, 0, 0},
{"cas.ecmg.emm-pid", tvh_apply_cas_emm_pid, 0, 0},
{"cas.ecmg.resilience", tvh_apply_cas_resilience, 0, 0},
{"cas.ecmg.required", tvh_apply_cas_required, 0, 0},
{"cas.ecmg.cwenc-algo", tvh_apply_cas_cwenc_algo, 0, 0},
{"cas.ecmg.cwenc-aes-mode", tvh_apply_cas_cwenc_aes_mode, 0, 0},
{"cas.ecmg.cwenc-fixed-key", tvh_apply_cas_cwenc_fixed_key, 0, 0},
{"cas.ecmg.cwenc-key-list-a", tvh_apply_cas_cwenc_key_list_a, 1, 0},
{"cas.ecmg.cwenc-key-list-b", tvh_apply_cas_cwenc_key_list_b, 1, 0},
{"biss1.sw", tvh_apply_biss1_sw, 0, 0},
{"biss2.sw", tvh_apply_biss2_sw, 0, 0},
{"biss2.emit-esw", tvh_apply_biss2_emit_esw, 0, 0},
{"biss2.ca-receivers", tvh_apply_biss2_ca_receivers, 1, 0},
{"biss2.ca-session-id", tvh_apply_biss2_ca_session_id, 0, 0},
};

int tvh_cfg_load(config_t *cfg, const char *path, int strict) {
  yamlcfg_t y;
  int mode = strict ? YAMLCFG_STRICT : 0;
  int rc = yamlcfg_load_items(&y, TOOL_NAME, mode, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], cfg, tvh_item_hook);
  return rc == YAMLCFG_ERROR ? -1 : 0;
}

typedef struct {
  yamlcfg_t *y;
  int fatal;
} report_t;

static void warn_report(void *ud, int fatal, const char *msg) {
  report_t *r = ud;
  r->fatal += fatal;
  yamlcfg_warn(r->y, "%s", msg);
}

static void warn_if(yamlcfg_t *y, int cond, const char *msg) {
  if (cond) yamlcfg_warn(y, "%s", msg);
}

int tvh_cfg_test(const char *path, int strict) {
  yamlcfg_t y;
  config_t cfg;
  report_t r = {&y, 0};
  int fatal;
  tvh_cfg_defaults(&cfg);
  if (yamlcfg_load_items(&y, TOOL_NAME, strict ? YAMLCFG_CHECK | YAMLCFG_STRICT : YAMLCFG_CHECK, path, DEFAULT_CONFIG_PATH, keys, sizeof keys / sizeof keys[0], &cfg, tvh_item_hook) != YAMLCFG_LOADED) return -1;
  warn_if(&y, cfg.n_inputs == 0, "input not set (required unless given on the command line)");
  warn_if(&y, !cfg.mcast_port && cfg.n_rist == 0 && cfg.n_srt == 0, "mcast or rist not set (one is required unless given on the command line)");
  fatal = tvh_cfg_check(&cfg, 1, warn_report, &r);
  if (fatal > r.fatal) y.warnings += (unsigned)(fatal - r.fatal);
  return yamlcfg_report(&y);
}
