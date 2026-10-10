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

START_TEST(av1_seq_hdr_obu_cached_verbatim) {
  esc_track_t es;
  unsigned char *vbuf = NULL;
  unsigned char *rb = NULL;
  size_t vbuflen = 0;
  size_t vbufcap = 0;
  size_t rbcap = 0;
  int key = 0;
  static const unsigned char buf[] = {0x00, 0x00, 0x01, 0x08, 0xAA, 0xBB, 0xCC};

  memset(&es, 0, sizeof es);
  esc_split_obus(&es, &vbuf, &vbuflen, &vbufcap, buf, sizeof buf, &key, &rb, &rbcap);

  ck_assert_uint_eq(es.spslen, 4u);
  ck_assert_mem_eq(es.sps, "\x08\xAA\xBB\xCC", 4);
  ck_assert_uint_eq(vbuflen, 0u);

  free(vbuf);
  free(rb);
}
END_TEST

START_TEST(av1_temporal_delimiter_dropped_frame_reframed_to_lobf) {
  esc_track_t es;
  unsigned char *vbuf = NULL;
  unsigned char *rb = NULL;
  size_t vbuflen = 0;
  size_t vbufcap = 0;
  size_t rbcap = 0;
  int key = 0;
  static const unsigned char buf[] = {
    0x00, 0x00, 0x01, 0x10,
    0x00, 0x00, 0x01, 0x30, 0x00, 0xAB, 0xCD,
  };
  static const unsigned char expect[] = {0x32, 0x03, 0x00, 0xAB, 0xCD};
  memset(&es, 0, sizeof es);
  es.spslen = 1;
  esc_split_obus(&es, &vbuf, &vbuflen, &vbufcap, buf, sizeof buf, &key, &rb, &rbcap);
  ck_assert_int_eq(key, 1);
  ck_assert_uint_eq(vbuflen, sizeof expect);
  ck_assert_mem_eq(vbuf, expect, sizeof expect);
  free(vbuf);
  free(rb);
}
END_TEST

START_TEST(av1_inter_frame_not_marked_key) {
  esc_track_t es;
  unsigned char *vbuf = NULL;
  unsigned char *rb = NULL;
  size_t vbuflen = 0;
  size_t vbufcap = 0;
  size_t rbcap = 0;
  int key = 0;
  static const unsigned char buf[] = {0x00, 0x00, 0x01, 0x30, 0x20, 0xAB, 0xCD};
  memset(&es, 0, sizeof es);
  es.spslen = 1;
  esc_split_obus(&es, &vbuf, &vbuflen, &vbufcap, buf, sizeof buf, &key, &rb, &rbcap);
  ck_assert_int_eq(key, 0);
  ck_assert_uint_gt(vbuflen, 0u);
  free(vbuf);
  free(rb);
}
END_TEST

START_TEST(av1_source_size_field_stripped_and_rebuilt) {
  esc_track_t es;
  unsigned char *vbuf = NULL;
  unsigned char *rb = NULL;
  size_t vbuflen = 0;
  size_t vbufcap = 0;
  size_t rbcap = 0;
  int key = 0;
  static const unsigned char buf[] = {0x00, 0x00, 0x01, 0x32, 0x03, 0x00, 0xAB, 0xCD};
  static const unsigned char expect[] = {0x32, 0x03, 0x00, 0xAB, 0xCD};

  memset(&es, 0, sizeof es);
  es.spslen = 1;
  esc_split_obus(&es, &vbuf, &vbuflen, &vbufcap, buf, sizeof buf, &key, &rb, &rbcap);

  ck_assert_int_eq(key, 1);
  ck_assert_uint_eq(vbuflen, sizeof expect);
  ck_assert_mem_eq(vbuf, expect, sizeof expect);

  free(vbuf);
  free(rb);
}
END_TEST

typedef enum { PS_NONE, PS_VPS, PS_SPS, PS_PPS } ps_kind_t;

typedef struct {
  unsigned type;
  size_t vbuf_bytes;
  int key;
  ps_kind_t ps;
} nal_row_t;

typedef void (*nal_handler_t)(esc_track_t *, unsigned char **, size_t *, size_t *, unsigned, const unsigned char *, size_t, int *, const lcevc_strip_t *);

