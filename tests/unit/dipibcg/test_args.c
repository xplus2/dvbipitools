/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "dipibcg/args.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(announce_requires_input_and_map) {
  char *argv[] = {"dipibcg", "-a", "-m", "239.1.2.3:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(announce_with_required_options_applies_defaults) {
  char *argv[] = {"dipibcg", "-a", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.mode, MODE_ANNOUNCE);
  ck_assert_str_eq(cfg.input_path, "guide.xml");
  ck_assert_str_eq(cfg.map_path, "map.csv");
  ck_assert_int_eq(cfg.window_hours, 24);
  ck_assert_int_eq(cfg.interval_s, 5);
}
END_TEST

START_TEST(announce_window_and_interval_are_overridable) {
  char *argv[] = {"dipibcg", "-a", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000",
                  "-w", "48", "-t", "10", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.window_hours, 48);
  ck_assert_int_eq(cfg.interval_s, 10);
}
END_TEST

START_TEST(announce_rejects_zero_window) {
  char *argv[] = {"dipibcg", "-a", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000",
                  "-w", "0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(listen_defaults_output_to_stdout_marker) {
  char *argv[] = {"dipibcg", "-l", "-m", "239.1.2.3:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.mode, MODE_LISTEN);
  ck_assert_str_eq(cfg.output_path, "-");
  ck_assert_int_eq(cfg.timeout_s, 35);
}
END_TEST

START_TEST(listen_timeout_is_overridable) {
  char *argv[] = {"dipibcg", "-l", "-m", "239.1.2.3:5000", "-t", "60", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.timeout_s, 60);
}
END_TEST

START_TEST(listen_rejects_negative_timeout) {
  char *argv[] = {"dipibcg", "-l", "-m", "239.1.2.3:5000", "-t", "-1", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(mode_is_required) {
  char *argv[] = {"dipibcg", "-m", "239.1.2.3:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(mode_is_exclusive) {
  char *argv[] = {"dipibcg", "-a", "-l", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(mcast_is_required) {
  char *argv[] = {"dipibcg", "-l", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(mcast_rejects_non_multicast_ipv4) {
  char *argv[] = {"dipibcg", "-l", "-m", "10.0.0.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(mcast_accepts_multicast_ipv6) {
  char *argv[] = {"dipibcg", "-l", "-m", "[ff15::1]:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
}
END_TEST

START_TEST(mcast_rejects_non_multicast_ipv6) {
  char *argv[] = {"dipibcg", "-l", "-m", "[::1]:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(unexpected_positional_argument_is_rejected) {
  char *argv[] = {"dipibcg", "-l", "-m", "239.1.2.3:5000", "extra", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(help_returns_help_status) {
  char *argv[] = {"dipibcg", "-h", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
}
END_TEST

START_TEST(metrics_options_require_metrics_id) {
  char *argv[] = {"dipibcg", "-a", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000",
                  "--metrics", "/tmp/x.sock", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(metrics_id_is_announce_only) {
  char *argv[] = {"dipibcg", "-l", "-m", "239.1.2.3:5000", "--metrics-id", "inst1", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(metrics_id_enables_metrics_in_announce_mode) {
  char *argv[] = {"dipibcg", "-a", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000",
                  "--metrics-id", "inst1", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.metrics_id, "inst1");
}
END_TEST

START_TEST(metrics_interval_out_of_range_is_rejected) {
  char *argv[] = {"dipibcg", "-a", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000",
                  "--metrics-id", "inst1", "--metrics-interval", "0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(invalid_color_mode_is_rejected) {
  char *argv[] = {"dipibcg", "-l", "-m", "239.1.2.3:5000", "--color", "sometimes", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(compress_flag_enables_in_announce_mode) {
  char *argv[] = {"dipibcg", "-a", "-i", "guide.xml", "-M", "map.csv", "-m", "239.1.2.3:5000",
                  "-Z", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.compress, 1);
}
END_TEST

START_TEST(compress_flag_is_announce_only) {
  char *argv[] = {"dipibcg", "-l", "-m", "239.1.2.3:5000", "--compress", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(mcast_describe_formats_ipv4) {
  config_t cfg;
  char buf[64];
  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET;
  strcpy(cfg.mcast_group, "239.1.2.3");
  cfg.mcast_port = 5000;
  mcast_describe(&cfg, buf, sizeof buf);
  ck_assert_str_eq(buf, "239.1.2.3:5000");
}
END_TEST

START_TEST(mcast_describe_formats_ipv6) {
  config_t cfg;
  char buf[64];
  memset(&cfg, 0, sizeof cfg);
  cfg.family = AF_INET6;
  strcpy(cfg.mcast_group, "ff15::1");
  cfg.mcast_port = 5000;
  mcast_describe(&cfg, buf, sizeof buf);
  ck_assert_str_eq(buf, "[ff15::1]:5000");
}
END_TEST

static Suite *args_suite(void) {
  Suite *s = suite_create("dipibcg_args");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, announce_requires_input_and_map);
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
  tcase_add_test(tc, invalid_color_mode_is_rejected);
  tcase_add_test(tc, compress_flag_enables_in_announce_mode);
  tcase_add_test(tc, compress_flag_is_announce_only);
  tcase_add_test(tc, mcast_describe_formats_ipv4);
  tcase_add_test(tc, mcast_describe_formats_ipv6);
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
