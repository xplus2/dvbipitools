/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/rtcp.h"
#include "lib/mux/rtcp_build.h"

#define RTCP_FMT_RAMS 6

static void wr32(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)(v >> 24);
  p[1] = (unsigned char)(v >> 16);
  p[2] = (unsigned char)(v >> 8);
  p[3] = (unsigned char)v;
}

static void wr16(unsigned char *p, uint16_t v) {
  p[0] = (unsigned char)(v >> 8);
  p[1] = (unsigned char)v;
}

/* RAMS header (16 bytes) + fci, fci_len must be a multiple of 4. returns total bytes written */
static size_t build_rams(unsigned char *out, unsigned sfmt, uint32_t sender_ssrc, uint32_t media_ssrc, const unsigned char *fci, size_t fci_len) {
  size_t total = 16 + fci_len;
  uint16_t words = (uint16_t)(total / 4 - 1);
  out[0] = (unsigned char)((2 << 6) | RTCP_FMT_RAMS);
  out[1] = 205; /* RTPFB */
  wr16(out + 2, words);
  wr32(out + 4, sender_ssrc);
  wr32(out + 8, media_ssrc);
  out[12] = (unsigned char)sfmt;
  out[13] = out[14] = out[15] = 0;
  if (fci_len)
    memcpy(out + 16, fci, fci_len);
  return total;
}

static rtcp_nack_t g_nack;
static int g_nack_calls;
static void nack_cb(const rtcp_nack_t *n, void *user) {
  (void)user;
  g_nack = *n;
  g_nack_calls++;
}

static rtcp_rams_r_t g_rams_r;
static int g_rams_r_calls;
static void rams_r_cb(const rtcp_rams_r_t *r, void *user) {
  (void)user;
  g_rams_r = *r;
  g_rams_r_calls++;
}

static rtcp_rams_t_t g_rams_t;
static int g_rams_t_calls;
static void rams_t_cb(const rtcp_rams_t_t *t, void *user) {
  (void)user;
  g_rams_t = *t;
  g_rams_t_calls++;
}

static unsigned g_malformed_sfmt;
static uint32_t g_malformed_sender_ssrc, g_malformed_media_ssrc;
static int g_malformed_calls;
static void malformed_cb(unsigned sfmt, uint32_t sender_ssrc, uint32_t media_ssrc, void *user) {
  (void)user;
  g_malformed_sfmt = sfmt;
  g_malformed_sender_ssrc = sender_ssrc;
  g_malformed_media_ssrc = media_ssrc;
  g_malformed_calls++;
}

static void reset_counters(void) {
  g_nack_calls = 0;
  g_rams_r_calls = 0;
  g_rams_t_calls = 0;
  g_malformed_calls = 0;
}

START_TEST(nack_is_parsed_via_builder_round_trip) {
  unsigned char buf[64];
  rtcp_nack_entry_t entries[1] = {{0x1234, 0}};
  size_t n = rtcp_build_ff(0xAAAA, 0xBBBB, entries, 1, buf, sizeof buf);

  reset_counters();
  ck_assert_uint_gt(n, 0u);
  rtcp_parse(buf, n, nack_cb, NULL, NULL, NULL, NULL, NULL, NULL);
  ck_assert_int_eq(g_nack_calls, 1);
  ck_assert_uint_eq(g_nack.sender_ssrc, 0xAAAAu);
  ck_assert_uint_eq(g_nack.media_ssrc, 0xBBBBu);
  ck_assert_uint_eq(g_nack.entry_count, 1u);
  ck_assert_uint_eq(g_nack.entry[0].pid, 0x1234u);
}
END_TEST

START_TEST(rams_r_ignore_media_ssrc_flag_is_parsed) {
  unsigned char pkt[64];
  unsigned char fci[4] = {1, 0, 0, 0}; /* type=1 (ignore media ssrc), length=0 */
  size_t n = build_rams(pkt, RTCP_SFMT_RAMS_R, 0x1111, 0x2222, fci, sizeof fci);

  reset_counters();
  rtcp_parse(pkt, n, NULL, rams_r_cb, NULL, NULL, NULL, NULL, NULL);
  ck_assert_int_eq(g_rams_r_calls, 1);
  ck_assert_int_eq(g_rams_r.ignore_media_ssrc, 1);
  ck_assert_uint_eq(g_rams_r.sender_ssrc, 0x1111u);
  ck_assert_uint_eq(g_rams_r.media_ssrc, 0x2222u);
}
END_TEST

START_TEST(rams_t_with_no_tlv_is_parsed) {
  unsigned char pkt[64];
  size_t n = build_rams(pkt, RTCP_SFMT_RAMS_T, 0x3333, 0x4444, NULL, 0);

  reset_counters();
  rtcp_parse(pkt, n, NULL, NULL, NULL, rams_t_cb, NULL, NULL, NULL);
  ck_assert_int_eq(g_rams_t_calls, 1);
  ck_assert_int_eq(g_rams_t.has_first_mc_seqnum, 0);
  ck_assert_int_eq(g_malformed_calls, 0);
}
END_TEST

START_TEST(rams_t_with_seqnum_tlv_is_parsed) {
  unsigned char pkt[64];
  unsigned char fci[8];
  fci[0] = 61; /* type: extended seqnum of first mc packet */
  fci[1] = 0;
  wr16(fci + 2, 4); /* length 4 */
  wr32(fci + 4, 0xDEADBEEFu);
  size_t n = build_rams(pkt, RTCP_SFMT_RAMS_T, 0x5555, 0x6666, fci, sizeof fci);

  reset_counters();
  rtcp_parse(pkt, n, NULL, NULL, NULL, rams_t_cb, NULL, malformed_cb, NULL);
  ck_assert_int_eq(g_rams_t_calls, 1);
  ck_assert_int_eq(g_rams_t.has_first_mc_seqnum, 1);
  ck_assert_uint_eq(g_rams_t.first_mc_seqnum, 0xDEADBEEFu);
  ck_assert_int_eq(g_malformed_calls, 0);
}
END_TEST

