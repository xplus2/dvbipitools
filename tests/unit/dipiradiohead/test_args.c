/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dipiradiohead/args.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(single_input_matches_legacy_defaults) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_inputs, 1u);
  ck_assert_str_eq(cfg.inputs[0].uri, "http://a");
  ck_assert_uint_eq(cfg.inputs[0].sid, 1u);
  ck_assert_str_eq(cfg.inputs[0].sdt_text, "dipiradiohead");
}
END_TEST

START_TEST(multi_input_sid_auto_assign_skips_explicit) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "--sid", "5",
                  "-i", "http://b",
                  "-i", "http://c", "--sid", "2",
                  "-m", "239.1.1.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_inputs, 3u);
  ck_assert_uint_eq(cfg.inputs[0].sid, 5u);
  ck_assert_uint_eq(cfg.inputs[1].sid, 1u); /* lowest free id, 5 and 2 are taken */
  ck_assert_uint_eq(cfg.inputs[2].sid, 2u);
}
END_TEST

START_TEST(multi_input_sdt_auto_default_is_numbered) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-i", "http://b", "-i", "http://c",
                  "-m", "239.1.1.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.inputs[0].sdt_text, "dipiradiohead 1");
  ck_assert_str_eq(cfg.inputs[1].sdt_text, "dipiradiohead 2");
  ck_assert_str_eq(cfg.inputs[2].sdt_text, "dipiradiohead 3");
}
END_TEST

START_TEST(paired_sid_and_sdt_apply_to_preceding_input) {
  char *argv[] = {"dipiradiohead",
                  "-i", "http://a", "--sid", "10", "--sdt", "Radio A",
                  "-i", "http://b", "--sid", "20", "--sdt", "Radio B",
                  "-m", "239.1.1.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.inputs[0].sid, 10u);
  ck_assert_str_eq(cfg.inputs[0].sdt_text, "Radio A");
  ck_assert_uint_eq(cfg.inputs[1].sid, 20u);
  ck_assert_str_eq(cfg.inputs[1].sdt_text, "Radio B");
}
END_TEST

START_TEST(sid_before_any_input_is_rejected) {
  char *argv[] = {"dipiradiohead", "--sid", "5", "-i", "http://a", "-m", "239.1.1.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(sdt_before_any_input_is_rejected) {
  char *argv[] = {"dipiradiohead", "--sdt", "x", "-i", "http://a", "-m", "239.1.1.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(duplicate_explicit_sid_is_rejected) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "--sid", "10",
                  "-i", "http://b", "--sid", "10",
                  "-m", "239.1.1.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(missing_input_is_rejected) {
  char *argv[] = {"dipiradiohead", "-m", "239.1.1.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(missing_mcast_is_rejected) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(too_many_inputs_is_rejected) {
  char *argv[1 + (RADIOHEAD_MAX_INPUTS + 1) * 2 + 2 + 1];
  int n = 0;
  int i;
  config_t cfg;
  argv[n++] = "dipiradiohead";
  for (i = 0; i < RADIOHEAD_MAX_INPUTS + 1; i++) {
    argv[n++] = "-i";
    argv[n++] = "http://x";
  }
  argv[n++] = "-m";
  argv[n++] = "239.1.1.1:5000";
  argv[n] = NULL;
  ck_assert_int_eq(args_parse(n, argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(biss2_ca_receivers_enables_and_sets_dir) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000",
                  "--biss2-ca-receivers", "/etc/biss-ca/receivers", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.biss2_ca_enabled, 1);
  ck_assert_str_eq(cfg.biss2_ca_receivers_dir, "/etc/biss-ca/receivers");
}
END_TEST

START_TEST(biss2_ca_session_id_parses_hex) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000",
                  "--biss2-ca-receivers", "/etc/biss-ca/receivers",
                  "--biss2-ca-session-id", "0x1234", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.biss2_ca_session_id_given, 1);
  ck_assert_uint_eq(cfg.biss2_ca_session_id, 0x1234u);
}
END_TEST

START_TEST(biss2_ca_receivers_mutually_exclusive_with_cas_algo) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000",
                  "--cas-algo", "cissa",
                  "--biss2-ca-receivers", "/etc/biss-ca/receivers", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(biss2_ca_receivers_mutually_exclusive_with_biss2_sw) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000",
                  "--biss2-sw", "00112233445566778899aabbccddeeff",
                  "--biss2-ca-receivers", "/etc/biss-ca/receivers", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(biss2_ca_session_id_without_receivers_is_rejected) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000",
                  "--biss2-ca-session-id", "1", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rist_peer_is_repeatable_and_bonded) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000",
                  "-R", "rist://1.2.3.4:6000", "-R", "rist://5.6.7.8:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_rist, 2u);
  ck_assert_str_eq(cfg.rist_uri[0], "rist://1.2.3.4:6000");
  ck_assert_str_eq(cfg.rist_uri[1], "rist://5.6.7.8:6000");
}
END_TEST

START_TEST(rist_peer_without_rist_scheme_is_rejected) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000",
                  "-R", "udp://1.2.3.4:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rist_secret_without_profile_main_is_rejected) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000",
                  "-R", "rist://1.2.3.4:6000", "--rist-secret", "hunter2", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rist_secret_with_profile_main_is_accepted) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000",
                  "-R", "rist://1.2.3.4:6000", "--rist-profile", "main", "--rist-secret", "hunter2", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rist_profile, RIST_PROF_MAIN);
  ck_assert_str_eq(cfg.rist_secret, "hunter2");
}
END_TEST

