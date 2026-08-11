/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipixmltv/args.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(format_is_required_without_suggest_map) {
  char *argv[] = {"dipixmltv", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(format_xmltv_requires_map) {
  char *argv[] = {"dipixmltv", "-f", "xmltv", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(format_xmltv_with_map_applies_stdio_defaults) {
  char *argv[] = {"dipixmltv", "-f", "xmltv", "-M", "map.csv", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.format, FMT_XMLTV);
  ck_assert_str_eq(cfg.map_path, "map.csv");
  ck_assert_str_eq(cfg.input_path, "-");
  ck_assert_str_eq(cfg.output_path, "-");
}
END_TEST

START_TEST(format_tva_does_not_require_map) {
  char *argv[] = {"dipixmltv", "-f", "tva", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.format, FMT_TVA);
}
END_TEST

START_TEST(format_tva_accepts_optional_reverse_map) {
  char *argv[] = {"dipixmltv", "-f", "tva", "-R", "revmap.csv", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.revmap_path, "revmap.csv");
}
END_TEST

START_TEST(format_rejects_unknown_value) {
  char *argv[] = {"dipixmltv", "-f", "json", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(suggest_map_mode_skips_format_requirement) {
  char *argv[] = {"dipixmltv", "-S", "scan.csv", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.suggest_scan_path, "scan.csv");
}
END_TEST

START_TEST(input_and_output_paths_are_recorded) {
  char *argv[] = {"dipixmltv", "-f", "tva", "-i", "in.xml", "-o", "out.xml", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.input_path, "in.xml");
  ck_assert_str_eq(cfg.output_path, "out.xml");
}
END_TEST

START_TEST(invalid_color_mode_is_rejected) {
  char *argv[] = {"dipixmltv", "-f", "tva", "--color", "sometimes", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(unexpected_positional_argument_is_rejected) {
  char *argv[] = {"dipixmltv", "-f", "tva", "extra", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(help_returns_help_status) {
  char *argv[] = {"dipixmltv", "-h", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
}
END_TEST

static Suite *args_suite(void) {
  Suite *s = suite_create("dipixmltv_args");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, format_is_required_without_suggest_map);
  tcase_add_test(tc, format_xmltv_requires_map);
  tcase_add_test(tc, format_xmltv_with_map_applies_stdio_defaults);
  tcase_add_test(tc, format_tva_does_not_require_map);
  tcase_add_test(tc, format_tva_accepts_optional_reverse_map);
  tcase_add_test(tc, format_rejects_unknown_value);
  tcase_add_test(tc, suggest_map_mode_skips_format_requirement);
  tcase_add_test(tc, input_and_output_paths_are_recorded);
  tcase_add_test(tc, invalid_color_mode_is_rejected);
  tcase_add_test(tc, unexpected_positional_argument_is_rejected);
  tcase_add_test(tc, help_returns_help_status);
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
