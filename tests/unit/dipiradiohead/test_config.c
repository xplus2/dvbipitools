/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "../cfg_fixture.h"
#include "dipiradiohead/config.h"
#include "dipiradiohead/input/inputset.h"
#include "dipiradiohead/mux/tspacketizer.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/log.h"
#include "lib/net/netconnect.h"

#define A128 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define BASE "input: http://example.org/a\nmcast: 239.1.2.3:5000\n"
#define ECMG(body) BASE "cas:\n  algo: csa2\n  ecmg:\n    - tcp://h1:2222:\n" body

static const cfg_field_case_t field_cases[] = {
  CFG_FIELD(config_t, "input: http://example.org/a\n", CFG_INT, n_inputs, 1, NULL),
  CFG_FIELD(config_t, "input:\n  - http://example.org/a:\n      sid: 77\n", CFG_UINT, inputs[0].sid, 77, NULL),
  CFG_FIELD(config_t, "input:\n  - http://example.org/a:\n      jitter-ms: 120\n", CFG_UINT, inputs[0].jitter_ms, 120, NULL),
  CFG_FIELD(config_t, "input:\n  - http://example.org/a:\n      sdt: Name\n", CFG_CHARARR, inputs[0].sdt_text, 0, "Name"),
  CFG_FIELD(config_t, "input:\n  - http://example.org/a:\n      provider: Prov\n", CFG_CHARARR, inputs[0].provider_text, 0, "Prov"),
  CFG_FIELD(config_t, "mcast: 239.1.2.3:5000\n", CFG_UINT, mcast_port, 5000, NULL),
  CFG_FIELD(config_t, "mcast: 239.1.2.3:5000\n", CFG_CHARARR, mcast_group, 0, "239.1.2.3"),
  CFG_FIELD(config_t, "mcast: 239.1.2.3:5000\nout-iface: eth9\n", CFG_STRPTR, iface, 0, "eth9"),
  CFG_FIELD(config_t, "rtp: on\n", CFG_INT, rtp, 1, NULL),
  CFG_FIELD(config_t, "ttl: 9\n", CFG_UINT, ttl, 9, NULL),
  CFG_FIELD(config_t, "dscp: voice\n", CFG_INT, dscp, NET_DSCP_VOICE_BEARER, NULL),
  CFG_FIELD(config_t, "al-fec: 10:5\n", CFG_UINT, al_fec_l, 10, NULL),
  CFG_FIELD(config_t, "al-fec: 10:5\n", CFG_UINT, al_fec_d, 5, NULL),
  CFG_FIELD(config_t, "al-fec-port: 7004\n", CFG_UINT, al_fec_port, 7004, NULL),
  CFG_FIELD(config_t, "nit: Net\n", CFG_CHARARR, nit_text, 0, "Net"),
  CFG_FIELD(config_t, "default-provider: DP\n", CFG_CHARARR, default_provider_text, 0, "DP"),
  CFG_FIELD(config_t, "remote: rist://1.2.3.4:6000\n", CFG_INT, n_rist, 1, NULL),
  CFG_FIELD(config_t, "remote: srt://1.2.3.4:7000\n", CFG_INT, n_srt, 1, NULL),
  CFG_FIELD(config_t, "remote: srt://1.2.3.4:7000\n", CFG_UINT, srt_port[0], 7000, NULL),
  CFG_FIELD(config_t, "rist:\n  profile: main\n", CFG_INT, rist_profile, RIST_PROF_MAIN, NULL),
  CFG_FIELD(config_t, "rist:\n  profile: main\n", CFG_INT, rist_profile_given, 1, NULL),
  CFG_FIELD(config_t, "rist:\n  secret: s3cret\n", CFG_CHARARR, rist_secret, 0, "s3cret"),
  CFG_FIELD(config_t, "rist:\n  encryption-type: 256\n", CFG_INT, rist_key_size, 256, NULL),
  CFG_FIELD(config_t, "rist:\n  cname: cam\n", CFG_CHARARR, rist_cname, 0, "cam"),
  CFG_FIELD(config_t, "rist:\n  buffer: 800\n", CFG_UINT, rist_buffer_ms, 800, NULL),
  CFG_FIELD(config_t, "srt:\n  group-mode: backup\n", CFG_INT, srt_group_mode, SRT_BOND_BACKUP, NULL),
  CFG_FIELD(config_t, "srt:\n  group-mode: broadcast\n", CFG_INT, srt_group_mode, SRT_BOND_BROADCAST, NULL),
  CFG_FIELD(config_t, "srt:\n  passphrase: 0123456789ab\n", CFG_CHARARR, srt_passphrase, 0, "0123456789ab"),
  CFG_FIELD(config_t, "srt:\n  pbkeylen: 24\n", CFG_INT, srt_pbkeylen, 24, NULL),
  CFG_FIELD(config_t, "srt:\n  streamid: sid\n", CFG_CHARARR, srt_streamid, 0, "sid"),
  CFG_FIELD(config_t, "srt:\n  packetfilter: fec\n", CFG_CHARARR, srt_packetfilter, 0, "fec"),
  CFG_FIELD(config_t, "srt:\n  latency: 300\n", CFG_UINT, srt_latency_ms, 300, NULL),
  CFG_FIELD(config_t, "error: 12\n", CFG_LONG, error_retry_s, 12, NULL),
  CFG_FIELD(config_t, "insecure: on\n", CFG_INT, insecure_tls, 1, NULL),
  CFG_FIELD(config_t, "tsid: 77\n", CFG_UINT, tsid, 77, NULL),
  CFG_FIELD(config_t, "onid: 78\n", CFG_UINT, onid, 78, NULL),
  CFG_FIELD(config_t, "verbose: on\n", CFG_INT, verbose, 1, NULL),
  CFG_FIELD(config_t, "daemonize: on\n", CFG_INT, daemonize, 1, NULL),
  CFG_FIELD(config_t, "color: never\n", CFG_INT, color_mode, LOG_COLOR_NEVER, NULL),
  CFG_FIELD(config_t, "metrics:\n  sock: /tmp/r.sock\n", CFG_STRPTR, metrics_sock, 0, "/tmp/r.sock"),
  CFG_FIELD(config_t, "metrics:\n  id: rh\n", CFG_STRPTR, metrics_id, 0, "rh"),
  CFG_FIELD(config_t, "metrics:\n  interval: 33\n", CFG_UINT, metrics_interval_s, 33, NULL),
  CFG_FIELD(config_t, "metrics:\n  inspect-ts: medium\n", CFG_INT, metrics_inspect_ts, METRICS_INSPECT_TS_MEDIUM, NULL),
  CFG_FIELD(config_t, "metrics:\n  inspect-ts-pids: 256,257\n", CFG_UINT, metrics_n_known_pids, 2, NULL),
  CFG_FIELD(config_t, "cas:\n  algo: csa2\n", CFG_INT, cas_algo, CAS_ALGO_CSA2, NULL),
  CFG_FIELD(config_t, "cas:\n  cp-duration: 7000\n", CFG_UINT, cas_cp_duration_ms, 7000, NULL),
  CFG_FIELD(config_t, "cas:\n  fallback-clear: on\n", CFG_INT, cas_fallback_clear, 1, NULL),
  CFG_FIELD(config_t, ECMG("        ecmg-version: 3\n"), CFG_UINT, cas_vendors[0].ecmg_version, 3, NULL),
  CFG_FIELD(config_t, ECMG("        super-id: 0x1234\n"), CFG_UINT, cas_vendors[0].super_cas_id, 0x1234, NULL),
  CFG_FIELD(config_t, ECMG("        ecm-id: 5\n"), CFG_UINT, cas_vendors[0].ecm_id, 5, NULL),
  CFG_FIELD(config_t, ECMG("        ecm-pid: 6000\n"), CFG_UINT, cas_vendors[0].ecm_pid, 6000, NULL),
  CFG_FIELD(config_t, ECMG("        emmg-listen: 127.0.0.1:9999\n"), CFG_UINT, cas_vendors[0].emmg_port, 9999, NULL),
  CFG_FIELD(config_t, ECMG("        emmg-max-conns: 4\n"), CFG_UINT, cas_vendors[0].emmg_max_conns, 4, NULL),
  CFG_FIELD(config_t, ECMG("        emmg-version: 3\n"), CFG_UINT, cas_vendors[0].emmg_version, 3, NULL),
  CFG_FIELD(config_t, ECMG("        emmg-reverse: tcp://r1:4444\n"), CFG_UINT, cas_vendors[0].emmg_reverse_port, 4444, NULL),
  CFG_FIELD(config_t, ECMG("        emm-pid: 6001\n"), CFG_UINT, cas_vendors[0].emm_pid, 6001, NULL),
  CFG_FIELD(config_t, ECMG("        resilience: cycling\n"), CFG_INT, cas_vendors[0].resilience, CAS_OUTAGE_CYCLING, NULL),
  CFG_FIELD(config_t, ECMG("        required: on\n"), CFG_INT, cas_vendors[0].required, 1, NULL),
  CFG_FIELD(config_t, ECMG("        cwenc-algo: aes128\n"), CFG_CHARARR, cas_vendors[0].cwenc_algorithm, 0, "aes128"),
  CFG_FIELD(config_t, ECMG("        cwenc-aes-mode: ecb\n"), CFG_CHARARR, cas_vendors[0].cwenc_aes_mode, 0, "ecb"),
  CFG_FIELD(config_t, ECMG("        cwenc-fixed-key: 00112233445566778899aabbccddeeff\n"), CFG_CHARARR, cas_vendors[0].cwenc_fixed_key_hex, 0, "00112233445566778899aabbccddeeff"),
  CFG_FIELD(config_t, "biss1:\n  sw: 001122334455\n", CFG_INT, biss1_enabled, 1, NULL),
  CFG_FIELD(config_t, "biss2:\n  sw: 00112233445566778899aabbccddeeff\n", CFG_INT, biss2_enabled, 1, NULL),
  CFG_FIELD(config_t, "biss2:\n  sw: 00112233445566778899aabbccddeeff\n  emit-esw: 00112233445566778899aabbccddeeff\n", CFG_INT, biss2_emit_esw, 1, NULL),
  CFG_FIELD(config_t, "biss2:\n  ca-session-id: 0x10\n", CFG_UINT, biss2_ca_session_id, 16, NULL),
  CFG_FIELD(config_t, "biss2:\n  ca-session-id: 0x10\n", CFG_INT, biss2_ca_session_id_given, 1, NULL),
};

