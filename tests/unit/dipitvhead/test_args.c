/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipitvhead/args.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(single_input_matches_legacy_defaults) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_inputs, 1u);
  ck_assert_uint_eq(cfg.inputs[0].sid, 1u);
  ck_assert_int_eq(cfg.inputs[0].sdt_mode, TABLE_PASSTHROUGH);
  ck_assert_uint_eq(cfg.inputs[0].pmt_pid, 0u);
}
END_TEST

START_TEST(multi_input_sid_auto_assign_skips_explicit) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "--sid", "5",
                  "-i", "udp://@239.1.1.2:5000",
                  "-i", "udp://@239.1.1.3:5000", "--sid", "2",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_inputs, 3u);
  ck_assert_uint_eq(cfg.inputs[0].sid, 5u);
  ck_assert_uint_eq(cfg.inputs[1].sid, 1u); /* lowest free id, 5 and 2 are taken */
  ck_assert_uint_eq(cfg.inputs[2].sid, 2u);
}
END_TEST

START_TEST(paired_options_apply_to_preceding_input) {
  char *argv[] = {"dipitvhead",
                  "-i", "udp://@239.1.1.1:5000", "--sid", "10", "-s", "Channel A", "-p", "0x0100",
                  "-i", "udp://@239.1.1.2:5000", "--sid", "20", "-s", "Channel B", "--strip-eit",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.inputs[0].sid, 10u);
  ck_assert_int_eq(cfg.inputs[0].sdt_mode, TABLE_OVERRIDE);
  ck_assert_str_eq(cfg.inputs[0].sdt_text, "Channel A");
  ck_assert_uint_eq(cfg.inputs[0].pmt_pid, 0x0100u);
  ck_assert_int_eq(cfg.inputs[0].strip_eit, 0);
  ck_assert_uint_eq(cfg.inputs[1].sid, 20u);
  ck_assert_str_eq(cfg.inputs[1].sdt_text, "Channel B");
  ck_assert_int_eq(cfg.inputs[1].strip_eit, 1);
}
END_TEST

START_TEST(hbbtv_triplet_pairs_with_preceding_input) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000",
                  "--hbbtv", "http://example.invalid/app.html", "--hbbtv-org-id", "1", "--hbbtv-app-id", "2",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.inputs[0].hbbtv_url, "http://example.invalid/app.html");
  ck_assert_uint_eq(cfg.inputs[0].hbbtv_org_id, 1u);
  ck_assert_uint_eq(cfg.inputs[0].hbbtv_app_id, 2u);
}
END_TEST

