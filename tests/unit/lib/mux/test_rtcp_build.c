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
  rtcp_parse(buf, n, nack_cb, NULL, NULL, NULL);

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

START_TEST(rtcp_build_rsi_addr_ipv4_layout) {
  unsigned char buf[32];
  static const unsigned char addr[4] = {192, 168, 1, 1};
  size_t n = rtcp_build_rsi_addr(0xAAAAAAAAu, 0xBBBBBBBBu, 111, 222, 5000, addr, 4, buf, sizeof buf);

  ck_assert_uint_eq(n, 20u + 4u + 4u);
  ck_assert_uint_eq(buf[0], 0x80);
  ck_assert_uint_eq(buf[1], 208u); /* RTCP_PT_RSI */
  ck_assert_uint_eq(buf[20], 0u); /* SRBT = IPv4 */
  ck_assert_uint_eq(((unsigned)buf[22] << 8) | buf[23], 5000u); /* port */
  ck_assert_mem_eq(buf + 24, addr, 4);
}
END_TEST

START_TEST(rtcp_build_rsi_addr_rejects_bad_addr_len) {
  unsigned char buf[32], addr[6] = {0};
  ck_assert_uint_eq(rtcp_build_rsi_addr(1, 1, 0, 0, 0, addr, 6, buf, sizeof buf), 0u);
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

static Suite *rtcp_build_suite(void) {
  Suite *s = suite_create("rtcp_build");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, rtcp_build_ff_round_trips_through_rtcp_parse);
  tcase_add_test(tc, rtcp_build_ff_rejects_zero_entries_and_small_cap);
  tcase_add_test(tc, rtcp_build_rsi_addr_ipv4_layout);
  tcase_add_test(tc, rtcp_build_rsi_addr_rejects_bad_addr_len);
  tcase_add_test(tc, rtcp_build_rams_i_header_with_no_tlvs);
  tcase_add_test(tc, rtcp_build_rams_i_appends_requested_tlvs);
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
