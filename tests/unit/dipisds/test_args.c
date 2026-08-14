/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipisds/args.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(announce_requires_provider_and_offering_for_playlist_input) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-m", "239.1.2.3:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(announce_playlist_input_ok_with_provider_and_offering) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "My Headend",
                  "-m", "239.1.2.3:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.provider, "example.org");
  ck_assert_str_eq(cfg.offering, "My Headend");
  ck_assert_int_eq(cfg.interval_s, 5);
  ck_assert_int_eq(memcmp(cfg.lang, "deu", 3), 0);
}
END_TEST

START_TEST(announce_raw_xml_input_does_not_require_provider_or_offering) {
  char *argv[] = {"dipisds", "-a", "-i", "raw.xml", "-m", "239.1.2.3:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
}
END_TEST

START_TEST(lang_must_be_three_letters) {
  char *argv[] = {"dipisds", "-a", "-i", "raw.xml", "-m", "239.1.2.3:5000", "-L", "engl", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(lang_override_is_applied) {
  char *argv[] = {"dipisds", "-a", "-i", "raw.xml", "-m", "239.1.2.3:5000", "-L", "eng", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(memcmp(cfg.lang, "eng", 3), 0);
}
END_TEST

START_TEST(listen_defaults_output_and_timeout) {
  char *argv[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.output_path, "-");
  ck_assert_int_eq(cfg.timeout_s, 35);
  ck_assert_int_eq(cfg.format, OUT_M3U);
}
END_TEST

START_TEST(format_flag_selects_requested_format) {
  char *argv[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "-f", "xspf", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.format, OUT_XSPF);
}
END_TEST

START_TEST(format_flag_rejects_unknown_value) {
  char *argv[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "-f", "bogus", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(mode_is_required) {
  char *argv[] = {"dipisds", "-m", "239.1.2.3:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(mode_is_exclusive) {
  char *argv[] = {"dipisds", "-a", "-l", "-i", "raw.xml", "-m", "239.1.2.3:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(mcast_is_required) {
  char *argv[] = {"dipisds", "-l", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(mcast_rejects_non_multicast_address) {
  char *argv[] = {"dipisds", "-l", "-m", "10.0.0.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(ret_addr_enables_ret_with_defaults) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--ret-addr", "10.0.0.1:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.ret_enabled, 1);
  ck_assert_str_eq(cfg.ret_addr, "10.0.0.1");
  ck_assert_uint_eq(cfg.ret_port, 6000u);
  ck_assert_uint_eq(cfg.ret_rtx_time, 2000u);
  ck_assert_uint_eq(cfg.ret_rtx_pt, 99u);
}
END_TEST

START_TEST(ret_addr_rejected_with_raw_xml_input) {
  char *argv[] = {"dipisds", "-a", "-i", "raw.xml", "-m", "239.1.2.3:5000",
                  "--ret-addr", "10.0.0.1:6000", NULL};
  /* raw.xml matches has_suffix(".xml") so --ret-addr is rejected here */
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(ret_rtx_time_without_ret_addr_is_rejected) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--ret-rtx-time", "1000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(ret_options_are_announce_only) {
  char *argv[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "--ret-mc", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(ret_rsi_mc_ret_requires_ret_mc) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--ret-addr", "10.0.0.1:6000", "--ret-rsi-mc-ret", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(ret_rsi_mc_ret_accepted_with_ret_mc) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--ret-addr", "10.0.0.1:6000", "--ret-mc", "--ret-rsi-mc-ret", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.ret_rsi_mc_ret, 1);
}
END_TEST

START_TEST(fcc_addr_enables_fcc_with_defaults) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--fcc-addr", "10.0.0.1:7000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.fcc_enabled, 1);
  ck_assert_str_eq(cfg.fcc_addr, "10.0.0.1");
  ck_assert_uint_eq(cfg.fcc_port, 7000u);
  ck_assert_uint_eq(cfg.fcc_rtx_time, 2000u);
  ck_assert_uint_eq(cfg.fcc_rtx_pt, 99u);
}
END_TEST

START_TEST(fcc_rtx_pt_out_of_range_is_rejected) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--fcc-addr", "10.0.0.1:7000", "--fcc-rtx-pt", "200", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(fcc_options_are_announce_only) {
  char *argv[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "--fcc-rtx-time", "1000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(fcc_resolve_by_port_enables_with_default_max_channels) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--fcc-addr", "10.0.0.1:7000", "--fcc-resolve-by-port", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.fcc_resolve_by_port, 1);
  ck_assert_uint_eq(cfg.fcc_resolve_max_channels, 384u);
}
END_TEST

START_TEST(fcc_resolve_max_channels_overrides_default) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--fcc-addr", "10.0.0.1:7000",
                  "--fcc-resolve-by-port", "--fcc-resolve-max-channels", "512", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.fcc_resolve_max_channels, 512u);
}
END_TEST

START_TEST(fcc_resolve_by_port_requires_fcc_addr) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--fcc-resolve-by-port", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(fcc_resolve_by_port_is_announce_only) {
  char *argv[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "--fcc-resolve-by-port", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(metrics_options_require_metrics_id) {
  char *argv[] = {"dipisds", "-a", "-i", "raw.xml", "-m", "239.1.2.3:5000",
                  "--metrics", "/tmp/x.sock", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(metrics_id_is_announce_only) {
  char *argv[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "--metrics-id", "inst1", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(help_returns_help_status) {
  char *argv[] = {"dipisds", "-h", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
}
END_TEST

START_TEST(unexpected_positional_argument_is_rejected) {
  char *argv[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "extra", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

static Suite *args_suite(void) {
  Suite *s = suite_create("dipisds_args");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, announce_requires_provider_and_offering_for_playlist_input);
  tcase_add_test(tc, announce_playlist_input_ok_with_provider_and_offering);
  tcase_add_test(tc, announce_raw_xml_input_does_not_require_provider_or_offering);
  tcase_add_test(tc, lang_must_be_three_letters);
  tcase_add_test(tc, lang_override_is_applied);
  tcase_add_test(tc, listen_defaults_output_and_timeout);
  tcase_add_test(tc, format_flag_selects_requested_format);
  tcase_add_test(tc, format_flag_rejects_unknown_value);
  tcase_add_test(tc, mode_is_required);
  tcase_add_test(tc, mode_is_exclusive);
  tcase_add_test(tc, mcast_is_required);
  tcase_add_test(tc, mcast_rejects_non_multicast_address);
  tcase_add_test(tc, ret_addr_enables_ret_with_defaults);
  tcase_add_test(tc, ret_addr_rejected_with_raw_xml_input);
  tcase_add_test(tc, ret_rtx_time_without_ret_addr_is_rejected);
  tcase_add_test(tc, ret_options_are_announce_only);
  tcase_add_test(tc, ret_rsi_mc_ret_requires_ret_mc);
  tcase_add_test(tc, ret_rsi_mc_ret_accepted_with_ret_mc);
  tcase_add_test(tc, fcc_addr_enables_fcc_with_defaults);
  tcase_add_test(tc, fcc_rtx_pt_out_of_range_is_rejected);
  tcase_add_test(tc, fcc_options_are_announce_only);
  tcase_add_test(tc, fcc_resolve_by_port_enables_with_default_max_channels);
  tcase_add_test(tc, fcc_resolve_max_channels_overrides_default);
  tcase_add_test(tc, fcc_resolve_by_port_requires_fcc_addr);
  tcase_add_test(tc, fcc_resolve_by_port_is_announce_only);
  tcase_add_test(tc, metrics_options_require_metrics_id);
  tcase_add_test(tc, metrics_id_is_announce_only);
  tcase_add_test(tc, help_returns_help_status);
  tcase_add_test(tc, unexpected_positional_argument_is_rejected);
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
