/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/mux/flv/flv.h"

#include "ts_test_util.h"

#define NELEM(a) (sizeof(a) / sizeof((a)[0]))
#define VPID 0x201
#define APID 0x101
#define PMT_PID 0x100

typedef struct {
  unsigned video_tags;
  unsigned audio_tags;
  unsigned script_tags;
  unsigned markers;
  uint32_t last_ts;
} sink_t;

static void sink_cb(void *ctx, flv_tag_type_t type, uint32_t ts, const unsigned char *hdr, size_t hn, const unsigned char *payload, size_t pn) {
  sink_t *s = ctx;
  (void)hdr;
  (void)hn;
  if (type == FLV_TAG_VIDEO) s->video_tags++;
  else if (type == FLV_TAG_AUDIO) s->audio_tags++;
  else s->script_tags++;
  s->markers += count_frame_markers(payload, pn);
  s->last_ts = ts;
}

typedef struct {
  flv_opts_t opts;
  flv_t *f;
  sink_t sink;
  unsigned long long bytes;
} rig_t;

static void rig_open(rig_t *r) {
  memset(r, 0, sizeof *r);
  r->f = flv_new(&r->opts, 0, sink_cb, &r->sink, &r->bytes);
  ck_assert_ptr_nonnull(r->f);
}

static void rig_feed(rig_t *r, const ts_script_t *s) {
  for (size_t i = 0; i < s->n; i++) flv_feed(r->f, s->pkt + i * 188);
}

static void pes_at(ts_script_t *s, unsigned pid, unsigned char sid, int has_pts, uint64_t pts, int has_dts, uint64_t dts, const unsigned char *es, size_t n) {
  unsigned char *pes = malloc(n + 64);
  size_t len;
  ck_assert_ptr_nonnull(pes);
  len = build_pes_any(pes, sid, has_pts, pts, has_dts, dts, es, n);
  ts_script_pes(s, pid, pes, len);
  free(pes);
}

static void video_pes(ts_script_t *s, int has_pts, uint64_t pts, int has_dts, uint64_t dts, const unsigned char *es, size_t n) {
  pes_at(s, VPID, 0xE0, has_pts, pts, has_dts, dts, es, n);
}

static void audio_pes(ts_script_t *s, unsigned pid, int has_pts, uint64_t pts, const unsigned char *es, size_t n) {
  pes_at(s, pid, 0xC0, has_pts, pts, 0, 0, es, n);
}

static void discovery(ts_script_t *s, const ts_es_spec_t *es, size_t n) {
  ts_script_discovery(s, 101, PMT_PID, NULL, 0, es, n, 1);
}

static const ts_es_spec_t ES_H264[] = {{0x1B, VPID, NULL, 0}};
static const ts_es_spec_t ES_HEVC[] = {{0x24, VPID, NULL, 0}};
static const ts_es_spec_t ES_VVC[] = {{0x33, VPID, NULL, 0}};
static const ts_es_spec_t ES_MPEG2[] = {{0x02, VPID, NULL, 0}};
static const unsigned char AV1_DESC[] = {0x05, 0x04, 'A', 'V', '0', '1'};
static const ts_es_spec_t ES_AV1[] = {{0x06, VPID, AV1_DESC, sizeof AV1_DESC}};
static const ts_es_spec_t ES_AAC[] = {{0x0F, APID, NULL, 0}};
static const ts_es_spec_t ES_VIDEO_AAC[] = {{0x1B, VPID, NULL, 0}, {0x0F, APID, NULL, 0}};

