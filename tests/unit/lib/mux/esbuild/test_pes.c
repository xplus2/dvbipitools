/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/mux/esbuild/pes.h"
#include "lib/mux/esbuild/tspacketize.h"

START_TEST(esbuild_pes_build_pts_only_matches_dipiradiohead_layout) {
  static const unsigned char frame[] = {0xAA, 0xBB, 0xCC};
  unsigned char out[64];
  size_t n = esbuild_pes_build(0xC0, 0x123456789ULL & 0x1FFFFFFFFULL, 0, 0, frame, sizeof frame, out, sizeof out);

  ck_assert_uint_eq(n, 14 + sizeof frame);
  ck_assert_uint_eq(out[0], 0);
  ck_assert_uint_eq(out[1], 0);
  ck_assert_uint_eq(out[2], 1);
  ck_assert_uint_eq(out[3], 0xC0);
  ck_assert_uint_eq((unsigned)((out[4] << 8) | out[5]), 8u + sizeof frame);
  ck_assert_uint_eq(out[6], 0x85);
  ck_assert_uint_eq(out[7], 0x80);
  ck_assert_uint_eq(out[8], 5);
  ck_assert_uint_eq(out[9] & 0xF0, 0x20);
  ck_assert_int_eq(memcmp(out + 14, frame, sizeof frame), 0);
}
END_TEST

START_TEST(esbuild_pes_build_pts_dts_sets_both_marker_prefixes) {
  static const unsigned char frame[] = {1, 2, 3, 4};
  unsigned char out[64];
  size_t n = esbuild_pes_build(0xE0, 90000, 1, 45000, frame, sizeof frame, out, sizeof out);

  ck_assert_uint_eq(n, 19 + sizeof frame);
  ck_assert_uint_eq(out[7], 0xC0);
  ck_assert_uint_eq(out[8], 10);
  ck_assert_uint_eq(out[9] & 0xF0, 0x30);
  ck_assert_uint_eq(out[14] & 0xF0, 0x10);
  ck_assert_uint_eq((unsigned)((out[4] << 8) | out[5]), 13u + sizeof frame);
  ck_assert_int_eq(memcmp(out + 19, frame, sizeof frame), 0);
}
END_TEST

START_TEST(esbuild_pes_build_large_video_frame_uses_unbounded_length) {
  static unsigned char frame[70000];
  unsigned char *out = malloc(70100);
  size_t n;
  ck_assert_ptr_nonnull(out);
  memset(frame, 0x5A, sizeof frame);

  n = esbuild_pes_build(0xE0, 90000, 0, 0, frame, sizeof frame, out, 70100);
  ck_assert_uint_eq(n, 14 + sizeof frame);
  ck_assert_uint_eq(out[4], 0);
  ck_assert_uint_eq(out[5], 0);
  free(out);
}
END_TEST

START_TEST(esbuild_pes_build_large_audio_frame_rejected) {
  static unsigned char frame[70000];
  unsigned char *out = malloc(70100);
  size_t n;
  ck_assert_ptr_nonnull(out);
  n = esbuild_pes_build(0xC0, 90000, 0, 0, frame, sizeof frame, out, 70100);
  ck_assert_uint_eq(n, 0u);
  free(out);
}
END_TEST

typedef struct {
  unsigned char packets[16][188];
  int count;
} collected_t;

static void collect_cb(void *ctx, const unsigned char *pkt) {
  collected_t *c = ctx;
  if (c->count < 16) memcpy(c->packets[c->count], pkt, 188);
  c->count++;
}

START_TEST(esbuild_ts_packetize_produces_valid_ts_packets) {
  static const unsigned char frame[] = {0x11, 0x22, 0x33, 0x44, 0x55};
  unsigned char cc = 0;
  unsigned char pesbuf[64];
  collected_t c;
  size_t n;

  memset(&c, 0, sizeof c);
  n = esbuild_ts_packetize(0x101, &cc, 0xC0, 90000, 0, 0, frame, sizeof frame, 0, 0, pesbuf, sizeof pesbuf, collect_cb, &c);

  ck_assert_uint_eq(n, 1u);
  ck_assert_int_eq(c.count, 1);
  ck_assert_uint_eq(c.packets[0][0], 0x47);
  ck_assert_uint_eq(c.packets[0][1] & 0x40, 0x40); /* PUSI set on first PES packet */
  ck_assert_uint_eq(((unsigned)(c.packets[0][1] & 0x1F) << 8) | c.packets[0][2], 0x101u);
  ck_assert_uint_eq(cc, 1u);
}
END_TEST

START_TEST(esbuild_ts_packetize_cc_persists_across_calls) {
  static const unsigned char frame1[] = {0x11, 0x22};
  static const unsigned char frame2[] = {0x33, 0x44};
  unsigned char cc = 0;
  unsigned char pesbuf[64];
  collected_t c;

  memset(&c, 0, sizeof c);
  esbuild_ts_packetize(0x101, &cc, 0xC0, 90000, 0, 0, frame1, sizeof frame1, 0, 0, pesbuf, sizeof pesbuf, collect_cb, &c);
  esbuild_ts_packetize(0x101, &cc, 0xC0, 90090, 0, 0, frame2, sizeof frame2, 0, 0, pesbuf, sizeof pesbuf, collect_cb, &c);

  ck_assert_int_eq(c.count, 2);
  ck_assert_uint_eq(c.packets[0][3] & 0x0F, 1u);
  ck_assert_uint_eq(c.packets[1][3] & 0x0F, 2u);
  ck_assert_uint_eq(cc, 2u);
}
END_TEST

static Suite *esbuild_pes_suite(void) {
  Suite *s = suite_create("esbuild_pes");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, esbuild_pes_build_pts_only_matches_dipiradiohead_layout);
  tcase_add_test(tc, esbuild_pes_build_pts_dts_sets_both_marker_prefixes);
  tcase_add_test(tc, esbuild_pes_build_large_video_frame_uses_unbounded_length);
  tcase_add_test(tc, esbuild_pes_build_large_audio_frame_rejected);
  tcase_add_test(tc, esbuild_ts_packetize_produces_valid_ts_packets);
  tcase_add_test(tc, esbuild_ts_packetize_cc_persists_across_calls);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(esbuild_pes_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
