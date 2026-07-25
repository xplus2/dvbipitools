/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/tspack.h"

static int g_calls;
static unsigned char g_last[188];
static int g_stop_after_first;

static void fill_packet(unsigned char *p) {
  size_t i;
  p[0] = 0x47;
  for (i = 1; i < 188; i++)
    p[i] = (unsigned char)i;
}

static int capture_cb(void *ctx, const unsigned char *pkt) {
  (void)ctx;
  g_calls++;
  memcpy(g_last, pkt, 188);
  return g_stop_after_first && g_calls == 1;
}

START_TEST(tspack_feed_single_call) {
  tspack_t pz = {{0}, 0};
  unsigned char pkt[188];
  fill_packet(pkt);
  g_calls = 0;
  g_stop_after_first = 0;

  ck_assert_int_eq(tspack_feed(&pz, pkt, sizeof pkt, capture_cb, NULL), 0);
  ck_assert_int_eq(g_calls, 1);
  ck_assert_mem_eq(g_last, pkt, 188);
}
END_TEST

START_TEST(tspack_feed_reassembles_split_input) {
  tspack_t pz = {{0}, 0};
  unsigned char pkt[188];
  fill_packet(pkt);
  g_calls = 0;
  g_stop_after_first = 0;

  ck_assert_int_eq(tspack_feed(&pz, pkt, 100, capture_cb, NULL), 0);
  ck_assert_int_eq(g_calls, 0); /* not enough for a full packet yet */
  ck_assert_int_eq(tspack_feed(&pz, pkt + 100, 88, capture_cb, NULL), 0);
  ck_assert_int_eq(g_calls, 1);
  ck_assert_mem_eq(g_last, pkt, 188);
}
END_TEST

START_TEST(tspack_feed_resyncs_past_garbage) {
  tspack_t pz = {{0}, 0};
  unsigned char buf[3 + 188];
  unsigned char pkt[188];
  fill_packet(pkt);
  buf[0] = 0x00;
  buf[1] = 0x11; /* not 0x47: garbage before sync */
  buf[2] = 0x22;
  memcpy(buf + 3, pkt, 188);
  g_calls = 0;
  g_stop_after_first = 0;

  ck_assert_int_eq(tspack_feed(&pz, buf, sizeof buf, capture_cb, NULL), 0);
  ck_assert_int_eq(g_calls, 1);
  ck_assert_mem_eq(g_last, pkt, 188);
}
END_TEST

START_TEST(tspack_feed_stops_when_callback_returns_nonzero) {
  tspack_t pz = {{0}, 0};
  unsigned char buf[2 * 188];
  unsigned char pkt[188];
  fill_packet(pkt);
  memcpy(buf, pkt, 188);
  memcpy(buf + 188, pkt, 188);
  g_calls = 0;
  g_stop_after_first = 1;

  ck_assert_int_eq(tspack_feed(&pz, buf, sizeof buf, capture_cb, NULL), 1);
  ck_assert_int_eq(g_calls, 1); /* second packet never delivered */
}
END_TEST

static Suite *tspack_suite(void) {
  Suite *s = suite_create("tspack");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, tspack_feed_single_call);
  tcase_add_test(tc, tspack_feed_reassembles_split_input);
  tcase_add_test(tc, tspack_feed_resyncs_past_garbage);
  tcase_add_test(tc, tspack_feed_stops_when_callback_returns_nonzero);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(tspack_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
