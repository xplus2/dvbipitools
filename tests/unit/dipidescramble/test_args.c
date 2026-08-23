/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipidescramble/args.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(missing_input_is_rejected) {
  char *argv[] = {"dipidescramble", "-o", "out.ts", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(missing_output_is_rejected) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(minimal_valid_args_ok) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "out.ts", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.input.kind, INPUT_UDP);
  ck_assert_int_eq(cfg.n_out, 1);
}
END_TEST

START_TEST(metrics_options_require_metrics_id) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "out.ts",
                  "--metrics", "/tmp/x.sock", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(metrics_id_alone_is_accepted) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "out.ts",
                  "--metrics-id", "inst1", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.metrics_id, "inst1");
}
END_TEST

START_TEST(rist_input_with_at_is_accepted) {
  char *argv[] = {"dipidescramble", "-i", "rist://@127.0.0.1:6000", "-o", "out.ts", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.input.kind, INPUT_RIST);
  ck_assert_str_eq(cfg.input.rist_uri, "rist://@127.0.0.1:6000");
}
END_TEST

START_TEST(rist_input_without_at_is_rejected) {
  char *argv[] = {"dipidescramble", "-i", "rist://127.0.0.1:6000", "-o", "out.ts", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rist_profile_main_is_parsed) {
  char *argv[] = {"dipidescramble", "-i", "rist://@127.0.0.1:6000", "-o", "out.ts",
                  "--profile", "main", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rist_profile_main, 1);
}
END_TEST

START_TEST(invalid_rist_profile_is_rejected) {
  char *argv[] = {"dipidescramble", "-i", "rist://@127.0.0.1:6000", "-o", "out.ts",
                  "--profile", "bogus", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

static Suite *args_suite(void) {
  Suite *s = suite_create("dipidescramble_args");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, missing_input_is_rejected);
  tcase_add_test(tc, missing_output_is_rejected);
  tcase_add_test(tc, minimal_valid_args_ok);
  tcase_add_test(tc, metrics_options_require_metrics_id);
  tcase_add_test(tc, metrics_id_alone_is_accepted);
  tcase_add_test(tc, rist_input_with_at_is_accepted);
  tcase_add_test(tc, rist_input_without_at_is_rejected);
  tcase_add_test(tc, rist_profile_main_is_parsed);
  tcase_add_test(tc, invalid_rist_profile_is_rejected);
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