static const char *const bad_cases[] = {
  "input:\n  - http://example.org/a:\n      sid: 0\n",
  "input:\n  - http://example.org/a:\n      jitter-ms: 0\n",
  "input:\n  - http://example.org/a:\n      sdt: " A128 A128 "\n",
  "sid: 5\n",
  "mcast: nonsense\n",
  "rtp: maybe\n",
  "ttl: 0\n",
  "dscp: loud\n",
  "al-fec: 41:1\n",
  "al-fec-port: 0\n",
  "nit: " A128 A128 A128 "\n",
  "remote: http://x/y\n",
  "remote: rist://1.2.3.4:6000\nremote: srt://1.2.3.4:7000\n",
  "remote: srt://1.2.3.4:7000\nremote: rist://1.2.3.4:6000\n",
  "remote: srt://@1.2.3.4:7000\n",
  "remote: srt://nonsense\n",
  "rist:\n  profile: ultra\n",
  "rist:\n  secret: " A128 A128 "\n",
  "rist:\n  encryption-type: 192\n",
  "rist:\n  buffer: 0\n",
  "srt:\n  group-mode: both\n",
  "srt:\n  pbkeylen: 20\n",
  "srt:\n  latency: 0\n",
  "error: abc\n",
  "tsid: 0\n",
  "onid: 70000\n",
  "metrics:\n  interval: 0\n",
  "metrics:\n  inspect-ts-pids: x\n",
  "metrics:\n  inspect-ts: loud\n",
  "cas:\n  algo: rot13\n",
  "cas:\n  cp-duration: x\n",
  "cas:\n  fallback-clear: maybe\n",
  "cas:\n  ecmg:\n    - nocolon:\n        super-id: 1\n",
  ECMG("        required: maybe\n"),
  ECMG("        ecmg-version: 9\n"),
  ECMG("        emmg-max-conns: 0\n"),
  ECMG("        resilience: sleepy\n"),
  ECMG("        cwenc-algo: rot13\n"),
  ECMG("        cwenc-aes-mode: cbc\n"),
  "input:\n  - http://example.org/a\n  - sid: 3\n",
  "cas:\n  ecmg-version: 3\n",
  "biss1:\n  sw: zz\n",
  "biss2:\n  sw: zz\n",
  "biss2:\n  emit-esw: zz\n",
  "biss2:\n  ca-session-id: zz\n",
};