static void run_nal_rows(nal_handler_t handler, const nal_row_t *rows, size_t n_rows) {
  static const unsigned char nal[] = {0x00, 0x01, 0xAA, 0xBB};

  for (size_t i = 0; i < n_rows; i++) {
    esc_track_t es;
    unsigned char *vbuf = NULL;
    size_t vbuflen = 0;
    size_t vbufcap = 0;
    int key = 0;

    memset(&es, 0, sizeof es);
    handler(&es, &vbuf, &vbuflen, &vbufcap, rows[i].type, nal, sizeof nal, &key, NULL);
    ck_assert_msg(vbuflen == rows[i].vbuf_bytes, "row %zu: vbuflen %zu", i, vbuflen);
    ck_assert_msg(key == rows[i].key, "row %zu: key %d", i, key);
    ck_assert_msg((es.vpslen == sizeof nal) == (rows[i].ps == PS_VPS), "row %zu: vps", i);
    ck_assert_msg((es.spslen == sizeof nal) == (rows[i].ps == PS_SPS), "row %zu: sps", i);
    ck_assert_msg((es.ppslen == sizeof nal) == (rows[i].ps == PS_PPS), "row %zu: pps", i);
    free(vbuf);
  }
}

START_TEST(h264_nal_types_are_routed) {
  static const nal_row_t rows[] = {
    {H264_NAL_SPS, 0, 0, PS_SPS},
    {H264_NAL_PPS, 0, 0, PS_PPS},
    {H264_NAL_AUD, 0, 0, PS_NONE},
    {H264_NAL_FILLER, 0, 0, PS_NONE},
    {H264_NAL_LCEVC_NON_IDR, 8, 0, PS_NONE},
    {H264_NAL_LCEVC_IDR, 8, 0, PS_NONE},
    {H264_NAL_SEI, 8, 0, PS_NONE},
    {H264_NAL_IDR, 8, 1, PS_NONE},
    {1, 8, 0, PS_NONE},
  };

  run_nal_rows(esc_handle_h264_nal, rows, sizeof rows / sizeof rows[0]);
}
END_TEST

START_TEST(hevc_nal_types_are_routed) {
  static const nal_row_t rows[] = {
    {HEVC_NAL_VPS, 0, 0, PS_VPS},
    {HEVC_NAL_SPS, 0, 0, PS_SPS},
    {HEVC_NAL_PPS, 0, 0, PS_PPS},
    {HEVC_NAL_AUD, 0, 0, PS_NONE},
    {HEVC_NAL_FILLER, 0, 0, PS_NONE},
    {HEVC_NAL_LCEVC_NON_IDR, 8, 0, PS_NONE},
    {HEVC_NAL_LCEVC_IDR, 8, 0, PS_NONE},
    {HEVC_NAL_SEI_PREFIX, 8, 0, PS_NONE},
    {HEVC_NAL_IRAP_FIRST, 8, 1, PS_NONE},
    {HEVC_NAL_IRAP_LAST, 8, 1, PS_NONE},
    {HEVC_NAL_IRAP_FIRST - 1, 8, 0, PS_NONE},
    {HEVC_NAL_IRAP_LAST + 1, 8, 0, PS_NONE},
  };

  run_nal_rows(esc_handle_hevc_nal, rows, sizeof rows / sizeof rows[0]);
}
END_TEST

START_TEST(vvc_nal_types_are_routed) {
  static const nal_row_t rows[] = {
    {VVC_NAL_VPS, 0, 0, PS_VPS},
    {VVC_NAL_SPS, 0, 0, PS_SPS},
    {VVC_NAL_PPS, 0, 0, PS_PPS},
    {VVC_NAL_AUD, 0, 0, PS_NONE},
    {VVC_NAL_FILLER, 0, 0, PS_NONE},
    {VVC_NAL_LCEVC, 8, 0, PS_NONE},
    {VVC_NAL_SEI_PREFIX, 8, 0, PS_NONE},
    {VVC_NAL_IRAP_FIRST, 8, 1, PS_NONE},
    {VVC_NAL_IRAP_LAST, 8, 1, PS_NONE},
    {VVC_NAL_IRAP_FIRST - 1, 8, 0, PS_NONE},
    {VVC_NAL_IRAP_LAST + 1, 8, 0, PS_NONE},
  };

  run_nal_rows(esc_handle_vvc_nal, rows, sizeof rows / sizeof rows[0]);
}
END_TEST

