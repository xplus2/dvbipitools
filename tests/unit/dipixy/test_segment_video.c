/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipixy/segment/priv.h"

int hls_push_segment_at(hls_store_t *s, const uint8_t *data, size_t size, double duration) {
  (void)s; (void)data; (void)size; (void)duration;
  return 0;
}
int hls_push_part_at(hls_store_t *s, const uint8_t *data, size_t size, double duration, int independent) {
  (void)s; (void)data; (void)size; (void)duration; (void)independent;
  return 0;
}
int hls_push_segment_ll_at(hls_store_t *s, double duration) {
  (void)s; (void)duration;
  return 0;
}
void mp4push_deliver(const hls_seg_ctx_t *s, const unsigned char *data, size_t len) {
  (void)s; (void)data; (void)len;
}
int hls_set_init_segment_at(hls_store_t *s, codec_t video_codec, const uint8_t *data, size_t size) {
  (void)s; (void)video_codec; (void)data; (void)size;
  return 0;
}
int64_t pts_unwrap(pts_unwrap_t *st, uint64_t raw) {
  (void)st; (void)raw;
  return 0;
}
int buf_reserve(unsigned char **buf, size_t *cap, size_t need) {
  (void)buf; (void)cap; (void)need;
  return -1;
}
fmp4_mux_t *fmp4_mux_new(const fmp4_track_cfg_t *tracks, int ntracks) {
  (void)tracks; (void)ntracks;
  return NULL;
}
size_t fmp4_init_segment(fmp4_mux_t *m, unsigned char **out) {
  (void)m; (void)out;
  return 0;
}
void fmp4_segment_begin(fmp4_mux_t *m, uint32_t sequence_number) {
  (void)m; (void)sequence_number;
}
void fmp4_segment_add_sample(fmp4_mux_t *m, const fmp4_sample_t *s) {
  (void)m; (void)s;
}
void fmp4_track_seed_dts(fmp4_mux_t *m, int track_idx, uint64_t dts) {
  (void)m; (void)track_idx; (void)dts;
}
size_t fmp4_segment_end(fmp4_mux_t *m, unsigned char **out) {
  (void)m; (void)out;
  return 0;
}

static void wrap_unit(unsigned char *out, size_t *n, const unsigned char *unit, size_t ulen) {
  static const unsigned char sc[] = {0x00, 0x00, 0x01};
  *n = 0;
  memcpy(out + *n, sc, sizeof sc);
  *n += sizeof sc;
  memcpy(out + *n, unit, ulen);
  *n += ulen;
}

static hls_seg_ctx_t *new_ctx(codec_t codec) {
  hls_seg_ctx_t *s = calloc(1, sizeof *s);
  s->demux.video_codec = codec;
  return s;
}

START_TEST(h264_idr_nal_is_keyframe) {
  static const unsigned char idr[] = {0x65, 0xAA, 0xBB};
  unsigned char buf[16];
  size_t n;
  hls_seg_ctx_t *s = new_ctx(CODEC_H264);
  wrap_unit(buf, &n, idr, sizeof idr);
  ck_assert_int_eq(detect_keyframe(s, buf, n), 1);
  free(s->video.nal_scratch);
  free(s);
}
END_TEST

START_TEST(h264_non_idr_nal_is_not_keyframe) {
  static const unsigned char slice[] = {0x61, 0xAA, 0xBB};
  unsigned char buf[16];
  size_t n;
  hls_seg_ctx_t *s = new_ctx(CODEC_H264);
  wrap_unit(buf, &n, slice, sizeof slice);
  ck_assert_int_eq(detect_keyframe(s, buf, n), 0);
  free(s->video.nal_scratch);
  free(s);
}
END_TEST

START_TEST(hevc_irap_nal_is_keyframe) {
  static const unsigned char idr[] = {0x26, 0x01, 0xAA, 0xBB};
  unsigned char buf[16];
  size_t n;
  hls_seg_ctx_t *s = new_ctx(CODEC_HEVC);
  wrap_unit(buf, &n, idr, sizeof idr);
  ck_assert_int_eq(detect_keyframe(s, buf, n), 1);
  free(s->video.nal_scratch);
  free(s);
}
END_TEST

