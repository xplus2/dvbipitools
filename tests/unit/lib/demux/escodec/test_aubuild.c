/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/escodec/aubuild.h"

/* payload_type/size use ff_byte extension coding */
static size_t build_sei_nal(unsigned char *out, unsigned hdr, unsigned type1, const unsigned char *pl1, size_t pl1len, unsigned type2, const unsigned char *pl2, size_t pl2len) {
  size_t n = 0;
  for (unsigned i = 0; i < hdr; i++) out[n++] = 0x00;
  out[n++] = (unsigned char)type1;
  out[n++] = (unsigned char)pl1len;
  memcpy(out + n, pl1, pl1len);
  n += pl1len;
  if (pl2) {
    out[n++] = (unsigned char)type2;
    out[n++] = (unsigned char)pl2len;
    memcpy(out + n, pl2, pl2len);
    n += pl2len;
  }
  out[n++] = 0x80;
  return n;
}

START_TEST(non_lcevc_sei_passes_through_unchanged) {
  unsigned char nal[64];
  static const unsigned char pl[] = {0x01, 0x02, 0x03};
  size_t n = build_sei_nal(nal, 1, 5, pl, sizeof pl, 0, NULL, 0);
  unsigned char *rb = NULL;
  unsigned char *esc = NULL;
  size_t rbcap = 0;
  size_t esccap = 0;
  size_t outlen;
  int r = esc_strip_lcevc_sei(nal, n, 1, &rb, &rbcap, &esc, &esccap, &outlen);
  ck_assert_int_eq(r, 0);
  free(rb);
  free(esc);
}
END_TEST

START_TEST(lcevc_only_message_drops_whole_nal) {
  unsigned char nal[64];
  unsigned char t35[] = {0xB4, 0x00, 0x50, 0x00, 0xBB};
  size_t n = build_sei_nal(nal, 1, 4, t35, sizeof t35, 0, NULL, 0);
  unsigned char *rb = NULL;
  unsigned char *esc = NULL;
  size_t rbcap = 0;
  size_t esccap = 0;
  size_t outlen = 123;
  int r = esc_strip_lcevc_sei(nal, n, 1, &rb, &rbcap, &esc, &esccap, &outlen);
  ck_assert_int_eq(r, 1);
  ck_assert_uint_eq(outlen, 0u);
  free(rb);
  free(esc);
}
END_TEST

START_TEST(lcevc_message_removed_other_message_kept) {
  unsigned char nal[64];
  static const unsigned char other[] = {0x11, 0x22, 0x33};
  unsigned char t35[] = {0xB4, 0x00, 0x50, 0x00};
  size_t n = build_sei_nal(nal, 1, 5, other, sizeof other, 4, t35, sizeof t35);
  unsigned char *rb = NULL;
  unsigned char *esc = NULL;
  size_t rbcap = 0;
  size_t esccap = 0;
  size_t outlen;
  int r = esc_strip_lcevc_sei(nal, n, 1, &rb, &rbcap, &esc, &esccap, &outlen);
  ck_assert_int_eq(r, 1);
  ck_assert_uint_ge(outlen, 1u);
  ck_assert_uint_eq(esc[0], 0x00); /* hdrlen byte preserved */
  ck_assert(memmem(esc, outlen, other, sizeof other) != NULL);
  ck_assert(memmem(esc, outlen, t35, sizeof t35) == NULL);
  free(rb);
  free(esc);
}
END_TEST

START_TEST(t35_with_wrong_provider_is_not_lcevc) {
  unsigned char nal[64];
  unsigned char not_lcevc[] = {0xB4, 0x11, 0x22, 0xAA};
  size_t n = build_sei_nal(nal, 1, 4, not_lcevc, sizeof not_lcevc, 0, NULL, 0);
  unsigned char *rb = NULL;
  unsigned char *esc = NULL;
  size_t rbcap = 0;
  size_t esccap = 0;
  size_t outlen;
  int r = esc_strip_lcevc_sei(nal, n, 1, &rb, &rbcap, &esc, &esccap, &outlen);
  ck_assert_int_eq(r, 0);
  free(rb);
  free(esc);
}
END_TEST

START_TEST(three_byte_prefix_is_not_lcevc) {
  unsigned char nal[64];
  unsigned char not_lcevc[] = {0xB4, 0x50, 0x00};
  size_t n = build_sei_nal(nal, 1, 4, not_lcevc, sizeof not_lcevc, 0, NULL, 0);
  unsigned char *rb = NULL;
  unsigned char *esc = NULL;
  size_t rbcap = 0;
  size_t esccap = 0;
  size_t outlen;
  int r = esc_strip_lcevc_sei(nal, n, 1, &rb, &rbcap, &esc, &esccap, &outlen);
  ck_assert_int_eq(r, 0);
  free(rb);
  free(esc);
}
END_TEST

