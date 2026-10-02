/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/net/jitbuf.h"

#define MS 1000000ULL

static size_t build_rtp(unsigned char *buf, uint16_t seq, uint32_t ssrc) {
  memset(buf, 0, 12 + 188);
  buf[0] = 0x80;
  buf[1] = 33;
  buf[2] = (unsigned char)(seq >> 8);
  buf[3] = (unsigned char)seq;
  buf[8] = (unsigned char)(ssrc >> 24);
  buf[9] = (unsigned char)(ssrc >> 16);
  buf[10] = (unsigned char)(ssrc >> 8);
  buf[11] = (unsigned char)ssrc;
  buf[12] = 0x47;
  return 12 + 188;
}

static void push_seq(jitbuf_t *j, uint16_t seq, uint32_t ssrc, uint64_t now) {
  unsigned char p[12 + 188];
  size_t plen = build_rtp(p, seq, ssrc);
  jitbuf_push(j, p, plen, now);
}

static int pop_seq(jitbuf_t *j, uint64_t now) {
  unsigned char out[JITBUF_MAX_DGRAM];
  size_t n = jitbuf_pop(j, out, sizeof out, now);
  return n ? (out[2] << 8) | out[3] : -1;
}

START_TEST(rejects_bad_delay) {
  ck_assert_ptr_null(jitbuf_new(0, JITBUF_AUTO));
  ck_assert_ptr_null(jitbuf_new(JITBUF_MAX_DELAY_MS + 1, JITBUF_AUTO));
}
END_TEST

START_TEST(rtp_in_order_held_until_delay) {
  jitbuf_t *j = jitbuf_new(100, JITBUF_AUTO);
  push_seq(j, 10, 1, 0);
  push_seq(j, 11, 1, 5 * MS);
  ck_assert_int_eq(pop_seq(j, 99 * MS), -1);
  ck_assert_int_eq(jitbuf_next_ns(j, 99 * MS), (int64_t)(1 * MS));
  ck_assert_int_eq(pop_seq(j, 100 * MS), 10);
  ck_assert_int_eq(pop_seq(j, 100 * MS), -1);
  ck_assert_int_eq(pop_seq(j, 105 * MS), 11);
  ck_assert_int_eq(jitbuf_next_ns(j, 105 * MS), -1);
  jitbuf_free(j);
}
END_TEST

START_TEST(depth_is_age_of_oldest_capped_at_delay) {
  jitbuf_t *j = jitbuf_new(100, JITBUF_AUTO);

  ck_assert_uint_eq(jitbuf_depth_ns(j, 0), 0u);
  push_seq(j, 1, 7, 1000 * MS);
  ck_assert_uint_eq(jitbuf_depth_ns(j, 1000 * MS), 0u);
  push_seq(j, 2, 7, 1030 * MS);
  ck_assert_uint_eq(jitbuf_depth_ns(j, 1040 * MS), 40 * MS);
  ck_assert_uint_eq(jitbuf_depth_ns(j, 1099 * MS), 99 * MS);
  ck_assert_uint_eq(jitbuf_depth_ns(j, 1500 * MS), 100 * MS);
  ck_assert_int_eq(pop_seq(j, 1100 * MS), 1);
  ck_assert_uint_eq(jitbuf_depth_ns(j, 1110 * MS), 80 * MS);
  ck_assert_int_eq(pop_seq(j, 1130 * MS), 2);
  ck_assert_uint_eq(jitbuf_depth_ns(j, 1140 * MS), 0u);
  jitbuf_free(j);
}
END_TEST

START_TEST(rtp_reorders) {
  jitbuf_t *j = jitbuf_new(50, JITBUF_AUTO);
  jitbuf_stats_t st;
  push_seq(j, 1, 7, 0);
  push_seq(j, 3, 7, 1 * MS);
  push_seq(j, 2, 7, 2 * MS);
  ck_assert_int_eq(pop_seq(j, 60 * MS), 1);
  ck_assert_int_eq(pop_seq(j, 60 * MS), 2);
  ck_assert_int_eq(pop_seq(j, 60 * MS), 3);
  jitbuf_stats(j, &st);
  ck_assert_uint_eq(st.lost, 0);
  ck_assert_uint_eq(st.reordered, 1);
  jitbuf_free(j);
}
END_TEST

START_TEST(rtp_gap_given_up_when_next_due) {
  jitbuf_t *j = jitbuf_new(50, JITBUF_AUTO);
  jitbuf_stats_t st;
  push_seq(j, 1, 7, 0);
  push_seq(j, 4, 7, 1 * MS);
  ck_assert_int_eq(pop_seq(j, 50 * MS), 1);
  ck_assert_int_eq(pop_seq(j, 50 * MS), -1);
  ck_assert_int_eq(jitbuf_next_ns(j, 50 * MS), (int64_t)(1 * MS));
  ck_assert_int_eq(pop_seq(j, 51 * MS), 4);
  jitbuf_stats(j, &st);
  ck_assert_uint_eq(st.lost, 2);
  jitbuf_free(j);
}
END_TEST

START_TEST(rtp_late_and_dup_dropped) {
  jitbuf_t *j = jitbuf_new(10, JITBUF_AUTO);
  jitbuf_stats_t st;
  push_seq(j, 5, 7, 0);
  push_seq(j, 5, 7, 1 * MS);
  ck_assert_int_eq(pop_seq(j, 10 * MS), 5);
  push_seq(j, 5, 7, 11 * MS);
  push_seq(j, 4, 7, 11 * MS);
  ck_assert_int_eq(pop_seq(j, 100 * MS), -1);
  jitbuf_stats(j, &st);
  ck_assert_uint_eq(st.dup, 1);
  ck_assert_uint_eq(st.late, 2);
  jitbuf_free(j);
}
END_TEST