START_TEST(hevc_non_irap_nal_is_not_keyframe) {
  static const unsigned char trail[] = {0x02, 0x01, 0xAA, 0xBB};
  unsigned char buf[16];
  size_t n;
  hls_seg_ctx_t *s = new_ctx(CODEC_HEVC);
  wrap_unit(buf, &n, trail, sizeof trail);
  ck_assert_int_eq(detect_keyframe(s, buf, n), 0);
  free(s->video.nal_scratch);
  free(s);
}
END_TEST

START_TEST(vvc_irap_nal_is_keyframe) {
  static const unsigned char idr[] = {0x00, 0x38, 0xAA, 0xBB};
  unsigned char buf[16];
  size_t n;
  hls_seg_ctx_t *s = new_ctx(CODEC_VVC);
  wrap_unit(buf, &n, idr, sizeof idr);
  ck_assert_int_eq(detect_keyframe(s, buf, n), 1);
  free(s->video.nal_scratch);
  free(s);
}
END_TEST

START_TEST(vvc_non_irap_nal_is_not_keyframe) {
  static const unsigned char slice[] = {0x00, 0x10, 0xAA, 0xBB};
  unsigned char buf[16];
  size_t n;
  hls_seg_ctx_t *s = new_ctx(CODEC_VVC);
  wrap_unit(buf, &n, slice, sizeof slice);
  ck_assert_int_eq(detect_keyframe(s, buf, n), 0);
  free(s->video.nal_scratch);
  free(s);
}
END_TEST

START_TEST(av1_key_frame_obu_is_keyframe) {
  static const unsigned char frame[] = {0x30, 0x00, 0xAB, 0xCD};
  unsigned char buf[16];
  size_t n;
  hls_seg_ctx_t *s = new_ctx(CODEC_AV1);
  s->video.es.spslen = 1; /* keyframe check requires a sequence header already cached */
  wrap_unit(buf, &n, frame, sizeof frame);
  ck_assert_int_eq(detect_keyframe(s, buf, n), 1);
  free(s->video.nal_scratch);
  free(s->video.av1_rb);
  free(s);
}
END_TEST

START_TEST(av1_inter_frame_obu_is_not_keyframe) {
  static const unsigned char frame[] = {0x30, 0x20, 0xAB, 0xCD};
  unsigned char buf[16];
  size_t n;
  hls_seg_ctx_t *s = new_ctx(CODEC_AV1);
  s->video.es.spslen = 1;
  wrap_unit(buf, &n, frame, sizeof frame);
  ck_assert_int_eq(detect_keyframe(s, buf, n), 0);
  free(s->video.nal_scratch);
  free(s->video.av1_rb);
  free(s);
}
END_TEST

START_TEST(build_video_track_cfg_h264) {
  static const unsigned char sps[] = {0x67, 0x42, 0x00, 0x00, 0xFB, 0x80};
  static const unsigned char pps[] = {0x68, 0xAA};
  unsigned char cpriv[256];
  fmp4_track_cfg_t trk;
  hls_seg_ctx_t *s = new_ctx(CODEC_H264);
  memcpy(s->video.es.sps, sps, sizeof sps);
  s->video.es.spslen = sizeof sps;
  memcpy(s->video.es.pps, pps, sizeof pps);
  s->video.es.ppslen = sizeof pps;
  ck_assert_int_eq(build_video_track_cfg(s, &trk, cpriv, sizeof cpriv), 1);
  ck_assert_int_eq(trk.codec, CODEC_H264);
  ck_assert_uint_eq(trk.width, 16);
  ck_assert_uint_eq(trk.height, 16);
  ck_assert(trk.cpriv_len > 0);
  free(s);
}
END_TEST

