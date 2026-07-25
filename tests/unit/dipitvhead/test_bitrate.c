/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <time.h>

#include "dipitvhead/mux/bitrate.h"

static double mono(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

START_TEST(bitrate_pacer_lifecycle_does_not_crash) {
  bitrate_pacer_t *p = bitrate_pacer_new(1000000, 1, 1);
  ck_assert_ptr_nonnull(p);
  bitrate_account(p);
  bitrate_pace(p);
  ck_assert_int_ge(bitrate_stuff_due(p), 0);
  bitrate_pacer_free(p);
}
END_TEST

START_TEST(bitrate_disabled_when_target_bps_zero) {
  bitrate_pacer_t *p = bitrate_pacer_new(0, 1, 1);
  double t0 = mono();
  bitrate_pace(p); /* must return immediately, no sleep */
  ck_assert(mono() - t0 < 0.05);
  ck_assert_int_eq(bitrate_stuff_due(p), 0);
  bitrate_pacer_free(p);
}
END_TEST

START_TEST(bitrate_pace_disabled_when_burst_limit_zero) {
  bitrate_pacer_t *p = bitrate_pacer_new(100, 0, 0); /* burst_limit=0: no pacing regardless of target_bps */
  double t0;
  int i;
  for (i = 0; i < 10; i++)
    bitrate_account(p); /* far ahead of a 100 bps target */
  t0 = mono();
  bitrate_pace(p);
  ck_assert(mono() - t0 < 0.05); /* must not sleep */
  bitrate_pacer_free(p);
}
END_TEST

START_TEST(bitrate_stuff_due_disabled_when_stuff_zero) {
  bitrate_pacer_t *p = bitrate_pacer_new(1000000000, 0, 0); /* stuff=0: never reports stuffing */
  struct timespec ts = {0, 20000000}; /* 20ms, enough for a huge deficit at 1 Gbps */
  nanosleep(&ts, NULL);
  ck_assert_int_eq(bitrate_stuff_due(p), 0);
  bitrate_pacer_free(p);
}
END_TEST

START_TEST(bitrate_stuff_due_reports_deficit_after_falling_behind) {
  /* very high target, nothing accounted: after a short real sleep we're
     unambiguously behind schedule by many packets' worth of bits */
  bitrate_pacer_t *p = bitrate_pacer_new(1000000000, 1, 0);
  struct timespec ts = {0, 20000000}; /* 20ms */
  nanosleep(&ts, NULL);
  ck_assert_int_gt(bitrate_stuff_due(p), 0);
  bitrate_pacer_free(p);
}
END_TEST

START_TEST(bitrate_pace_sleeps_when_far_ahead_of_schedule) {
  /* low target, several packets accounted almost instantly: pace() must
     measurably delay to let the clock catch up */
  bitrate_pacer_t *p = bitrate_pacer_new(100000, 1, 1); /* 100 kbps */
  double t0;
  int i;
  for (i = 0; i < 3; i++)
    bitrate_account(p); /* ~4512 bits sent "instantly" -> ~45ms owed at 100kbps */
  t0 = mono();
  bitrate_pace(p);
  ck_assert(mono() - t0 > 0.015); /* generous lower bound well under the ~45ms nominal sleep */
  bitrate_pacer_free(p);
}
END_TEST

static Suite *bitrate_suite(void) {
  Suite *s = suite_create("bitrate");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, bitrate_pacer_lifecycle_does_not_crash);
  tcase_add_test(tc, bitrate_disabled_when_target_bps_zero);
  tcase_add_test(tc, bitrate_pace_disabled_when_burst_limit_zero);
  tcase_add_test(tc, bitrate_stuff_due_disabled_when_stuff_zero);
  tcase_add_test(tc, bitrate_stuff_due_reports_deficit_after_falling_behind);
  tcase_add_test(tc, bitrate_pace_sleeps_when_far_ahead_of_schedule);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(bitrate_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
