/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>

#include "lib/helper/idxstack.h"

START_TEST(lifo_order_and_empty) {
  idxstack_t st;
  ck_assert_int_eq(idxstack_init(&st, 4), 0);
  ck_assert_int_eq(idxstack_pop(&st), -1);
  idxstack_push(&st, 0);
  idxstack_push(&st, 3);
  idxstack_push(&st, 1);
  ck_assert_int_eq(idxstack_pop(&st), 1);
  ck_assert_int_eq(idxstack_pop(&st), 3);
  idxstack_push(&st, 2);
  ck_assert_int_eq(idxstack_pop(&st), 2);
  ck_assert_int_eq(idxstack_pop(&st), 0);
  ck_assert_int_eq(idxstack_pop(&st), -1);
  idxstack_free(&st);
}
END_TEST

START_TEST(out_of_range_push_ignored) {
  idxstack_t st;
  ck_assert_int_eq(idxstack_init(&st, 2), 0);
  idxstack_push(&st, -1);
  idxstack_push(&st, 2);
  ck_assert_int_eq(idxstack_pop(&st), -1);
  idxstack_free(&st);
}
END_TEST

START_TEST(init_rejects_bad_cap) {
  idxstack_t st;
  ck_assert_int_eq(idxstack_init(&st, 0), -1);
  ck_assert_int_eq(idxstack_init(&st, -5), -1);
  ck_assert_int_eq(idxstack_pop(&st), -1);
  idxstack_free(&st);
}
END_TEST

#define CONC_CAP 64
#define CONC_THREADS 8
#define CONC_ROUNDS 20000

static idxstack_t g_st;
static _Atomic int g_owner[CONC_CAP];
static _Atomic int g_double_owned;

static void *churn(void *arg) {
  int self = (int)(intptr_t)arg + 1;
  for (int r = 0; r < CONC_ROUNDS; r++) {
    int idx = idxstack_pop(&g_st);
    if (idx < 0) continue;
    if (atomic_exchange(&g_owner[idx], self) != 0) atomic_store(&g_double_owned, 1);
    atomic_store(&g_owner[idx], 0);
    idxstack_push(&g_st, idx);
  }
  return NULL;
}

START_TEST(concurrent_pop_push_unique_owner) {
  pthread_t th[CONC_THREADS];
  int seen[CONC_CAP] = {0};
  int n = 0;
  int idx;
  ck_assert_int_eq(idxstack_init(&g_st, CONC_CAP), 0);
  for (int i = 0; i < CONC_CAP; i++) idxstack_push(&g_st, i);
  for (int i = 0; i < CONC_THREADS; i++) ck_assert_int_eq(pthread_create(&th[i], NULL, churn, (void *)(intptr_t)i), 0);
  for (int i = 0; i < CONC_THREADS; i++) pthread_join(th[i], NULL);
  ck_assert_int_eq(atomic_load(&g_double_owned), 0);
  while ((idx = idxstack_pop(&g_st)) >= 0) {
    ck_assert_int_eq(seen[idx], 0);
    seen[idx] = 1;
    n++;
  }
  ck_assert_int_eq(n, CONC_CAP);
  idxstack_free(&g_st);
}
END_TEST

static Suite *idxstack_suite(void) {
  Suite *s = suite_create("idxstack");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 60);
  tcase_add_test(tc, lifo_order_and_empty);
  tcase_add_test(tc, out_of_range_push_ignored);
  tcase_add_test(tc, init_rejects_bad_cap);
  tcase_add_test(tc, concurrent_pop_push_unique_owner);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(idxstack_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
