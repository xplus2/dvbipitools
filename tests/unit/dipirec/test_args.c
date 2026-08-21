/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipirec/args.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(rist_out_uri_is_parsed) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.out[0].kind, OUT_RIST);
  ck_assert_str_eq(cfg.out[0].rist_uri, "rist://1.2.3.4:6000");
}
END_TEST

START_TEST(rist_out_rejects_mkv_format) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "-f", "mkv", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rist_out_default_format_is_ts) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.format, FMT_TS);
}
END_TEST

START_TEST(default_profile_is_simple) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rist_profile, RIST_PROF_SIMPLE);
}
END_TEST

START_TEST(profile_main_is_accepted) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--profile", "main", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rist_profile, RIST_PROF_MAIN);
}
END_TEST

START_TEST(unknown_profile_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--profile", "advanced", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(secret_without_profile_main_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--secret", "hunter2", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(secret_with_profile_main_is_accepted) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000",
                  "--profile", "main", "--secret", "hunter2", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.rist_secret, "hunter2");
}
END_TEST

START_TEST(cname_and_buffer_are_applied) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000",
                  "--cname", "encoder1", "--buffer", "1000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.rist_cname, "encoder1");
  ck_assert_uint_eq(cfg.rist_buffer_ms, 1000u);
}
END_TEST

START_TEST(buffer_zero_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--buffer", "0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rist_options_without_rist_out_are_harmless) {
  /* --profile/--secret/--cname/--buffer with a non-rist -o: no-op, just a logged warning */
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "show.ts", "--cname", "x", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.out[0].kind, OUT_FILE);
}
END_TEST

START_TEST(out_iface_has_no_effect_on_rist_but_is_not_an_error) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "-O", "eth0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
}
END_TEST

START_TEST(metrics_options_require_metrics_id) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "show.ts", "--metrics", "/tmp/x.sock", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(metrics_id_alone_is_accepted) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "show.ts",
                  "--metrics-id", "inst1", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.metrics_id, "inst1");
}
END_TEST

static Suite *args_suite(void) {
  Suite *s = suite_create("dipirec_args");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, rist_out_uri_is_parsed);
  tcase_add_test(tc, rist_out_rejects_mkv_format);
  tcase_add_test(tc, rist_out_default_format_is_ts);
  tcase_add_test(tc, default_profile_is_simple);
  tcase_add_test(tc, profile_main_is_accepted);
  tcase_add_test(tc, unknown_profile_is_rejected);
  tcase_add_test(tc, secret_without_profile_main_is_rejected);
  tcase_add_test(tc, secret_with_profile_main_is_accepted);
  tcase_add_test(tc, cname_and_buffer_are_applied);
  tcase_add_test(tc, buffer_zero_is_rejected);
  tcase_add_test(tc, rist_options_without_rist_out_are_harmless);
  tcase_add_test(tc, out_iface_has_no_effect_on_rist_but_is_not_an_error);
  tcase_add_test(tc, metrics_options_require_metrics_id);
  tcase_add_test(tc, metrics_id_alone_is_accepted);
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