static cfg_fixture_t g_fx;

static int load(const char *text, config_t *cfg) {
  int rc;

  cfg_fixture_write(&g_fx, text);
  rdh_cfg_defaults(cfg);
  rc = rdh_cfg_load(cfg, g_fx.path, 1);
  cfg_fixture_remove(&g_fx);
  return rc;
}

START_TEST(each_key_sets_its_config_field) {
  const cfg_field_case_t *c = &field_cases[_i];
  config_t cfg;

  ck_assert_int_eq(load(c->yaml, &cfg), 0);
  cfg_field_check(&cfg, c);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(invalid_values_are_rejected) {
  config_t cfg;

  ck_assert_int_eq(load(bad_cases[_i], &cfg), -1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(key_list_files_and_ca_receivers_are_accepted) {
  config_t cfg;
  cfg_fixture_write_with_file(&g_fx, BASE "cas:\n  algo: csa2\n  ecmg:\n    - tcp://h1:2222:\n        cwenc-key-list-a: @F@\n        cwenc-key-list-b: @F@\n", "@F@", "keys.bin");
  rdh_cfg_defaults(&cfg);
  ck_assert_int_eq(rdh_cfg_load(&cfg, g_fx.path, 1), 0);
  ck_assert_ptr_nonnull(strstr(cfg.cas_vendors[0].cwenc_key_list_a_path, "keys.bin"));
  ck_assert_ptr_nonnull(strstr(cfg.cas_vendors[0].cwenc_key_list_b_path, "keys.bin"));
  cfg_fixture_remove(&g_fx);
  yamlcfg_strpool_free(cfg.str_pool);
  cfg_fixture_write_with_file(&g_fx, BASE "biss2:\n  ca-receivers: @F@\n", "@F@", "recv.pem");
  rdh_cfg_defaults(&cfg);
  ck_assert_int_eq(rdh_cfg_load(&cfg, g_fx.path, 1), 0);
  ck_assert_int_eq(cfg.biss2_ca_enabled, 1);
  ck_assert_ptr_nonnull(strstr(cfg.biss2_ca_receivers_dir, "recv.pem"));
  cfg_fixture_remove(&g_fx);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(too_many_inputs_and_peers_are_rejected) {
  char yaml[4096];
  size_t n = 0;
  config_t cfg;
  n += (size_t)snprintf(yaml + n, sizeof yaml - n, "input:\n");
  for (int i = 0; i < RADIOHEAD_MAX_INPUTS + 1; i++) n += (size_t)snprintf(yaml + n, sizeof yaml - n, "  - http://example.org/%d\n", i);
  ck_assert_int_eq(load(yaml, &cfg), -1);
  yamlcfg_strpool_free(cfg.str_pool);
  n = (size_t)snprintf(yaml, sizeof yaml, "remote:\n");
  for (int i = 0; i < ARGS_MAX_RIST_PEERS + 1; i++) n += (size_t)snprintf(yaml + n, sizeof yaml - n, "  - rist://1.2.3.%d:6000\n", i + 1);
  ck_assert_int_eq(load(yaml, &cfg), -1);
  yamlcfg_strpool_free(cfg.str_pool);
  n = (size_t)snprintf(yaml, sizeof yaml, "remote:\n");
  for (int i = 0; i < ARGS_MAX_SRT_PEERS + 1; i++) n += (size_t)snprintf(yaml + n, sizeof yaml - n, "  - srt://1.2.3.%d:7000\n", i + 1);
  ck_assert_int_eq(load(yaml, &cfg), -1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(unknown_key_and_missing_file_behave_per_strictness) {
  config_t cfg;
  cfg_fixture_write(&g_fx, "bogus-key: 1\n");
  rdh_cfg_defaults(&cfg);
  ck_assert_int_eq(rdh_cfg_load(&cfg, g_fx.path, 1), -1);
  rdh_cfg_defaults(&cfg);
  ck_assert_int_eq(rdh_cfg_load(&cfg, g_fx.path, 0), 0);
  cfg_fixture_remove(&g_fx);
  yamlcfg_strpool_free(cfg.str_pool);
  rdh_cfg_defaults(&cfg);
  ck_assert_int_eq(rdh_cfg_load(&cfg, "/nonexistent/dipiradiohead.yaml", 0), -1);
}
END_TEST

START_TEST(defaults_are_the_documented_values) {
  config_t cfg;
  rdh_cfg_defaults(&cfg);
  ck_assert_uint_eq(cfg.tsid, 1u);
  ck_assert_uint_eq(cfg.onid, 1u);
  ck_assert_uint_eq(cfg.cas_cp_duration_ms, 10000u);
  ck_assert_int_eq(cfg.n_inputs, 0);
}
END_TEST

typedef struct {
  const char *yaml;
  int strict_result;
} cfgtest_case_t;

#define OK_BASE BASE
#define RIST_BASE "input: http://example.org/a\nremote: rist://1.2.3.4:6000\n"
#define SRT_BASE "input: http://example.org/a\nremote: srt://1.2.3.4:7000\n"

static const cfgtest_case_t cfgtest_cases[] = {
  {OK_BASE, 0},
  {"mcast: 239.1.2.3:5000\n", -1},
  {"input: http://example.org/a\n", -1},
  {OK_BASE "metrics:\n  sock: /tmp/x.sock\n", -1},
  {OK_BASE "metrics:\n  interval: 10\n", -1},
  {OK_BASE "metrics:\n  inspect-ts: basic\n", -1},
  {OK_BASE "al-fec: 10:5\n", -1},
  {OK_BASE "al-fec-port: 7004\n", -1},
  {OK_BASE "rtp: on\nal-fec: 10:5\nal-fec-port: 7004\n", 0},
  {RIST_BASE "al-fec: 10:5\nal-fec-port: 7004\nrtp: on\n", -1},
  {OK_BASE "rist:\n  profile: main\n", -1},
  {RIST_BASE "rist:\n  secret: s3cret\n", -1},
  {RIST_BASE "rist:\n  encryption-type: 128\n", -1},
  {RIST_BASE "rist:\n  profile: main\n  secret: s3cret\n  encryption-type: 128\n", 0},
  {"input: http://example.org/a\nremote:\n  - srt://1.2.3.4:7000\n  - srt://1.2.3.5:7000\n", -1},
  {SRT_BASE "srt:\n  group-mode: backup\n", -1},
  {OK_BASE "srt:\n  latency: 100\n", -1},
  {SRT_BASE "srt:\n  passphrase: short\n", -1},
  {SRT_BASE "srt:\n  pbkeylen: 16\n", -1},
  {SRT_BASE "srt:\n  passphrase: 0123456789ab\n  pbkeylen: 16\n", 0},
  {OK_BASE "cas:\n  cp-duration: 5000\n", -1},
  {OK_BASE "cas:\n  algo: csa2\n  ecmg:\n    - tcp://h1:2222:\n        super-id: 1\n        ecm-id: 1\n", 0},
  {OK_BASE "biss1:\n  sw: 001122334455\ncas:\n  algo: csa2\n", -1},
};

START_TEST(config_test_reports_warnings_and_fails_only_in_strict_mode) {
  cfg_fixture_write(&g_fx, cfgtest_cases[_i].yaml);
  ck_assert_int_eq(rdh_cfg_test(g_fx.path, 0), 0);
  ck_assert_int_eq(rdh_cfg_test(g_fx.path, 1), cfgtest_cases[_i].strict_result);
  cfg_fixture_remove(&g_fx);
}
END_TEST

START_TEST(config_test_rejects_unreadable_or_invalid_files) {
  ck_assert_int_eq(rdh_cfg_test("/nonexistent/dipiradiohead.yaml", 0), -1);
  cfg_fixture_write(&g_fx, "ttl: 0\n");
  ck_assert_int_eq(rdh_cfg_test(g_fx.path, 0), -1);
  cfg_fixture_remove(&g_fx);
}
END_TEST

START_TEST(cas_pid_clash_detects_reserved_and_program_pids) {
  static const struct {
    unsigned n_inputs;
    unsigned ecm;
    unsigned emm;
    int clash;
    unsigned pid;
    int is_emm;
  } t[] = {
      {1, 0x1FF0, 0x1FF1, 0, 0, 0},
      {1, 0x0010, 0x1FF1, 1, 0x0010, 0},
      {1, 0x1FF0, 0x0011, 1, 0x0011, 1},
      {1, 0x1FF0, 0x0001, 1, 0x0001, 1},
      {1, 0x1FF0, 0x0012, 1, 0x0012, 1},
      {1, TSPACKETIZER_PID_PMT, 0x1FF1, 1, TSPACKETIZER_PID_PMT, 0},
      {1, 0x1FF0, TSPACKETIZER_PID_AUDIO, 1, TSPACKETIZER_PID_AUDIO, 1},
      {2, 0x0014, 0x1FF1, 1, 0x0014, 0},
      {2, INPUTSET_AUDIO_PID_BASE + 1, 0x1FF1, 1, INPUTSET_AUDIO_PID_BASE + 1, 0},
      {2, 0x1FF0, INPUTSET_PMT_PID_BASE, 1, INPUTSET_PMT_PID_BASE, 1},
      {2, 0x1FF0, 0x1FF1, 0, 0, 0},
  };
  config_t cfg;
  unsigned pid = 0;
  int is_emm = 0;
  rdh_cfg_defaults(&cfg);
  cfg.n_inputs = t[_i].n_inputs;
  cfg.n_cas_vendors = 1;
  cfg.cas_vendors[0].ecm_pid = t[_i].ecm;
  cfg.cas_vendors[0].emm_pid = t[_i].emm;
  ck_assert_int_eq(rdh_cas_pid_clash(&cfg, &pid, &is_emm), t[_i].clash);
  if (t[_i].clash) {
    ck_assert_uint_eq(pid, t[_i].pid);
    ck_assert_int_eq(is_emm, t[_i].is_emm);
  }
}
END_TEST

static Suite *config_suite(void) {
  Suite *s = suite_create("dipiradiohead_config");
  TCase *tc = tcase_create("core");
  tcase_add_loop_test(tc, each_key_sets_its_config_field, 0, (int)(sizeof field_cases / sizeof field_cases[0]));
  tcase_add_loop_test(tc, invalid_values_are_rejected, 0, (int)(sizeof bad_cases / sizeof bad_cases[0]));
  tcase_add_test(tc, key_list_files_and_ca_receivers_are_accepted);
  tcase_add_test(tc, too_many_inputs_and_peers_are_rejected);
  tcase_add_test(tc, unknown_key_and_missing_file_behave_per_strictness);
  tcase_add_test(tc, defaults_are_the_documented_values);
  tcase_add_loop_test(tc, config_test_reports_warnings_and_fails_only_in_strict_mode, 0, (int)(sizeof cfgtest_cases / sizeof cfgtest_cases[0]));
  tcase_add_test(tc, config_test_rejects_unreadable_or_invalid_files);
  tcase_add_loop_test(tc, cas_pid_clash_detects_reserved_and_program_pids, 0, 11);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(config_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
