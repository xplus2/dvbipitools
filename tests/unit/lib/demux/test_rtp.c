/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/rtp.h"

/* builds an RTP header (no CSRC content, no real extension payload beyond
 * the 4-byte extension header itself), returns header length */
static size_t build_rtp_header(unsigned char *buf, unsigned cc, int ext, unsigned pt, int marker, uint16_t seq, uint32_t ts, uint32_t ssrc) {
  size_t off, k;

  buf[0] = (unsigned char)(0x80 | (cc & 0x0F));
  if (ext)
    buf[0] |= 0x10;
  buf[1] = (unsigned char)((marker ? 0x80 : 0x00) | (pt & 0x7F));
  buf[2] = (unsigned char)(seq >> 8);
  buf[3] = (unsigned char)seq;
  buf[4] = (unsigned char)(ts >> 24);
  buf[5] = (unsigned char)(ts >> 16);
  buf[6] = (unsigned char)(ts >> 8);
  buf[7] = (unsigned char)ts;
  buf[8] = (unsigned char)(ssrc >> 24);
  buf[9] = (unsigned char)(ssrc >> 16);
  buf[10] = (unsigned char)(ssrc >> 8);
  buf[11] = (unsigned char)ssrc;
  off = 12;
  for (k = 0; k < cc; k++) {
    buf[off + k * 4 + 0] = 0;
    buf[off + k * 4 + 1] = 0;
    buf[off + k * 4 + 2] = 0;
    buf[off + k * 4 + 3] = 0;
  }
  off += (size_t)cc * 4;
  if (ext) {
    buf[off + 0] = 0;
    buf[off + 1] = 0;
    buf[off + 2] = 0; /* extension length, in 32-bit words */
    buf[off + 3] = 0;
    off += 4;
  }
  return off;
}

START_TEST(rtp_payload_offset_plain_header) {
  unsigned char pkt[32];
  size_t off = build_rtp_header(pkt, 0, 0, 33, 0, 1, 0, 0);
  pkt[off] = 0x47;
  ck_assert_uint_eq(rtp_payload_offset(pkt, off + 1), off);
}
END_TEST

START_TEST(rtp_payload_offset_with_csrc_and_extension) {
  unsigned char pkt[32];
  size_t off = build_rtp_header(pkt, 1, 1, 33, 0, 1, 0, 0);
  pkt[off] = 0x47;
  ck_assert_uint_eq(rtp_payload_offset(pkt, off + 1), off);
}
END_TEST

START_TEST(rtp_payload_offset_rejects_bad_version) {
  unsigned char pkt[32];
  size_t off = build_rtp_header(pkt, 0, 0, 33, 0, 1, 0, 0);
  pkt[0] = 0x40; /* version 1 */
  pkt[off] = 0x47;
  ck_assert_uint_eq(rtp_payload_offset(pkt, off + 1), 0u);
}
END_TEST

START_TEST(rtp_payload_offset_rejects_non_ts_payload) {
  unsigned char pkt[32];
  size_t off = build_rtp_header(pkt, 0, 0, 33, 0, 1, 0, 0);
  pkt[off] = 0x00;
  ck_assert_uint_eq(rtp_payload_offset(pkt, off + 1), 0u);
}
END_TEST

START_TEST(rtp_payload_offset_rejects_short_packet) {
  unsigned char pkt[12] = {0x80, 33, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  ck_assert_uint_eq(rtp_payload_offset(pkt, 11), 0u);
}
END_TEST

START_TEST(rtp_parse_header_fills_fields) {
  unsigned char pkt[32];
  rtp_hdr_t h;
  size_t off = build_rtp_header(pkt, 0, 0, 96, 1, 0x1234, 0xAABBCCDDu, 0x11223344u);

  ck_assert_int_eq(rtp_parse_header(pkt, off, &h), 1);
  ck_assert_uint_eq(h.pt, 96u); /* marker bit masked off */
  ck_assert_uint_eq(h.seq, 0x1234u);
  ck_assert_uint_eq(h.timestamp, 0xAABBCCDDu);
  ck_assert_uint_eq(h.ssrc, 0x11223344u);
  ck_assert_uint_eq(h.payload_off, off);
}
END_TEST

START_TEST(rtp_parse_header_rejects_bad_version_and_short_input) {
  unsigned char pkt[12] = {0x40, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  rtp_hdr_t h;
  ck_assert_int_eq(rtp_parse_header(pkt, sizeof pkt, &h), 0);
  pkt[0] = 0x80;
  ck_assert_int_eq(rtp_parse_header(pkt, 11, &h), 0);
}
END_TEST

static Suite *rtp_suite(void) {
  Suite *s = suite_create("rtp");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, rtp_payload_offset_plain_header);
  tcase_add_test(tc, rtp_payload_offset_with_csrc_and_extension);
  tcase_add_test(tc, rtp_payload_offset_rejects_bad_version);
  tcase_add_test(tc, rtp_payload_offset_rejects_non_ts_payload);
  tcase_add_test(tc, rtp_payload_offset_rejects_short_packet);
  tcase_add_test(tc, rtp_parse_header_fills_fields);
  tcase_add_test(tc, rtp_parse_header_rejects_bad_version_and_short_input);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(rtp_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
