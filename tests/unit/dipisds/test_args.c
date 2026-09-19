/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
  ck_assert_uint_eq(cfg.fcc_resolve_max_channels, 300u);
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

START_TEST(packages_path_rejected_with_raw_xml_input) {
  char *argv[] = {"dipisds", "-a", "-i", "raw.xml", "-m", "239.1.2.3:5000",
                  "--packages", "pkgs.csv", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(packages_path_accepted_with_playlist_input) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--packages", "pkgs.csv", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.packages_path, "pkgs.csv");
}
END_TEST

START_TEST(cells_path_rejected_with_raw_xml_input) {
  char *argv[] = {"dipisds", "-a", "-i", "raw.xml", "-m", "239.1.2.3:5000",
                  "--cells", "cells.csv", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rms_name_enables_rms_with_lang_default) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--rms-name", "My RMS", "--rms-location", "https://rms.example/", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rms_enabled, 1);
  ck_assert_str_eq(cfg.rms_name, "My RMS");
  ck_assert_str_eq(cfg.rms_location, "https://rms.example/");
  ck_assert_int_eq(memcmp(cfg.rms_lang, "deu", 3), 0);
}
END_TEST

START_TEST(rms_name_requires_rms_location) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--rms-name", "My RMS", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rms_location_without_rms_name_is_rejected) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--rms-location", "https://rms.example/", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(fus_name_enables_fus_with_lang_default) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--fus-name", "My FUS", "--fus-id", "42", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.fus_enabled, 1);
  ck_assert_str_eq(cfg.fus_name, "My FUS");
  ck_assert_uint_eq((unsigned)cfg.fus_id, 42u);
  ck_assert_int_eq(memcmp(cfg.fus_lang, "deu", 3), 0);
}
END_TEST

START_TEST(fus_name_requires_fus_id) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--fus-name", "My FUS", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(fus_announce_is_parsed) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--fus-name", "My FUS", "--fus-id", "1",
                  "--fus-announce", "239.1.1.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.fus_announce_addr, "239.1.1.1");
  ck_assert_uint_eq(cfg.fus_announce_port, 5000u);
}
END_TEST

START_TEST(rms_name_and_fus_name_are_mutually_exclusive) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--rms-name", "R", "--rms-location", "https://r/",
                  "--fus-name", "F", "--fus-id", "1", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rms_and_fus_options_are_announce_only) {
  char *argv[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "--rms-name", "R", NULL};
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

START_TEST(listen_format_follows_output_suffix) {
  char *argv[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "-o", "out.csv", NULL};
  char *argv2[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "-o", "out.csv", "-f", "xspf", NULL};
  char *argv3[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "-o", "out.txt", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.format, OUT_CSV);
  ck_assert_int_eq(args_parse(ARGC(argv2), argv2, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.format, OUT_XSPF);
  ck_assert_int_eq(args_parse(ARGC(argv3), argv3, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.format, OUT_M3U);
}
END_TEST

static void write_cfg(char *path, const char *text) {
  int fd = mkstemp(path);
  ck_assert_int_ge(fd, 0);
  ck_assert_int_eq((int)write(fd, text, strlen(text)), (int)strlen(text));
  close(fd);
}

START_TEST(config_file_provides_settings) {
  char path[] = "/tmp/dipisds_cfg_XXXXXX";
  char *argv[] = {"dipisds", "-c", path, NULL};
  config_t cfg;
  write_cfg(path, "announce: on\nmcast: 239.1.2.3:5000\ninput: /tmp/x.xml\ninterval: 9\nmetrics:\n  id: sds\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.mode, MODE_ANNOUNCE);
  ck_assert_str_eq(cfg.mcast_group, "239.1.2.3");
  ck_assert_int_eq(cfg.interval_s, 9);
  ck_assert_str_eq(cfg.metrics_id, "sds");
}
END_TEST

START_TEST(cmdline_wins_over_config) {
  char path[] = "/tmp/dipisds_cfg_XXXXXX";
  char *argv[] = {"dipisds", "-c", path, "-l", "-t", "3", "-o", "/tmp/out.m3u", NULL};
  config_t cfg;
  write_cfg(path, "announce: on\nmcast: 239.1.2.3:5000\ninterval: 9\ntimeout: 50\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.mode, MODE_LISTEN);
  ck_assert_int_eq(cfg.timeout_s, 3);
  ck_assert_str_eq(cfg.output_path, "/tmp/out.m3u");
}
END_TEST

START_TEST(config_missing_file_is_error) {
  char *argv[] = {"dipisds", "-c", "/nonexistent/dipisds.yaml", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(config_invalid_value_is_error) {
  char path[] = "/tmp/dipisds_cfg_XXXXXX";
  char *argv[] = {"dipisds", "-c", path, NULL};
  config_t cfg;
  write_cfg(path, "listen: on\nmcast: 239.1.2.3:5000\nformat: bogus\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
}
END_TEST

START_TEST(config_both_modes_is_error) {
  char path[] = "/tmp/dipisds_cfg_XXXXXX";
  char *argv[] = {"dipisds", "-c", path, NULL};
  config_t cfg;
  write_cfg(path, "announce: on\nlisten: on\nmcast: 239.1.2.3:5000\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
}
END_TEST

START_TEST(configtest_reports_by_exit_status) {
  char path[] = "/tmp/dipisds_cfg_XXXXXX";
  char *argv[] = {"dipisds", "--configtest", "-c", path, NULL};
  char *argv2[] = {"dipisds", "--configtest", "-c", "/nonexistent/dipisds.yaml", NULL};
  config_t cfg;
  write_cfg(path, "bogus: 1\nannounce: on\nlisten: on\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
  ck_assert_int_eq(args_parse(ARGC(argv2), argv2, &cfg), ARGS_ERR);
  unlink(path);
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
  tcase_add_test(tc, packages_path_rejected_with_raw_xml_input);
  tcase_add_test(tc, packages_path_accepted_with_playlist_input);
  tcase_add_test(tc, cells_path_rejected_with_raw_xml_input);
  tcase_add_test(tc, rms_name_enables_rms_with_lang_default);
  tcase_add_test(tc, rms_name_requires_rms_location);
  tcase_add_test(tc, rms_location_without_rms_name_is_rejected);
  tcase_add_test(tc, fus_name_enables_fus_with_lang_default);
  tcase_add_test(tc, fus_name_requires_fus_id);
  tcase_add_test(tc, fus_announce_is_parsed);
  tcase_add_test(tc, rms_name_and_fus_name_are_mutually_exclusive);
  tcase_add_test(tc, rms_and_fus_options_are_announce_only);
  tcase_add_test(tc, help_returns_help_status);
  tcase_add_test(tc, unexpected_positional_argument_is_rejected);
  tcase_add_test(tc, listen_format_follows_output_suffix);
  tcase_add_test(tc, config_file_provides_settings);
  tcase_add_test(tc, cmdline_wins_over_config);
  tcase_add_test(tc, config_missing_file_is_error);
  tcase_add_test(tc, config_invalid_value_is_error);
  tcase_add_test(tc, config_both_modes_is_error);
  tcase_add_test(tc, configtest_reports_by_exit_status);
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
