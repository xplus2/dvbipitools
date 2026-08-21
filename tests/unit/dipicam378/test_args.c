/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipicam378/args.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(key_is_required) {
  char *argv[] = {"dipicam378", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(key_alone_applies_defaults) {
  char *argv[] = {"dipicam378", "-k", "device.key", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.key_path, "device.key");
  ck_assert_uint_eq(cfg.port, 27500u);
  ck_assert_str_eq(cfg.password, "dipicam378");
  ck_assert_ptr_null(cfg.username);
  ck_assert_int_eq(cfg.cw_len, 16);
  ck_assert_uint_eq(cfg.caid, 0u);
}
END_TEST

START_TEST(port_is_overridable) {
  char *argv[] = {"dipicam378", "-k", "device.key", "-p", "9999", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.port, 9999u);
}
END_TEST

START_TEST(invalid_port_is_rejected) {
  char *argv[] = {"dipicam378", "-k", "device.key", "-p", "not-a-port", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(auth_without_colon_sets_password_only) {
  char *argv[] = {"dipicam378", "-k", "device.key", "-a", "secret", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_ptr_null(cfg.username);
  ck_assert_str_eq(cfg.password, "secret");
}
END_TEST

START_TEST(auth_with_colon_splits_user_and_password) {
  char auth[] = "alice:secret"; /* args_parse writes a NUL into this in place, needs to be writable */
  char *argv[] = {"dipicam378", "-k", "device.key", "-a", auth, NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.username, "alice");
  ck_assert_str_eq(cfg.password, "secret");
}
END_TEST

START_TEST(caid_parses_hex) {
  char *argv[] = {"dipicam378", "-k", "device.key", "--caid", "2602", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.caid, 0x2602u);
}
END_TEST

START_TEST(caid_rejects_zero) {
  char *argv[] = {"dipicam378", "-k", "device.key", "--caid", "0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(caid_rejects_out_of_range) {
  char *argv[] = {"dipicam378", "-k", "device.key", "--caid", "10000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(algo_csa2_sets_8_byte_cw) {
  char *argv[] = {"dipicam378", "-k", "device.key", "--algo", "csa2", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.cw_len, 8);
}
END_TEST

START_TEST(algo_cissa_sets_16_byte_cw) {
  char *argv[] = {"dipicam378", "-k", "device.key", "--algo", "cissa", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.cw_len, 16);
}
END_TEST

START_TEST(algo_rejects_unknown_value) {
  char *argv[] = {"dipicam378", "-k", "device.key", "--algo", "aes", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(invalid_color_mode_is_rejected) {
  char *argv[] = {"dipicam378", "-k", "device.key", "--color", "sometimes", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(unexpected_positional_argument_is_rejected) {
  char *argv[] = {"dipicam378", "-k", "device.key", "extra", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(help_returns_help_status) {
  char *argv[] = {"dipicam378", "-h", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
}
END_TEST

START_TEST(serial_and_verbose_are_recorded) {
  char *argv[] = {"dipicam378", "-k", "device.key", "-s", "e2e-01", "-v", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.serial, "e2e-01");
  ck_assert_int_eq(cfg.verbose, 1);
}
END_TEST

START_TEST(metrics_options_require_metrics_id) {
  char *argv[] = {"dipicam378", "-k", "device.key", "--metrics", "/tmp/x.sock", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(metrics_id_alone_is_accepted) {
  char *argv[] = {"dipicam378", "-k", "device.key", "--metrics-id", "inst1", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.metrics_id, "inst1");
}
END_TEST

static Suite *args_suite(void) {
  Suite *s = suite_create("dipicam378_args");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, key_is_required);
  tcase_add_test(tc, key_alone_applies_defaults);
  tcase_add_test(tc, port_is_overridable);
  tcase_add_test(tc, invalid_port_is_rejected);
  tcase_add_test(tc, auth_without_colon_sets_password_only);
  tcase_add_test(tc, auth_with_colon_splits_user_and_password);
  tcase_add_test(tc, caid_parses_hex);
  tcase_add_test(tc, caid_rejects_zero);
  tcase_add_test(tc, caid_rejects_out_of_range);
  tcase_add_test(tc, algo_csa2_sets_8_byte_cw);
  tcase_add_test(tc, algo_cissa_sets_16_byte_cw);
  tcase_add_test(tc, algo_rejects_unknown_value);
  tcase_add_test(tc, invalid_color_mode_is_rejected);
  tcase_add_test(tc, unexpected_positional_argument_is_rejected);
  tcase_add_test(tc, help_returns_help_status);
  tcase_add_test(tc, serial_and_verbose_are_recorded);
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
