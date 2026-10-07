/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../cfg_fixture.h"
#include "../log_capture.h"
#include "lib/config/yamlcfg.h"

#include "dipisds/cli/args.h"
#include "dipisds/cli/priv.h"
#include "lib/helper/log.h"
#include "lib/net/netconnect.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(announce_requires_provider_and_offering_for_playlist_input) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-m", "239.1.2.3:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(announce_playlist_input_ok_with_provider_and_offering) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "My Headend",
                  "-m", "239.1.2.3:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.provider, "example.org");
  ck_assert_str_eq(cfg.offering, "My Headend");
  ck_assert_int_eq(cfg.interval_s, 5);
  ck_assert_int_eq(memcmp(cfg.lang, "deu", 3), 0);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(announce_raw_xml_input_does_not_require_provider_or_offering) {
  char *argv[] = {"dipisds", "-a", "-i", "raw.xml", "-m", "239.1.2.3:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(lang_must_be_three_letters) {
  char *argv[] = {"dipisds", "-a", "-i", "raw.xml", "-m", "239.1.2.3:5000", "-L", "engl", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(lang_override_is_applied) {
  char *argv[] = {"dipisds", "-a", "-i", "raw.xml", "-m", "239.1.2.3:5000", "-L", "eng", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(memcmp(cfg.lang, "eng", 3), 0);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(listen_defaults_output_and_timeout) {
  char *argv[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.output_path, "-");
  ck_assert_int_eq(cfg.timeout_s, 35);
  ck_assert_int_eq(cfg.format, OUT_M3U);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(format_flag_selects_requested_format) {
  char *argv[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "-f", "xspf", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.format, OUT_XSPF);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(format_flag_rejects_unknown_value) {
  char *argv[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "-f", "bogus", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(mode_is_required) {
  char *argv[] = {"dipisds", "-m", "239.1.2.3:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(mode_is_exclusive) {
  char *argv[] = {"dipisds", "-a", "-l", "-i", "raw.xml", "-m", "239.1.2.3:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(mcast_is_required) {
  char *argv[] = {"dipisds", "-l", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(mcast_rejects_non_multicast_address) {
  char *argv[] = {"dipisds", "-l", "-m", "10.0.0.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(ret_addr_enables_ret_with_defaults) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--ret-addr", "10.0.0.1:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.ret_enabled, 1);
  ck_assert_str_eq(cfg.ret_addr, "10.0.0.1");
  ck_assert_uint_eq(cfg.ret_port, 6000u);
  ck_assert_uint_eq(cfg.ret_rtx_time, 2000u);
  ck_assert_uint_eq(cfg.ret_rtx_pt, 99u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(ret_addr_rejected_with_raw_xml_input) {
  char *argv[] = {"dipisds", "-a", "-i", "raw.xml", "-m", "239.1.2.3:5000",
                  "--ret-addr", "10.0.0.1:6000", NULL};
  /* raw.xml matches has_suffix(".xml") so --ret-addr is rejected here */
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(ret_rtx_time_without_ret_addr_is_rejected) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--ret-rtx-time", "1000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(ret_options_are_announce_only) {
  char *argv[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "--ret-mc", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(ret_rsi_mc_ret_requires_ret_mc) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--ret-addr", "10.0.0.1:6000", "--ret-rsi-mc-ret", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(ret_rsi_mc_ret_accepted_with_ret_mc) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--ret-addr", "10.0.0.1:6000", "--ret-mc", "--ret-rsi-mc-ret", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.ret_rsi_mc_ret, 1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(fcc_addr_enables_fcc_with_defaults) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--fcc-addr", "10.0.0.1:7000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.fcc_enabled, 1);
  ck_assert_str_eq(cfg.fcc_addr, "10.0.0.1");
  ck_assert_uint_eq(cfg.fcc_port, 7000u);
  ck_assert_uint_eq(cfg.fcc_rtx_time, 2000u);
  ck_assert_uint_eq(cfg.fcc_rtx_pt, 99u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(fcc_rtx_pt_out_of_range_is_rejected) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--fcc-addr", "10.0.0.1:7000", "--fcc-rtx-pt", "200", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(fcc_options_are_announce_only) {
  char *argv[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "--fcc-rtx-time", "1000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(fcc_resolve_by_port_enables_with_default_max_channels) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--fcc-addr", "10.0.0.1:7000", "--fcc-resolve-by-port", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.fcc_resolve_by_port, 1);
  ck_assert_uint_eq(cfg.fcc_resolve_max_channels, 300u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(fcc_resolve_max_channels_overrides_default) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--fcc-addr", "10.0.0.1:7000",
                  "--fcc-resolve-by-port", "--fcc-resolve-max-channels", "512", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.fcc_resolve_max_channels, 512u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(fcc_resolve_by_port_requires_fcc_addr) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--fcc-resolve-by-port", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(fcc_resolve_by_port_is_announce_only) {
  char *argv[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "--fcc-resolve-by-port", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(metrics_options_require_metrics_id) {
  char *argv[] = {"dipisds", "-a", "-i", "raw.xml", "-m", "239.1.2.3:5000",
                  "--metrics", "/tmp/x.sock", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(metrics_id_is_announce_only) {
  char *argv[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "--metrics-id", "inst1", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(packages_path_rejected_with_raw_xml_input) {
  char *argv[] = {"dipisds", "-a", "-i", "raw.xml", "-m", "239.1.2.3:5000",
                  "--packages", "pkgs.csv", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(packages_path_accepted_with_playlist_input) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--packages", "pkgs.csv", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.packages_path, "pkgs.csv");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(cells_path_rejected_with_raw_xml_input) {
  char *argv[] = {"dipisds", "-a", "-i", "raw.xml", "-m", "239.1.2.3:5000",
                  "--cells", "cells.csv", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rms_name_enables_rms_with_lang_default) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--rms-name", "My RMS", "--rms-location", "https://rms.example/", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rms_enabled, 1);
  ck_assert_str_eq(cfg.rms_name, "My RMS");
  ck_assert_str_eq(cfg.rms_location, "https://rms.example/");
  ck_assert_int_eq(memcmp(cfg.rms_lang, "deu", 3), 0);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rms_name_requires_rms_location) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--rms-name", "My RMS", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rms_location_without_rms_name_is_rejected) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--rms-location", "https://rms.example/", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(fus_name_enables_fus_with_lang_default) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--fus-name", "My FUS", "--fus-id", "42", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.fus_enabled, 1);
  ck_assert_str_eq(cfg.fus_name, "My FUS");
  ck_assert_uint_eq((unsigned)cfg.fus_id, 42u);
  ck_assert_int_eq(memcmp(cfg.fus_lang, "deu", 3), 0);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(fus_name_requires_fus_id) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--fus-name", "My FUS", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(fus_announce_is_parsed) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--fus-name", "My FUS", "--fus-id", "1",
                  "--fus-announce", "239.1.1.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.fus_announce_addr, "239.1.1.1");
  ck_assert_uint_eq(cfg.fus_announce_port, 5000u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rms_name_and_fus_name_are_mutually_exclusive) {
  char *argv[] = {"dipisds", "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Name",
                  "-m", "239.1.2.3:5000", "--rms-name", "R", "--rms-location", "https://r/",
                  "--fus-name", "F", "--fus-id", "1", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rms_and_fus_options_are_announce_only) {
  char *argv[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "--rms-name", "R", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(help_returns_help_status) {
  char *argv[] = {"dipisds", "-h", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(unexpected_positional_argument_is_rejected) {
  char *argv[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "extra", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(listen_format_follows_output_suffix) {
  char *argv[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "-o", "out.csv", NULL};
  char *argv2[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "-o", "out.csv", "-f", "xspf", NULL};
  char *argv3[] = {"dipisds", "-l", "-m", "239.1.2.3:5000", "-o", "out.txt", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.format, OUT_CSV);
  ck_assert_int_eq(args_parse(ARGC(argv2), argv2, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.format, OUT_XSPF);
  ck_assert_int_eq(args_parse(ARGC(argv3), argv3, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.format, OUT_M3U);
  yamlcfg_strpool_free(cfg.str_pool);
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
  config_t cfg = {0};
  write_cfg(path, "announce: on\nmcast: 239.1.2.3:5000\ninput: /tmp/x.xml\ninterval: 9\nmetrics:\n  id: sds\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.mode, MODE_ANNOUNCE);
  ck_assert_str_eq(cfg.mcast_group, "239.1.2.3");
  ck_assert_int_eq(cfg.interval_s, 9);
  ck_assert_str_eq(cfg.metrics_id, "sds");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(cmdline_wins_over_config) {
  char path[] = "/tmp/dipisds_cfg_XXXXXX";
  char *argv[] = {"dipisds", "-c", path, "-l", "-t", "3", "-o", "/tmp/out.m3u", NULL};
  config_t cfg = {0};
  write_cfg(path, "announce: on\nmcast: 239.1.2.3:5000\ninterval: 9\ntimeout: 50\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.mode, MODE_LISTEN);
  ck_assert_int_eq(cfg.timeout_s, 3);
  ck_assert_str_eq(cfg.output_path, "/tmp/out.m3u");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_missing_file_is_error) {
  char *argv[] = {"dipisds", "-c", "/nonexistent/dipisds.yaml", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_invalid_value_is_error) {
  char path[] = "/tmp/dipisds_cfg_XXXXXX";
  char *argv[] = {"dipisds", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "listen: on\nmcast: 239.1.2.3:5000\nformat: bogus\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_both_modes_is_error) {
  char path[] = "/tmp/dipisds_cfg_XXXXXX";
  char *argv[] = {"dipisds", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "announce: on\nlisten: on\nmcast: 239.1.2.3:5000\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(configtest_reports_by_exit_status) {
  char path[] = "/tmp/dipisds_cfg_XXXXXX";
  char *argv[] = {"dipisds", "--configtest", "-c", path, NULL};
  char *argv2[] = {"dipisds", "--configtest", "-c", "/nonexistent/dipisds.yaml", NULL};
  config_t cfg = {0};
  write_cfg(path, "bogus: 1\nannounce: on\nlisten: on\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
  ck_assert_int_eq(args_parse(ARGC(argv2), argv2, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

#define ARGV_MAX 24
#define MSG_MAX 2048

typedef struct {
  char *argv[ARGV_MAX];
  int argc;
  cfg_kind_t kind;
  size_t off;
  double num;
  const char *str;
} option_case_t;

#define OPT(field, kind, num, str, ...) {{"dipisds", __VA_ARGS__}, 1 + (int)(sizeof((const char *[]){__VA_ARGS__}) / sizeof(char *)), kind, offsetof(config_t, field), num, str}
#define ANN_BASE "-a", "-i", "channels.csv", "-p", "example.org", "-O", "Offer", "-m", "239.1.2.3:5000"

static const option_case_t ret_option_cases[] = {
  OPT(ret_enabled, CFG_INT, 1, NULL, ANN_BASE, "--ret-addr", "10.0.0.1:6000"),
  OPT(ret_addr, CFG_CHARARR, 0, "10.0.0.1", ANN_BASE, "--ret-addr", "10.0.0.1:6000"),
  OPT(ret_port, CFG_UINT, 6000, NULL, ANN_BASE, "--ret-addr", "10.0.0.1:6000"),
  OPT(ret_rtx_time, CFG_UINT, 2000, NULL, ANN_BASE, "--ret-addr", "10.0.0.1:6000"),
  OPT(ret_rtx_pt, CFG_UCHAR, 99, NULL, ANN_BASE, "--ret-addr", "10.0.0.1:6000"),
  OPT(ret_rtx_time, CFG_UINT, 1500, NULL, ANN_BASE, "--ret-addr", "10.0.0.1:6000", "--ret-rtx-time", "1500"),
  OPT(ret_rtx_pt, CFG_UCHAR, 100, NULL, ANN_BASE, "--ret-addr", "10.0.0.1:6000", "--ret-rtx-pt", "100"),
  OPT(ret_mc, CFG_INT, 1, NULL, ANN_BASE, "--ret-addr", "10.0.0.1:6000", "--ret-mc"),
  OPT(ret_mc_port, CFG_UINT, 6002, NULL, ANN_BASE, "--ret-addr", "10.0.0.1:6000", "--ret-mc-port", "6002"),
  OPT(ret_rsi_mc_ret, CFG_INT, 1, NULL, ANN_BASE, "--ret-addr", "10.0.0.1:6000", "--ret-mc", "--ret-rsi-mc-ret"),
  OPT(fcc_enabled, CFG_INT, 1, NULL, ANN_BASE, "--fcc-addr", "10.0.0.2:6001"),
  OPT(fcc_addr, CFG_CHARARR, 0, "10.0.0.2", ANN_BASE, "--fcc-addr", "10.0.0.2:6001"),
  OPT(fcc_port, CFG_UINT, 6001, NULL, ANN_BASE, "--fcc-addr", "10.0.0.2:6001"),
  OPT(fcc_rtx_time, CFG_UINT, 2000, NULL, ANN_BASE, "--fcc-addr", "10.0.0.2:6001"),
  OPT(fcc_rtx_pt, CFG_UCHAR, 99, NULL, ANN_BASE, "--fcc-addr", "10.0.0.2:6001"),
  OPT(fcc_resolve_max_channels, CFG_SIZE, 300, NULL, ANN_BASE, "--fcc-addr", "10.0.0.2:6001"),
  OPT(fcc_rtx_time, CFG_UINT, 1600, NULL, ANN_BASE, "--fcc-addr", "10.0.0.2:6001", "--fcc-rtx-time", "1600"),
  OPT(fcc_rtx_pt, CFG_UCHAR, 101, NULL, ANN_BASE, "--fcc-addr", "10.0.0.2:6001", "--fcc-rtx-pt", "101"),
  OPT(fcc_resolve_by_port, CFG_INT, 1, NULL, ANN_BASE, "--fcc-addr", "10.0.0.2:6001", "--fcc-resolve-by-port"),
  OPT(fcc_resolve_base_port, CFG_UINT, 30000, NULL, ANN_BASE, "--fcc-addr", "10.0.0.2:6001", "--fcc-resolve-base-port", "30000"),
  OPT(fcc_resolve_max_channels, CFG_SIZE, 123, NULL, ANN_BASE, "--fcc-addr", "10.0.0.2:6001", "--fcc-resolve-max-channels", "123"),
  OPT(al_fec_enabled, CFG_INT, 1, NULL, ANN_BASE, "--al-fec-addr", "10.0.0.3:6004"),
  OPT(al_fec_addr, CFG_CHARARR, 0, "10.0.0.3", ANN_BASE, "--al-fec-addr", "10.0.0.3:6004"),
  OPT(al_fec_port, CFG_UINT, 6004, NULL, ANN_BASE, "--al-fec-addr", "10.0.0.3:6004"),
  OPT(al_fec_pt, CFG_UCHAR, 96, NULL, ANN_BASE, "--al-fec-addr", "10.0.0.3:6004"),
  OPT(al_fec_pt, CFG_UCHAR, 97, NULL, ANN_BASE, "--al-fec-addr", "10.0.0.3:6004", "--al-fec-pt", "97"),
};

static const option_case_t rms_option_cases[] = {
  OPT(metrics_sock, CFG_STRPTR, 0, "/tmp/m.sock", ANN_BASE, "--metrics", "/tmp/m.sock", "--metrics-id", "sds1"),
  OPT(metrics_id, CFG_STRPTR, 0, "sds1", ANN_BASE, "--metrics-id", "sds1"),
  OPT(metrics_interval_s, CFG_UINT, 42, NULL, ANN_BASE, "--metrics-id", "sds1", "--metrics-interval", "42"),
  OPT(packages_path, CFG_STRPTR, 0, "pkgs.xml", ANN_BASE, "--packages", "pkgs.xml"),
  OPT(cells_path, CFG_STRPTR, 0, "cells.xml", ANN_BASE, "--cells", "cells.xml"),
  OPT(rms_enabled, CFG_INT, 1, NULL, ANN_BASE, "--rms-name", "Regional", "--rms-location", "Berlin"),
  OPT(rms_name, CFG_STRPTR, 0, "Regional", ANN_BASE, "--rms-name", "Regional", "--rms-location", "Berlin"),
  OPT(rms_location, CFG_STRPTR, 0, "Berlin", ANN_BASE, "--rms-name", "Regional", "--rms-location", "Berlin"),
  OPT(rms_lang, CFG_CHARARR, 0, "deu", ANN_BASE, "--rms-name", "Regional", "--rms-location", "Berlin"),
  OPT(rms_lang, CFG_CHARARR, 0, "fra", ANN_BASE, "--rms-name", "Regional", "--rms-location", "Berlin", "--rms-lang", "fra"),
  OPT(rms_logo, CFG_STRPTR, 0, "logo.png", ANN_BASE, "--rms-name", "Regional", "--rms-location", "Berlin", "--rms-logo", "logo.png"),
  OPT(fus_enabled, CFG_INT, 1, NULL, ANN_BASE, "--fus-name", "Follow", "--fus-id", "4711"),
  OPT(fus_name, CFG_STRPTR, 0, "Follow", ANN_BASE, "--fus-name", "Follow", "--fus-id", "4711"),
  OPT(fus_id, CFG_SIZE, 4711, NULL, ANN_BASE, "--fus-name", "Follow", "--fus-id", "4711"),
  OPT(fus_lang, CFG_CHARARR, 0, "deu", ANN_BASE, "--fus-name", "Follow", "--fus-id", "4711"),
  OPT(fus_lang, CFG_CHARARR, 0, "fra", ANN_BASE, "--fus-name", "Follow", "--fus-id", "4711", "--fus-lang", "fra"),
  OPT(fus_announce_addr, CFG_CHARARR, 0, "239.9.9.9", ANN_BASE, "--fus-name", "Follow", "--fus-id", "4711", "--fus-announce", "239.9.9.9:5000"),
  OPT(fus_announce_port, CFG_UINT, 5000, NULL, ANN_BASE, "--fus-name", "Follow", "--fus-id", "4711", "--fus-announce", "239.9.9.9:5000"),
  OPT(fus_logo, CFG_STRPTR, 0, "f.png", ANN_BASE, "--fus-name", "Follow", "--fus-id", "4711", "--fus-logo", "f.png"),
};

static const option_case_t listen_option_cases[] = {
  OPT(format, CFG_INT, OUT_CSV, NULL, "-l", "-m", "239.1.2.3:5000", "-o", "out.csv"),
  OPT(format, CFG_INT, OUT_XSPF, NULL, "-l", "-m", "239.1.2.3:5000", "-o", "out.xspf"),
  OPT(format, CFG_INT, OUT_XML, NULL, "-l", "-m", "239.1.2.3:5000", "-o", "out.xml"),
  OPT(format, CFG_INT, OUT_M3U, NULL, "-l", "-m", "239.1.2.3:5000", "-o", "out.txt"),
  OPT(format, CFG_INT, OUT_NULL, NULL, "-l", "-m", "239.1.2.3:5000", "-o", "out.csv", "-f", "null"),
  OPT(output_path, CFG_STRPTR, 0, "-", "-l", "-m", "239.1.2.3:5000"),
  OPT(timeout_s, CFG_LONG, 35, NULL, "-l", "-m", "239.1.2.3:5000"),
  OPT(timeout_s, CFG_LONG, 12, NULL, "-l", "-m", "239.1.2.3:5000", "-t", "12"),
  OPT(interval_s, CFG_LONG, 9, NULL, ANN_BASE, "--interval", "9"),
  OPT(dscp, CFG_INT, NET_DSCP_VIDEO_LOW, NULL, ANN_BASE, "--dscp", "video-low"),
  OPT(daemonize, CFG_INT, 1, NULL, ANN_BASE, "-d"),
  OPT(verbose, CFG_INT, 1, NULL, ANN_BASE, "-v"),
  OPT(iface, CFG_STRPTR, 0, "eth7", ANN_BASE, "-I", "eth7"),
  OPT(color_mode, CFG_INT, LOG_COLOR_NEVER, NULL, ANN_BASE, "--color", "never"),
};

static void run_option_case(const option_case_t *c) {
  cfg_field_case_t fc = {NULL, c->kind, c->off, c->num, c->str};
  config_t cfg = {0};
  char *av[ARGV_MAX];

  memcpy(av, c->argv, sizeof av);
  ck_assert_int_eq(args_parse(c->argc, av, &cfg), ARGS_OK);
  cfg_field_check(&cfg, &fc);
  yamlcfg_strpool_free(cfg.str_pool);
}

START_TEST(ret_fcc_al_fec_options_set_their_fields) {
  run_option_case(&ret_option_cases[_i]);
}
END_TEST

START_TEST(rms_fus_metrics_options_set_their_fields) {
  run_option_case(&rms_option_cases[_i]);
}
END_TEST

START_TEST(general_and_listen_options_set_their_fields) {
  run_option_case(&listen_option_cases[_i]);
}
END_TEST

typedef struct {
  char *argv[ARGV_MAX];
  int argc;
  args_status_t status;
  const char *message;
} message_case_t;

#define MSG(status, message, ...) {{"dipisds", __VA_ARGS__}, 1 + (int)(sizeof((const char *[]){__VA_ARGS__}) / sizeof(char *)), status, message}
#define MC "-m", "239.1.2.3:5000"

static const message_case_t error_cases[] = {
  MSG(ARGS_ERR, "invalid -L lang: de (3-letter ISO 639-2 code)", ANN_BASE, "-L", "de"),
  MSG(ARGS_ERR, "invalid -m group:port: bogus", "-a", "-m", "bogus"),
  MSG(ARGS_ERR, "invalid --dscp: loud", ANN_BASE, "--dscp", "loud"),
  MSG(ARGS_ERR, "invalid -t seconds: soon", ANN_BASE, "-t", "soon"),
  MSG(ARGS_ERR, "invalid --format: pdf", "-l", MC, "-f", "pdf"),
  MSG(ARGS_ERR, "invalid --color: rainbow", ANN_BASE, "--color", "rainbow"),
  MSG(ARGS_ERR, "invalid --ret-addr: bogus", ANN_BASE, "--ret-addr", "bogus"),
  MSG(ARGS_ERR, "invalid --ret-rtx-time: 0", ANN_BASE, "--ret-addr", "10.0.0.1:6000", "--ret-rtx-time", "0"),
  MSG(ARGS_ERR, "invalid --ret-rtx-pt: 128 (0..127)", ANN_BASE, "--ret-addr", "10.0.0.1:6000", "--ret-rtx-pt", "128"),
  MSG(ARGS_ERR, "invalid --ret-mc-port: 0", ANN_BASE, "--ret-addr", "10.0.0.1:6000", "--ret-mc-port", "0"),
  MSG(ARGS_ERR, "invalid --fcc-addr: bogus", ANN_BASE, "--fcc-addr", "bogus"),
  MSG(ARGS_ERR, "invalid --al-fec-addr: bogus", ANN_BASE, "--al-fec-addr", "bogus"),
  MSG(ARGS_ERR, "invalid --al-fec-pt: 128 (0..127)", ANN_BASE, "--al-fec-addr", "10.0.0.3:6004", "--al-fec-pt", "128"),
  MSG(ARGS_ERR, "invalid --fcc-rtx-time: 0", ANN_BASE, "--fcc-addr", "10.0.0.2:6001", "--fcc-rtx-time", "0"),
  MSG(ARGS_ERR, "invalid --fcc-rtx-pt: 128 (0..127)", ANN_BASE, "--fcc-addr", "10.0.0.2:6001", "--fcc-rtx-pt", "128"),
  MSG(ARGS_ERR, "invalid --fcc-resolve-base-port: 0", ANN_BASE, "--fcc-addr", "10.0.0.2:6001", "--fcc-resolve-base-port", "0"),
  MSG(ARGS_ERR, "invalid --fcc-resolve-max-channels: 0", ANN_BASE, "--fcc-addr", "10.0.0.2:6001", "--fcc-resolve-max-channels", "0"),
  MSG(ARGS_ERR, "invalid --metrics-interval: 0", ANN_BASE, "--metrics-id", "m", "--metrics-interval", "0"),
  MSG(ARGS_ERR, "invalid --rms-lang: fr (3-letter ISO 639-2 code)", ANN_BASE, "--rms-name", "R", "--rms-location", "L", "--rms-lang", "fr"),
  MSG(ARGS_ERR, "invalid --fus-lang: fr (3-letter ISO 639-2 code)", ANN_BASE, "--fus-name", "F", "--fus-id", "1", "--fus-lang", "fr"),
  MSG(ARGS_ERR, "invalid --fus-id: 12ab", ANN_BASE, "--fus-name", "F", "--fus-id", "12ab"),
  MSG(ARGS_ERR, "invalid --fus-announce: bogus", ANN_BASE, "--fus-name", "F", "--fus-id", "1", "--fus-announce", "bogus"),
  MSG(ARGS_ERR, "", ANN_BASE, "--no-such-option"),
};

static const message_case_t check_cases[] = {
  MSG(ARGS_ERR, "exactly one of -a/--announce or -l/--listen is required", MC),
  MSG(ARGS_ERR, "exactly one of -a/--announce or -l/--listen is required", "-a", "-l", MC),
  MSG(ARGS_ERR, "missing -m multicast group:port", "-a", "-i", "raw.xml"),
  MSG(ARGS_ERR, "--metrics/--metrics-interval require --metrics-id", "-a", "-i", "raw.xml", MC, "--metrics", "/tmp/m.sock"),
  MSG(ARGS_ERR, "missing -i input", "-a", MC),
  MSG(ARGS_ERR, "missing -p provider", "-a", "-i", "channels.csv", MC),
  MSG(ARGS_ERR, "missing -O offering", "-a", "-i", "channels.csv", "-p", "example.org", MC),
  MSG(ARGS_ERR, "--ret-addr has no effect with a raw .xml", "-a", "-i", "raw.xml", MC, "--ret-addr", "10.0.0.1:6000"),
  MSG(ARGS_ERR, "--ret-rtx-time/--ret-rtx-pt/--ret-mc/--ret-mc-port/--ret-rsi-mc-ret require --ret-addr", ANN_BASE, "--ret-rtx-time", "100"),
  MSG(ARGS_ERR, "--ret-rsi-mc-ret requires --ret-mc", ANN_BASE, "--ret-addr", "10.0.0.1:6000", "--ret-rsi-mc-ret"),
  MSG(ARGS_ERR, "--fcc-addr has no effect with a raw .xml", "-a", "-i", "raw.xml", MC, "--fcc-addr", "10.0.0.2:6001"),
  MSG(ARGS_ERR, "--fcc-rtx-time/--fcc-rtx-pt/--fcc-resolve-* require --fcc-addr", ANN_BASE, "--fcc-resolve-by-port"),
  MSG(ARGS_ERR, "--al-fec-addr has no effect with a raw .xml", "-a", "-i", "raw.xml", MC, "--al-fec-addr", "10.0.0.3:6004"),
  MSG(ARGS_ERR, "--al-fec-pt requires --al-fec-addr", ANN_BASE, "--al-fec-pt", "97"),
  MSG(ARGS_ERR, "have no effect with a raw .xml -i input", "-a", "-i", "raw.xml", MC, "--packages", "p.xml"),
  MSG(ARGS_ERR, "--rms-name and --fus-name are mutually exclusive", ANN_BASE, "--rms-name", "R", "--rms-location", "L", "--fus-name", "F", "--fus-id", "1"),
  MSG(ARGS_ERR, "--rms-lang/--rms-location/--rms-logo require --rms-name", ANN_BASE, "--rms-lang", "fra"),
  MSG(ARGS_ERR, "--rms-name requires --rms-location", ANN_BASE, "--rms-name", "R"),
  MSG(ARGS_ERR, "--fus-lang/--fus-id/--fus-announce/--fus-logo require --fus-name", ANN_BASE, "--fus-lang", "fra"),
  MSG(ARGS_ERR, "--fus-name requires --fus-id", ANN_BASE, "--fus-name", "F"),
  MSG(ARGS_ERR, "--ret-* options are announce-only", "-l", MC, "--ret-addr", "10.0.0.1:6000"),
  MSG(ARGS_ERR, "--fcc-* options are announce-only", "-l", MC, "--fcc-addr", "10.0.0.2:6001"),
  MSG(ARGS_ERR, "--metrics-id is announce-only", "-l", MC, "--metrics-id", "m"),
  MSG(ARGS_ERR, "--packages/--cells/--rms-*/--fus-* options are announce-only", "-l", MC, "--cells", "c.xml"),
};

static void run_message_case(const message_case_t *c) {
  char msg[MSG_MAX];
  config_t cfg = {0};
  char *av[ARGV_MAX];
  args_status_t st;
  memcpy(av, c->argv, sizeof av);
  log_capture_begin();
  st = args_parse(c->argc, av, &cfg);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(st, c->status);
  ck_assert_msg(strstr(msg, c->message) != NULL, "want '%s' in '%s'", c->message, msg);
  yamlcfg_strpool_free(cfg.str_pool);
}

START_TEST(option_errors_report_their_message) {
  run_message_case(&error_cases[_i]);
}
END_TEST

START_TEST(consistency_checks_report_their_message) {
  run_message_case(&check_cases[_i]);
}
END_TEST

typedef struct {
  const char *mcast;
  const char *want;
} describe_case_t;

static const describe_case_t describe_cases[] = {
  {"239.1.2.3:5000", "239.1.2.3:5000"},
  {"[ff15::1]:6000", "[ff15::1]:6000"},
};

START_TEST(mcast_describe_formats_group_and_port) {
  config_t cfg = {0};
  char buf[96];

  ck_assert_int_eq(sds_mcast_parse(describe_cases[_i].mcast, &cfg), 0);
  mcast_describe(&cfg, buf, sizeof buf);
  ck_assert_str_eq(buf, describe_cases[_i].want);
}
END_TEST

START_TEST(suffix_and_address_helpers) {
  char addr[64];
  unsigned port = 0;

  ck_assert_int_eq(sds_has_suffix("a.xml", ".xml"), 1);
  ck_assert_int_eq(sds_has_suffix("xml", ".xml"), 0);
  ck_assert_int_eq(sds_has_suffix("a.xmlx", ".xml"), 0);
  ck_assert_int_eq(sds_ret_addr_parse("10.0.0.1:6000", addr, sizeof addr, &port), 0);
  ck_assert_str_eq(addr, "10.0.0.1");
  ck_assert_uint_eq(port, 6000u);
  ck_assert_int_eq(sds_ret_addr_parse("10.0.0.1", addr, sizeof addr, &port), -1);
  ck_assert_int_eq(sds_ret_addr_parse("10.0.0.1:0", addr, sizeof addr, &port), -1);
  ck_assert_int_eq(sds_mcast_parse("127.0.0.1:5000", &(config_t){0}), -1);
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
  tcase_add_loop_test(tc, ret_fcc_al_fec_options_set_their_fields, 0, (int)(sizeof ret_option_cases / sizeof ret_option_cases[0]));
  tcase_add_loop_test(tc, rms_fus_metrics_options_set_their_fields, 0, (int)(sizeof rms_option_cases / sizeof rms_option_cases[0]));
  tcase_add_loop_test(tc, general_and_listen_options_set_their_fields, 0, (int)(sizeof listen_option_cases / sizeof listen_option_cases[0]));
  tcase_add_loop_test(tc, option_errors_report_their_message, 0, (int)(sizeof error_cases / sizeof error_cases[0]));
  tcase_add_loop_test(tc, consistency_checks_report_their_message, 0, (int)(sizeof check_cases / sizeof check_cases[0]));
  tcase_add_loop_test(tc, mcast_describe_formats_group_and_port, 0, (int)(sizeof describe_cases / sizeof describe_cases[0]));
  tcase_add_test(tc, suffix_and_address_helpers);
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