START_TEST(h264_timestamp_forms) {
  static const struct {
    int has_pts;
    int has_dts;
    uint64_t pts[6];
    uint64_t dts[6];
  } t[] = {
      {1, 1, {90000, 100800, 97200, 104400, 108000, 111600}, {90000, 93600, 97200, 100800, 104400, 108000}},
      {1, 0, {90000, 93600, 97200, 100800, 104400, 108000}, {0}},
      {0, 0, {0}, {0}},
      {1, 1, {90000, 93600, 93600, 90000, 400000, 403600}, {90000, 93600, 93600, 90000, 400000, 403600}},
      {1, 1, {90000, 93600, 400000000, 400003600, 90000, 93600}, {90000, 93600, 400000000, 400003600, 90000, 93600}},
  };
  for (size_t c = 0; c < NELEM(t); c++) {
    rig_t r;
    ts_script_t s;
    unsigned char au[256] = {0};
    memset(&s, 0, sizeof s);
    rig_open(&r);
    discovery(&s, ES_H264, 1);
    for (size_t i = 0; i < 6; i++) video_pes(&s, t[c].has_pts, t[c].pts[i], t[c].has_dts, t[c].dts[i], au, build_h264_au(au, i == 0));
    rig_feed(&r, &s);
    ck_assert_int_eq(flv_close(r.f), 0);
    ck_assert_uint_gt(r.sink.video_tags, 0u);
    ck_assert_uint_gt(r.sink.script_tags, 0u);
    ts_script_free(&s);
  }
}
END_TEST

START_TEST(other_video_codecs_mux) {
  unsigned char au[256] = {0};
  const struct {
    const ts_es_spec_t *es;
    size_t (*build)(unsigned char *);
  } t[] = {{ES_VVC, build_vvc_au}, {ES_AV1, build_av1_au}};
  for (size_t c = 0; c < NELEM(t); c++) {
    rig_t r;
    ts_script_t s;
    memset(&s, 0, sizeof s);
    rig_open(&r);
    discovery(&s, t[c].es, 1);
    for (unsigned i = 0; i < 4; i++) video_pes(&s, 1, 90000 + i * 3600, 1, 90000 + i * 3600, au, t[c].build(au));
    rig_feed(&r, &s);
    ck_assert_int_eq(flv_close(r.f), 0);
    ck_assert_uint_gt(r.sink.video_tags, 0u);
    ts_script_free(&s);
  }
}
END_TEST

START_TEST(hevc_with_lcevc_strip_mux) {
  rig_t r;
  ts_script_t s;
  unsigned char au[256] = {0};
  memset(&s, 0, sizeof s);
  rig_open(&r);
  r.opts.strip_lcevc = 1;
  discovery(&s, ES_HEVC, 1);
  video_pes(&s, 1, 90000, 1, 90000, au, build_hevc_au(au, 1));
  video_pes(&s, 1, 93600, 1, 93600, au, build_hevc_au(au, 0));
  video_pes(&s, 1, 97200, 1, 97200, au, build_hevc_au(au, 0));
  rig_feed(&r, &s);
  ck_assert_int_eq(flv_close(r.f), 0);
  ck_assert_uint_gt(r.sink.video_tags, 0u);
  ts_script_free(&s);
}
END_TEST

static const unsigned char BAD_H264_SPS[] = {0x67, 0x00};
static const unsigned char BAD_H264_PPS[] = {0x68, 0xEB};
static const unsigned char BAD_IDR[] = {0x65, 0x88, 0x84, 0xAB};
static const unsigned char BAD_HEVC_VPS[] = {0x40, 0x01, 0x0C};
static const unsigned char BAD_HEVC_SPS[] = {0x42, 0x01, 0x01};
static const unsigned char BAD_HEVC_PPS[] = {0x44, 0x01};
static const unsigned char BAD_HEVC_IDR[] = {0x26, 0x01, 0xAF, 0xAB};
static const unsigned char BAD_VVC_VPS[] = {0x00, 0x71, 0xAA};
static const unsigned char BAD_VVC_SPS[] = {0x00, 0x79, 0x11};
static const unsigned char BAD_VVC_PPS[] = {0x00, 0x81, 0xCC};
static const unsigned char BAD_VVC_IDR[] = {0x00, 0x39, 0xEE, 0xAB};
static const unsigned char BAD_AV1_SEQ[] = {0x08, 0xFF};
static const unsigned char BAD_AV1_FRAME[] = {0x30, 0x00, 0xAB};

