/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dipiradiohead/mux/pes.h"
#include "lib/demux/pes.h"

static int g_calls;
static unsigned g_pid;
static int g_has_pts;
static uint64_t g_pts;
static unsigned char g_data[512];
static size_t g_len;

static void capture_cb(void *ctx, unsigned pid, int has_pts, uint64_t pts, int has_dts, uint64_t dts, const unsigned char *data,
                        size_t len) {
  (void)ctx;
  (void)has_dts;
  (void)dts;
  g_calls++;
  g_pid = pid;
  g_has_pts = has_pts;
  g_pts = pts;
  g_len = len;
  memcpy(g_data, data, len < sizeof g_data ? len : sizeof g_data);
}

START_TEST(pes_build_round_trips_through_lib_demux_pes) {
  pes_t *dec = pes_new(capture_cb, NULL);
  unsigned char frame[170], pesbuf[8192], pkt[188];
  size_t peslen, i;

  for (i = 0; i < sizeof frame; i++)
    frame[i] = (unsigned char)(i & 0xFF);

  peslen = pes_build(900000u, frame, sizeof frame, pesbuf, sizeof pesbuf);
  ck_assert_uint_eq(peslen, 14u + sizeof frame);
  ck_assert_uint_eq(peslen, 184u); /* exactly fills one TS packet payload, no padding needed */

  pkt[0] = 0x47;
  pkt[1] = 0x40 | ((0x0101 >> 8) & 0x1F);
  pkt[2] = 0x0101 & 0xFF;
  pkt[3] = 0x10;
  memcpy(pkt + 4, pesbuf, peslen);

  ck_assert_int_eq(pes_track(dec, 0x0101), 0);
  g_calls = 0;
  pes_feed(dec, pkt);
  pes_flush(dec);

  ck_assert_int_eq(g_calls, 1);
  ck_assert_uint_eq(g_pid, 0x0101u);
  ck_assert_int_eq(g_has_pts, 1);
  ck_assert_uint_eq((unsigned)g_pts, 900000u);
  ck_assert_uint_eq(g_len, sizeof frame);
  ck_assert_mem_eq(g_data, frame, sizeof frame);

  pes_free(dec);
}
END_TEST

START_TEST(pes_build_rejects_oversized_frame) {
  unsigned char out[16];
  static unsigned char big[70000]; /* > 65527 max */
  ck_assert_uint_eq(pes_build(0, big, sizeof big, out, sizeof out), 0u);
}
END_TEST

START_TEST(pes_build_rejects_small_cap) {
  unsigned char frame[4] = {1, 2, 3, 4};
  unsigned char out[10]; /* header alone is 14 bytes */
  ck_assert_uint_eq(pes_build(0, frame, sizeof frame, out, sizeof out), 0u);
}
END_TEST

static Suite *pes_suite(void) {
  Suite *s = suite_create("dipiradiohead_pes");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, pes_build_round_trips_through_lib_demux_pes);
  tcase_add_test(tc, pes_build_rejects_oversized_frame);
  tcase_add_test(tc, pes_build_rejects_small_cap);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(pes_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
