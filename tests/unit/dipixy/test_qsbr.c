/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "dipixy/reactor/qsbr.h"

#define WORKERS 4
#define TICKS 2000

START_TEST(creation_clamps_the_worker_count_to_at_least_one) {
  static const int requested[] = {1, 0, -5, 7, QSBR_MAX_WORKERS};
  static const int expected[] = {1, 1, 1, 7, QSBR_MAX_WORKERS};
  qsbr_domain_t *d = qsbr_domain_create(requested[_i]);

  ck_assert_ptr_nonnull(d);
  ck_assert_int_eq(qsbr_worker_count(d), expected[_i]);
  qsbr_domain_destroy(d);
}
END_TEST

START_TEST(destroying_nothing_is_harmless) {
  qsbr_domain_destroy(NULL);
}
END_TEST

START_TEST(null_domain_is_inert_and_always_passed) {
  uint64_t mark[WORKERS] = {1, 2, 3, 4};

  ck_assert_int_eq(qsbr_worker_count(NULL), 0);
  qsbr_worker_quiescent(NULL, 0);
  qsbr_mark(NULL, mark);
  ck_assert_uint_eq(mark[0], 1u);
  ck_assert_int_eq(qsbr_mark_passed(NULL, mark), 1);
  ck_assert_int_eq(qsbr_mark_passed_excl(NULL, mark, 0), 1);
}
END_TEST

START_TEST(grace_period_needs_every_worker_to_pass_a_quiescent_point) {
  qsbr_domain_t *d = qsbr_domain_create(WORKERS);
  uint64_t mark[WORKERS];

  qsbr_mark(d, mark);
  ck_assert_int_eq(qsbr_mark_passed(d, mark), 0);
  for (int tid = 0; tid < WORKERS - 1; tid++) {
    qsbr_worker_quiescent(d, tid);
    ck_assert_int_eq(qsbr_mark_passed(d, mark), 0);
  }
  qsbr_worker_quiescent(d, WORKERS - 1);
  ck_assert_int_eq(qsbr_mark_passed(d, mark), 1);
  qsbr_domain_destroy(d);
}
END_TEST

START_TEST(a_mark_taken_later_is_not_satisfied_by_older_ticks) {
  qsbr_domain_t *d = qsbr_domain_create(2);
  uint64_t first[2];
  uint64_t second[2];

  qsbr_mark(d, first);
  qsbr_worker_quiescent(d, 0);
  qsbr_worker_quiescent(d, 1);
  ck_assert_int_eq(qsbr_mark_passed(d, first), 1);
  qsbr_mark(d, second);
  ck_assert_int_eq(qsbr_mark_passed(d, second), 0);
  ck_assert_int_eq(qsbr_mark_passed(d, first), 1);
  qsbr_domain_destroy(d);
}
END_TEST

START_TEST(exclusion_skips_the_calling_worker_only) {
  qsbr_domain_t *d = qsbr_domain_create(3);
  uint64_t mark[3];

  qsbr_mark(d, mark);
  qsbr_worker_quiescent(d, 1);
  qsbr_worker_quiescent(d, 2);
  ck_assert_int_eq(qsbr_mark_passed(d, mark), 0);
  ck_assert_int_eq(qsbr_mark_passed_excl(d, mark, 0), 1);
  ck_assert_int_eq(qsbr_mark_passed_excl(d, mark, 1), 0);
  ck_assert_int_eq(qsbr_mark_passed_excl(d, mark, -1), 0);
  ck_assert_int_eq(qsbr_mark_passed_excl(d, mark, 99), 0);
  qsbr_domain_destroy(d);
}
END_TEST

START_TEST(out_of_range_workers_are_ignored) {
  qsbr_domain_t *d = qsbr_domain_create(2);
  uint64_t mark[2];

  qsbr_mark(d, mark);
  qsbr_worker_quiescent(d, -1);
  qsbr_worker_quiescent(d, 2);
  qsbr_worker_quiescent(d, 1000);
  ck_assert_int_eq(qsbr_mark_passed(d, mark), 0);
  qsbr_domain_destroy(d);
}
END_TEST

START_TEST(single_worker_domain_passes_after_one_tick) {
  qsbr_domain_t *d = qsbr_domain_create(1);
  uint64_t mark[1];

  qsbr_mark(d, mark);
  ck_assert_int_eq(qsbr_mark_passed(d, mark), 0);
  qsbr_worker_quiescent(d, 0);
  ck_assert_int_eq(qsbr_mark_passed(d, mark), 1);
  qsbr_domain_destroy(d);
}
END_TEST

START_TEST(backoff_returns_at_every_spin_level) {
  struct timespec a;
  struct timespec b;
  long elapsed_ms;

  qsbr_backoff(0);
  qsbr_backoff(99);
  clock_gettime(CLOCK_MONOTONIC, &a);
  qsbr_backoff(100);
  qsbr_backoff(999);
  qsbr_backoff(1000);
  clock_gettime(CLOCK_MONOTONIC, &b);
  elapsed_ms = (b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000;
  ck_assert_int_lt((int)elapsed_ms, 1000);
}
END_TEST

typedef struct {
  qsbr_domain_t *d;
  int tid;
} ticker_t;

static void *ticker(void *arg) {
  ticker_t *t = arg;

  for (int i = 0; i < TICKS; i++) qsbr_worker_quiescent(t->d, t->tid);
  return NULL;
}

START_TEST(concurrent_workers_eventually_satisfy_a_pending_mark) {
  qsbr_domain_t *d = qsbr_domain_create(WORKERS);
  pthread_t th[WORKERS];
  ticker_t args[WORKERS];
  uint64_t mark[WORKERS];
  int spins = 0;

  qsbr_mark(d, mark);
  for (int i = 0; i < WORKERS; i++) {
    args[i].d = d;
    args[i].tid = i;
    ck_assert_int_eq(pthread_create(&th[i], NULL, ticker, &args[i]), 0);
  }
  for (int i = 0; i < WORKERS; i++) pthread_join(th[i], NULL);
  while (!qsbr_mark_passed(d, mark) && spins < 1000) qsbr_backoff(spins++);
  ck_assert_int_eq(qsbr_mark_passed(d, mark), 1);
  qsbr_domain_destroy(d);
}
END_TEST

static Suite *qsbr_suite(void) {
  Suite *s = suite_create("dipixy_qsbr");
  TCase *tc = tcase_create("core");
  tcase_add_loop_test(tc, creation_clamps_the_worker_count_to_at_least_one, 0, 5);
  tcase_add_test(tc, destroying_nothing_is_harmless);
  tcase_add_test(tc, null_domain_is_inert_and_always_passed);
  tcase_add_test(tc, grace_period_needs_every_worker_to_pass_a_quiescent_point);
  tcase_add_test(tc, a_mark_taken_later_is_not_satisfied_by_older_ticks);
  tcase_add_test(tc, exclusion_skips_the_calling_worker_only);
  tcase_add_test(tc, out_of_range_workers_are_ignored);
  tcase_add_test(tc, single_worker_domain_passes_after_one_tick);
  tcase_add_test(tc, backoff_returns_at_every_spin_level);
  tcase_add_test(tc, concurrent_workers_eventually_satisfy_a_pending_mark);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(qsbr_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