START_TEST(unparsable_or_incomplete_parameter_sets_emit_nothing) {
  static const unsigned char *const h264_bad[] = {BAD_H264_SPS, BAD_H264_PPS, BAD_IDR};
  static const size_t h264_bad_len[] = {sizeof BAD_H264_SPS, sizeof BAD_H264_PPS, sizeof BAD_IDR};
  static const unsigned char *const h264_sps_only[] = {h264_sps_1080p, BAD_IDR};
  static const size_t h264_sps_only_len[] = {sizeof h264_sps_1080p, sizeof BAD_IDR};
  static const unsigned char *const hevc_bad[] = {BAD_HEVC_VPS, BAD_HEVC_SPS, BAD_HEVC_PPS, BAD_HEVC_IDR};
  static const size_t hevc_bad_len[] = {sizeof BAD_HEVC_VPS, sizeof BAD_HEVC_SPS, sizeof BAD_HEVC_PPS, sizeof BAD_HEVC_IDR};
  static const unsigned char *const hevc_no_vps[] = {hevc_sps_1080p, hevc_pps, BAD_HEVC_IDR};
  static const size_t hevc_no_vps_len[] = {sizeof hevc_sps_1080p, sizeof hevc_pps, sizeof BAD_HEVC_IDR};
  static const unsigned char *const vvc_bad[] = {BAD_VVC_VPS, BAD_VVC_SPS, BAD_VVC_PPS, BAD_VVC_IDR};
  static const size_t vvc_bad_len[] = {sizeof BAD_VVC_VPS, sizeof BAD_VVC_SPS, sizeof BAD_VVC_PPS, sizeof BAD_VVC_IDR};
  static const unsigned char *const vvc_no_vps[] = {BAD_VVC_SPS, BAD_VVC_PPS, BAD_VVC_IDR};
  static const size_t vvc_no_vps_len[] = {sizeof BAD_VVC_SPS, sizeof BAD_VVC_PPS, sizeof BAD_VVC_IDR};
  static const unsigned char *const av1_bad[] = {BAD_AV1_SEQ, BAD_AV1_FRAME};
  static const size_t av1_bad_len[] = {sizeof BAD_AV1_SEQ, sizeof BAD_AV1_FRAME};
  static const unsigned char *const av1_frame_only[] = {BAD_AV1_FRAME};
  static const size_t av1_frame_only_len[] = {sizeof BAD_AV1_FRAME};
  static const struct {
    const ts_es_spec_t *es;
    const unsigned char *const *nals;
    const size_t *lens;
    unsigned n;
  } t[] = {
      {ES_H264, h264_bad, h264_bad_len, 3},     {ES_H264, h264_sps_only, h264_sps_only_len, 2}, {ES_HEVC, hevc_bad, hevc_bad_len, 4},
      {ES_HEVC, hevc_no_vps, hevc_no_vps_len, 3}, {ES_VVC, vvc_bad, vvc_bad_len, 4},           {ES_VVC, vvc_no_vps, vvc_no_vps_len, 3},
      {ES_AV1, av1_bad, av1_bad_len, 2},        {ES_AV1, av1_frame_only, av1_frame_only_len, 1},
  };
  for (size_t c = 0; c < NELEM(t); c++) {
    rig_t r;
    ts_script_t s;
    unsigned char au[256] = {0};
    memset(&s, 0, sizeof s);
    rig_open(&r);
    discovery(&s, t[c].es, 1);
    for (unsigned i = 0; i < 3; i++) video_pes(&s, 1, 90000 + i * 3600, 1, 90000 + i * 3600, au, build_annexb_au(au, t[c].nals, t[c].lens, t[c].n));
    rig_feed(&r, &s);
    ck_assert_int_eq(flv_close(r.f), 0);
    ck_assert_uint_eq(r.sink.video_tags, 0u);
    ts_script_free(&s);
  }
}
END_TEST

