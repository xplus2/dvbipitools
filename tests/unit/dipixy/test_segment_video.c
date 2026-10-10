/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipixy/segment/priv.h"

static int g_mux_enabled;
static char g_mux_dummy;
static char g_store_dummy;
static int g_ntracks;
static fmp4_track_cfg_t g_tracks[FMP4_MAX_TRACKS];
static int g_init_result;
static int g_push_result;
static int g_segment_count;
static int g_part_count;
static int g_segment_ll_count;
static int g_begin_count;
static int g_sample_count;
static int g_seed_count;
static int g_end_count;
static int g_deliver_count;
static unsigned char g_out[8] = "fmp4out";

static void stubs_reset(void) {
  g_mux_enabled = 1;
  g_ntracks = 0;
  g_init_result = 0;
  g_push_result = 0;
  g_segment_count = 0;
  g_part_count = 0;
  g_segment_ll_count = 0;
  g_begin_count = 0;
  g_sample_count = 0;
  g_seed_count = 0;
  g_end_count = 0;
  g_deliver_count = 0;
}

int hls_push_segment_at(hls_store_t *s, const uint8_t *data, size_t size, double duration) {
  (void)s; (void)data; (void)size; (void)duration;
  g_segment_count++;
  return g_push_result;
}
int hls_push_part_at(hls_store_t *s, const uint8_t *data, size_t size, double duration, int independent) {
  (void)s; (void)data; (void)size; (void)duration; (void)independent;
  g_part_count++;
  return g_push_result;
}
int hls_push_segment_ll_at(hls_store_t *s, double duration) {
  (void)s; (void)duration;
  g_segment_ll_count++;
  return g_push_result;
}
void mp4push_deliver(const hls_seg_ctx_t *s, const unsigned char *data, size_t len) {
  (void)s; (void)data; (void)len;
  g_deliver_count++;
}
int hls_set_init_segment_at(hls_store_t *s, codec_t video_codec, const uint8_t *data, size_t size) {
  (void)s; (void)video_codec; (void)data; (void)size;
  return g_init_result;
}
int64_t pts_unwrap(pts_unwrap_t *st, uint64_t raw) {
  (void)st;
  return (int64_t)(raw / 90);
}
int buf_reserve(unsigned char **buf, size_t *cap, size_t need) {
  unsigned char *grown;

  if (need <= *cap) return 0;
  grown = realloc(*buf, need);
  if (!grown) return -1;
  *buf = grown;
  *cap = need;
  return 0;
}
fmp4_mux_t *fmp4_mux_new(const fmp4_track_cfg_t *tracks, int ntracks) {
  if (!g_mux_enabled) return NULL;
  g_ntracks = ntracks;
  memcpy(g_tracks, tracks, (size_t)ntracks * sizeof tracks[0]);
  return (fmp4_mux_t *)&g_mux_dummy;
}
size_t fmp4_init_segment(fmp4_mux_t *m, unsigned char **out) {
  (void)m;
  *out = g_out;
  return sizeof g_out;
}
void fmp4_segment_begin(fmp4_mux_t *m, uint32_t sequence_number) {
  (void)m; (void)sequence_number;
  g_begin_count++;
}
void fmp4_segment_add_sample(fmp4_mux_t *m, const fmp4_sample_t *s) {
  (void)m; (void)s;
  g_sample_count++;
}
void fmp4_track_seed_dts(fmp4_mux_t *m, int track_idx, uint64_t dts) {
  (void)m; (void)track_idx; (void)dts;
  g_seed_count++;
}
size_t fmp4_segment_end(fmp4_mux_t *m, unsigned char **out) {
  (void)m;
  *out = g_out;
  g_end_count++;
  return sizeof g_out;
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
  ck_assert_ptr_nonnull(s);
  s->demux.video_codec = codec;
  s->video.es.codec = codec;
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

static hls_seg_ctx_t *ready_ctx(void) {
  static const unsigned char sps[] = {0x67, 0x42, 0x00, 0x00, 0xFB, 0x80};
  static const unsigned char pps[] = {0x68, 0xAA};
  hls_seg_ctx_t *s = new_ctx(CODEC_H264);

  memcpy(s->video.es.sps, sps, sizeof sps);
  s->video.es.spslen = sizeof sps;
  memcpy(s->video.es.pps, pps, sizeof pps);
  s->video.es.ppslen = sizeof pps;
  s->store = (hls_store_t *)&g_store_dummy;
  stubs_reset();
  return s;
}

static void free_ctx(hls_seg_ctx_t *s) {
  free(s->fmp4.pend_data);
  free(s->lcevc_track.pend_data);
  free(s->video.nal_scratch);
  free(s);
}

static void set_au(hls_seg_ctx_t *s, size_t len) {
  ck_assert_int_eq(buf_reserve(&s->video.nal_scratch, &s->video.nal_scratch_cap, len), 0);
  memset(s->video.nal_scratch, 0xAB, len);
  s->video.nal_scratch_len = len;
}

START_TEST(fmux_waits_for_video_parameters_and_audio) {
  hls_seg_ctx_t *s = ready_ctx();

  s->video.es.spslen = 0;
  try_create_fmux(s);
  ck_assert_ptr_null(s->fmp4.fmux);
  s->video.es.spslen = 6;
  s->audio.present = 1;
  s->audio.ready = 0;
  try_create_fmux(s);
  ck_assert_ptr_null(s->fmp4.fmux);
  free_ctx(s);
}
END_TEST

START_TEST(fmux_is_created_once_with_the_audio_codec_parameters) {
  static const struct {
    codec_t codec;
  } codecs[] = {{CODEC_AAC}, {CODEC_AAC_LATM}, {CODEC_AC4}, {CODEC_AC3}, {CODEC_EAC3}, {CODEC_TRUEHD}, {CODEC_DTS}, {CODEC_DTS_HD}, {CODEC_DTS_HD_MA}, {CODEC_OPUS}};
  hls_seg_ctx_t *s = ready_ctx();

  s->audio.present = 1;
  s->audio.ready = 1;
  s->audio.rate = 48000;
  s->audio.channels = 2;
  s->audio.bsid = 6;
  s->audio.bsmod = 1;
  s->audio.acmod = 7;
  s->audio.lfeon = 1;
  s->audio.bitrate_code = 12;
  s->audio.truehd_format_info = 0x1234;
  s->audio.truehd_peak_data_rate = 0x2345;
  s->audio.dts_has_core = 1;
  s->demux.audio_codec = codecs[_i].codec;
  try_create_fmux(s);
  ck_assert_ptr_nonnull(s->fmp4.fmux);
  ck_assert_int_eq(g_ntracks, 2);
  ck_assert_int_eq(g_tracks[1].codec, codecs[_i].codec);
  ck_assert_uint_eq(g_tracks[1].rate, 48000u);
  ck_assert_int_eq(s->fmp4.fmp4_audio_track_idx, 1);
  ck_assert_int_eq(s->fmp4.fmp4_lcevc_track_idx, -1);
  if (codecs[_i].codec == CODEC_AC3 || codecs[_i].codec == CODEC_EAC3) ck_assert_uint_eq(g_tracks[1].ac3_acmod, 7u);
  if (codecs[_i].codec == CODEC_TRUEHD) ck_assert_uint_eq(g_tracks[1].truehd_format_info, 0x1234u);
  if (codecs[_i].codec == CODEC_DTS) ck_assert_int_eq(g_tracks[1].dts_has_core, 1);
  g_ntracks = 0;
  try_create_fmux(s);
  ck_assert_int_eq(g_ntracks, 0);
  free_ctx(s);
}
END_TEST

START_TEST(fmux_gains_an_lcevc_track_when_one_is_known) {
  hls_seg_ctx_t *s = ready_ctx();

  s->demux.lcevc_pid_known = 1;
  try_create_fmux(s);
  ck_assert_ptr_nonnull(s->fmp4.fmux);
  ck_assert_int_eq(g_ntracks, 2);
  ck_assert_int_eq(g_tracks[1].codec, CODEC_LCEVC);
  ck_assert_int_eq(s->fmp4.fmp4_lcevc_track_idx, 1);
  free_ctx(s);
}
END_TEST

START_TEST(fmux_creation_failures_are_tolerated) {
  hls_seg_ctx_t *s = ready_ctx();

  g_mux_enabled = 0;
  try_create_fmux(s);
  ck_assert_ptr_null(s->fmp4.fmux);
  g_mux_enabled = 1;
  g_init_result = -1;
  try_create_fmux(s);
  ck_assert_ptr_nonnull(s->fmp4.fmux);
  s->fmp4.fmux = NULL;
  s->store = NULL;
  try_create_fmux(s);
  ck_assert_ptr_nonnull(s->fmp4.fmux);
  free_ctx(s);
}
END_TEST

START_TEST(access_units_open_fragments_and_close_segments) {
  hls_seg_ctx_t *s = ready_ctx();

  set_au(s, 32);
  fmp4_feed_au(s, 1, 0, 0, 1, 0, 0.0);
  ck_assert_int_eq(g_begin_count, 0);
  fmp4_feed_au(s, 0, 40, 0, 0, 0, 0.0);
  ck_assert_int_eq(g_begin_count, 1);
  ck_assert_int_eq(g_sample_count, 1);
  fmp4_feed_au(s, 0, 80, 0, 0, 0, 0.0);
  ck_assert_int_eq(g_sample_count, 2);
  fmp4_feed_au(s, 1, 120, 0, 1, 1, 0.12);
  fmp4_feed_au(s, 0, 160, 0, 0, 0, 0.0);
  ck_assert_int_eq(g_end_count, 1);
  ck_assert_int_eq(g_deliver_count, 1);
  ck_assert_int_eq(g_segment_count, 1);
  ck_assert_int_eq(g_part_count, 0);
  free_ctx(s);
}
END_TEST

START_TEST(failed_pushes_do_not_stop_the_mux) {
  hls_seg_ctx_t *s = ready_ctx();

  g_push_result = -1;
  set_au(s, 32);
  fmp4_feed_au(s, 1, 0, 0, 1, 0, 0.0);
  fmp4_feed_au(s, 0, 40, 0, 0, 0, 0.0);
  fmp4_feed_au(s, 1, 80, 0, 1, 1, 0.08);
  fmp4_feed_au(s, 0, 120, 0, 0, 0, 0.0);
  ck_assert_int_eq(g_end_count, 1);
  s->store = NULL;
  fmp4_feed_au(s, 1, 160, 0, 1, 1, 0.16);
  fmp4_feed_au(s, 0, 200, 0, 0, 0, 0.0);
  ck_assert_int_eq(g_end_count, 2);
  free_ctx(s);
}
END_TEST

START_TEST(low_latency_chunks_close_on_the_part_target) {
  hls_seg_ctx_t *s = ready_ctx();

  atomic_store(&s->part.part_target, 0.1);
  set_au(s, 32);
  fmp4_feed_au(s, 1, 0, 0, 1, 0, 0.0);
  fmp4_feed_au(s, 0, 40, 0, 0, 0, 0.0);
  fmp4_feed_au(s, 0, 80, 0, 0, 0, 0.0);
  fmp4_feed_au(s, 0, 120, 0, 0, 0, 0.0);
  fmp4_feed_au(s, 0, 160, 0, 0, 0, 0.0);
  ck_assert_int_ge(g_part_count, 1);
  fmp4_feed_au(s, 1, 200, 0, 1, 1, 0.2);
  fmp4_feed_au(s, 0, 240, 0, 0, 0, 0.0);
  ck_assert_int_ge(g_segment_ll_count, 1);
  g_push_result = -1;
  fmp4_feed_au(s, 1, 280, 0, 1, 1, 0.28);
  fmp4_feed_au(s, 0, 320, 0, 0, 0, 0.0);
  s->store = NULL;
  fmp4_feed_au(s, 1, 360, 0, 1, 1, 0.36);
  fmp4_feed_au(s, 0, 400, 0, 0, 0, 0.0);
  free_ctx(s);
}
END_TEST

START_TEST(an_unavailable_mux_drops_access_units) {
  hls_seg_ctx_t *s = ready_ctx();

  g_mux_enabled = 0;
  set_au(s, 16);
  fmp4_feed_au(s, 1, 0, 0, 1, 0, 0.0);
  ck_assert_int_eq(s->fmp4.fmp4_have_pend, 0);
  free_ctx(s);
}
END_TEST

START_TEST(ac4_segment_cuts_wait_for_an_independent_frame) {
  hls_seg_ctx_t *s = ready_ctx();

  s->audio.present = 1;
  s->audio.ready = 1;
  s->audio.rate = 48000;
  s->audio.channels = 2;
  s->demux.audio_codec = CODEC_AC4;
  set_au(s, 32);
  fmp4_feed_au(s, 1, 0, 0, 1, 0, 0.0);
  fmp4_feed_au(s, 0, 40, 0, 0, 0, 0.0);
  fmp4_feed_au(s, 0, 80, 0, 1, 1, 0.08);
  ck_assert_int_eq(s->fmp4.fmp4_ac4_defer, 1);
  s->audio.ac4_frame_count = 1;
  fmp4_feed_au(s, 0, 120, 0, 0, 0, 0.0);
  ck_assert_int_eq(s->fmp4.fmp4_ac4_defer, 1);
  s->audio.ac4_last_iframe = 1;
  fmp4_feed_au(s, 0, 160, 0, 0, 0, 0.0);
  ck_assert_int_eq(s->fmp4.fmp4_ac4_defer, 0);
  s->audio.ac4_last_iframe = 0;
  fmp4_feed_au(s, 0, 200, 0, 1, 0, 0.2);
  ck_assert_int_eq(s->fmp4.fmp4_ac4_defer, 1);
  s->audio.ac4_frame_count = 10;
  fmp4_feed_au(s, 0, 240, 0, 0, 0, 0.0);
  ck_assert_int_eq(s->fmp4.fmp4_ac4_defer, 0);
  free_ctx(s);
}
END_TEST

START_TEST(lcevc_units_are_seeded_and_paired_with_the_next_timestamp) {
  hls_seg_ctx_t *s = ready_ctx();
  static const unsigned char au[] = {1, 2, 3, 4};

  s->demux.lcevc_pid_known = 1;
  set_au(s, 32);
  fmp4_feed_lcevc_au(s, 0, au, sizeof au);
  ck_assert_int_eq(g_seed_count, 0);
  fmp4_feed_au(s, 1, 100, 0, 1, 0, 0.0);
  fmp4_feed_au(s, 0, 140, 0, 0, 0, 0.0);
  ck_assert_int_eq(s->fmp4.fmp4_frag_open, 1);
  fmp4_feed_lcevc_au(s, 20, au, sizeof au);
  ck_assert_int_eq(g_seed_count, 0);
  fmp4_feed_lcevc_au(s, 120, au, sizeof au);
  ck_assert_int_eq(g_seed_count, 1);
  ck_assert_int_eq(s->lcevc_track.have_pend, 1);
  fmp4_feed_lcevc_au(s, 160, au, sizeof au);
  ck_assert_int_ge(g_sample_count, 2);
  free_ctx(s);
}
END_TEST

typedef struct {
  const char *name;
  unsigned char au[12];
  size_t n;
  int want;
} mpeg2_case_t;

static const mpeg2_case_t mpeg2_cases[] = {
  {"intra picture", {0x00, 0x00, 0x01, 0x00, 0x00, 0x08, 0xAA}, 7, 1},
  {"predicted picture", {0x00, 0x00, 0x01, 0x00, 0x00, 0x10, 0xAA}, 7, 0},
  {"sequence header only", {0x00, 0x00, 0x01, 0xB3, 0x00, 0x08, 0xAA}, 7, 0},
  {"picture header cut short", {0x00, 0x00, 0x01, 0x00, 0x00}, 5, 0},
};

START_TEST(mpeg2_intra_pictures_are_keyframes) {
  const mpeg2_case_t *c = &mpeg2_cases[_i];
  hls_seg_ctx_t *s = new_ctx(CODEC_MPEG2V);

  ck_assert_msg(detect_keyframe(s, c->au, c->n) == c->want, "%s", c->name);
  free(s);
}
END_TEST

static unsigned char g_idr[8];
static size_t g_idr_len;

static hls_seg_ctx_t *ts_ctx(double part_target, int boot_left) {
  static const unsigned char idr[] = {0x65, 0xAA, 0xBB};
  hls_seg_ctx_t *s = ready_ctx();

  wrap_unit(g_idr, &g_idr_len, idr, sizeof idr);
  s->container = SEG_CONTAINER_TS;
  s->buf = calloc(1, 256);
  ck_assert_ptr_nonnull(s->buf);
  s->len = 100;
  s->have_pending_pes = 1;
  s->pending_pes_off = 10;
  s->video.seg_target = 2.0;
  s->video.first_ts_ms = -1;
  s->video.boot_left = boot_left;
  atomic_store(&s->part.part_target, part_target);
  return s;
}

static void ts_free(hls_seg_ctx_t *s) {
  free(s->buf);
  free_ctx(s);
}

static void feed_idr(hls_seg_ctx_t *s, unsigned ts_ms) {
  handle_video_pes(s, 1, (uint64_t)ts_ms * 90, 1, (uint64_t)ts_ms * 90, g_idr, g_idr_len);
}

static void feed_slice(hls_seg_ctx_t *s, unsigned ts_ms) {
  static const unsigned char slice[] = {0x41, 0xAA};
  unsigned char buf[8];
  size_t n;

  wrap_unit(buf, &n, slice, sizeof slice);
  handle_video_pes(s, 1, (uint64_t)ts_ms * 90, 1, (uint64_t)ts_ms * 90, buf, n);
}

START_TEST(video_pes_without_a_recorded_offset_is_ignored) {
  hls_seg_ctx_t *s = ts_ctx(0.0, 0);

  s->have_pending_pes = 0;
  feed_idr(s, 0);
  ck_assert_int_eq(s->video.seg_open, 0);
  ck_assert_uint_eq(s->len, 100u);
  ts_free(s);
}
END_TEST

START_TEST(classic_ts_cuts_segments_at_keyframes_after_the_target) {
  hls_seg_ctx_t *s = ts_ctx(0.0, 0);

  feed_idr(s, 0);
  ck_assert_int_eq(s->video.seg_open, 1);
  ck_assert_uint_eq(s->len, 90u);
  feed_slice(s, 500);
  feed_idr(s, 1000);
  ck_assert_int_eq(g_segment_count, 0);
  feed_idr(s, 2500);
  ck_assert_int_eq(g_segment_count, 1);
  ck_assert_int_eq((int)s->video.first_ts_ms, 2500);
  g_push_result = -1;
  feed_idr(s, 5000);
  ck_assert_int_eq(g_segment_count, 2);
  s->store = NULL;
  feed_idr(s, 7500);
  ck_assert_int_eq(g_segment_count, 2);
  ts_free(s);
}
END_TEST

START_TEST(classic_ts_boot_cuts_early_keyframes_then_follows_the_target) {
  hls_seg_ctx_t *s = ts_ctx(0.0, 2);

  feed_idr(s, 0);
  feed_idr(s, 100);
  ck_assert_int_eq(g_segment_count, 1);
  ck_assert_int_eq(s->video.boot_left, 1);
  feed_idr(s, 200);
  ck_assert_int_eq(g_segment_count, 2);
  ck_assert_int_eq(s->video.boot_left, 0);
  feed_idr(s, 300);
  ck_assert_int_eq(g_segment_count, 2);
  ts_free(s);
}
END_TEST

START_TEST(low_latency_ts_closes_parts_and_cuts_segments_on_keyframes) {
  hls_seg_ctx_t *s = ts_ctx(0.5, 0);

  feed_idr(s, 0);
  ck_assert_int_eq(s->part.part_open, 1);
  feed_slice(s, 200);
  ck_assert_int_eq(s->part.have_last_au, 1);
  s->len = 100;
  feed_slice(s, 600);
  ck_assert_int_ge(g_part_count, 1);
  feed_idr(s, 2500);
  ck_assert_int_eq(g_segment_ll_count, 1);
  g_push_result = -1;
  feed_slice(s, 2900);
  feed_idr(s, 5000);
  ck_assert_int_eq(g_segment_ll_count, 2);
  s->store = NULL;
  feed_slice(s, 5400);
  feed_idr(s, 7500);
  ck_assert_int_eq(g_segment_ll_count, 2);
  ts_free(s);
}
END_TEST

START_TEST(fmp4_container_hands_access_units_to_the_mux) {
  hls_seg_ctx_t *s = ready_ctx();
  static const unsigned char idr[] = {0x65, 0xAA, 0xBB};
  unsigned char buf[8];
  size_t n;

  wrap_unit(buf, &n, idr, sizeof idr);
  s->container = SEG_CONTAINER_FMP4;
  s->buf = calloc(1, 64);
  s->len = 32;
  s->have_pending_pes = 1;
  s->pending_pes_off = 0;
  s->video.seg_target = 2.0;
  s->video.first_ts_ms = -1;
  handle_video_pes(s, 1, 0, 0, 0, buf, n);
  handle_video_pes(s, 1, 90 * 40, 0, 0, buf, n);
  ck_assert_int_eq(s->video.seg_open, 1);
  free(s->buf);
  free_ctx(s);
}
END_TEST

START_TEST(lcevc_pes_needs_a_pts) {
  hls_seg_ctx_t *s = ready_ctx();
  static const unsigned char au[] = {1, 2, 3, 4};

  s->demux.lcevc_pid_known = 1;
  handle_lcevc_pes(s, 0, 0, au, sizeof au);
  ck_assert_int_eq(s->lcevc_track.have_pend, 0);
  handle_lcevc_pes(s, 1, 90 * 20, au, sizeof au);
  free_ctx(s);
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
  tcase_add_test(tc, fmux_waits_for_video_parameters_and_audio);
  tcase_add_loop_test(tc, fmux_is_created_once_with_the_audio_codec_parameters, 0, 10);
  tcase_add_test(tc, fmux_gains_an_lcevc_track_when_one_is_known);
  tcase_add_test(tc, fmux_creation_failures_are_tolerated);
  tcase_add_test(tc, access_units_open_fragments_and_close_segments);
  tcase_add_test(tc, failed_pushes_do_not_stop_the_mux);
  tcase_add_test(tc, low_latency_chunks_close_on_the_part_target);
  tcase_add_test(tc, an_unavailable_mux_drops_access_units);
  tcase_add_test(tc, ac4_segment_cuts_wait_for_an_independent_frame);
  tcase_add_test(tc, lcevc_units_are_seeded_and_paired_with_the_next_timestamp);
  tcase_add_loop_test(tc, mpeg2_intra_pictures_are_keyframes, 0, (int)(sizeof mpeg2_cases / sizeof mpeg2_cases[0]));
  tcase_add_test(tc, video_pes_without_a_recorded_offset_is_ignored);
  tcase_add_test(tc, classic_ts_cuts_segments_at_keyframes_after_the_target);
  tcase_add_test(tc, classic_ts_boot_cuts_early_keyframes_then_follows_the_target);
  tcase_add_test(tc, low_latency_ts_closes_parts_and_cuts_segments_on_keyframes);
  tcase_add_test(tc, fmp4_container_hands_access_units_to_the_mux);
  tcase_add_test(tc, lcevc_pes_needs_a_pts);
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
