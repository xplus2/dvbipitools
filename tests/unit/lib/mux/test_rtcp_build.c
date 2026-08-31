/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/rtcp.h"
#include "lib/mux/rtcp_build.h"

static int g_nack_calls;
static rtcp_nack_t g_nack;

static void nack_cb(const rtcp_nack_t *n, void *user) {
  (void)user;
  g_nack_calls++;
  g_nack = *n;
}

START_TEST(rtcp_build_ff_round_trips_through_rtcp_parse) {
  unsigned char buf[32];
  rtcp_nack_entry_t entries[2] = {{100, 0x0001}, {200, 0x8000}};
  size_t n = rtcp_build_ff(0x11111111u, 0x22222222u, entries, 2, buf, sizeof buf);

  ck_assert_uint_eq(n, 12u + 2u * 4u);

  g_nack_calls = 0;
  rtcp_parse(buf, n, nack_cb, NULL, NULL, NULL, NULL, NULL, NULL);

  ck_assert_int_eq(g_nack_calls, 1);
  ck_assert_uint_eq(g_nack.sender_ssrc, 0x11111111u);
  ck_assert_uint_eq(g_nack.media_ssrc, 0x22222222u);
  ck_assert_uint_eq(g_nack.entry_count, 2u);
  ck_assert_uint_eq(g_nack.entry[0].pid, 100u);
  ck_assert_uint_eq(g_nack.entry[0].blp, 0x0001u);
  ck_assert_uint_eq(g_nack.entry[1].pid, 200u);
  ck_assert_uint_eq(g_nack.entry[1].blp, 0x8000u);
}
END_TEST

START_TEST(rtcp_build_ff_rejects_zero_entries_and_small_cap) {
  unsigned char buf[32];
  rtcp_nack_entry_t e = {1, 1};
  ck_assert_uint_eq(rtcp_build_ff(1, 1, &e, 0, buf, sizeof buf), 0u);
  ck_assert_uint_eq(rtcp_build_ff(1, 1, &e, 1, buf, 8), 0u);
}
END_TEST

START_TEST(rtcp_build_rsi_header_and_addr_layout) {
  unsigned char buf[32];
  static const unsigned char addr[4] = {192, 168, 1, 1};
  size_t hdr_n, addr_n;

  hdr_n = rtcp_build_rsi_header(0xAAAAAAAAu, 0xBBBBBBBBu, 111, 222, 20 + 8, buf, sizeof buf);
  addr_n = rtcp_build_rsi_srbt_addr(addr, 4, 5000, buf + hdr_n, sizeof(buf) - hdr_n);

  ck_assert_uint_eq(hdr_n, 20u);
  ck_assert_uint_eq(addr_n, 4u + 4u);
  ck_assert_uint_eq(buf[0], 0x80);
  ck_assert_uint_eq(buf[1], 208u); /* RTCP_PT_RSI */
  ck_assert_uint_eq(((unsigned)buf[2] << 8) | buf[3], (20u + 8u) / 4u - 1u); /* length words */
  ck_assert_uint_eq(((unsigned)buf[4] << 24) | ((unsigned)buf[5] << 16) | ((unsigned)buf[6] << 8) | buf[7], 0xAAAAAAAAu);
  ck_assert_uint_eq(((unsigned)buf[8] << 24) | ((unsigned)buf[9] << 16) | ((unsigned)buf[10] << 8) | buf[11], 0xBBBBBBBBu);
  ck_assert_uint_eq(buf[20], 0u); /* SRBT = IPv4 */
  ck_assert_uint_eq(((unsigned)buf[22] << 8) | buf[23], 5000u); /* port */
  ck_assert_mem_eq(buf + 24, addr, 4);
}
END_TEST

START_TEST(rtcp_build_rsi_srbt_addr_rejects_bad_addr_len) {
  unsigned char buf[32], addr[6] = {0};
  ck_assert_uint_eq(rtcp_build_rsi_srbt_addr(addr, 6, 0, buf, sizeof buf), 0u);
}
END_TEST

START_TEST(rtcp_build_rsi_header_rejects_small_cap_or_bad_total) {
  unsigned char buf[32];
  ck_assert_uint_eq(rtcp_build_rsi_header(1, 1, 0, 0, 28, buf, 10), 0u); /* cap too small */
  ck_assert_uint_eq(rtcp_build_rsi_header(1, 1, 0, 0, 10, buf, sizeof buf), 0u); /* total_len < 20 */
  ck_assert_uint_eq(rtcp_build_rsi_header(1, 1, 0, 0, 21, buf, sizeof buf), 0u); /* not a multiple of 4 */
}
END_TEST