START_TEST(header_without_keyframe_waits) {
  rig_t r;
  ts_script_t s;
  unsigned char au[256] = {0};
  unsigned char slice[4] = {0x41, 0x9A, 0xAB, 0xAB};
  const unsigned char *nals[3] = {h264_sps_1080p, h264_pps, slice};
  size_t lens[3] = {sizeof h264_sps_1080p, sizeof h264_pps, sizeof slice};
  memset(&s, 0, sizeof s);
  rig_open(&r);
  discovery(&s, ES_H264, 1);
  for (unsigned i = 0; i < 3; i++) video_pes(&s, 1, 90000 + i * 3600, 1, 90000 + i * 3600, au, build_annexb_au(au, nals, lens, 3));
  rig_feed(&r, &s);
  ck_assert_int_eq(flv_close(r.f), 0);
  ck_assert_uint_eq(r.sink.video_tags, 0u);
  ts_script_free(&s);
}
END_TEST

START_TEST(unsupported_streams_are_skipped) {
  static const ts_es_spec_t mp2[] = {{0x03, APID, NULL, 0}};
  static const unsigned char ac4_desc[] = {0x7F, 0x02, 0x15, 0x00};
  static const ts_es_spec_t ac4[] = {{0x06, APID, ac4_desc, sizeof ac4_desc}};
  static const ts_es_spec_t none[] = {{0x05, 0x300, NULL, 0}};
  const ts_es_spec_t *sets[] = {ES_MPEG2, mp2, ac4, none};
  for (size_t c = 0; c < NELEM(sets); c++) {
    rig_t r;
    ts_script_t s;
    memset(&s, 0, sizeof s);
    rig_open(&r);
    discovery(&s, sets[c], 1);
    rig_feed(&r, &s);
    ck_assert_int_eq(flv_close(r.f), 0);
    ck_assert_uint_eq(r.sink.script_tags, 0u);
    ts_script_free(&s);
  }
}
END_TEST

START_TEST(audio_codecs_and_track_selection) {
  static const ts_es_spec_t ac3[] = {{0x81, APID, NULL, 0}};
  static const ts_es_spec_t eac3[] = {{0x87, APID, NULL, 0}};
  static const ts_es_spec_t latm[] = {{0x11, APID, NULL, 0}};
  static const ts_es_spec_t two[] = {{0x0F, 0x110, NULL, 0}, {0x0F, 0x111, NULL, 0}};
  static const struct {
    const ts_es_spec_t *es;
    size_t n;
    unsigned track;
  } t[] = {{ac3, 1, 0}, {eac3, 1, 0}, {latm, 1, 0}, {two, 2, 2}, {two, 2, 0}, {two, 2, 3}};
  for (size_t c = 0; c < NELEM(t); c++) {
    rig_t r;
    ts_script_t s;
    unsigned char adts[128];
    size_t alen = build_adts_frame(adts, 100);
    memset(&s, 0, sizeof s);
    rig_open(&r);
    r.opts.audio_track = t[c].track;
    discovery(&s, t[c].es, t[c].n);
    for (unsigned i = 0; i < 4; i++) {
      audio_pes(&s, 0x110, 1, 90000 + i * 1920, adts, alen);
      audio_pes(&s, 0x111, 1, 90000 + i * 1920, adts, alen);
    }
    rig_feed(&r, &s);
    ck_assert_int_eq(flv_close(r.f), 0);
    ts_script_free(&s);
  }
}
END_TEST

