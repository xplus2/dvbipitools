/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>

#include "dipibim/cli/args.h"
#include "lib/bim/codec.h"

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

START_TEST(schema_option_is_repeatable) {
  char *argv[] = {"dipibim", "-f", "xml", "-s", "a.xsd", "--schema", "dir", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.nschemas, 2);
  ck_assert_str_eq(cfg.schemas[0], "a.xsd");
  ck_assert_str_eq(cfg.schemas[1], "dir");
}
END_TEST

START_TEST(schema_entries_are_capped) {
  char *argv[3 + 2 * (SCHEMA_MAX + 1) + 1] = {"dipibim", "-f", "xml"};
  int n = 3;
  config_t cfg;
  for (int i = 0; i <= SCHEMA_MAX; i++) {
    argv[n++] = "-s";
    argv[n++] = "x";
  }
  argv[n] = NULL;
  ck_assert_int_eq(args_parse(n, argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(profile_defaults_to_dvb_tva_and_parses_names) {
  char *plain[] = {"dipibim", "-f", "xml", NULL};
  char *gen[] = {"dipibim", "-f", "xml", "--profile", "generic", NULL};
  char *bad[] = {"dipibim", "-f", "xml", "--profile", "mpeg7", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(plain), plain, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.profile, PROFILE_DVB_TVA);
  ck_assert_int_eq(args_parse(ARGC(gen), gen, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.profile, PROFILE_GENERIC);
  ck_assert_int_eq(args_parse(ARGC(bad), bad, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(publisher_options_are_recorded_and_checked) {
  char *ok[] = {"dipibim", "-f", "xml", "--len-mode", "2", "--deferred", "--cast", "--dvb-locator", "--dvb-duration", "--dvb-pubtime", "--zbuf-size", "64", "--zgather", NULL};
  char *bad_len[] = {"dipibim", "-f", "xml", "--len-mode", "3", NULL};
  char *bad_z[] = {"dipibim", "-f", "xml", "--zbuf-size", "16777216", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(ok), ok, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.len_mode, 2);
  ck_assert_int_eq(cfg.deferred && cfg.cast && cfg.dvb_locator && cfg.dvb_duration && cfg.dvb_pubtime && cfg.zgather, 1);
  ck_assert_uint_eq(cfg.zbuf_size, 64);
  ck_assert_int_eq(args_parse(ARGC(bad_len), bad_len, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(bad_z), bad_z, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(syntax_defaults_to_23001_and_parses_names) {
  char *plain[] = {"dipibim", "-f", "bim", NULL};
  char *old[] = {"dipibim", "-f", "bim", "--syntax", "15938-1", NULL};
  char *bad[] = {"dipibim", "-f", "bim", "--syntax", "2002", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(plain), plain, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.syntax, SYNTAX_23001);
  ck_assert_int_eq(args_parse(ARGC(old), old, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.syntax, SYNTAX_15938);
  ck_assert_int_eq(args_parse(ARGC(bad), bad, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(compat_collects_names_and_rejects_unknown) {
  char *none[] = {"dipibim", "-f", "bim", NULL};
  char *two[] = {"dipibim", "-f", "bim", "--compat", "attr-local,list-string", NULL};
  char *all[] = {"dipibim", "-f", "bim", "--compat", "conformance", NULL};
  char *bad[] = {"dipibim", "-f", "bim", "--compat", "attr-local,bogus", NULL};
  char *empty_name[] = {"dipibim", "-f", "bim", "--compat", "attr-local,,list-string", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(none), none, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.compat, 0);
  ck_assert_int_eq(args_parse(ARGC(two), two, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.compat, BIM_COMPAT_ATTR_LOCAL | BIM_COMPAT_LIST_STRING);
  ck_assert_int_eq(args_parse(ARGC(all), all, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.compat, BIM_COMPAT_NO_SCHEMA_MODE | BIM_COMPAT_M7_FLAGS | BIM_COMPAT_ATTR_LOCAL | BIM_COMPAT_FIXED_ATTR | BIM_COMPAT_LIST_STRING);
  ck_assert_int_eq(args_parse(ARGC(bad), bad, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(empty_name), empty_name, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(generic_and_dvb_options_are_recorded_and_checked) {
  char *ok[] = {"dipibim", "-f", "xml", "--len-mode", "2", "--deferred", "--cast", "--dvb-locator", "--dvb-duration", "--dvb-pubtime", "--zbuf-size", "4096", "--zgather", "--color", "never", "-v", NULL};
  char *bad_len[] = {"dipibim", "-f", "xml", "--len-mode", "3", NULL};
  char *bad_zbuf[] = {"dipibim", "-f", "xml", "--zbuf-size", "16777216", NULL};
  char *bad_zbuf2[] = {"dipibim", "-f", "xml", "--zbuf-size", "x", NULL};
  char *bad_profile[] = {"dipibim", "-f", "xml", "--profile", "nope", NULL};
  char *bad_syntax[] = {"dipibim", "-f", "xml", "--syntax", "nope", NULL};
  char *unknown[] = {"dipibim", "-f", "xml", "--no-such-option", NULL};
  char *long_name[] = {"dipibim", "-f", "xml", "--compat", "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", NULL};
  char *trailing[] = {"dipibim", "-f", "xml", "--compat", "attr-local,", NULL};
  char *explicit_cfg[] = {"dipibim", "-c", "/nonexistent/dipibim.yaml", "-f", "xml", NULL};
  char *cfgtest[] = {"dipibim", "--configtest", "-c", "/nonexistent/dipibim.yaml", NULL};
  char *cfgtest_strict[] = {"dipibim", "--configtest", "--config-strict", "-c", "/nonexistent/dipibim.yaml", NULL};
  char *strict[] = {"dipibim", "--config-strict", "-f", "xml", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(ok), ok, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.len_mode, 2u);
  ck_assert_int_eq(cfg.deferred, 1);
  ck_assert_int_eq(cfg.cast, 1);
  ck_assert_int_eq(cfg.dvb_locator, 1);
  ck_assert_int_eq(cfg.dvb_duration, 1);
  ck_assert_int_eq(cfg.dvb_pubtime, 1);
  ck_assert_uint_eq(cfg.zbuf_size, 4096u);
  ck_assert_int_eq(cfg.zgather, 1);
  ck_assert_int_eq(args_parse(ARGC(bad_len), bad_len, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(bad_zbuf), bad_zbuf, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(bad_zbuf2), bad_zbuf2, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(bad_profile), bad_profile, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(bad_syntax), bad_syntax, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(unknown), unknown, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(long_name), long_name, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(trailing), trailing, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(explicit_cfg), explicit_cfg, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(cfgtest), cfgtest, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(cfgtest_strict), cfgtest_strict, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(strict), strict, &cfg), ARGS_OK);
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
  tcase_add_test(tc, schema_option_is_repeatable);
  tcase_add_test(tc, schema_entries_are_capped);
  tcase_add_test(tc, profile_defaults_to_dvb_tva_and_parses_names);
  tcase_add_test(tc, publisher_options_are_recorded_and_checked);
  tcase_add_test(tc, syntax_defaults_to_23001_and_parses_names);
  tcase_add_test(tc, compat_collects_names_and_rejects_unknown);
  tcase_add_test(tc, generic_and_dvb_options_are_recorded_and_checked);
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