START_TEST(dedicated_lcevc_nals_are_dropped_when_stripping_for_hevc_and_vvc) {
  static const unsigned char nal[] = {0x00, 0x01, 0xAA, 0xBB};
  unsigned char *rb = NULL;
  unsigned char *esc = NULL;
  size_t rbcap = 0;
  size_t esccap = 0;
  lcevc_strip_t strip = {&rb, &rbcap, &esc, &esccap};
  esc_track_t es;
  unsigned char *vbuf = NULL;
  size_t vbuflen = 0;
  size_t vbufcap = 0;
  int key = 0;

  memset(&es, 0, sizeof es);
  esc_handle_hevc_nal(&es, &vbuf, &vbuflen, &vbufcap, HEVC_NAL_LCEVC_NON_IDR, nal, sizeof nal, &key, &strip);
  esc_handle_hevc_nal(&es, &vbuf, &vbuflen, &vbufcap, HEVC_NAL_LCEVC_IDR, nal, sizeof nal, &key, &strip);
  esc_handle_vvc_nal(&es, &vbuf, &vbuflen, &vbufcap, VVC_NAL_LCEVC, nal, sizeof nal, &key, &strip);
  ck_assert_uint_eq(vbuflen, 0u);
  free(vbuf);
  free(rb);
  free(esc);
}
END_TEST

START_TEST(parameter_set_store_ignores_empty_and_oversize_input) {
  unsigned char dst[ESCODEC_PS_MAX];
  unsigned char src[ESCODEC_PS_MAX + 1];
  size_t len = 0;

  memset(src, 0x5A, sizeof src);
  esc_ps_store(dst, &len, src, 0);
  ck_assert_uint_eq(len, 0u);
  esc_ps_store(dst, &len, src, ESCODEC_PS_MAX + 1);
  ck_assert_uint_eq(len, 0u);
  esc_ps_store(dst, &len, src, ESCODEC_PS_MAX);
  ck_assert_uint_eq(len, (size_t)ESCODEC_PS_MAX);
}
END_TEST

typedef struct {
  const char *name;
  unsigned char nal[8];
  size_t n;
} malformed_sei_t;

static const malformed_sei_t malformed_sei[] = {
  {"payload type runs off the end", {0x00, 0xFF, 0xFF}, 3},
  {"payload size runs off the end", {0x00, 0x05, 0xFF, 0xFF}, 4},
  {"payload size exceeds the nal", {0x00, 0x05, 0x10, 0x01}, 4},
  {"header only", {0x00}, 1},
};

START_TEST(malformed_sei_is_left_alone) {
  const malformed_sei_t *c = &malformed_sei[_i];
  unsigned char *rb = NULL;
  unsigned char *esc = NULL;
  size_t rbcap = 0;
  size_t esccap = 0;
  size_t outlen = 0;
  int r = esc_strip_lcevc_sei(c->nal, c->n, 1, &rb, &rbcap, &esc, &esccap, &outlen);

  ck_assert_msg(r == 0, "%s", c->name);
  free(rb);
  free(esc);
}
END_TEST

typedef struct {
  const char *name;
  unsigned char seq[4];
  size_t seq_n;
  unsigned char frame[6];
  size_t frame_n;
  unsigned frame_type;
  int want_key;
  size_t want_vbuf;
} av1_frame_case_t;

static const av1_frame_case_t av1_frame_cases[] = {
  {"reduced still picture header marks key", {0x0A, 0x01, 0x18}, 3, {0x32, 0x01, 0x00}, 3, OBU_FRAME, 1, 3},
  {"show existing frame is not key", {0x0A, 0x01, 0x00}, 3, {0x32, 0x01, 0x80}, 3, OBU_FRAME, 0, 3},
  {"non-key frame type", {0x0A, 0x01, 0x00}, 3, {0x32, 0x01, 0x40}, 3, OBU_FRAME, 0, 3},
  {"key frame type", {0x0A, 0x01, 0x00}, 3, {0x32, 0x01, 0x00}, 3, OBU_FRAME, 1, 3},
  {"extension header is kept", {0x0A, 0x01, 0x00}, 3, {0x36, 0x00, 0x01, 0x00}, 4, OBU_FRAME, 1, 4},
  {"unterminated size field is dropped", {0x0A, 0x01, 0x00}, 3, {0x32, 0x80}, 2, OBU_FRAME, 0, 0},
  {"redundant frame header is kept", {0x0A, 0x01, 0x00}, 3, {0x3A, 0x01, 0x00}, 3, OBU_REDUNDANT_FRAME_HEADER, 0, 3},
  {"padding obu is dropped", {0x0A, 0x01, 0x00}, 3, {0x7A, 0x01, 0x00}, 3, OBU_PADDING, 0, 0},
};

