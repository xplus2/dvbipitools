/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/pes.h"

static int g_calls;
static unsigned g_pid;
static int g_has_pts, g_has_dts;
static uint64_t g_pts, g_dts;
static unsigned char g_data[512];
static size_t g_len;

static void capture_cb(void *ctx, unsigned pid, int has_pts, uint64_t pts, int has_dts, uint64_t dts, const unsigned char *data,
                        size_t len) {
  (void)ctx;
  g_calls++;
  g_pid = pid;
  g_has_pts = has_pts;
  g_pts = pts;
  g_has_dts = has_dts;
  g_dts = dts;
  g_len = len;
  memcpy(g_data, data, len < sizeof g_data ? len : sizeof g_data);
}

/* PES optional header PTS-only encoding, see ISO/IEC 13818-1 2.4.3.7 */
static void encode_pts(uint64_t pts, unsigned char out[5]) {
  unsigned b = (unsigned)((pts >> 30) & 0x7);
  unsigned p1 = (unsigned)((pts >> 22) & 0xFF);
  unsigned c = (unsigned)((pts >> 15) & 0x7F);
  unsigned p3 = (unsigned)((pts >> 7) & 0xFF);
  unsigned p4 = (unsigned)(pts & 0x7F);
  out[0] = (unsigned char)(0x21 | (b << 1));
  out[1] = (unsigned char)p1;
  out[2] = (unsigned char)((c << 1) | 1);
  out[3] = (unsigned char)p3;
  out[4] = (unsigned char)((p4 << 1) | 1);
}

/* pl: 184-byte TS payload for a PES-start packet, header_data_length=5 (PTS only) */
static void build_pes_start_payload(unsigned char *pl, unsigned stream_id, uint64_t pts, unsigned char fill_base) {
  size_t i;
  pl[0] = 0x00;
  pl[1] = 0x00;
  pl[2] = 0x01;
  pl[3] = (unsigned char)stream_id;
  pl[4] = 0x00;
  pl[5] = 0x00;
  pl[6] = 0x80;
  pl[7] = 0x80; /* PTS_DTS_flags = '10' (PTS only) */
  pl[8] = 0x05; /* PES_header_data_length */
  encode_pts(pts, pl + 9);
  for (i = 14; i < 184; i++)
    pl[i] = (unsigned char)((i - 14 + fill_base) & 0xFF);
}

/* pl: 184-byte TS payload for a PES-start packet, header_data_length=10 (PTS+DTS).
   marker nibbles left as encode_pts() writes them (0010): the reader under test
   never checks them, only PTS_DTS_flags and the 5-byte value layout */
static void build_pes_start_payload_pts_dts(unsigned char *pl, unsigned stream_id, uint64_t pts, uint64_t dts,
                                             unsigned char fill_base) {
  pl[0] = 0x00;
  pl[1] = 0x00;
  pl[2] = 0x01;
  pl[3] = (unsigned char)stream_id;
  pl[4] = 0x00;
  pl[5] = 0x00;
  pl[6] = 0x80;
  pl[7] = 0xC0; /* PTS_DTS_flags = '11' (PTS + DTS) */
  pl[8] = 0x0A; /* PES_header_data_length */
  encode_pts(pts, pl + 9);
  encode_pts(dts, pl + 14);
  for (size_t i = 19; i < 184; i++)
    pl[i] = (unsigned char)((i - 19 + fill_base) & 0xFF);
}