START_TEST(audio_only_carry_over_and_resync) {
  rig_t r;
  ts_script_t s;
  unsigned char adts[256];
  unsigned char junk[40];
  size_t alen;
  memset(&s, 0, sizeof s);
  memset(junk, 0x5A, sizeof junk);
  rig_open(&r);
  discovery(&s, ES_AAC, 1);
  alen = build_adts_frame(adts, 100);
  audio_pes(&s, APID, 1, 90000, junk, sizeof junk);
  audio_pes(&s, APID, 1, 90000, adts, alen);
  audio_pes(&s, APID, 1, 93000, adts, 50);
  audio_pes(&s, APID, 1, 93000, adts + 50, alen - 50);
  audio_pes(&s, APID, 0, 0, adts, alen);
  audio_pes(&s, APID, 1, 99000, adts, alen);
  audio_pes(&s, APID, 1, 99500, adts, alen);
  rig_feed(&r, &s);
  ck_assert_int_eq(flv_close(r.f), 0);
  ck_assert_uint_ge(r.sink.audio_tags, 3u);
  ts_script_free(&s);
}
END_TEST

START_TEST(audio_waits_for_video_header) {
  rig_t r;
  ts_script_t s;
  unsigned char adts[128];
  unsigned char au[256] = {0};
  size_t alen = build_adts_frame(adts, 100);
  memset(&s, 0, sizeof s);
  rig_open(&r);
  discovery(&s, ES_VIDEO_AAC, 2);
  for (unsigned i = 0; i < 3; i++) audio_pes(&s, APID, 1, 90000 + i * 1920, adts, alen);
  rig_feed(&r, &s);
  ck_assert_uint_eq(r.sink.script_tags, 0u);
  ts_script_free(&s);
  for (unsigned i = 0; i < 4; i++) video_pes(&s, 1, 91000 + i * 3600, 1, 91000 + i * 3600, au, build_h264_au(au, i == 0));
  for (unsigned i = 3; i < 6; i++) audio_pes(&s, APID, 1, 90000 + i * 1920, adts, alen);
  rig_feed(&r, &s);
  ck_assert_int_eq(flv_close(r.f), 0);
  ck_assert_uint_gt(r.sink.script_tags, 0u);
  ck_assert_uint_gt(r.sink.video_tags, 0u);
  ck_assert_uint_gt(r.sink.audio_tags, 0u);
  ts_script_free(&s);
}
END_TEST

START_TEST(unrelated_pids_are_ignored) {
  rig_t r;
  ts_script_t s;
  unsigned char adts[128];
  unsigned char au[64] = {0};
  size_t alen = build_adts_frame(adts, 100);
  memset(&s, 0, sizeof s);
  rig_open(&r);
  discovery(&s, ES_AAC, 1);
  for (unsigned i = 0; i < 3; i++) {
    audio_pes(&s, APID, 1, 90000 + i * 1920, adts, alen);
    pes_at(&s, 0x555, 0xC0, 1, 90000, 0, 0, au, sizeof au);
  }
  rig_feed(&r, &s);
  ck_assert_int_eq(flv_close(r.f), 0);
  ts_script_free(&s);
}
END_TEST

static Suite *flv_feed_suite(void) {
  Suite *su = suite_create("flv_feed");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, h264_timestamp_forms);
  tcase_add_test(tc, other_video_codecs_mux);
  tcase_add_test(tc, hevc_with_lcevc_strip_mux);
  tcase_add_test(tc, unparsable_or_incomplete_parameter_sets_emit_nothing);
  tcase_add_test(tc, header_without_keyframe_waits);
  tcase_add_test(tc, unsupported_streams_are_skipped);
  tcase_add_test(tc, audio_codecs_and_track_selection);
  tcase_add_test(tc, audio_only_carry_over_and_resync);
  tcase_add_test(tc, audio_waits_for_video_header);
  tcase_add_test(tc, unrelated_pids_are_ignored);
  suite_add_tcase(su, tc);
  return su;
}

int main(void) {
  SRunner *sr = srunner_create(flv_feed_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