START_TEST(av1_frame_obu_variants) {
  const av1_frame_case_t *c = &av1_frame_cases[_i];
  esc_track_t es;
  unsigned char *vbuf = NULL;
  size_t vbuflen = 0;
  size_t vbufcap = 0;
  int key = 0;

  memset(&es, 0, sizeof es);
  esc_handle_av1_obu(&es, &vbuf, &vbuflen, &vbufcap, OBU_SEQUENCE_HEADER, c->seq, c->seq_n, &key, NULL);
  ck_assert_uint_eq(es.spslen, c->seq_n);
  ck_assert_uint_eq(vbuflen, 0u);
  esc_handle_av1_obu(&es, &vbuf, &vbuflen, &vbufcap, c->frame_type, c->frame, c->frame_n, &key, NULL);
  ck_assert_msg(key == c->want_key, "%s: key %d", c->name, key);
  ck_assert_msg(vbuflen == c->want_vbuf, "%s: vbuflen %zu", c->name, vbuflen);
  free(vbuf);
}
END_TEST

START_TEST(av1_empty_obu_is_ignored) {
  esc_track_t es;
  unsigned char *vbuf = NULL;
  size_t vbuflen = 0;
  size_t vbufcap = 0;
  int key = 0;

  memset(&es, 0, sizeof es);
  esc_handle_av1_obu(&es, &vbuf, &vbuflen, &vbufcap, OBU_FRAME, (const unsigned char *)"", 0, &key, NULL);
  ck_assert_uint_eq(vbuflen, 0u);
  ck_assert_int_eq(key, 0);
}
END_TEST

typedef struct {
  const char *name;
  codec_t codec;
  unsigned char au[16];
  size_t n;
  int want_key;
  size_t want_vbuf;
  ps_kind_t ps;
} split_case_t;

static const split_case_t split_cases[] = {
  {"h264 idr", CODEC_H264, {0x00, 0x00, 0x00, 0x01, 0x65, 0x88}, 6, 1, 6, PS_NONE},
  {"h264 sps then idr", CODEC_H264, {0x00, 0x00, 0x01, 0x67, 0x11, 0x00, 0x00, 0x01, 0x65, 0x88}, 10, 1, 6, PS_SPS},
  {"hevc irap", CODEC_HEVC, {0x00, 0x00, 0x01, HEVC_NAL_IRAP_FIRST << 1, 0x01, 0xAA}, 6, 1, 7, PS_NONE},
  {"vvc irap", CODEC_VVC, {0x00, 0x00, 0x01, 0x00, VVC_NAL_IRAP_FIRST << 3, 0xAA}, 6, 1, 7, PS_NONE},
  {"vvc one byte nal", CODEC_VVC, {0x00, 0x00, 0x01, 0x00}, 4, 0, 5, PS_NONE},
  {"no start code", CODEC_H264, {0x11, 0x22, 0x33}, 3, 0, 0, PS_NONE},
};

START_TEST(split_nals_routes_each_codec) {
  const split_case_t *c = &split_cases[_i];
  esc_track_t es;
  unsigned char *vbuf = NULL;
  size_t vbuflen = 0;
  size_t vbufcap = 0;
  int key = 0;

  memset(&es, 0, sizeof es);
  es.codec = c->codec;
  esc_split_nals(&es, &vbuf, &vbuflen, &vbufcap, c->au, c->n, &key, NULL);
  ck_assert_msg(key == c->want_key, "%s: key %d", c->name, key);
  ck_assert_msg(vbuflen == c->want_vbuf, "%s: vbuflen %zu", c->name, vbuflen);
  ck_assert_msg((es.spslen != 0) == (c->ps == PS_SPS), "%s: sps", c->name);
  free(vbuf);
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
  tcase_add_test(tc, av1_seq_hdr_obu_cached_verbatim);
  tcase_add_test(tc, av1_temporal_delimiter_dropped_frame_reframed_to_lobf);
  tcase_add_test(tc, av1_inter_frame_not_marked_key);
  tcase_add_test(tc, av1_source_size_field_stripped_and_rebuilt);
  tcase_add_test(tc, h264_nal_types_are_routed);
  tcase_add_test(tc, hevc_nal_types_are_routed);
  tcase_add_test(tc, vvc_nal_types_are_routed);
  tcase_add_test(tc, dedicated_lcevc_nals_are_dropped_when_stripping_for_hevc_and_vvc);
  tcase_add_test(tc, parameter_set_store_ignores_empty_and_oversize_input);
  tcase_add_loop_test(tc, malformed_sei_is_left_alone, 0, (int)(sizeof malformed_sei / sizeof malformed_sei[0]));
  tcase_add_loop_test(tc, av1_frame_obu_variants, 0, (int)(sizeof av1_frame_cases / sizeof av1_frame_cases[0]));
  tcase_add_test(tc, av1_empty_obu_is_ignored);
  tcase_add_loop_test(tc, split_nals_routes_each_codec, 0, (int)(sizeof split_cases / sizeof split_cases[0]));
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