START_TEST(rtcp_build_rsi_srbt_dns_layout) {
  unsigned char buf[32];
  size_t n = rtcp_build_rsi_srbt_dns("host.example.com", 17, 5000, buf, sizeof buf);

  ck_assert_uint_eq(n, 4u + 20u); /* 17 + NUL = 18, padded to 20 */
  ck_assert_uint_eq(buf[0], 2u); /* SRBT */
  ck_assert_uint_eq(buf[1], 6u); /* length words: (4+20)/4 */
  ck_assert_uint_eq(((unsigned)buf[2] << 8) | buf[3], 5000u); /* port */
  ck_assert_mem_eq(buf + 4, "host.example.com", 17);
  ck_assert_uint_eq(buf[4 + 17], 0u); /* NUL terminator */
  ck_assert_uint_eq(buf[4 + 19], 0u); /* trailing pad byte */
}
END_TEST

START_TEST(rtcp_build_rsi_srbt_dns_rejects_bad_input) {
  unsigned char buf[300];
  ck_assert_uint_eq(rtcp_build_rsi_srbt_dns("host", 0, 0, buf, sizeof buf), 0u);
  ck_assert_uint_eq(rtcp_build_rsi_srbt_dns("host", 4, 0, buf, 4), 0u); /* cap too small */
}
END_TEST

START_TEST(rtcp_build_rsi_srbt_bandwidth_layout) {
  unsigned char buf[8];
  size_t n = rtcp_build_rsi_srbt_bandwidth(1.5, buf, sizeof buf);
  uint32_t fixed_point = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) | ((uint32_t)buf[6] << 8) | buf[7];

  ck_assert_uint_eq(n, 8u);
  ck_assert_uint_eq(buf[0], 11u); /* SRBT */
  ck_assert_uint_eq(buf[1], 2u);  /* length words */
  ck_assert_uint_eq(((unsigned)buf[2] << 8) | buf[3], 0x4000u); /* S=0,R=1,Reserved=0 */
  ck_assert_uint_eq(fixed_point, (uint32_t)(1.5 * 65536.0));
}
END_TEST

START_TEST(rtcp_build_rsi_srbt_bandwidth_rejects_out_of_range) {
  unsigned char buf[8];
  ck_assert_uint_eq(rtcp_build_rsi_srbt_bandwidth(-1.0, buf, sizeof buf), 0u);
  ck_assert_uint_eq(rtcp_build_rsi_srbt_bandwidth(65536.0, buf, sizeof buf), 0u);
}
END_TEST

START_TEST(rtcp_build_rsi_srbt_collision_layout) {
  unsigned char buf[32];
  uint32_t ssrcs[2] = {0x11111111u, 0x22222222u};
  size_t n = rtcp_build_rsi_srbt_collision(ssrcs, 2, buf, sizeof buf);

  ck_assert_uint_eq(n, 4u + 8u);
  ck_assert_uint_eq(buf[0], 8u); /* SRBT */
  ck_assert_uint_eq(buf[1], 3u); /* length words: 1 + count */
  ck_assert_uint_eq(((unsigned)buf[4] << 24) | ((unsigned)buf[5] << 16) | ((unsigned)buf[6] << 8) | buf[7], 0x11111111u);
  ck_assert_uint_eq(((unsigned)buf[8] << 24) | ((unsigned)buf[9] << 16) | ((unsigned)buf[10] << 8) | buf[11], 0x22222222u);
}
END_TEST

START_TEST(rtcp_build_rsi_srbt_collision_rejects_zero_count) {
  unsigned char buf[32];
  ck_assert_uint_eq(rtcp_build_rsi_srbt_collision(NULL, 0, buf, sizeof buf), 0u);
}
END_TEST

START_TEST(rtcp_build_rams_i_header_with_no_tlvs) {
  unsigned char buf[32];
  size_t n = rtcp_build_rams_i(0x11111111u, 0x22222222u, 7, 1, NULL, buf, sizeof buf);

  ck_assert_uint_eq(n, 16u);
  ck_assert_uint_eq(buf[0], 0x80 | 6); /* V=2, FMT=RAMS */
  ck_assert_uint_eq(buf[1], 205u);     /* RTCP_PT_RTPFB */
  ck_assert_uint_eq(buf[12], 2u);      /* SFMT = RAMS-I */
  ck_assert_uint_eq(buf[13], 7u);      /* msn */
  ck_assert_uint_eq(((unsigned)buf[14] << 8) | buf[15], 1u); /* response */
}
END_TEST