static void wrap_ts_raw(unsigned char pkt[188], unsigned pid, int pusi, const unsigned char *payload184) {
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((pusi ? 0x40 : 0x00) | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10; /* adaptation_field_control = payload only */
  memcpy(pkt + 4, payload184, 184);
}

START_TEST(pes_reassembles_across_packets_and_extracts_pts) {
  pes_t *p = pes_new(capture_cb, NULL);
  unsigned char payload[184], pkt[188];
  size_t i;

  ck_assert_int_eq(pes_track(p, 0x101), 0);

  build_pes_start_payload(payload, 0xE0, 900000u, 0x00);
  wrap_ts_raw(pkt, 0x101, 1, payload);
  g_calls = 0;
  pes_feed(p, pkt);
  ck_assert_int_eq(g_calls, 0); /* not delivered until next start or flush */

  for (i = 0; i < 184; i++)
    payload[i] = (unsigned char)((i + 100) & 0xFF);
  wrap_ts_raw(pkt, 0x101, 0, payload);
  pes_feed(p, pkt);

  pes_flush(p);

  ck_assert_int_eq(g_calls, 1);
  ck_assert_uint_eq(g_pid, 0x101u);
  ck_assert_int_eq(g_has_pts, 1);
  ck_assert_uint_eq((unsigned)g_pts, 900000u);
  ck_assert_uint_eq(g_len, 170u + 184u);
  for (i = 0; i < 170; i++)
    ck_assert_uint_eq(g_data[i], (unsigned char)(i & 0xFF));
  for (i = 0; i < 184; i++)
    ck_assert_uint_eq(g_data[170 + i], (unsigned char)((i + 100) & 0xFF));

  pes_free(p);
}
END_TEST

START_TEST(pes_extracts_dts_when_present) {
  pes_t *p = pes_new(capture_cb, NULL);
  unsigned char payload[184], pkt[188];

  ck_assert_int_eq(pes_track(p, 0x101), 0);
  build_pes_start_payload_pts_dts(payload, 0xE0, 900000u, 810000u, 0x00);
  wrap_ts_raw(pkt, 0x101, 1, payload);
  g_calls = 0;
  pes_feed(p, pkt);
  pes_flush(p);

  ck_assert_int_eq(g_calls, 1);
  ck_assert_int_eq(g_has_pts, 1);
  ck_assert_uint_eq((unsigned)g_pts, 900000u);
  ck_assert_int_eq(g_has_dts, 1);
  ck_assert_uint_eq((unsigned)g_dts, 810000u);

  pes_free(p);
}
END_TEST

START_TEST(pes_has_no_dts_when_pts_only) {
  pes_t *p = pes_new(capture_cb, NULL);
  unsigned char payload[184], pkt[188];

  ck_assert_int_eq(pes_track(p, 0x101), 0);
  build_pes_start_payload(payload, 0xE0, 900000u, 0x00); /* PTS_DTS_flags = '10' */
  wrap_ts_raw(pkt, 0x101, 1, payload);
  g_calls = 0;
  pes_feed(p, pkt);
  pes_flush(p);

  ck_assert_int_eq(g_calls, 1);
  ck_assert_int_eq(g_has_pts, 1);
  ck_assert_int_eq(g_has_dts, 0);

  pes_free(p);
}
END_TEST

START_TEST(pes_ignores_untracked_pid) {
  pes_t *p = pes_new(capture_cb, NULL);
  unsigned char payload[184], pkt[188];

  ck_assert_int_eq(pes_track(p, 0x101), 0);
  build_pes_start_payload(payload, 0xE0, 0, 0);
  wrap_ts_raw(pkt, 0x102, 1, payload); /* different pid, never tracked */
  g_calls = 0;
  pes_feed(p, pkt);
  pes_flush(p);

  ck_assert_int_eq(g_calls, 0);
  pes_free(p);
}
END_TEST

START_TEST(pes_ignores_start_without_valid_startcode) {
  pes_t *p = pes_new(capture_cb, NULL);
  unsigned char payload[184], pkt[188];
  size_t i;

  ck_assert_int_eq(pes_track(p, 0x101), 0);
  for (i = 0; i < 184; i++)
    payload[i] = 0xAB; /* no 00 00 01 prefix */
  wrap_ts_raw(pkt, 0x101, 1, payload);
  g_calls = 0;
  pes_feed(p, pkt);
  pes_flush(p);

  ck_assert_int_eq(g_calls, 0);
  pes_free(p);
}
END_TEST

static Suite *pes_suite(void) {
  Suite *s = suite_create("pes");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, pes_reassembles_across_packets_and_extracts_pts);
  tcase_add_test(tc, pes_extracts_dts_when_present);
  tcase_add_test(tc, pes_has_no_dts_when_pts_only);
  tcase_add_test(tc, pes_ignores_untracked_pid);
  tcase_add_test(tc, pes_ignores_start_without_valid_startcode);
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
