/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include "lib/helper/growutil.h"

START_TEST(array_grow_policy) {
  int cap = 0;
  int *a = NULL;
  void *p;
  a = array_grow(a, &cap, 1, sizeof *a);
  ck_assert_ptr_nonnull(a);
  ck_assert_int_eq(cap, 16);
  p = array_grow(a, &cap, 10, sizeof *a);
  ck_assert_ptr_eq(p, a);
  ck_assert_int_eq(cap, 16);
  a = array_grow(a, &cap, 17, sizeof *a);
  ck_assert_int_eq(cap, 32);
  a = array_grow(a, &cap, 100, sizeof *a);
  ck_assert_int_eq(cap, 100);
  ck_assert_ptr_null(array_grow(a, &cap, INT_MAX, SIZE_MAX / 4));
  ck_assert_int_eq(cap, 100);
  free(a);
}
END_TEST

START_TEST(growbuf_reserve_policy) {
  void *buf = NULL;
  size_t cap = 0;
  ck_assert_int_eq(growbuf_reserve(&buf, &cap, 4, 0, 8), 0);
  ck_assert_uint_eq(cap, 0u);
  ck_assert_int_eq(growbuf_reserve(&buf, &cap, 4, 1, 0), 0);
  ck_assert_uint_eq(cap, 1u);
  ck_assert_int_eq(growbuf_reserve(&buf, &cap, 4, 5, 0), 0);
  ck_assert_uint_eq(cap, 8u);
  ck_assert_int_eq(growbuf_reserve(&buf, &cap, 4, 5, 0), 0);
  ck_assert_uint_eq(cap, 8u);
  ck_assert_int_eq(growbuf_reserve(&buf, &cap, 0, 100, 0), -1);
  ck_assert_int_eq(growbuf_reserve(&buf, &cap, SIZE_MAX / 2, 100, 0), -1);
  ck_assert_int_eq(growbuf_reserve(&buf, &cap, 1, SIZE_MAX - 5, 0), -1);
  ck_assert_int_eq(growbuf_reserve(&buf, &cap, (size_t)1 << 20, SIZE_MAX / ((size_t)1 << 20) + 1, 0), -1);
  ck_assert_uint_eq(cap, 8u);
  free(buf);
  buf = NULL;
  cap = 0;
  ck_assert_int_eq(growbuf_reserve(&buf, &cap, 2, 3, 64), 0);
  ck_assert_uint_eq(cap, 64u);
  free(buf);
}
END_TEST

START_TEST(next_pow2_rounds_up) {
  ck_assert_uint_eq(next_pow2(0), 1u);
  ck_assert_uint_eq(next_pow2(1), 1u);
  ck_assert_uint_eq(next_pow2(2), 2u);
  ck_assert_uint_eq(next_pow2(3), 4u);
  ck_assert_uint_eq(next_pow2(1000), 1024u);
}
END_TEST

static Suite *growutil_suite(void) {
  Suite *s = suite_create("growutil");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, array_grow_policy);
  tcase_add_test(tc, growbuf_reserve_policy);
  tcase_add_test(tc, next_pow2_rounds_up);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(growutil_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