START_TEST(rtcp_build_rams_i_appends_requested_tlvs) {
  unsigned char buf[64];
  rtcp_rams_i_tlvs_t tlvs;
  size_t n;
  memset(&tlvs, 0, sizeof tlvs);
  tlvs.has_media_ssrc_tlv = 1;
  tlvs.media_ssrc_tlv = 0x99999999u;
  tlvs.has_burst_duration = 1;
  tlvs.burst_duration_ms = 5000;

  n = rtcp_build_rams_i(1, 2, 0, 0, &tlvs, buf, sizeof buf);

  ck_assert_uint_eq(n, 16u + 8u + 8u);
  ck_assert_uint_eq(buf[16], 31u); /* type: media SSRC */
  ck_assert_uint_eq(((unsigned)buf[18] << 8) | buf[19], 4u); /* TLV length */
  ck_assert_uint_eq(((unsigned)buf[20] << 24) | ((unsigned)buf[21] << 16) | ((unsigned)buf[22] << 8) | buf[23], 0x99999999u);
  ck_assert_uint_eq(buf[24], 34u); /* type: burst duration */
  ck_assert_uint_eq(((unsigned)buf[28] << 24) | ((unsigned)buf[29] << 16) | ((unsigned)buf[30] << 8) | buf[31], 5000u);
}
END_TEST

static int g_rams_r_calls;
static rtcp_rams_r_t g_rams_r;

static void rams_r_cb(const rtcp_rams_r_t *req, void *user) {
  (void)user;
  g_rams_r_calls++;
  g_rams_r = *req;
}

START_TEST(rtcp_build_rams_r_round_trips_through_rtcp_parse) {
  unsigned char buf[64];
  rtcp_rams_r_t req;
  size_t n;

  memset(&req, 0, sizeof req);
  req.sender_ssrc = 0x11111111u;
  req.media_ssrc = 0x22222222u;
  req.ignore_media_ssrc = 1;
  req.has_min_buffer_fill = 1;
  req.min_buffer_fill_ms = 100;
  req.has_max_buffer_fill = 1;
  req.max_buffer_fill_ms = 2000;
  req.has_max_bitrate = 1;
  req.max_bitrate_bps = 5000000;

  n = rtcp_build_rams_r(&req, buf, sizeof buf);
  ck_assert_uint_gt(n, 0u);

  g_rams_r_calls = 0;
  rtcp_parse(buf, n, NULL, rams_r_cb, NULL, NULL, NULL, NULL, NULL);

  ck_assert_int_eq(g_rams_r_calls, 1);
  ck_assert_uint_eq(g_rams_r.sender_ssrc, 0x11111111u);
  ck_assert_uint_eq(g_rams_r.media_ssrc, 0x22222222u);
  ck_assert_int_eq(g_rams_r.ignore_media_ssrc, 1);
  ck_assert_int_eq(g_rams_r.has_min_buffer_fill, 1);
  ck_assert_uint_eq(g_rams_r.min_buffer_fill_ms, 100u);
  ck_assert_int_eq(g_rams_r.has_max_buffer_fill, 1);
  ck_assert_uint_eq(g_rams_r.max_buffer_fill_ms, 2000u);
  ck_assert_int_eq(g_rams_r.has_max_bitrate, 1);
  ck_assert_uint_eq((unsigned long)g_rams_r.max_bitrate_bps, 5000000u);
}
END_TEST

START_TEST(rtcp_build_rams_r_rejects_small_cap) {
  unsigned char buf[8];
  rtcp_rams_r_t req;
  memset(&req, 0, sizeof req);
  ck_assert_uint_eq(rtcp_build_rams_r(&req, buf, sizeof buf), 0u);
}
END_TEST

static int g_rams_t_calls;
static rtcp_rams_t_t g_rams_t;

static void rams_t_cb(const rtcp_rams_t_t *term, void *user) {
  (void)user;
  g_rams_t_calls++;
  g_rams_t = *term;
}

START_TEST(rtcp_build_rams_t_round_trips_through_rtcp_parse) {
  unsigned char buf[32];
  rtcp_rams_t_t term;
  size_t n;

  memset(&term, 0, sizeof term);
  term.sender_ssrc = 0x33333333u;
  term.media_ssrc = 0x44444444u;
  term.has_first_mc_seqnum = 1;
  term.first_mc_seqnum = 123456;

  n = rtcp_build_rams_t(&term, buf, sizeof buf);
  ck_assert_uint_gt(n, 0u);

  g_rams_t_calls = 0;
  rtcp_parse(buf, n, NULL, NULL, NULL, rams_t_cb, NULL, NULL, NULL);

  ck_assert_int_eq(g_rams_t_calls, 1);
  ck_assert_uint_eq(g_rams_t.sender_ssrc, 0x33333333u);
  ck_assert_uint_eq(g_rams_t.media_ssrc, 0x44444444u);
  ck_assert_int_eq(g_rams_t.has_first_mc_seqnum, 1);
  ck_assert_uint_eq(g_rams_t.first_mc_seqnum, 123456u);
}
END_TEST

