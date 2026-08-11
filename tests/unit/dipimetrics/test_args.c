/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "lib/metrics/protocol.h"

#include "dipimetrics/args.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(no_args_applies_all_defaults) {
  char *argv[] = {"dipimetrics", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.sock_path, METRICS_DEFAULT_SOCK_PATH);
  ck_assert_int_eq(cfg.family, AF_INET);
  ck_assert_str_eq(cfg.listen_addr, "127.0.0.1");
  ck_assert_uint_eq(cfg.listen_port, 9109u);
  ck_assert_int_eq(cfg.expiry_s, 30);
}
END_TEST

START_TEST(sock_path_is_overridable) {
  char *argv[] = {"dipimetrics", "-S", "/tmp/custom.sock", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.sock_path, "/tmp/custom.sock");
}
END_TEST

START_TEST(listen_addr_is_overridable) {
  char *argv[] = {"dipimetrics", "-l", "0.0.0.0:8080", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.listen_addr, "0.0.0.0");
  ck_assert_uint_eq(cfg.listen_port, 8080u);
}
END_TEST

START_TEST(listen_rejects_malformed_addr) {
  char *argv[] = {"dipimetrics", "-l", "not-an-addr", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(expiry_is_overridable) {
  char *argv[] = {"dipimetrics", "-e", "120", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.expiry_s, 120);
}
END_TEST

START_TEST(expiry_rejects_zero_and_negative) {
  char *argv[] = {"dipimetrics", "-e", "0", NULL};
  char *argv2[] = {"dipimetrics", "-e", "-5", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(argv2), argv2, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(invalid_color_mode_is_rejected) {
  char *argv[] = {"dipimetrics", "--color", "sometimes", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(unexpected_positional_argument_is_rejected) {
  char *argv[] = {"dipimetrics", "extra", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(help_returns_help_status) {
  char *argv[] = {"dipimetrics", "-h", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
}
END_TEST

static Suite *args_suite(void) {
  Suite *s = suite_create("dipimetrics_args");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, no_args_applies_all_defaults);
  tcase_add_test(tc, sock_path_is_overridable);
  tcase_add_test(tc, listen_addr_is_overridable);
  tcase_add_test(tc, listen_rejects_malformed_addr);
  tcase_add_test(tc, expiry_is_overridable);
  tcase_add_test(tc, expiry_rejects_zero_and_negative);
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
