/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipiradiohead/input/framequeue.h"

static source_frame_t frame_of(const unsigned char *data, size_t len) {
  source_frame_t f;
  memset(&f, 0, sizeof f);
  f.sample_rate = 48000;
  f.samples = 1024;
  f.data = data;
  f.len = len;
  return f;
}

START_TEST(orders_frames_and_sums_duration) {
  framequeue_t *q = framequeue_new();
  unsigned char d1[4] = {1, 2, 3, 4}, d2[2] = {9, 8};
  source_frame_t a = frame_of(d1, sizeof d1), b = frame_of(d2, sizeof d2), out;

  ck_assert_ptr_nonnull(q);
  ck_assert_int_eq(framequeue_push(q, &a), 0);
  ck_assert_int_eq(framequeue_push(q, &b), 0);
  ck_assert_uint_eq(framequeue_count(q), 2);
  ck_assert_uint_eq(framequeue_ms(q), 42);
  ck_assert_int_eq(framequeue_pop(q, &out), 1);
  ck_assert_uint_eq(out.len, 4);
  ck_assert_int_eq(memcmp(out.data, d1, 4), 0);
  ck_assert_int_eq(framequeue_pop(q, &out), 1);
  ck_assert_uint_eq(out.len, 2);
  ck_assert_int_eq(memcmp(out.data, d2, 2), 0);
  ck_assert_int_eq(framequeue_pop(q, &out), 0);
  ck_assert_uint_eq(framequeue_ms(q), 0);
  framequeue_free(q);
}
END_TEST

START_TEST(push_copies_data) {
  framequeue_t *q = framequeue_new();
  unsigned char d[3] = {5, 6, 7};
  source_frame_t a = frame_of(d, sizeof d);
  source_frame_t out;

  framequeue_push(q, &a);
  memset(d, 0, sizeof d);
  ck_assert_int_eq(framequeue_pop(q, &out), 1);
  ck_assert_uint_eq(out.data[0], 5);
  ck_assert_uint_eq(out.data[2], 7);
  framequeue_free(q);
}
END_TEST

START_TEST(popped_data_valid_until_next_pop) {
  framequeue_t *q = framequeue_new();
  unsigned char d1[2] = {1, 1};
  unsigned char d2[2] = {2, 2};
  source_frame_t a = frame_of(d1, 2);
  source_frame_t b = frame_of(d2, 2);
  source_frame_t o1;
  source_frame_t o2;

  framequeue_push(q, &a);
  framequeue_push(q, &b);
  framequeue_pop(q, &o1);
  ck_assert_uint_eq(o1.data[0], 1);
  framequeue_pop(q, &o2);
  ck_assert_uint_eq(o2.data[0], 2);
  framequeue_free(q);
}
END_TEST

START_TEST(zero_sample_rate_counts_no_duration) {
  framequeue_t *q = framequeue_new();
  unsigned char d[1] = {0};
  source_frame_t a = frame_of(d, 1);

  a.sample_rate = 0;
  framequeue_push(q, &a);
  ck_assert_uint_eq(framequeue_ms(q), 0);
  ck_assert_uint_eq(framequeue_count(q), 1);
  framequeue_free(q);
}
END_TEST

static Suite *framequeue_suite(void) {
  Suite *s = suite_create("framequeue");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, orders_frames_and_sums_duration);
  tcase_add_test(tc, push_copies_data);
  tcase_add_test(tc, popped_data_valid_until_next_pop);
  tcase_add_test(tc, zero_sample_rate_counts_no_duration);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(framequeue_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