START_TEST(rtcp_build_rams_t_rejects_small_cap) {
  unsigned char buf[8];
  rtcp_rams_t_t term;
  memset(&term, 0, sizeof term);
  term.has_first_mc_seqnum = 1;
  ck_assert_uint_eq(rtcp_build_rams_t(&term, buf, sizeof buf), 0u);
}
END_TEST

static int g_rams_i_calls;
static rtcp_rams_i_t g_rams_i;

static void rams_i_cb(const rtcp_rams_i_t *info, void *user) {
  (void)user;
  g_rams_i_calls++;
  g_rams_i = *info;
}

START_TEST(rtcp_build_rams_i_round_trips_through_rtcp_parse) {
  unsigned char buf[64];
  rtcp_rams_i_tlvs_t tlvs;
  size_t n;

  memset(&tlvs, 0, sizeof tlvs);
  tlvs.has_first_packet_seqnum = 1;
  tlvs.first_packet_seqnum = 4242;
  tlvs.has_max_transmit_bitrate = 1;
  tlvs.max_transmit_bitrate_bps = 8000000;

  n = rtcp_build_rams_i(0x55555555u, 0x66666666u, 3, 200, &tlvs, buf, sizeof buf);
  ck_assert_uint_gt(n, 0u);

  g_rams_i_calls = 0;
  rtcp_parse(buf, n, NULL, NULL, rams_i_cb, NULL, NULL, NULL, NULL);

  ck_assert_int_eq(g_rams_i_calls, 1);
  ck_assert_uint_eq(g_rams_i.sender_ssrc, 0x55555555u);
  ck_assert_uint_eq(g_rams_i.media_ssrc, 0x66666666u);
  ck_assert_uint_eq(g_rams_i.msn, 3u);
  ck_assert_uint_eq(g_rams_i.response, 200u);
  ck_assert_int_eq(g_rams_i.has_first_packet_seqnum, 1);
  ck_assert_uint_eq(g_rams_i.first_packet_seqnum, 4242u);
  ck_assert_int_eq(g_rams_i.has_max_transmit_bitrate, 1);
  ck_assert_uint_eq((unsigned long)g_rams_i.max_transmit_bitrate_bps, 8000000u);
}
END_TEST

static Suite *rtcp_build_suite(void) {
  Suite *s = suite_create("rtcp_build");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, rtcp_build_ff_round_trips_through_rtcp_parse);
  tcase_add_test(tc, rtcp_build_ff_rejects_zero_entries_and_small_cap);
  tcase_add_test(tc, rtcp_build_rsi_header_and_addr_layout);
  tcase_add_test(tc, rtcp_build_rsi_srbt_addr_rejects_bad_addr_len);
  tcase_add_test(tc, rtcp_build_rsi_header_rejects_small_cap_or_bad_total);
  tcase_add_test(tc, rtcp_build_rsi_srbt_dns_layout);
  tcase_add_test(tc, rtcp_build_rsi_srbt_dns_rejects_bad_input);
  tcase_add_test(tc, rtcp_build_rsi_srbt_bandwidth_layout);
  tcase_add_test(tc, rtcp_build_rsi_srbt_bandwidth_rejects_out_of_range);
  tcase_add_test(tc, rtcp_build_rsi_srbt_collision_layout);
  tcase_add_test(tc, rtcp_build_rsi_srbt_collision_rejects_zero_count);
  tcase_add_test(tc, rtcp_build_rams_i_header_with_no_tlvs);
  tcase_add_test(tc, rtcp_build_rams_i_appends_requested_tlvs);
  tcase_add_test(tc, rtcp_build_rams_r_round_trips_through_rtcp_parse);
  tcase_add_test(tc, rtcp_build_rams_r_rejects_small_cap);
  tcase_add_test(tc, rtcp_build_rams_t_round_trips_through_rtcp_parse);
  tcase_add_test(tc, rtcp_build_rams_t_rejects_small_cap);
  tcase_add_test(tc, rtcp_build_rams_i_round_trips_through_rtcp_parse);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(rtcp_build_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
