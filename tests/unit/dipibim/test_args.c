/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipibim/args.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(format_is_required) {
  char *argv[] = {"dipibim", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(format_xml_applies_stdio_defaults) {
  char *argv[] = {"dipibim", "-f", "xml", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.format, FMT_XML);
  ck_assert_str_eq(cfg.input_path, "-");
  ck_assert_str_eq(cfg.output_path, "-");
}
END_TEST

START_TEST(format_bim_is_accepted) {
  char *argv[] = {"dipibim", "-f", "bim", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.format, FMT_BIM);
}
END_TEST

START_TEST(format_rejects_unknown_value) {
  char *argv[] = {"dipibim", "-f", "json", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(input_and_output_paths_are_recorded) {
  char *argv[] = {"dipibim", "-f", "xml", "-i", "in.xml", "-o", "out.bim", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.input_path, "in.xml");
  ck_assert_str_eq(cfg.output_path, "out.bim");
}
END_TEST

START_TEST(invalid_color_mode_is_rejected) {
  char *argv[] = {"dipibim", "-f", "xml", "--color", "sometimes", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(unexpected_positional_argument_is_rejected) {
  char *argv[] = {"dipibim", "-f", "xml", "extra", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(help_returns_help_status) {
  char *argv[] = {"dipibim", "-h", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
}
END_TEST

START_TEST(verbose_flag_is_recorded) {
  char *argv[] = {"dipibim", "-f", "xml", "-v", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.verbose, 1);
}
END_TEST

static Suite *args_suite(void) {
  Suite *s = suite_create("dipibim_args");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, format_is_required);
  tcase_add_test(tc, format_xml_applies_stdio_defaults);
  tcase_add_test(tc, format_bim_is_accepted);
  tcase_add_test(tc, format_rejects_unknown_value);
  tcase_add_test(tc, input_and_output_paths_are_recorded);
  tcase_add_test(tc, invalid_color_mode_is_rejected);
  tcase_add_test(tc, unexpected_positional_argument_is_rejected);
  tcase_add_test(tc, help_returns_help_status);
  tcase_add_test(tc, verbose_flag_is_recorded);
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
