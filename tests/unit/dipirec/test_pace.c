/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dipirec/filter/pace.h"

/* adaptation field only, PCR_flag set, no extension (ext9=0).
   same layout as tspacket_write.c's put_pcr(), inverse-checked by hand */
static void build_pcr_pkt(unsigned char *pkt, unsigned pid, uint64_t base33) {
  memset(pkt, 0xFF, 188);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((pid >> 8) & 0x1F);
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x20;
  pkt[4] = 7;
  pkt[5] = 0x10;
  pkt[6] = (unsigned char)(base33 >> 25);
  pkt[7] = (unsigned char)(base33 >> 17);
  pkt[8] = (unsigned char)(base33 >> 9);
  pkt[9] = (unsigned char)(base33 >> 1);
  pkt[10] = (unsigned char)(((base33 & 1) << 7) | 0x7E);
  pkt[11] = 0x00;
}

START_TEST(pace_accumulate_simple_forward) {
  uint64_t last = 1000, elapsed = 0;
  pace_accumulate(&last, &elapsed, 1500, 32);
  ck_assert_uint_eq(elapsed, 500u);
  ck_assert_uint_eq(last, 1500u);
}
END_TEST

START_TEST(pace_accumulate_wraps_32bit) {
  uint64_t last = 4294967290ULL, elapsed = 0; /* 2^32 minus 6 */
  pace_accumulate(&last, &elapsed, 100, 32);
  ck_assert_uint_eq(elapsed, 106u); /* 6 to wrap, then 100 more */
  ck_assert_uint_eq(last, 100u);
}
END_TEST

START_TEST(pace_accumulate_wraps_33bit) {
  uint64_t last = (((uint64_t)1 << 33) - 1) - 4, elapsed = 0; /* 5 short of wrap */
  pace_accumulate(&last, &elapsed, 50, 33);
  ck_assert_uint_eq(elapsed, 55u); /* 5 to wrap, then 50 more */
  ck_assert_uint_eq(last, 50u);
}
END_TEST

START_TEST(pace_accumulate_ignores_small_backward_jitter) {
  uint64_t last = 10000, elapsed = 0;
  pace_accumulate(&last, &elapsed, 9990, 32); /* reorder/jitter, not a wrap */
  ck_assert_uint_eq(elapsed, 0u);
  ck_assert_uint_eq(last, 9990u);
}
END_TEST

START_TEST(pace_feed_pcr_pkt_ignores_wrong_pid_and_no_pcr_flag) {
  pace_ctrl_t *p = pace_new();
  unsigned char pkt[188];

  build_pcr_pkt(pkt, 0x0101, 12345);
  pace_feed_pcr_pkt(p, pkt, 0); /* pcr_pid unresolved */
  ck_assert_int_eq(pace_has_baseline(p), 0);

  pace_feed_pcr_pkt(p, pkt, 0x0102); /* wrong pid */
  ck_assert_int_eq(pace_has_baseline(p), 0);

  pkt[5] = 0x00; /* PCR_flag cleared */
  pace_feed_pcr_pkt(p, pkt, 0x0101);
  ck_assert_int_eq(pace_has_baseline(p), 0);

  pace_free(p);
}
END_TEST

START_TEST(pace_feed_pcr_pkt_baselines_on_matching_pid) {
  pace_ctrl_t *p = pace_new();
  unsigned char pkt[188];

  build_pcr_pkt(pkt, 0x0101, 12345);
  pace_feed_pcr_pkt(p, pkt, 0x0101);
  ck_assert_int_eq(pace_has_baseline(p), 1); /* first sample: baseline only, never sleeps */

  pace_free(p);
}
END_TEST

START_TEST(pace_feed_rtp_ts_baselines_on_first_call) {
  pace_ctrl_t *p = pace_new();
  ck_assert_int_eq(pace_has_baseline(p), 0);
  pace_feed_rtp_ts(p, 90000);
  ck_assert_int_eq(pace_has_baseline(p), 1);
  pace_free(p);
}
END_TEST

static Suite *pace_suite(void) {
  Suite *s = suite_create("pace");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, pace_accumulate_simple_forward);
  tcase_add_test(tc, pace_accumulate_wraps_32bit);
  tcase_add_test(tc, pace_accumulate_wraps_33bit);
  tcase_add_test(tc, pace_accumulate_ignores_small_backward_jitter);
  tcase_add_test(tc, pace_feed_pcr_pkt_ignores_wrong_pid_and_no_pcr_flag);
  tcase_add_test(tc, pace_feed_pcr_pkt_baselines_on_matching_pid);
  tcase_add_test(tc, pace_feed_rtp_ts_baselines_on_first_call);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(pace_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