START_TEST(hevc_two_byte_header_preserved) {
  unsigned char nal[64];
  static const unsigned char other[] = {0x11, 0x22};
  unsigned char t35[] = {0xB4, 0x00, 0x50, 0x00};
  size_t n = build_sei_nal(nal, 2, 5, other, sizeof other, 4, t35, sizeof t35);
  unsigned char *rb = NULL;
  unsigned char *esc = NULL;
  size_t rbcap = 0;
  size_t esccap = 0;
  size_t outlen;
  int r = esc_strip_lcevc_sei(nal, n, 2, &rb, &rbcap, &esc, &esccap, &outlen);
  ck_assert_int_eq(r, 1);
  ck_assert_uint_ge(outlen, 2u);
  ck_assert_uint_eq(esc[0], 0x00);
  ck_assert_uint_eq(esc[1], 0x00);
  free(rb);
  free(esc);
}
END_TEST

START_TEST(emulation_prevention_in_surviving_message_roundtrips) {
  unsigned char nal[64];
  static const unsigned char other[] = {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x02};
  unsigned char t35[] = {0xB4, 0x00, 0x50, 0x00};
  size_t n = build_sei_nal(nal, 1, 5, other, sizeof other, 4, t35, sizeof t35);
  unsigned char *rb = NULL;
  unsigned char *esc = NULL;
  size_t rbcap = 0;
  size_t esccap = 0;
  size_t outlen;
  int r = esc_strip_lcevc_sei(nal, n, 1, &rb, &rbcap, &esc, &esccap, &outlen);
  ck_assert_int_eq(r, 1);
  ck_assert(memmem(esc, outlen, "\x05\x07\x00\x00\x03\x00\x01\x00\x00\x03\x02", 11) != NULL);
  free(rb);
  free(esc);
}
END_TEST

START_TEST(truncated_nal_is_unchanged) {
  static const unsigned char nal[] = {0x00};
  unsigned char *rb = NULL;
  unsigned char *esc = NULL;
  size_t rbcap = 0;
  size_t esccap = 0;
  size_t outlen;
  int r = esc_strip_lcevc_sei(nal, sizeof nal, 1, &rb, &rbcap, &esc, &esccap, &outlen);
  ck_assert_int_eq(r, 0);
  free(rb);
  free(esc);
}
END_TEST

START_TEST(reused_buffers_grow_once_then_stay_across_calls) {
  unsigned char nal[64];
  static const unsigned char other[] = {0x11, 0x22, 0x33};
  unsigned char t35[] = {0xB4, 0x00, 0x50, 0x00};
  size_t n = build_sei_nal(nal, 1, 5, other, sizeof other, 4, t35, sizeof t35);
  unsigned char *rb = NULL;
  unsigned char *esc = NULL;
  size_t rbcap = 0;
  size_t esccap = 0;
  size_t outlen;
  unsigned char *rb_after_first;
  unsigned char *esc_after_first;
  int r = esc_strip_lcevc_sei(nal, n, 1, &rb, &rbcap, &esc, &esccap, &outlen);
  ck_assert_int_eq(r, 1);
  rb_after_first = rb;
  esc_after_first = esc;
  r = esc_strip_lcevc_sei(nal, n, 1, &rb, &rbcap, &esc, &esccap, &outlen);
  ck_assert_int_eq(r, 1);
  ck_assert_ptr_eq(rb, rb_after_first);
  ck_assert_ptr_eq(esc, esc_after_first);
  free(rb);
  free(esc);
}
END_TEST

START_TEST(esc_handle_h264_nal_strips_lcevc_sei_when_enabled) {
  unsigned char nal[64];
  unsigned char t35[] = {0xB4, 0x00, 0x50, 0x00};
  size_t n = build_sei_nal(nal, 1, 4, t35, sizeof t35, 0, NULL, 0);
  esc_track_t es;
  unsigned char *vbuf = NULL;
  unsigned char *rb = NULL;
  unsigned char *esc = NULL;
  size_t vbuflen = 0;
  size_t vbufcap = 0;
  size_t rbcap = 0;
  size_t esccap = 0;
  lcevc_strip_t strip;
  int key = 0;
  memset(&es, 0, sizeof es);
  strip.rb = &rb;
  strip.rbcap = &rbcap;
  strip.esc = &esc;
  strip.esccap = &esccap;
  esc_handle_h264_nal(&es, &vbuf, &vbuflen, &vbufcap, H264_NAL_SEI, nal, n, &key, &strip);
  ck_assert_uint_eq(vbuflen, 0);
  free(vbuf);
  free(rb);
  free(esc);
}
END_TEST