START_TEST(build_video_track_cfg_hevc) {
  unsigned char sps[17] = {0x42, 0x01, 0x00};
  static const unsigned char vps[] = {0x40, 0x01, 0xAA};
  static const unsigned char pps[] = {0x44, 0x01, 0xBB};
  unsigned char cpriv[256];
  fmp4_track_cfg_t trk;
  hls_seg_ctx_t *s = new_ctx(CODEC_HEVC);
  memset(sps + 3, 0xAA, 12);
  sps[15] = 0xA4;
  sps[16] = 0x80;
  memcpy(s->video.es.sps, sps, sizeof sps);
  s->video.es.spslen = sizeof sps;
  memcpy(s->video.es.vps, vps, sizeof vps);
  s->video.es.vpslen = sizeof vps;
  memcpy(s->video.es.pps, pps, sizeof pps);
  s->video.es.ppslen = sizeof pps;
  ck_assert_int_eq(build_video_track_cfg(s, &trk, cpriv, sizeof cpriv), 1);
  ck_assert_int_eq(trk.codec, CODEC_HEVC);
  ck_assert_uint_eq(trk.width, 1);
  ck_assert_uint_eq(trk.height, 1);
  ck_assert(trk.cpriv_len > 0);
  free(s);
}
END_TEST

START_TEST(build_video_track_cfg_vvc) {
  static const unsigned char sps[] = {0x00, 0x79, 0x11, 0x0B, 0xFF, 0xFF, 0xDF, 0x00, 0x12};
  static const unsigned char vps[] = {0x00, 0x71, 0xAA, 0xBB};
  static const unsigned char pps[] = {0x00, 0x81, 0xCC, 0xDD};
  unsigned char cpriv[256];
  fmp4_track_cfg_t trk;
  hls_seg_ctx_t *s = new_ctx(CODEC_VVC);
  memcpy(s->video.es.sps, sps, sizeof sps);
  s->video.es.spslen = sizeof sps;
  memcpy(s->video.es.vps, vps, sizeof vps);
  s->video.es.vpslen = sizeof vps;
  memcpy(s->video.es.pps, pps, sizeof pps);
  s->video.es.ppslen = sizeof pps;
  ck_assert_int_eq(build_video_track_cfg(s, &trk, cpriv, sizeof cpriv), 1);
  ck_assert_int_eq(trk.codec, CODEC_VVC);
  ck_assert_uint_eq(trk.width, 1);
  ck_assert_uint_eq(trk.height, 1);
  ck_assert(trk.cpriv_len > 0);
  free(s);
}
END_TEST

START_TEST(build_video_track_cfg_av1) {
  static const unsigned char sps[] = {0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC0, 0x01};
  unsigned char cpriv[256];
  fmp4_track_cfg_t trk;
  hls_seg_ctx_t *s = new_ctx(CODEC_AV1);
  memcpy(s->video.es.sps, sps, sizeof sps);
  s->video.es.spslen = sizeof sps;
  ck_assert_int_eq(build_video_track_cfg(s, &trk, cpriv, sizeof cpriv), 1);
  ck_assert_int_eq(trk.codec, CODEC_AV1);
  ck_assert_uint_eq(trk.width, 1);
  ck_assert_uint_eq(trk.height, 1);
  ck_assert(trk.cpriv_len > 0);
  free(s);
}
END_TEST

static Suite *segment_video_suite(void) {
  Suite *s = suite_create("dipixy_segment_video");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, h264_idr_nal_is_keyframe);
  tcase_add_test(tc, h264_non_idr_nal_is_not_keyframe);
  tcase_add_test(tc, hevc_irap_nal_is_keyframe);
  tcase_add_test(tc, hevc_non_irap_nal_is_not_keyframe);
  tcase_add_test(tc, vvc_irap_nal_is_keyframe);
  tcase_add_test(tc, vvc_non_irap_nal_is_not_keyframe);
  tcase_add_test(tc, av1_key_frame_obu_is_keyframe);
  tcase_add_test(tc, av1_inter_frame_obu_is_not_keyframe);
  tcase_add_test(tc, build_video_track_cfg_h264);
  tcase_add_test(tc, build_video_track_cfg_hevc);
  tcase_add_test(tc, build_video_track_cfg_vvc);
  tcase_add_test(tc, build_video_track_cfg_av1);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(segment_video_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