START_TEST(srt_peer_is_accepted) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "srt://1.2.3.4:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_srt, 1u);
  ck_assert_str_eq(cfg.srt_host[0], "1.2.3.4");
  ck_assert_uint_eq(cfg.srt_port[0], 6000u);
}
END_TEST

START_TEST(srt_peer_with_at_is_rejected) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "srt://@1.2.3.4:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(srt_peers_bonded_require_group_mode) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "srt://1.2.3.4:6000", "-R", "srt://5.6.7.8:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(srt_peers_bonded_with_group_mode_is_accepted) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "srt://1.2.3.4:6000", "-R", "srt://5.6.7.8:6000", "--srt-group-mode", "broadcast", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_srt, 2u);
  ck_assert_int_eq(cfg.srt_group_mode, SRT_BOND_BROADCAST);
}
END_TEST

START_TEST(srt_group_mode_with_single_peer_is_rejected) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "srt://1.2.3.4:6000", "--srt-group-mode", "broadcast", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rist_and_srt_peers_cannot_mix) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "rist://1.2.3.4:6000", "-R", "srt://5.6.7.8:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(srt_passphrase_length_is_validated) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "srt://1.2.3.4:6000", "--srt-passphrase", "short", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(srt_pbkeylen_requires_passphrase) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "srt://1.2.3.4:6000", "--srt-pbkeylen", "16", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

static void write_cfg(char *path, const char *text) {
  int fd = mkstemp(path);
  ck_assert_int_ge(fd, 0);
  ck_assert_int_eq((int)write(fd, text, strlen(text)), (int)strlen(text));
  close(fd);
}