START_TEST(esc_handle_h264_nal_keeps_sei_when_strip_disabled) {
  unsigned char nal[64];
  unsigned char t35[] = {0xB4, 0x00, 0x50, 0x00};
  size_t n = build_sei_nal(nal, 1, 4, t35, sizeof t35, 0, NULL, 0);
  esc_track_t es;
  unsigned char *vbuf = NULL;
  size_t vbuflen = 0;
  size_t vbufcap = 0;
  int key = 0;
  memset(&es, 0, sizeof es);
  esc_handle_h264_nal(&es, &vbuf, &vbuflen, &vbufcap, H264_NAL_SEI, nal, n, &key, NULL);
  ck_assert_uint_gt(vbuflen, 0);
  free(vbuf);
}
END_TEST

START_TEST(esc_handle_h264_nal_drops_dedicated_lcevc_nal_when_stripping) {
  static const unsigned char nal[] = {0x00, 0xAA, 0xBB, 0xCC};
  esc_track_t es;
  unsigned char *vbuf = NULL;
  unsigned char *rb = NULL;
  unsigned char *esc = NULL;
  size_t vbuflen = 0;
  size_t vbufcap = 0;
  size_t rbcap = 0;
  size_t esccap = 0;
  lcevc_strip_t strip;
  int key = 0;
  memset(&es, 0, sizeof es);
  strip.rb = &rb;
  strip.rbcap = &rbcap;
  strip.esc = &esc;
  strip.esccap = &esccap;

  esc_handle_h264_nal(&es, &vbuf, &vbuflen, &vbufcap, H264_NAL_LCEVC_IDR, nal, sizeof nal, &key, &strip);
  ck_assert_uint_eq(vbuflen, 0);

  esc_handle_h264_nal(&es, &vbuf, &vbuflen, &vbufcap, H264_NAL_LCEVC_NON_IDR, nal, sizeof nal, &key, NULL);
  ck_assert_uint_gt(vbuflen, 0);

  free(vbuf);
  free(rb);
  free(esc);
}
END_TEST

START_TEST(esc_split_nals_strips_lcevc_end_to_end) {
  unsigned char buf[128];
  size_t n = 0;
  unsigned char t35[] = {0xB4, 0x00, 0x50, 0x00};
  size_t sei_len;
  esc_track_t es;
  unsigned char *vbuf = NULL;
  unsigned char *rb = NULL;
  unsigned char *esc = NULL;
  size_t vbuflen = 0;
  size_t vbufcap = 0;
  size_t rbcap = 0;
  size_t esccap = 0;
  lcevc_strip_t strip;
  int key = 0;
  memset(&es, 0, sizeof es);
  es.codec = CODEC_H264;
  strip.rb = &rb;
  strip.rbcap = &rbcap;
  strip.esc = &esc;
  strip.esccap = &esccap;

  buf[n++] = 0x00; buf[n++] = 0x00; buf[n++] = 0x00; buf[n++] = 0x01; /* start code */
  buf[n++] = 0x65; buf[n++] = 0xAA; buf[n++] = 0xBB; /* fake IDR slice, type 5 */
  buf[n++] = 0x00; buf[n++] = 0x00; buf[n++] = 0x00; buf[n++] = 0x01; /* start code */
  sei_len = build_sei_nal(buf + n, 1, 4, t35, sizeof t35, 0, NULL, 0);
  buf[n] = 0x06; /* real SEI nal header byte */
  n += sei_len;

  esc_split_nals(&es, &vbuf, &vbuflen, &vbufcap, buf, n, &key, &strip);
  ck_assert_int_eq(key, 1);
  ck_assert(memmem(vbuf, vbuflen, "\x65\xAA\xBB", 3) != NULL);
  ck_assert(memmem(vbuf, vbuflen, t35, sizeof t35) == NULL);

  free(vbuf);
  free(rb);
  free(esc);
}
END_TEST

static Suite *aubuild_suite(void) {
  Suite *s = suite_create("lib_demux_escodec_aubuild");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, non_lcevc_sei_passes_through_unchanged);
  tcase_add_test(tc, lcevc_only_message_drops_whole_nal);
  tcase_add_test(tc, lcevc_message_removed_other_message_kept);
  tcase_add_test(tc, t35_with_wrong_provider_is_not_lcevc);
  tcase_add_test(tc, three_byte_prefix_is_not_lcevc);
  tcase_add_test(tc, hevc_two_byte_header_preserved);
  tcase_add_test(tc, emulation_prevention_in_surviving_message_roundtrips);
  tcase_add_test(tc, truncated_nal_is_unchanged);
  tcase_add_test(tc, reused_buffers_grow_once_then_stay_across_calls);
  tcase_add_test(tc, esc_handle_h264_nal_strips_lcevc_sei_when_enabled);
  tcase_add_test(tc, esc_handle_h264_nal_keeps_sei_when_strip_disabled);
  tcase_add_test(tc, esc_handle_h264_nal_drops_dedicated_lcevc_nal_when_stripping);
  tcase_add_test(tc, esc_split_nals_strips_lcevc_end_to_end);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(aubuild_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