START_TEST(hbbtv_url_without_org_or_app_id_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000",
                  "--hbbtv", "http://example.invalid/app.html",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(hbbtv_org_id_without_url_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000",
                  "--hbbtv-org-id", "1", "--hbbtv-app-id", "2",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(sid_before_any_input_is_rejected) {
  char *argv[] = {"dipitvhead", "--sid", "5", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(sdt_before_any_input_is_rejected) {
  char *argv[] = {"dipitvhead", "-s", "x", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(pmt_pid_before_any_input_is_rejected) {
  char *argv[] = {"dipitvhead", "-p", "0x0100", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(iface_before_any_input_is_rejected) {
  char *argv[] = {"dipitvhead", "-I", "eth0", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(duplicate_explicit_sid_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "--sid", "10",
                  "-i", "udp://@239.1.1.2:5000", "--sid", "10",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(missing_input_is_rejected) {
  char *argv[] = {"dipitvhead", "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(missing_mcast_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(too_many_inputs_is_rejected) {
  char *argv[1 + (ARGS_MAX_INPUTS + 1) * 2 + 2 + 1];
  int n = 0;
  int i;
  config_t cfg;
  argv[n++] = "dipitvhead";
  for (i = 0; i < ARGS_MAX_INPUTS + 1; i++) {
    argv[n++] = "-i";
    argv[n++] = "udp://@239.1.1.1:5000";
  }
  argv[n++] = "-m";
  argv[n++] = "239.1.2.1:5000";
  argv[n] = NULL;
  ck_assert_int_eq(args_parse(n, argv, &cfg), ARGS_ERR);
}
END_TEST

/* mux-wide flags stay global, not per-input: NIT, tsid/onid, bitrate pacing, CAS */
START_TEST(global_flags_are_not_per_input) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-i", "udp://@239.1.1.2:5000",
                  "-n", "My Network", "--tsid", "7", "--onid", "8", "-b", "5000", "-S",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.nit_mode, TABLE_OVERRIDE);
  ck_assert_str_eq(cfg.nit_text, "My Network");
  ck_assert_uint_eq(cfg.tsid, 7u);
  ck_assert_uint_eq(cfg.onid, 8u);
  ck_assert_uint_eq(cfg.bitrate_kbps, 5000u);
  ck_assert_int_eq(cfg.stuff, 1);
}
END_TEST

START_TEST(biss2_ca_receivers_enables_and_sets_dir) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "--biss2-ca-receivers", "/etc/biss-ca/receivers", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.biss2_ca_enabled, 1);
  ck_assert_str_eq(cfg.biss2_ca_receivers_dir, "/etc/biss-ca/receivers");
  ck_assert_int_eq(cfg.biss2_ca_session_id_given, 0);
}
END_TEST

START_TEST(biss2_ca_session_id_parses_hex_and_dec) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "--biss2-ca-receivers", "/etc/biss-ca/receivers",
                  "--biss2-ca-session-id", "0x1234", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.biss2_ca_session_id_given, 1);
  ck_assert_uint_eq(cfg.biss2_ca_session_id, 0x1234u);
}
END_TEST

START_TEST(biss2_ca_session_id_without_receivers_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "--biss2-ca-session-id", "1", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(biss2_ca_receivers_rejects_out_of_range_session_id) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "--biss2-ca-receivers", "/etc/biss-ca/receivers",
                  "--biss2-ca-session-id", "0x10000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(biss2_ca_receivers_mutually_exclusive_with_cas_algo) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "--cas-algo", "cissa",
                  "--biss2-ca-receivers", "/etc/biss-ca/receivers", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(biss2_ca_receivers_mutually_exclusive_with_biss2_sw) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "--biss2-sw", "00112233445566778899aabbccddeeff",
                  "--biss2-ca-receivers", "/etc/biss-ca/receivers", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(biss2_ca_receivers_defaults_cas_pids_to_video_audio) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "--biss2-ca-receivers", "/etc/biss-ca/receivers", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.cas_pids_video, 1);
  ck_assert_int_eq(cfg.cas_pids_audio, 1);
}
END_TEST

static Suite *args_suite(void) {
  Suite *s = suite_create("args");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, single_input_matches_legacy_defaults);
  tcase_add_test(tc, multi_input_sid_auto_assign_skips_explicit);
  tcase_add_test(tc, paired_options_apply_to_preceding_input);
  tcase_add_test(tc, hbbtv_triplet_pairs_with_preceding_input);
  tcase_add_test(tc, hbbtv_url_without_org_or_app_id_is_rejected);
  tcase_add_test(tc, hbbtv_org_id_without_url_is_rejected);
  tcase_add_test(tc, sid_before_any_input_is_rejected);
  tcase_add_test(tc, sdt_before_any_input_is_rejected);
  tcase_add_test(tc, pmt_pid_before_any_input_is_rejected);
  tcase_add_test(tc, iface_before_any_input_is_rejected);
  tcase_add_test(tc, duplicate_explicit_sid_is_rejected);
  tcase_add_test(tc, missing_input_is_rejected);
  tcase_add_test(tc, missing_mcast_is_rejected);
  tcase_add_test(tc, too_many_inputs_is_rejected);
  tcase_add_test(tc, global_flags_are_not_per_input);
  tcase_add_test(tc, biss2_ca_receivers_enables_and_sets_dir);
  tcase_add_test(tc, biss2_ca_session_id_parses_hex_and_dec);
  tcase_add_test(tc, biss2_ca_session_id_without_receivers_is_rejected);
  tcase_add_test(tc, biss2_ca_receivers_rejects_out_of_range_session_id);
  tcase_add_test(tc, biss2_ca_receivers_mutually_exclusive_with_cas_algo);
  tcase_add_test(tc, biss2_ca_receivers_mutually_exclusive_with_biss2_sw);
  tcase_add_test(tc, biss2_ca_receivers_defaults_cas_pids_to_video_audio);
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
