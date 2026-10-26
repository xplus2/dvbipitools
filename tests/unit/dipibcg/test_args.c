/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include "lib/bim/index.h"
#include "lib/config/yamlcfg.h"

#include "dipibcg/cli/args.h"
#include "../cfg_fixture.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(announce_publisher_lang) {
  config_t cfg;
  char *ok[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "g.xml", "-M", "m.csv", "-m", "239.1.2.3:5000", "--publisher", "example.com/My%20Guide", "--lang", "de-AT", "--index", "pi-crid,schedule-time", NULL};
  char *bad_pub[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "g.xml", "-M", "m.csv", "-m", "239.1.2.3:5000", "--publisher", "My Guide", NULL};
  char *bad_lang[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "g.xml", "-M", "m.csv", "-m", "239.1.2.3:5000", "--publisher", "a.b/x", "--lang", "toolonglang", NULL};
  char *lang_only[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "g.xml", "-M", "m.csv", "-m", "239.1.2.3:5000", "--lang", "en", NULL};
  char *bad_index[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "g.xml", "-M", "m.csv", "-m", "239.1.2.3:5000", "--index", "group-crid", NULL};
  char *listen[] = {"dipibcg", "-s", "x.xsd", "-l", "-m", "239.1.2.3:5000", "--publisher", "a.b/x", NULL};
  ck_assert_int_eq(args_parse(ARGC(ok), ok, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.publisher, "example.com/My%20Guide");
  ck_assert_str_eq(cfg.lang, "de-AT");
  ck_assert_uint_eq(cfg.index_mask, 1u << BIM_INDEX_PI_CRID | 1u << BIM_INDEX_SCHED_TIME);
  ck_assert_int_eq(args_parse(ARGC(bad_pub), bad_pub, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(bad_lang), bad_lang, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(lang_only), lang_only, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(bad_index), bad_index, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(listen), listen, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(announce_requires_input_and_map) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-a", "-m", "239.1.2.3:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(announce_with_required_options_applies_defaults) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.mode, MODE_ANNOUNCE);
  ck_assert_str_eq(cfg.input_path, "guide.xml");
  ck_assert_str_eq(cfg.map_path, "map.csv");
  ck_assert_int_eq(cfg.window_hours, 24);
  ck_assert_int_eq(cfg.interval_s, 5);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(announce_window_and_interval_are_overridable) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000",
                  "-w", "48", "-t", "10", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.window_hours, 48);
  ck_assert_int_eq(cfg.interval_s, 10);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(announce_rejects_zero_window) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000",
                  "-w", "0", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(listen_defaults_output_to_stdout_marker) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-l", "-m", "239.1.2.3:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.mode, MODE_LISTEN);
  ck_assert_str_eq(cfg.output_path, "-");
  ck_assert_int_eq(cfg.timeout_s, 35);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(listen_timeout_is_overridable) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-l", "-m", "239.1.2.3:5000", "-t", "60", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.timeout_s, 60);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(listen_rejects_negative_timeout) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-l", "-m", "239.1.2.3:5000", "-t", "-1", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(listen_rejects_interval_long_option) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-l", "-m", "239.1.2.3:5000", "--interval", "10", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(announce_rejects_timeout_long_option) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000", "--timeout", "10", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(long_options_set_matching_mode_value) {
  char *la[] = {"dipibcg", "-s", "x.xsd", "-l", "-m", "239.1.2.3:5000", "--timeout", "12", NULL};
  char *aa[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000", "--interval", "7", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(la), la, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.timeout_s, 12);
  yamlcfg_strpool_free(cfg.str_pool);
  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(args_parse(ARGC(aa), aa, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.interval_s, 7);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(mode_is_required) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-m", "239.1.2.3:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(mode_is_exclusive) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-a", "-l", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(mcast_is_required) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-l", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(mcast_rejects_non_multicast_ipv4) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-l", "-m", "10.0.0.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(mcast_accepts_multicast_ipv6) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-l", "-m", "[ff15::1]:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(mcast_rejects_non_multicast_ipv6) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-l", "-m", "[::1]:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(unexpected_positional_argument_is_rejected) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-l", "-m", "239.1.2.3:5000", "extra", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(help_returns_help_status) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-h", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(metrics_options_require_metrics_id) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000",
                  "--metrics", "/tmp/x.sock", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(metrics_id_is_announce_only) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-l", "-m", "239.1.2.3:5000", "--metrics-id", "inst1", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(metrics_id_enables_metrics_in_announce_mode) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000",
                  "--metrics-id", "inst1", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.metrics_id, "inst1");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(metrics_interval_out_of_range_is_rejected) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000",
                  "--metrics-id", "inst1", "--metrics-interval", "0", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(refresh_defaults_to_one_and_is_overridable) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000", NULL};
  char *ar[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000", "--refresh", "4", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.refresh_cycles, 1);
  yamlcfg_strpool_free(cfg.str_pool);
  memset(&cfg, 0, sizeof cfg);
  optind = 1;
  ck_assert_int_eq(args_parse(ARGC(ar), ar, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.refresh_cycles, 4);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(refresh_zero_is_rejected) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000", "--refresh", "0", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(refresh_is_announce_only) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-l", "-m", "239.1.2.3:5000", "--refresh", "3", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(announce_interval_zero_is_rejected) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000", "-t", "0", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(invalid_color_mode_is_rejected) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-l", "-m", "239.1.2.3:5000", "--color", "sometimes", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(compress_flag_enables_in_announce_mode) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000",
                  "-Z", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.compress, 1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(compress_flag_is_announce_only) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-l", "-m", "239.1.2.3:5000", "--compress", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(mcast_describe_formats_ipv4) {
  config_t cfg = {0};
  char buf[64];
  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET;
  strcpy(cfg.mcast_group, "239.1.2.3");
  cfg.mcast_port = 5000;
  mcast_describe(&cfg, buf, sizeof buf);
  ck_assert_str_eq(buf, "239.1.2.3:5000");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(mcast_describe_formats_ipv6) {
  config_t cfg = {0};
  char buf[64];
  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET6;
  strcpy(cfg.mcast_group, "ff15::1");
  cfg.mcast_port = 5000;
  mcast_describe(&cfg, buf, sizeof buf);
  ck_assert_str_eq(buf, "[ff15::1]:5000");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_file_provides_settings) {
  char path[] = "/tmp/dipibcg_cfg_XXXXXX";
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "announce: on\nmcast: 239.1.2.3:5000\ninput: g.xml\nmap: m.csv\nwindow: 12\ncompress: on\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.mode, MODE_ANNOUNCE);
  ck_assert_str_eq(cfg.mcast_group, "239.1.2.3");
  ck_assert_int_eq(cfg.window_hours, 12);
  ck_assert_int_eq(cfg.compress, 1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(cmdline_wins_over_config) {
  char path[] = "/tmp/dipibcg_cfg_XXXXXX";
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-c", path, "-l", "-t", "3", NULL};
  config_t cfg = {0};
  write_cfg(path, "announce: on\nmcast: 239.1.2.3:5000\ninterval: 9\ntimeout: 50\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.mode, MODE_LISTEN);
  ck_assert_int_eq(cfg.timeout_s, 3);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_missing_file_is_error) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-c", "/nonexistent/dipibcg.yaml", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_invalid_value_is_error) {
  char path[] = "/tmp/dipibcg_cfg_XXXXXX";
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "listen: on\nmcast: 239.1.2.3:5000\nwindow: 0\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_both_modes_is_error) {
  char path[] = "/tmp/dipibcg_cfg_XXXXXX";
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "announce: on\nlisten: on\nmcast: 239.1.2.3:5000\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(configtest_reports_by_exit_status) {
  char path[] = "/tmp/dipibcg_cfg_XXXXXX";
  char *argv[] = {"dipibcg", "-s", "x.xsd", "--configtest", "-c", path, NULL};
  char *argv2[] = {"dipibcg", "-s", "x.xsd", "--configtest", "-c", "/nonexistent/dipibcg.yaml", NULL};
  config_t cfg = {0};
  write_cfg(path, "bogus: 1\nannounce: on\nlisten: on\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
  ck_assert_int_eq(args_parse(ARGC(argv2), argv2, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(invalid_dscp_is_rejected) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-l", "-m", "239.1.2.3:5000", "--dscp", "bogus", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(announce_requires_map_when_input_is_given) {
  char *argv[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "guide.xml", "-m", "239.1.2.3:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_strict_option_rejects_unknown_keys) {
  char path[] = "/tmp/dipibcg_cfg_XXXXXX";
  char *strict_argv[] = {"dipibcg", "-s", "x.xsd", "--config-strict", "-c", path, NULL};
  char *lenient_argv[] = {"dipibcg", "-s", "x.xsd", "-c", path, NULL};
  char *test_argv[] = {"dipibcg", "-s", "x.xsd", "--configtest", "--config-strict", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "bogus: 1\nlisten: on\nmcast: 239.1.2.3:5000\n");
  ck_assert_int_eq(args_parse(ARGC(strict_argv), strict_argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
  cfg = (config_t){0};
  ck_assert_int_eq(args_parse(ARGC(lenient_argv), lenient_argv, &cfg), ARGS_OK);
  yamlcfg_strpool_free(cfg.str_pool);
  cfg = (config_t){0};
  ck_assert_int_eq(args_parse(ARGC(test_argv), test_argv, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(schema_is_required) {
  char *argv[] = {"dipibcg", "-l", "-m", "239.1.2.3:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(schema_option_is_repeatable_and_capped) {
  char *two[] = {"dipibcg", "-l", "-m", "239.1.2.3:5000", "-s", "a.xsd", "--schema", "dir", NULL};
  char *many[4 + 2 * (SCHEMA_MAX + 1) + 1] = {"dipibcg", "-l", "-m"};
  config_t cfg;
  int n = 3;
  ck_assert_int_eq(args_parse(ARGC(two), two, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.nschemas, 2);
  ck_assert_str_eq(cfg.schemas[0], "a.xsd");
  ck_assert_str_eq(cfg.schemas[1], "dir");
  many[n++] = "239.1.2.3:5000";
  for (int i = 0; i <= SCHEMA_MAX; i++) {
    many[n++] = "-s";
    many[n++] = "x";
  }
  many[n] = NULL;
  ck_assert_int_eq(args_parse(n, many, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(dvb_options_are_announce_only) {
  char *ann[] = {"dipibcg", "-a", "-i", "g.xml", "-M", "m.csv", "-m", "239.1.2.3:5000", "-s", "x", "--dvb-locator", "--dvb-duration", "--dvb-pubtime", NULL};
  char *lis[] = {"dipibcg", "-l", "-m", "239.1.2.3:5000", "-s", "x", "--dvb-duration", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(ann), ann, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.dvb_locator && cfg.dvb_duration && cfg.dvb_pubtime, 1);
  ck_assert_int_eq(args_parse(ARGC(lis), lis, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(format_and_encoding_options) {
  char *ann[] = {"dipibcg", "-s", "x.xsd", "-a", "-f", "tva", "-i", "g.xml", "-m", "239.1.2.3:5000", "--encoding", "plain", NULL};
  char *bim[] = {"dipibcg", "-s", "x.xsd", "-a", "-f", "bim", "-i", "g.bim", "-m", "239.1.2.3:5000", NULL};
  char *xml_no_map[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "g.xml", "-m", "239.1.2.3:5000", NULL};
  char *lis[] = {"dipibcg", "-s", "x.xsd", "-l", "-f", "bim", "-m", "239.1.2.3:5000", NULL};
  char *bad_f[] = {"dipibcg", "-s", "x.xsd", "-l", "-f", "json", "-m", "239.1.2.3:5000", NULL};
  char *bad_e[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "g.xml", "-M", "m.csv", "-m", "239.1.2.3:5000", "--encoding", "gzip", NULL};
  char *lis_enc[] = {"dipibcg", "-s", "x.xsd", "-l", "-m", "239.1.2.3:5000", "--encoding", "plain", NULL};
  char *idx_plain[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "g.xml", "-M", "m.csv", "-m", "239.1.2.3:5000", "--index", "pi-crid", "--encoding", "plain", NULL};
  char *idx_tva[] = {"dipibcg", "-s", "x.xsd", "-a", "-f", "tva", "-i", "g.xml", "-m", "239.1.2.3:5000", "--index", "pi-crid", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(ann), ann, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.format, FMT_TVA);
  ck_assert_int_eq(cfg.encoding, ENC_PLAIN);
  ck_assert_int_eq(args_parse(ARGC(bim), bim, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.format, FMT_BIM);
  ck_assert_int_eq(cfg.encoding, ENC_BIM);
  ck_assert_int_eq(args_parse(ARGC(xml_no_map), xml_no_map, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(lis), lis, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.format, FMT_BIM);
  ck_assert_int_eq(args_parse(ARGC(bad_f), bad_f, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(bad_e), bad_e, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(lis_enc), lis_enc, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(idx_plain), idx_plain, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(idx_tva), idx_tva, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(every_option_is_parsed_and_each_conflict_rejected) {
  char *all[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "g.xml", "-M", "m.csv", "-w", "12", "-m", "239.1.2.3:5000", "-I", "eth0", "--interval", "7", "--refresh", "3", "-o", "o.xml",
                 "-C", "c.csv", "-Z", "-f", "xmltv", "--encoding", "bim", "--dvb-locator", "--dvb-duration", "--dvb-pubtime", "-v", "-d", "--color", "never", "--metrics", "/tmp/m.sock",
                 "--metrics-id", "bcg1", "--metrics-interval", "9", "--dscp", "video-high", "-T", "32", NULL};
  char *lst[] = {"dipibcg", "-s", "x.xsd", "-l", "-m", "239.1.2.3:5000", "--timeout", "9", "-f", "tva", NULL};
  char *plain_t[] = {"dipibcg", "-s", "x.xsd", "-l", "-m", "239.1.2.3:5000", "-t", "9", NULL};
  char *zero_t[] = {"dipibcg", "-s", "x.xsd", "-l", "-m", "239.1.2.3:5000", "-t", "0", NULL};
  const char *const base_a[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "g.xml", "-M", "m.csv", "-m", "239.1.2.3:5000"};
  const char *const base_l[] = {"dipibcg", "-s", "x.xsd", "-l", "-m", "239.1.2.3:5000"};
  static const char *const a_extra[][4] = {
    {"-w", "0"}, {"-m", "bogus"}, {"-t", "x"}, {"--refresh", "0"}, {"-f", "json"}, {"--encoding", "gzip"}, {"--color", "rainbow"}, {"--metrics-interval", "0"}, {"--dscp", "bogus"},
    {"--timeout", "5"}, {"-t", "0"}, {"--index", "pi-crid", "-f", "tva"}, {"-T", "0"}, {"--ttl", "256"}, {"--index", "gi-crid"}, {"--index", "pi-crid,gi-title"}, {"--lang", "en"}, {"--metrics", "/tmp/s"}, {"--no-such-option"}, {"stray"},
  };
  static const char *const l_extra[][4] = {
    {"--metrics-id", "m"}, {"-Z"}, {"--refresh", "2"}, {"-T", "8"}, {"--encoding", "plain"}, {"--publisher", "a.b/x"}, {"--dvb-locator"}, {"--interval", "5"},
  };
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(all), all, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.window_hours, 12u);
  ck_assert_uint_eq(cfg.refresh_cycles, 3u);
  ck_assert_uint_eq(cfg.ttl, 32u);
  ck_assert_int_eq(cfg.compress, 1);
  ck_assert_int_eq(cfg.daemonize, 1);
  ck_assert_int_eq(cfg.dvb_pubtime, 1);
  ck_assert_int_eq(args_parse(ARGC(lst), lst, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.timeout_s, 9);
  ck_assert_int_eq(args_parse(ARGC(plain_t), plain_t, &cfg), ARGS_OK);
  ck_assert_int_eq(args_parse(ARGC(zero_t), zero_t, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.timeout_s, 0);
  ck_assert_uint_eq(cfg.ttl, 16u);
  for (size_t k = 0; k < sizeof a_extra / sizeof *a_extra; k++) {
    char *argv[20];
    int n = 0;
    for (size_t j = 0; j < sizeof base_a / sizeof *base_a; j++) argv[n++] = (char *)base_a[j];
    for (size_t j = 0; j < 4 && a_extra[k][j]; j++) argv[n++] = (char *)a_extra[k][j];
    argv[n] = NULL;
    ck_assert_msg(args_parse(n, argv, &cfg) == ARGS_ERR, "announce case %zu accepted", k);
  }
  for (size_t k = 0; k < sizeof l_extra / sizeof *l_extra; k++) {
    char *argv[20];
    int n = 0;
    for (size_t j = 0; j < sizeof base_l / sizeof *base_l; j++) argv[n++] = (char *)base_l[j];
    for (size_t j = 0; j < 4 && l_extra[k][j]; j++) argv[n++] = (char *)l_extra[k][j];
    argv[n] = NULL;
    ck_assert_msg(args_parse(n, argv, &cfg) == ARGS_ERR, "listen case %zu accepted", k);
  }
  {
    char *no_mode[] = {"dipibcg", "-s", "x.xsd", "-m", "239.1.2.3:5000", NULL};
    char *no_mcast[] = {"dipibcg", "-s", "x.xsd", "-l", NULL};
    char *no_schema[] = {"dipibcg", "-l", "-m", "239.1.2.3:5000", NULL};
    char *no_input[] = {"dipibcg", "-s", "x.xsd", "-a", "-m", "239.1.2.3:5000", NULL};
    char *no_map[] = {"dipibcg", "-s", "x.xsd", "-a", "-i", "g.xml", "-m", "239.1.2.3:5000", NULL};
    char *many[] = {"dipibcg", "-l", "-m", "239.1.2.3:5000", "-s", "1", "-s", "2", "-s", "3", "-s", "4", "-s", "5", "-s", "6", "-s", "7", "-s", "8", "-s", "9", "-s", "a", "-s", "b",
                    "-s", "c", "-s", "d", "-s", "e", "-s", "f", "-s", "g", "-s", "h", NULL};
    ck_assert_int_eq(args_parse(ARGC(no_mode), no_mode, &cfg), ARGS_ERR);
    ck_assert_int_eq(args_parse(ARGC(no_mcast), no_mcast, &cfg), ARGS_ERR);
    ck_assert_int_eq(args_parse(ARGC(no_schema), no_schema, &cfg), ARGS_ERR);
    ck_assert_int_eq(args_parse(ARGC(no_input), no_input, &cfg), ARGS_ERR);
    ck_assert_int_eq(args_parse(ARGC(no_map), no_map, &cfg), ARGS_ERR);
    ck_assert_int_eq(args_parse(ARGC(many), many, &cfg), ARGS_ERR);
  }
}
END_TEST

static Suite *args_suite(void) {
  Suite *s = suite_create("dipibcg_args");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, every_option_is_parsed_and_each_conflict_rejected);
  tcase_add_test(tc, announce_requires_input_and_map);
  tcase_add_test(tc, announce_publisher_lang);
  tcase_add_test(tc, announce_with_required_options_applies_defaults);
  tcase_add_test(tc, announce_window_and_interval_are_overridable);
  tcase_add_test(tc, announce_rejects_zero_window);
  tcase_add_test(tc, listen_defaults_output_to_stdout_marker);
  tcase_add_test(tc, listen_timeout_is_overridable);
  tcase_add_test(tc, listen_rejects_negative_timeout);
  tcase_add_test(tc, mode_is_required);
  tcase_add_test(tc, mode_is_exclusive);
  tcase_add_test(tc, mcast_is_required);
  tcase_add_test(tc, mcast_rejects_non_multicast_ipv4);
  tcase_add_test(tc, mcast_accepts_multicast_ipv6);
  tcase_add_test(tc, mcast_rejects_non_multicast_ipv6);
  tcase_add_test(tc, unexpected_positional_argument_is_rejected);
  tcase_add_test(tc, help_returns_help_status);
  tcase_add_test(tc, metrics_options_require_metrics_id);
  tcase_add_test(tc, metrics_id_is_announce_only);
  tcase_add_test(tc, metrics_id_enables_metrics_in_announce_mode);
  tcase_add_test(tc, metrics_interval_out_of_range_is_rejected);
  tcase_add_test(tc, announce_interval_zero_is_rejected);
  tcase_add_test(tc, refresh_defaults_to_one_and_is_overridable);
  tcase_add_test(tc, refresh_zero_is_rejected);
  tcase_add_test(tc, refresh_is_announce_only);
  tcase_add_test(tc, invalid_color_mode_is_rejected);
  tcase_add_test(tc, compress_flag_enables_in_announce_mode);
  tcase_add_test(tc, compress_flag_is_announce_only);
  tcase_add_test(tc, mcast_describe_formats_ipv4);
  tcase_add_test(tc, mcast_describe_formats_ipv6);
  tcase_add_test(tc, config_file_provides_settings);
  tcase_add_test(tc, cmdline_wins_over_config);
  tcase_add_test(tc, config_missing_file_is_error);
  tcase_add_test(tc, config_invalid_value_is_error);
  tcase_add_test(tc, config_both_modes_is_error);
  tcase_add_test(tc, configtest_reports_by_exit_status);
  tcase_add_test(tc, invalid_dscp_is_rejected);
  tcase_add_test(tc, listen_rejects_interval_long_option);
  tcase_add_test(tc, announce_rejects_timeout_long_option);
  tcase_add_test(tc, long_options_set_matching_mode_value);
  tcase_add_test(tc, announce_requires_map_when_input_is_given);
  tcase_add_test(tc, config_strict_option_rejects_unknown_keys);
  tcase_add_test(tc, schema_is_required);
  tcase_add_test(tc, schema_option_is_repeatable_and_capped);
  tcase_add_test(tc, dvb_options_are_announce_only);
  tcase_add_test(tc, format_and_encoding_options);
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
