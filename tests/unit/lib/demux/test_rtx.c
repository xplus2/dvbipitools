/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/rtx.h"

/* plain 12-byte RTP header, no CSRC/extension */
static void build_rtp_header(unsigned char *buf, unsigned pt, uint32_t ssrc) {
  buf[0] = 0x80;
  buf[1] = (unsigned char)(pt & 0x7F);
  buf[2] = 0;
  buf[3] = 0;
  buf[4] = 0;
  buf[5] = 0;
  buf[6] = 0;
  buf[7] = 0;
  buf[8] = (unsigned char)(ssrc >> 24);
  buf[9] = (unsigned char)(ssrc >> 16);
  buf[10] = (unsigned char)(ssrc >> 8);
  buf[11] = (unsigned char)ssrc;
}

START_TEST(rtx_parse_extracts_osn_and_payload) {
  unsigned char pkt[32];
  rtx_pkt_t out;

  build_rtp_header(pkt, 99, 0x11223344u);
  pkt[12] = 0x00; /* OSN hi */
  pkt[13] = 0x2A; /* OSN lo -> 42 */
  pkt[14] = 0xDE;
  pkt[15] = 0xAD;

  ck_assert_int_eq(rtx_parse(pkt, 16, 99, &out), 1);
  ck_assert_uint_eq(out.ssrc, 0x11223344u);
  ck_assert_uint_eq(out.osn, 42u);
  ck_assert_uint_eq(out.payload_len, 2u);
  ck_assert_mem_eq(out.payload, pkt + 14, 2);
}
END_TEST

START_TEST(rtx_parse_rejects_payload_type_mismatch) {
  unsigned char pkt[16];
  rtx_pkt_t out;
  build_rtp_header(pkt, 99, 0x11223344u);
  pkt[12] = 0x00;
  pkt[13] = 0x2A;
  pkt[14] = 0xDE;
  pkt[15] = 0xAD;
  ck_assert_int_eq(rtx_parse(pkt, sizeof pkt, 100, &out), 0);
}
END_TEST

START_TEST(rtx_parse_rejects_missing_osn) {
  unsigned char pkt[13];
  rtx_pkt_t out;
  build_rtp_header(pkt, 99, 0x11223344u);
  ck_assert_int_eq(rtx_parse(pkt, sizeof pkt, 99, &out), 0);
}
END_TEST

static Suite *rtx_suite(void) {
  Suite *s = suite_create("rtx");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, rtx_parse_extracts_osn_and_payload);
  tcase_add_test(tc, rtx_parse_rejects_payload_type_mismatch);
  tcase_add_test(tc, rtx_parse_rejects_missing_osn);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(rtx_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
