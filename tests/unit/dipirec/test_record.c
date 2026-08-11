/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/signal.h"
#include "dipirec/record.h"

START_TEST(fmt_dur_under_an_hour_omits_hours) {
  char buf[16];
  fmt_dur(65.0, buf, sizeof buf); /* 1:05 */
  ck_assert_str_eq(buf, " 1:05");
}
END_TEST

START_TEST(fmt_dur_with_hours_includes_them) {
  char buf[16];
  fmt_dur(3725.0, buf, sizeof buf); /* 1:02:05 */
  ck_assert_str_eq(buf, " 1:02:05");
}
END_TEST

START_TEST(fmt_dur_zero_and_negative_clamp_to_zero) {
  char buf[16];
  fmt_dur(0.0, buf, sizeof buf);
  ck_assert_str_eq(buf, " 0:00");
  fmt_dur(-5.0, buf, sizeof buf);
  ck_assert_str_eq(buf, " 0:00");
}
END_TEST

START_TEST(fmt_dur_caps_at_99_59_59) {
  char buf[16];
  fmt_dur(1000000.0, buf, sizeof buf);
  ck_assert_str_eq(buf, "99:59:59");
}
END_TEST

START_TEST(stop_now_false_before_duration_elapses) {
  config_t cfg;
  double start = mono_seconds();
  memset(&cfg, 0, sizeof cfg);
  cfg.duration_s = 3600; /* an hour: nowhere near elapsed yet */
  ck_assert_int_eq(stop_now(&cfg, start), 0);
}
END_TEST

START_TEST(stop_now_true_once_duration_elapses) {
  config_t cfg;
  double start;
  struct timespec wait = {0, 20 * 1000 * 1000}; /* 20ms */
  memset(&cfg, 0, sizeof cfg);
  cfg.duration_s = 1; /* smallest representable unit; 0 means "run forever" */
  start = mono_seconds() - 2.0; /* pretend we started 2 real seconds ago */
  nanosleep(&wait, NULL);
  ck_assert_int_eq(stop_now(&cfg, start), 1);
}
END_TEST

START_TEST(stop_now_false_forever_when_duration_is_zero) {
  config_t cfg;
  double start = mono_seconds() - 1000000.0; /* absurdly long ago */
  memset(&cfg, 0, sizeof cfg);
  cfg.duration_s = 0; /* until stopped */
  ck_assert_int_eq(stop_now(&cfg, start), 0);
}
END_TEST

START_TEST(stop_now_true_on_stop_signal_regardless_of_duration) {
  config_t cfg;
  double start = mono_seconds();
  memset(&cfg, 0, sizeof cfg);
  cfg.duration_s = 3600;
  signals_install();
  raise(SIGTERM); /* Check forks each test, so this only affects this test's process */
  ck_assert_int_eq(stop_now(&cfg, start), 1);
}
END_TEST

static Suite *record_suite(void) {
  Suite *s = suite_create("dipirec_record");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, fmt_dur_under_an_hour_omits_hours);
  tcase_add_test(tc, fmt_dur_with_hours_includes_them);
  tcase_add_test(tc, fmt_dur_zero_and_negative_clamp_to_zero);
  tcase_add_test(tc, fmt_dur_caps_at_99_59_59);
  tcase_add_test(tc, stop_now_false_before_duration_elapses);
  tcase_add_test(tc, stop_now_true_once_duration_elapses);
  tcase_add_test(tc, stop_now_false_forever_when_duration_is_zero);
  tcase_add_test(tc, stop_now_true_on_stop_signal_regardless_of_duration);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(record_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
