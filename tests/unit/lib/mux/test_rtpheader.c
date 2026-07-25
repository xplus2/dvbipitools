/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>

#include "lib/demux/rtp.h"
#include "lib/mux/rtpheader.h"

START_TEST(rtpheader_build_is_v2_pt33_and_parseable) {
  rtpheader_t *r = rtpheader_new();
  unsigned char buf[12];
  rtp_hdr_t h;

  ck_assert_uint_eq(rtpheader_build(r, 900000u, buf, sizeof buf), 12u);
  ck_assert_int_eq(rtp_parse_header(buf, sizeof buf, &h), 1);
  ck_assert_uint_eq(h.pt, 33u);
  ck_assert_uint_eq(h.timestamp, 900000u);
  ck_assert_uint_eq(h.payload_off, 12u);

  rtpheader_free(r);
}
END_TEST

START_TEST(rtpheader_build_increments_seq_and_keeps_ssrc) {
  rtpheader_t *r = rtpheader_new();
  unsigned char b1[12], b2[12];
  rtp_hdr_t h1, h2;

  ck_assert_uint_eq(rtpheader_build(r, 0, b1, sizeof b1), 12u);
  ck_assert_uint_eq(rtpheader_build(r, 90000, b2, sizeof b2), 12u);
  rtp_parse_header(b1, sizeof b1, &h1);
  rtp_parse_header(b2, sizeof b2, &h2);

  ck_assert_uint_eq((uint16_t)(h2.seq - h1.seq), 1u);
  ck_assert_uint_eq(h1.ssrc, h2.ssrc);

  rtpheader_free(r);
}
END_TEST

START_TEST(rtpheader_build_rejects_small_cap) {
  rtpheader_t *r = rtpheader_new();
  unsigned char buf[11];
  ck_assert_uint_eq(rtpheader_build(r, 0, buf, sizeof buf), 0u);
  rtpheader_free(r);
}
END_TEST

static Suite *rtpheader_suite(void) {
  Suite *s = suite_create("rtpheader");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, rtpheader_build_is_v2_pt33_and_parseable);
  tcase_add_test(tc, rtpheader_build_increments_seq_and_keeps_ssrc);
  tcase_add_test(tc, rtpheader_build_rejects_small_cap);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(rtpheader_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
