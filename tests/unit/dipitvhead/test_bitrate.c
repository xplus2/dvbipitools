/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <limits.h>
#include <math.h>
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
  ck_assert_int_ge(bitrate_stuff_due(p, 0), 0);
  bitrate_pacer_free(p);
}
END_TEST

START_TEST(bitrate_disabled_when_target_bps_zero) {
  bitrate_pacer_t *p = bitrate_pacer_new(0, 1, 1);
  double t0 = mono();
  bitrate_pace(p); /* must return immediately, no sleep */
  ck_assert(mono() - t0 < 0.05);
  ck_assert_int_eq(bitrate_stuff_due(p, 0), 0);
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
  ck_assert_int_eq(bitrate_stuff_due(p, 0), 0);
  bitrate_pacer_free(p);
}
END_TEST

START_TEST(bitrate_stuff_due_reports_deficit_after_falling_behind) {
  /* very high target, nothing accounted: after a short real sleep we're
     unambiguously behind schedule by many packets' worth of bits */
  bitrate_pacer_t *p = bitrate_pacer_new(1000000000, 1, 0);
  struct timespec ts = {0, 20000000}; /* 20ms */
  nanosleep(&ts, NULL);
  ck_assert_int_gt(bitrate_stuff_due(p, 0), 0);
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

START_TEST(bitrate_pacer_shares_budget_regardless_of_source_order) {
  /* one pacer instance, fed packets "from" two different programs in different interleavings -
     only the total count should matter, never which program or in what order */
  bitrate_pacer_t *a = bitrate_pacer_new(100000, 1, 1);
  bitrate_pacer_t *b = bitrate_pacer_new(100000, 1, 1);
  int i;

  for (i = 0; i < 3; i++)
    bitrate_account(a); /* simulates program 1's packets */
  for (i = 0; i < 3; i++)
    bitrate_account(a); /* simulates program 2's packets, same pacer */

  for (i = 0; i < 3; i++) {
    bitrate_account(b); /* interleaved: 1, 2, 1, 2, 1, 2 */
    bitrate_account(b);
  }

  ck_assert_int_eq(bitrate_stuff_due(a, 0), bitrate_stuff_due(b, 0));

  bitrate_pacer_free(a);
  bitrate_pacer_free(b);
}
END_TEST

START_TEST(bitrate_pacer_logs_sustained_overage_without_crashing) {
  /* far more content accounted than the target allows: exercises the overage-warning path
     (not asserted on directly, log_line has no test-capturable sink - just must not crash and
     must not affect stuff_due()'s own correctness) */
  bitrate_pacer_t *p = bitrate_pacer_new(1000, 1, 1); /* 1 kbps: trivial to exceed */
  int i;

  for (i = 0; i < 50; i++)
    bitrate_account(p);
  ck_assert_int_eq(bitrate_stuff_due(p, 0), 0); /* far ahead, never behind - no stuffing due */

  bitrate_pacer_free(p);
}
END_TEST

START_TEST(bitrate_account_n_matches_n_single_calls) {
  bitrate_pacer_t *a = bitrate_pacer_new(100000, 1, 1);
  bitrate_pacer_t *b = bitrate_pacer_new(100000, 1, 1);
  int i;

  for (i = 0; i < 6; i++)
    bitrate_account(a);
  bitrate_account_n(b, 6);

  ck_assert_int_eq(bitrate_stuff_due(a, 0), bitrate_stuff_due(b, 0));

  bitrate_pacer_free(a);
  bitrate_pacer_free(b);
}
END_TEST

START_TEST(bitrate_pace_and_account_n_tolerate_null_pacer) {
  bitrate_pace(NULL);
  bitrate_account_n(NULL, 7);
}
END_TEST

static void wait_ms(long ms) {
  struct timespec ts = {0, ms * 1000000L};
  nanosleep(&ts, NULL);
}

START_TEST(bitrate_stuff_due_clamps_for_very_large_targets) {
  static const double targets[] = {1e12, 1e15, 1e18, 1e300, INFINITY};
  bitrate_pacer_t *p = bitrate_pacer_new(targets[_i], 1, 0);
  int due;

  wait_ms(20);
  due = bitrate_stuff_due(p, 0);
  ck_assert_int_gt(due, 0);
  if (targets[_i] >= 1e15) ck_assert_int_eq(due, INT_MAX);
  bitrate_pacer_free(p);
}
END_TEST

START_TEST(bitrate_stuff_due_survives_nan_target) {
  bitrate_pacer_t *p = bitrate_pacer_new(NAN, 1, 1);

  wait_ms(5);
  ck_assert_int_eq(bitrate_stuff_due(p, 0), 0);
  bitrate_pace(p);
  bitrate_account_n(p, 5);
  bitrate_pacer_free(p);
}
END_TEST

START_TEST(bitrate_account_n_accepts_the_largest_batch) {
  bitrate_pacer_t *p = bitrate_pacer_new(1000000.0, 1, 0);

  bitrate_account_n(p, UINT_MAX);
  ck_assert_int_eq(bitrate_stuff_due(p, 0), 0);
  bitrate_pacer_free(p);
}
END_TEST

static const unsigned burst_sizes[] = {2, 3, 1, 4};

static double delay_after_accounting(const unsigned *order, int single_calls) {
  bitrate_pacer_t *p = bitrate_pacer_new(100000, 1, 1);
  double t0;
  double delay;

  for (size_t i = 0; i < 4; i++) {
    unsigned n = burst_sizes[order[i]];

    if (single_calls)
      for (unsigned k = 0; k < n; k++) bitrate_account(p);
    else
      bitrate_account_n(p, n);
  }
  t0 = mono();
  bitrate_pace(p);
  delay = mono() - t0;
  bitrate_pacer_free(p);
  return delay;
}

START_TEST(bitrate_pacer_shares_one_budget_across_four_sources) {
  static const unsigned order_a[] = {0, 1, 2, 3};
  static const unsigned order_b[] = {3, 1, 0, 2};
  double delay_a = delay_after_accounting(order_a, 0);
  double delay_b = delay_after_accounting(order_b, 1);

  ck_assert_msg(delay_a > 0.05 && delay_a < 1.0, "batched order a: delay %.3f", delay_a);
  ck_assert_msg(delay_b > 0.05 && delay_b < 1.0, "single calls order b: delay %.3f", delay_b);
}
END_TEST

START_TEST(bitrate_stuff_due_discounts_pending_packets) {
  bitrate_pacer_t *p = bitrate_pacer_new(1000000000, 1, 0);
  struct timespec ts = {0, 20000000};
  nanosleep(&ts, NULL);
  ck_assert_int_gt(bitrate_stuff_due(p, 0), 0);
  ck_assert_int_eq(bitrate_stuff_due(p, 100000), 0);
  bitrate_pacer_free(p);
}
END_TEST

START_TEST(bitrate_stuff_due_tolerates_null_pacer) {
  ck_assert_int_eq(bitrate_stuff_due(NULL, 7), 0);
}
END_TEST

START_TEST(bitrate_pacer_start_discards_the_time_before_output_began) {
  bitrate_pacer_t *p = bitrate_pacer_new(1000000000, 1, 0);
  struct timespec ts = {0, 20000000};
  int before;
  nanosleep(&ts, NULL);
  before = bitrate_stuff_due(p, 0);
  ck_assert_int_gt(before, 0);
  bitrate_pacer_start(p);
  ck_assert_int_lt(bitrate_stuff_due(p, 0), before);
  nanosleep(&ts, NULL);
  ck_assert_int_gt(bitrate_stuff_due(p, 0), 0);
  bitrate_pacer_start(NULL);
  bitrate_pacer_free(p);
}
END_TEST

static Suite *bitrate_suite(void) {
  Suite *s = suite_create("bitrate");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, bitrate_pacer_lifecycle_does_not_crash);
  tcase_add_loop_test(tc, bitrate_stuff_due_clamps_for_very_large_targets, 0, 5);
  tcase_add_test(tc, bitrate_stuff_due_survives_nan_target);
  tcase_add_test(tc, bitrate_account_n_accepts_the_largest_batch);
  tcase_add_test(tc, bitrate_pacer_shares_one_budget_across_four_sources);
  tcase_add_test(tc, bitrate_disabled_when_target_bps_zero);
  tcase_add_test(tc, bitrate_pace_disabled_when_burst_limit_zero);
  tcase_add_test(tc, bitrate_stuff_due_disabled_when_stuff_zero);
  tcase_add_test(tc, bitrate_stuff_due_reports_deficit_after_falling_behind);
  tcase_add_test(tc, bitrate_pace_sleeps_when_far_ahead_of_schedule);
  tcase_add_test(tc, bitrate_pacer_shares_budget_regardless_of_source_order);
  tcase_add_test(tc, bitrate_pacer_logs_sustained_overage_without_crashing);
  tcase_add_test(tc, bitrate_account_n_matches_n_single_calls);
  tcase_add_test(tc, bitrate_pace_and_account_n_tolerate_null_pacer);
  tcase_add_test(tc, bitrate_stuff_due_discounts_pending_packets);
  tcase_add_test(tc, bitrate_stuff_due_tolerates_null_pacer);
  tcase_add_test(tc, bitrate_pacer_start_discards_the_time_before_output_began);
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