START_TEST(rtp_first_packets_swapped_not_late) {
  jitbuf_t *j = jitbuf_new(10, JITBUF_AUTO);
  jitbuf_stats_t st;
  push_seq(j, 11, 7, 0);
  push_seq(j, 10, 7, 1 * MS);
  ck_assert_int_eq(pop_seq(j, 20 * MS), 10);
  ck_assert_int_eq(pop_seq(j, 20 * MS), 11);
  jitbuf_stats(j, &st);
  ck_assert_uint_eq(st.late, 0);
  ck_assert_uint_eq(st.reordered, 1);
  jitbuf_free(j);
}
END_TEST

START_TEST(rtp_seq_wrap) {
  jitbuf_t *j = jitbuf_new(10, JITBUF_AUTO);
  push_seq(j, 65535, 7, 0);
  push_seq(j, 1, 7, 1 * MS);
  push_seq(j, 0, 7, 2 * MS);
  ck_assert_int_eq(pop_seq(j, 20 * MS), 65535);
  ck_assert_int_eq(pop_seq(j, 20 * MS), 0);
  ck_assert_int_eq(pop_seq(j, 20 * MS), 1);
  jitbuf_free(j);
}
END_TEST

START_TEST(rtp_ssrc_change_resyncs) {
  jitbuf_t *j = jitbuf_new(10, JITBUF_AUTO);
  jitbuf_stats_t st;
  push_seq(j, 100, 1, 0);
  push_seq(j, 7, 2, 1 * MS);
  jitbuf_stats(j, &st);
  ck_assert_uint_eq(st.resync, 1);
  ck_assert_int_eq(pop_seq(j, 20 * MS), 7);
  ck_assert_int_eq(pop_seq(j, 20 * MS), -1);
  jitbuf_free(j);
}
END_TEST

START_TEST(rtp_far_jump_needs_repeats) {
  jitbuf_t *j = jitbuf_new(10, JITBUF_AUTO);
  jitbuf_stats_t st;
  push_seq(j, 100, 1, 0);
  for (unsigned i = 0; i < 7; i++) push_seq(j, (uint16_t)(30000 + i), 1, 1 * MS);
  jitbuf_stats(j, &st);
  ck_assert_uint_eq(st.resync, 0);
  ck_assert_uint_eq(st.dropped, 7);
  push_seq(j, 30007, 1, 2 * MS);
  jitbuf_stats(j, &st);
  ck_assert_uint_eq(st.resync, 1);
  ck_assert_int_eq(pop_seq(j, 20 * MS), 30007);
  jitbuf_free(j);
}
END_TEST

START_TEST(fifo_for_non_rtp_keeps_arrival_order) {
  jitbuf_t *j = jitbuf_new(20, JITBUF_AUTO);
  unsigned char a[188], b[188], out[JITBUF_MAX_DGRAM];
  memset(a, 0x47, sizeof a);
  memset(b, 0x47, sizeof b);
  a[1] = 1;
  b[1] = 2;
  jitbuf_push(j, b, sizeof b, 0);
  jitbuf_push(j, a, sizeof a, 5 * MS);
  ck_assert_uint_eq(jitbuf_pop(j, out, sizeof out, 19 * MS), 0);
  ck_assert_uint_eq(jitbuf_pop(j, out, sizeof out, 20 * MS), 188);
  ck_assert_int_eq(out[1], 2);
  ck_assert_uint_eq(jitbuf_pop(j, out, sizeof out, 24 * MS), 0);
  ck_assert_uint_eq(jitbuf_pop(j, out, sizeof out, 25 * MS), 188);
  ck_assert_int_eq(out[1], 1);
  jitbuf_free(j);
}
END_TEST

START_TEST(oversize_dropped) {
  jitbuf_t *j = jitbuf_new(10, JITBUF_FIFO);
  unsigned char big[JITBUF_MAX_DGRAM + 1];
  jitbuf_stats_t st;
  memset(big, 0x47, sizeof big);
  jitbuf_push(j, big, sizeof big, 0);
  ck_assert_uint_eq(jitbuf_queued(j), 0);
  jitbuf_stats(j, &st);
  ck_assert_uint_eq(st.dropped, 1);
  jitbuf_free(j);
}
END_TEST

static Suite *jitbuf_suite(void) {
  Suite *s = suite_create("jitbuf");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, rejects_bad_delay);
  tcase_add_test(tc, rtp_in_order_held_until_delay);
  tcase_add_test(tc, depth_is_age_of_oldest_capped_at_delay);
  tcase_add_test(tc, rtp_reorders);
  tcase_add_test(tc, rtp_gap_given_up_when_next_due);
  tcase_add_test(tc, rtp_late_and_dup_dropped);
  tcase_add_test(tc, rtp_first_packets_swapped_not_late);
  tcase_add_test(tc, rtp_seq_wrap);
  tcase_add_test(tc, rtp_ssrc_change_resyncs);
  tcase_add_test(tc, rtp_far_jump_needs_repeats);
  tcase_add_test(tc, fifo_for_non_rtp_keeps_arrival_order);
  tcase_add_test(tc, oversize_dropped);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(jitbuf_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