START_TEST(malformed_rams_t_tlv_reports_via_malformed_cb_and_still_delivers_partial) {
  unsigned char pkt[64];
  unsigned char fci[4];
  size_t n;

  fci[0] = 61;
  fci[1] = 0;
  wr16(fci + 2, 100); /* claims 100 bytes of value, only 0 follow: malformed */
  n = build_rams(pkt, RTCP_SFMT_RAMS_T, 0x7777, 0x8888, fci, sizeof fci);

  reset_counters();
  rtcp_parse(pkt, n, NULL, NULL, NULL, rams_t_cb, NULL, malformed_cb, NULL);
  ck_assert_int_eq(g_rams_t_calls, 1); /* still delivered, no seqnum found before corruption */
  ck_assert_int_eq(g_rams_t.has_first_mc_seqnum, 0);
  ck_assert_int_eq(g_malformed_calls, 1);
  ck_assert_uint_eq(g_malformed_sfmt, RTCP_SFMT_RAMS_T);
  ck_assert_uint_eq(g_malformed_sender_ssrc, 0x7777u);
  ck_assert_uint_eq(g_malformed_media_ssrc, 0x8888u);
}
END_TEST

START_TEST(malformed_cb_not_called_for_well_formed_rams_t) {
  unsigned char pkt[64];
  size_t n = build_rams(pkt, RTCP_SFMT_RAMS_T, 0x9999, 0xAAAA, NULL, 0);

  reset_counters();
  rtcp_parse(pkt, n, NULL, NULL, NULL, rams_t_cb, NULL, malformed_cb, NULL);
  ck_assert_int_eq(g_malformed_calls, 0);
}
END_TEST

START_TEST(malformed_cb_not_called_for_rams_r) {
  unsigned char pkt[64];
  unsigned char fci[4];
  size_t n;

  fci[0] = 2; /* min buffer fill */
  fci[1] = 0;
  wr16(fci + 2, 100); /* also malformed, but RAMS-R never reports via malformed_cb (400 handled elsewhere) */
  n = build_rams(pkt, RTCP_SFMT_RAMS_R, 0xBBBB, 0xCCCC, fci, sizeof fci);

  reset_counters();
  rtcp_parse(pkt, n, NULL, rams_r_cb, NULL, NULL, NULL, malformed_cb, NULL);
  ck_assert_int_eq(g_malformed_calls, 0);
  ck_assert_int_eq(g_rams_r_calls, 1);
}
END_TEST

START_TEST(both_cb_and_malformed_cb_null_does_not_crash) {
  unsigned char pkt[64];
  size_t n = build_rams(pkt, RTCP_SFMT_RAMS_T, 1, 2, NULL, 0);
  rtcp_parse(pkt, n, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
}
END_TEST

static int g_rams_i_calls;
static rtcp_rams_i_t g_rams_i;

static void rams_i_cb(const rtcp_rams_i_t *info, void *user) {
  (void)user;
  g_rams_i_calls++;
  g_rams_i = *info;
}

START_TEST(rams_i_is_parsed_via_builder_round_trip) {
  unsigned char buf[64];
  rtcp_rams_i_tlvs_t tlvs;
  size_t n;

  memset(&tlvs, 0, sizeof tlvs);
  tlvs.has_earliest_join_time = 1;
  tlvs.earliest_join_time_ms = 0;
  tlvs.has_burst_duration = 1;
  tlvs.burst_duration_ms = 8000;

  n = rtcp_build_rams_i(0x77777777u, 0x88888888u, 5, 200, &tlvs, buf, sizeof buf);
  ck_assert_uint_gt(n, 0u);

  g_rams_i_calls = 0;
  rtcp_parse(buf, n, NULL, NULL, rams_i_cb, NULL, NULL, NULL, NULL);

  ck_assert_int_eq(g_rams_i_calls, 1);
  ck_assert_uint_eq(g_rams_i.sender_ssrc, 0x77777777u);
  ck_assert_uint_eq(g_rams_i.media_ssrc, 0x88888888u);
  ck_assert_uint_eq(g_rams_i.msn, 5u);
  ck_assert_uint_eq(g_rams_i.response, 200u);
  ck_assert_int_eq(g_rams_i.has_earliest_join_time, 1);
  ck_assert_int_eq(g_rams_i.has_burst_duration, 1);
  ck_assert_uint_eq(g_rams_i.burst_duration_ms, 8000u);
}
END_TEST

static Suite *rtcp_suite(void) {
  Suite *s = suite_create("rtcp");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, nack_is_parsed_via_builder_round_trip);
  tcase_add_test(tc, rams_r_ignore_media_ssrc_flag_is_parsed);
  tcase_add_test(tc, rams_t_with_no_tlv_is_parsed);
  tcase_add_test(tc, rams_t_with_seqnum_tlv_is_parsed);
  tcase_add_test(tc, malformed_rams_t_tlv_reports_via_malformed_cb_and_still_delivers_partial);
  tcase_add_test(tc, malformed_cb_not_called_for_well_formed_rams_t);
  tcase_add_test(tc, malformed_cb_not_called_for_rams_r);
  tcase_add_test(tc, both_cb_and_malformed_cb_null_does_not_crash);
  tcase_add_test(tc, rams_i_is_parsed_via_builder_round_trip);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(rtcp_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
