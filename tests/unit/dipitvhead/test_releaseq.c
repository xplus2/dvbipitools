/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <string.h>

#include "dipitvhead/mux/releaseq.h"

static void fill(unsigned char pkt[188], unsigned char marker) {
  memset(pkt, marker, 188);
}

START_TEST(empty_queue_has_no_head) {
  releaseq_t *q = releaseq_new(10);
  int has;
  uint64_t tag;
  ck_assert_ptr_nonnull(q);
  ck_assert_uint_eq((unsigned)releaseq_len(q), 0u);
  ck_assert_ptr_null(releaseq_head(q, &has, &tag));
  releaseq_pop(q);
  ck_assert_uint_eq((unsigned)releaseq_len(q), 0u);
  releaseq_free(q);
}
END_TEST

START_TEST(zero_capacity_is_rejected) {
  ck_assert_ptr_null(releaseq_new(0));
}
END_TEST

START_TEST(packets_come_out_in_push_order_with_their_tags) {
  releaseq_t *q = releaseq_new(100);
  unsigned char pkt[188];
  const unsigned char *h;
  int has;
  uint64_t tag;
  for (int i = 0; i < 5; i++) {
    fill(pkt, (unsigned char)(i + 1));
    ck_assert_int_eq(releaseq_push(q, pkt, i % 2, 1000u + (unsigned)i), 0);
  }
  ck_assert_uint_eq((unsigned)releaseq_len(q), 5u);
  for (int i = 0; i < 5; i++) {
    h = releaseq_head(q, &has, &tag);
    ck_assert_ptr_nonnull(h);
    ck_assert_uint_eq(h[0], (unsigned)(i + 1));
    ck_assert_uint_eq(h[187], (unsigned)(i + 1));
    ck_assert_int_eq(has, i % 2);
    ck_assert_uint_eq(tag, 1000u + (unsigned)i);
    releaseq_pop(q);
  }
  ck_assert_uint_eq((unsigned)releaseq_len(q), 0u);
  releaseq_free(q);
}
END_TEST

START_TEST(push_fails_at_the_maximum_and_keeps_contents) {
  releaseq_t *q = releaseq_new(300);
  unsigned char pkt[188];
  const unsigned char *h;
  int has;
  uint64_t tag;
  for (int i = 0; i < 300; i++) {
    fill(pkt, (unsigned char)i);
    ck_assert_int_eq(releaseq_push(q, pkt, 1, (uint64_t)i), 0);
  }
  fill(pkt, 0xEE);
  ck_assert_int_eq(releaseq_push(q, pkt, 1, 999), -1);
  ck_assert_uint_eq((unsigned)releaseq_len(q), 300u);
  h = releaseq_head(q, &has, &tag);
  ck_assert_uint_eq(h[0], 0u);
  releaseq_free(q);
}
END_TEST

START_TEST(growth_keeps_order_across_wraparound) {
  releaseq_t *q = releaseq_new(5000);
  unsigned char pkt[188];
  const unsigned char *h;
  int has;
  uint64_t tag;
  unsigned next_in = 0;
  unsigned next_out = 0;
  for (int round = 0; round < 40; round++) {
    for (int i = 0; i < 100; i++) {
      fill(pkt, (unsigned char)next_in);
      ck_assert_int_eq(releaseq_push(q, pkt, 1, next_in), 0);
      next_in++;
    }
    for (int i = 0; i < 60; i++) {
      h = releaseq_head(q, &has, &tag);
      ck_assert_ptr_nonnull(h);
      ck_assert_uint_eq(tag, next_out);
      ck_assert_uint_eq(h[0], (unsigned char)next_out);
      releaseq_pop(q);
      next_out++;
    }
  }
  ck_assert_uint_eq((unsigned)releaseq_len(q), (next_in - next_out));
  while (releaseq_len(q)) {
    h = releaseq_head(q, &has, &tag);
    ck_assert_uint_eq(tag, next_out);
    releaseq_pop(q);
    next_out++;
  }
  ck_assert_uint_eq(next_in, next_out);
  releaseq_free(q);
}
END_TEST

START_TEST(free_accepts_null) {
  releaseq_free(NULL);
}
END_TEST

static Suite *releaseq_suite(void) {
  Suite *s = suite_create("releaseq");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, empty_queue_has_no_head);
  tcase_add_test(tc, zero_capacity_is_rejected);
  tcase_add_test(tc, packets_come_out_in_push_order_with_their_tags);
  tcase_add_test(tc, push_fails_at_the_maximum_and_keeps_contents);
  tcase_add_test(tc, growth_keeps_order_across_wraparound);
  tcase_add_test(tc, free_accepts_null);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(releaseq_suite());
  int failed;
  srunner_run_all(sr, CK_NORMAL);
  failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? 0 : 1;
}