START_TEST(config_file_provides_settings) {
  char path[] = "/tmp/dipiradiohead_cfg_XXXXXX";
  char *argv[] = {"dipiradiohead", "-c", path, NULL};
  config_t cfg;
  write_cfg(path, "input:\n  - http://a\n  - http://b\nmcast: 239.1.1.1:5000\nrtp: on\nttl: 4\nnit: Net\nmetrics:\n  id: rh\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_uint_eq(cfg.n_inputs, 2u);
  ck_assert_str_eq(cfg.inputs[0].uri, "http://a");
  ck_assert_uint_eq(cfg.inputs[0].sid, 1u);
  ck_assert_str_eq(cfg.inputs[1].uri, "http://b");
  ck_assert_uint_eq(cfg.inputs[1].sid, 2u);
  ck_assert_uint_eq(cfg.mcast_port, 5000u);
  ck_assert_int_eq(cfg.rtp, 1);
  ck_assert_uint_eq(cfg.ttl, 4u);
  ck_assert_str_eq(cfg.metrics_id, "rh");
}
END_TEST

START_TEST(config_inputs_and_vendors_take_items) {
  char path[] = "/tmp/dipiradiohead_cfg_XXXXXX";
  char *argv[] = {"dipiradiohead", "-c", path, NULL};
  config_t cfg;
  write_cfg(path, "input:\n  - http://a:\n      sid: 7\n      sdt: A\n      provider: P\nmcast: 239.1.1.1:5000\ncas:\n  algo: csa2\n  ecmg:\n    - tcp://h1:2222:\n        super-id: 0x1234\n        ecm-id: 5\n        ecmg-version: 3\n        required: on\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_str_eq(cfg.inputs[0].uri, "http://a");
  ck_assert_uint_eq(cfg.inputs[0].sid, 7u);
  ck_assert_str_eq(cfg.inputs[0].sdt_text, "A");
  ck_assert_str_eq(cfg.inputs[0].provider_text, "P");
  ck_assert_uint_eq(cfg.n_cas_vendors, 1u);
  ck_assert_str_eq(cfg.cas_vendors[0].ecmg_host, "h1");
  ck_assert_uint_eq(cfg.cas_vendors[0].ecmg_version, 3u);
  ck_assert_int_eq(cfg.cas_vendors[0].required, 1);
}
END_TEST

START_TEST(config_item_without_source_is_error) {
  char path[] = "/tmp/dipiradiohead_cfg_XXXXXX";
  char *argv[] = {"dipiradiohead", "-c", path, NULL};
  config_t cfg;
  write_cfg(path, "input:\n  - sid: 3\nmcast: 239.1.1.1:5000\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
}
END_TEST

START_TEST(cmdline_lists_replace_config_lists) {
  char path[] = "/tmp/dipiradiohead_cfg_XXXXXX";
  char *argv[] = {"dipiradiohead", "-c", path, "-i", "http://c", "--sid", "9", "-R", "srt://127.0.0.1:7000", NULL};
  config_t cfg;
  write_cfg(path, "input:\n  - http://a\n  - http://b\nrist:\n  - rist://h:1\nmcast: 239.1.1.1:5000\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_uint_eq(cfg.n_inputs, 1u);
  ck_assert_str_eq(cfg.inputs[0].uri, "http://c");
  ck_assert_uint_eq(cfg.inputs[0].sid, 9u);
  ck_assert_uint_eq(cfg.n_rist, 0u);
  ck_assert_uint_eq(cfg.n_srt, 1u);
}
END_TEST

START_TEST(cmdline_scoped_option_needs_its_own_input) {
  char path[] = "/tmp/dipiradiohead_cfg_XXXXXX";
  char *argv[] = {"dipiradiohead", "-c", path, "--sid", "9", NULL};
  config_t cfg;
  write_cfg(path, "input: http://a\nmcast: 239.1.1.1:5000\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
}
END_TEST

START_TEST(config_missing_file_is_error) {
  char *argv[] = {"dipiradiohead", "-c", "/nonexistent/dipiradiohead.yaml", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(config_invalid_value_is_error) {
  char path[] = "/tmp/dipiradiohead_cfg_XXXXXX";
  char *argv[] = {"dipiradiohead", "-c", path, NULL};
  config_t cfg;
  write_cfg(path, "input: http://a\nmcast: 239.1.1.1:5000\nttl: 300\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
}
END_TEST

START_TEST(config_conflict_is_rejected) {
  char path[] = "/tmp/dipiradiohead_cfg_XXXXXX";
  char *argv[] = {"dipiradiohead", "-c", path, NULL};
  config_t cfg;
  write_cfg(path, "input: http://a\nmcast: 239.1.1.1:5000\nal-fec: 5:5\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
}
END_TEST

START_TEST(configtest_reports_by_exit_status) {
  char path[] = "/tmp/dipiradiohead_cfg_XXXXXX";
  char *argv[] = {"dipiradiohead", "--configtest", "-c", path, NULL};
  char *argv2[] = {"dipiradiohead", "--configtest", "-c", "/nonexistent/dipiradiohead.yaml", NULL};
  char *argv3[] = {"dipiradiohead", "--configtest", NULL};
  config_t cfg;
  write_cfg(path, "bogus: 1\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
  ck_assert_int_eq(args_parse(ARGC(argv2), argv2, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(argv3), argv3, &cfg), ARGS_ERR);
  unlink(path);
}
END_TEST

START_TEST(inspect_ts_level_is_recorded) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "--metrics-id", "inst1", "--metrics-inspect-ts", "medium", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_MEDIUM);
}
END_TEST

START_TEST(inspect_ts_defaults_to_off) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "--metrics-id", "inst1", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_OFF);
}
END_TEST

START_TEST(inspect_ts_requires_metrics_id) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "--metrics-inspect-ts", "basic", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(inspect_ts_rejects_unknown_level) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "--metrics-id", "inst1", "--metrics-inspect-ts", "bogus", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(inspect_ts_from_yaml) {
  char path[] = "/tmp/dipiradiohead_inspect_XXXXXX";
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-c", path, NULL};
  config_t cfg;
  write_cfg(path, "metrics:\n  id: a\n  inspect-ts: full\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_FULL);
}
END_TEST

START_TEST(inspect_ts_pids_parsed) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "--metrics-id", "inst1", "--metrics-inspect-ts", "full", "--metrics-inspect-ts-pids", "0x100,300", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.metrics_n_known_pids, 2u);
  ck_assert_uint_eq(cfg.metrics_known_pids[0], 256u);
  ck_assert_uint_eq(cfg.metrics_known_pids[1], 300u);
}
END_TEST

START_TEST(inspect_ts_pids_need_full_level) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "--metrics-id", "inst1", "--metrics-inspect-ts", "basic", "--metrics-inspect-ts-pids", "0x100", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(inspect_ts_pids_reject_bad_list) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "--metrics-id", "inst1", "--metrics-inspect-ts", "full", "--metrics-inspect-ts-pids", "0x100,9000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

static Suite *args_suite(void) {
  Suite *s = suite_create("args");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, single_input_matches_legacy_defaults);
  tcase_add_test(tc, multi_input_sid_auto_assign_skips_explicit);
  tcase_add_test(tc, multi_input_sdt_auto_default_is_numbered);
  tcase_add_test(tc, paired_sid_and_sdt_apply_to_preceding_input);
  tcase_add_test(tc, sid_before_any_input_is_rejected);
  tcase_add_test(tc, sdt_before_any_input_is_rejected);
  tcase_add_test(tc, duplicate_explicit_sid_is_rejected);
  tcase_add_test(tc, missing_input_is_rejected);
  tcase_add_test(tc, missing_mcast_is_rejected);
  tcase_add_test(tc, too_many_inputs_is_rejected);
  tcase_add_test(tc, biss2_ca_receivers_enables_and_sets_dir);
  tcase_add_test(tc, biss2_ca_session_id_parses_hex);
  tcase_add_test(tc, biss2_ca_receivers_mutually_exclusive_with_cas_algo);
  tcase_add_test(tc, biss2_ca_receivers_mutually_exclusive_with_biss2_sw);
  tcase_add_test(tc, biss2_ca_session_id_without_receivers_is_rejected);
  tcase_add_test(tc, rist_peer_is_repeatable_and_bonded);
  tcase_add_test(tc, rist_peer_without_rist_scheme_is_rejected);
  tcase_add_test(tc, rist_secret_without_profile_main_is_rejected);
  tcase_add_test(tc, rist_secret_with_profile_main_is_accepted);
  tcase_add_test(tc, srt_peer_is_accepted);
  tcase_add_test(tc, srt_peer_with_at_is_rejected);
  tcase_add_test(tc, srt_peers_bonded_require_group_mode);
  tcase_add_test(tc, srt_peers_bonded_with_group_mode_is_accepted);
  tcase_add_test(tc, srt_group_mode_with_single_peer_is_rejected);
  tcase_add_test(tc, rist_and_srt_peers_cannot_mix);
  tcase_add_test(tc, srt_passphrase_length_is_validated);
  tcase_add_test(tc, srt_pbkeylen_requires_passphrase);
  tcase_add_test(tc, config_file_provides_settings);
  tcase_add_test(tc, config_inputs_and_vendors_take_items);
  tcase_add_test(tc, config_item_without_source_is_error);
  tcase_add_test(tc, cmdline_lists_replace_config_lists);
  tcase_add_test(tc, cmdline_scoped_option_needs_its_own_input);
  tcase_add_test(tc, config_missing_file_is_error);
  tcase_add_test(tc, config_invalid_value_is_error);
  tcase_add_test(tc, config_conflict_is_rejected);
  tcase_add_test(tc, configtest_reports_by_exit_status);
  tcase_add_test(tc, inspect_ts_level_is_recorded);
  tcase_add_test(tc, inspect_ts_defaults_to_off);
  tcase_add_test(tc, inspect_ts_requires_metrics_id);
  tcase_add_test(tc, inspect_ts_rejects_unknown_level);
  tcase_add_test(tc, inspect_ts_from_yaml);
  tcase_add_test(tc, inspect_ts_pids_parsed);
  tcase_add_test(tc, inspect_ts_pids_need_full_level);
  tcase_add_test(tc, inspect_ts_pids_reject_bad_list);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(args_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
