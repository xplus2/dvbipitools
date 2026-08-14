/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

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
                  "-R", "rist://1.2.3.4:6000", "--secret", "hunter2", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rist_secret_with_profile_main_is_accepted) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000",
                  "-R", "rist://1.2.3.4:6000", "--profile", "main", "--secret", "hunter2", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rist_profile, RIST_PROF_MAIN);
  ck_assert_str_eq(cfg.rist_secret, "hunter2");
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
