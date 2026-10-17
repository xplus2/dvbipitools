/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../run_helper.h"
#include "lib/mux/mkv/mkv.h"
#include "lib/mux/psi_build.h"

#include "ts_test_util.h"

#define NELEM(a) (sizeof(a) / sizeof((a)[0]))
#define VPID 0x201
#define APID 0x101
#define TPID 0x301
#define PMT_PID 0x100

typedef struct {
  mkv_opts_t cfg;
  mkv_t *m;
  char path[48];
  int fd;
  unsigned long long bytes;
} rig_t;

static void rig_open(rig_t *r, int video_ok) {
  memset(r, 0, sizeof *r);
  r->cfg.audio_all = 1;
  r->cfg.app_name = "test";
  strcpy(r->path, "/tmp/dvbipitools_test_mkvfeed_XXXXXX");
  r->fd = mkstemp(r->path);
  ck_assert_int_ge(r->fd, 0);
  r->m = mkv_new(r->fd, &r->cfg, video_ok, &r->bytes, NULL, 0);
  ck_assert_ptr_nonnull(r->m);
}

static void rig_feed(rig_t *r, const ts_script_t *s) {
  for (size_t i = 0; i < s->n; i++) mkv_feed(r->m, s->pkt + i * 188);
}

static unsigned char *rig_finish(rig_t *r, size_t *len, int *close_rc) {
  unsigned char *buf;
  *close_rc = mkv_close(r->m);
  r->m = NULL;
  close(r->fd);
  buf = slurp_file(r->path, len);
  unlink(r->path);
  ck_assert_ptr_nonnull(buf);
  return buf;
}

static int has_str(const unsigned char *buf, size_t len, const char *s) {
  return memmem(buf, len, s, strlen(s)) != NULL;
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

static void discovery(ts_script_t *s, const ts_es_spec_t *es, size_t n, int sdt) {
  ts_script_discovery(s, 101, PMT_PID, NULL, 0, es, n, sdt);
}

static const ts_es_spec_t ES_H264[] = {{0x1B, VPID, NULL, 0}};
static const ts_es_spec_t ES_HEVC[] = {{0x24, VPID, NULL, 0}};
static const ts_es_spec_t ES_VVC[] = {{0x33, VPID, NULL, 0}};
static const ts_es_spec_t ES_MPEG2[] = {{0x02, VPID, NULL, 0}};
static const unsigned char AV1_DESC[] = {0x05, 0x04, 'A', 'V', '0', '1'};
static const ts_es_spec_t ES_AV1[] = {{0x06, VPID, AV1_DESC, sizeof AV1_DESC}};
static const ts_es_spec_t ES_AAC[] = {{0x0F, APID, NULL, 0}};

static unsigned count_markers(const unsigned char *buf, size_t len) {
  return count_frame_markers(buf, len);
}

START_TEST(video_without_pts_is_muxed) {
  rig_t r;
  ts_script_t s;
  unsigned char au[256] = {0};
  unsigned char *buf;
  size_t len;
  int rc;
  memset(&s, 0, sizeof s);
  rig_open(&r, 1);
  discovery(&s, ES_H264, 1, 1);
  video_pes(&s, 0, 0, 0, 0, au, build_h264_au(au, 1));
  video_pes(&s, 0, 0, 0, 0, au, build_h264_au(au, 0));
  video_pes(&s, 0, 0, 0, 0, au, build_h264_au(au, 0));
  rig_feed(&r, &s);
  buf = rig_finish(&r, &len, &rc);
  ck_assert_int_eq(rc, 0);
  ck_assert(has_str(buf, len, "V_MPEG4/ISO/AVC"));
  ck_assert_uint_eq(count_markers(buf, len), 2u);
  free(buf);
  ts_script_free(&s);
}
END_TEST

START_TEST(video_timestamp_steps_and_jumps) {
  static const struct {
    uint64_t pts[6];
  } t[] = {
      {{90000, 93600, 97200, 100800, 104400, 108000}},
      {{90000, 93600, 93600, 90000, 400000, 403600}},
      {{90000, 93600, 400000000, 400003600, 90000, 93600}},
  };
  for (size_t c = 0; c < NELEM(t); c++) {
    rig_t r;
    ts_script_t s;
    unsigned char au[256] = {0};
    unsigned char *buf;
    size_t len;
    int rc;
    memset(&s, 0, sizeof s);
    rig_open(&r, 1);
    discovery(&s, ES_H264, 1, 1);
    for (size_t i = 0; i < 6; i++) video_pes(&s, 1, t[c].pts[i], 1, t[c].pts[i], au, build_h264_au(au, i == 0));
    rig_feed(&r, &s);
    ck_assert_int_eq(mkv_error(r.m), 0);
    buf = rig_finish(&r, &len, &rc);
    ck_assert_int_eq(rc, 0);
    ck_assert_uint_ge(count_markers(buf, len), 1u);
    free(buf);
    ts_script_free(&s);
  }
}
END_TEST

START_TEST(missing_service_name_delays_start_until_close) {
  rig_t r;
  ts_script_t s;
  unsigned char au[256] = {0};
  unsigned char *buf;
  size_t len;
  int rc;
  memset(&s, 0, sizeof s);
  rig_open(&r, 1);
  discovery(&s, ES_H264, 1, 0);
  video_pes(&s, 1, 90000, 1, 90000, au, build_h264_au(au, 1));
  video_pes(&s, 1, 93600, 1, 93600, au, build_h264_au(au, 0));
  video_pes(&s, 1, 97200, 1, 97200, au, build_h264_au(au, 0));
  rig_feed(&r, &s);
  ck_assert_uint_eq((unsigned)r.bytes, 0u);
  buf = rig_finish(&r, &len, &rc);
  ck_assert_int_eq(rc, 0);
  ck_assert_uint_ge(count_markers(buf, len), 1u);
  free(buf);
  ts_script_free(&s);
}
END_TEST

typedef struct {
  const ts_es_spec_t *es;
  const unsigned char *nals[5];
  size_t lens[5];
  unsigned n;
} bad_sps_case_t;

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

START_TEST(unparsable_parameter_sets_never_start_the_track) {
  const bad_sps_case_t t[] = {
      {ES_H264, {BAD_H264_SPS, BAD_H264_PPS, BAD_IDR}, {sizeof BAD_H264_SPS, sizeof BAD_H264_PPS, sizeof BAD_IDR}, 3},
      {ES_HEVC, {BAD_HEVC_VPS, BAD_HEVC_SPS, BAD_HEVC_PPS, BAD_HEVC_IDR}, {sizeof BAD_HEVC_VPS, sizeof BAD_HEVC_SPS, sizeof BAD_HEVC_PPS, sizeof BAD_HEVC_IDR}, 4},
      {ES_VVC, {BAD_VVC_VPS, BAD_VVC_SPS, BAD_VVC_PPS, BAD_VVC_IDR}, {sizeof BAD_VVC_VPS, sizeof BAD_VVC_SPS, sizeof BAD_VVC_PPS, sizeof BAD_VVC_IDR}, 4},
      {ES_AV1, {BAD_AV1_SEQ, BAD_AV1_FRAME}, {sizeof BAD_AV1_SEQ, sizeof BAD_AV1_FRAME}, 2},
  };
  for (size_t c = 0; c < NELEM(t); c++) {
    rig_t r;
    ts_script_t s;
    unsigned char au[128] = {0};
    unsigned char *buf;
    size_t len;
    int rc;
    memset(&s, 0, sizeof s);
    rig_open(&r, 1);
    discovery(&s, t[c].es, 1, 1);
    for (unsigned i = 0; i < 3; i++) {
      video_pes(&s, 1, 90000 + i * 3600, 1, 90000 + i * 3600, au, build_annexb_au(au, t[c].nals, t[c].lens, t[c].n));
    }
    rig_feed(&r, &s);
    ck_assert_int_eq(mkv_error(r.m), 0);
    buf = rig_finish(&r, &len, &rc);
    ck_assert_int_eq(rc, 0);
    ck_assert_uint_eq(count_markers(buf, len), 0u);
    free(buf);
    ts_script_free(&s);
  }
}
END_TEST

START_TEST(lcevc_stripping_option_still_muxes) {
  rig_t r;
  ts_script_t s;
  unsigned char au[256] = {0};
  unsigned char *buf;
  size_t len;
  int rc;
  memset(&s, 0, sizeof s);
  rig_open(&r, 1);
  r.cfg.strip_lcevc = 1;
  discovery(&s, ES_H264, 1, 1);
  video_pes(&s, 1, 90000, 1, 90000, au, build_h264_au(au, 1));
  video_pes(&s, 1, 93600, 1, 93600, au, build_h264_au(au, 0));
  video_pes(&s, 1, 97200, 1, 97200, au, build_h264_au(au, 0));
  rig_feed(&r, &s);
  buf = rig_finish(&r, &len, &rc);
  ck_assert_int_eq(rc, 0);
  ck_assert_uint_ge(count_markers(buf, len), 2u);
  free(buf);
  ts_script_free(&s);
}
END_TEST

static const unsigned char M2_SEQ_OK[] = {0x00, 0x00, 0x01, 0xB3, 0x78, 0x04, 0x38, 0x13, 0xFF, 0xFF, 0xE0, 0x18};
static const unsigned char M2_SEQ_ZERO[] = {0x00, 0x00, 0x01, 0xB3, 0x00, 0x00, 0x00, 0x13, 0xFF, 0xFF, 0xE0, 0x18};
static const unsigned char M2_SEQ_SHORT[] = {0x00, 0x00, 0x01, 0xB3, 0x78, 0x04};
static const unsigned char M2_PIC_I[] = {0x00, 0x00, 0x01, 0x00, 0x00, 0x08, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB};
static const unsigned char M2_PIC_P[] = {0x00, 0x00, 0x01, 0x00, 0x00, 0x10, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB, 0xAB};
static const unsigned char M2_PIC_SHORT[] = {0x00, 0x00, 0x01, 0x00, 0x00};
static const unsigned char M2_TRAIL[] = {0x00, 0x00, 0x01};
static const unsigned char M2_NOSC[] = {0x12, 0x34, 0x56};

START_TEST(mpeg2_picture_and_header_variants) {
  static const struct {
    const unsigned char *part[4];
    size_t len[4];
    unsigned n;
    unsigned want_markers;
  } t[] = {
      {{M2_SEQ_OK, M2_PIC_I}, {sizeof M2_SEQ_OK, sizeof M2_PIC_I}, 2, 1},
      {{M2_SEQ_ZERO, M2_PIC_I}, {sizeof M2_SEQ_ZERO, sizeof M2_PIC_I}, 2, 0},
      {{M2_SEQ_SHORT, M2_PIC_I}, {sizeof M2_SEQ_SHORT, sizeof M2_PIC_I}, 2, 0},
      {{M2_PIC_P, M2_SEQ_OK}, {sizeof M2_PIC_P, sizeof M2_SEQ_OK}, 2, 0},
      {{M2_SEQ_OK, M2_PIC_SHORT, M2_PIC_P}, {sizeof M2_SEQ_OK, sizeof M2_PIC_SHORT, sizeof M2_PIC_P}, 3, 0},
      {{M2_SEQ_OK, M2_PIC_I, M2_TRAIL}, {sizeof M2_SEQ_OK, sizeof M2_PIC_I, sizeof M2_TRAIL}, 3, 1},
      {{M2_NOSC}, {sizeof M2_NOSC}, 1, 0},
  };
  for (size_t c = 0; c < NELEM(t); c++) {
    rig_t r;
    ts_script_t s;
    unsigned char au[256] = {0};
    unsigned char *buf;
    size_t len;
    size_t n = 0;
    int rc;
    for (unsigned i = 0; i < t[c].n; i++) {
      memcpy(au + n, t[c].part[i], t[c].len[i]);
      n += t[c].len[i];
    }
    memset(&s, 0, sizeof s);
    rig_open(&r, 1);
    discovery(&s, ES_MPEG2, 1, 1);
    video_pes(&s, 1, 90000, 0, 0, au, n);
    video_pes(&s, 0, 0, 0, 0, au, n);
    video_pes(&s, 1, 93600, 0, 0, au, n);
    rig_feed(&r, &s);
    buf = rig_finish(&r, &len, &rc);
    ck_assert_int_eq(rc, 0);
    if (t[c].want_markers) ck_assert_uint_ge(count_markers(buf, len), 1u);
    else ck_assert_uint_eq(count_markers(buf, len), 0u);
    free(buf);
    ts_script_free(&s);
  }
}
END_TEST

START_TEST(audio_carry_over_and_resync) {
  rig_t r;
  ts_script_t s;
  unsigned char adts[256];
  unsigned char junk[40];
  unsigned char *buf;
  size_t len;
  size_t alen;
  int rc;
  memset(&s, 0, sizeof s);
  memset(junk, 0x5A, sizeof junk);
  rig_open(&r, 0);
  discovery(&s, ES_AAC, 1, 1);
  alen = build_adts_frame(adts, 100);
  audio_pes(&s, APID, 1, 90000, junk, sizeof junk);
  audio_pes(&s, APID, 1, 90000, adts, alen);
  audio_pes(&s, APID, 1, 93000, adts, 50);
  audio_pes(&s, APID, 1, 93000, adts + 50, alen - 50);
  audio_pes(&s, APID, 0, 0, adts, alen);
  audio_pes(&s, APID, 1, 99000, adts, alen);
  rig_feed(&r, &s);
  buf = rig_finish(&r, &len, &rc);
  ck_assert_int_eq(rc, 0);
  ck_assert_uint_ge(count_markers(buf, len), 3u);
  free(buf);
  ts_script_free(&s);
}
END_TEST

static const unsigned char AC4_MONO[] = {0xac, 0x40, 0x00, 0x06, 0x00, 0x05, 0xb4, 0x00, 0x00, 0x00};
static const unsigned char AC4_NO_PRES[] = {0xac, 0x40, 0x00, 0x04, 0x00, 0x05, 0x30, 0x00};
static const unsigned char AC4_DESC[] = {0x7F, 0x02, 0x15, 0x00};
static const ts_es_spec_t ES_AC4[] = {{0x06, APID, AC4_DESC, sizeof AC4_DESC}};

START_TEST(ac4_frames_and_oversized_carry_over) {
  rig_t r;
  ts_script_t s;
  unsigned char *big = calloc(1, 60000);
  unsigned char *buf;
  size_t len;
  int rc;
  ck_assert_ptr_nonnull(big);
  big[0] = 0xAC;
  big[1] = 0x40;
  big[2] = 0xFF;
  big[3] = 0xFF;
  big[4] = 0x03;
  big[5] = 0x0D;
  big[6] = 0x40;
  memset(&s, 0, sizeof s);
  rig_open(&r, 0);
  discovery(&s, ES_AC4, 1, 1);
  audio_pes(&s, APID, 1, 90000, AC4_MONO, sizeof AC4_MONO);
  audio_pes(&s, APID, 1, 91800, AC4_NO_PRES, sizeof AC4_NO_PRES);
  audio_pes(&s, APID, 1, 93600, AC4_MONO, sizeof AC4_MONO);
  for (int i = 0; i < 3; i++) audio_pes(&s, APID, 1, 95000 + (unsigned)i * 1000, big, 60000);
  audio_pes(&s, APID, 1, 99000, AC4_MONO, sizeof AC4_MONO);
  audio_pes(&s, APID, 1, 99900, AC4_MONO, sizeof AC4_MONO);
  rig_feed(&r, &s);
  buf = rig_finish(&r, &len, &rc);
  ck_assert_int_eq(rc, 0);
  ck_assert(has_str(buf, len, "A_AC4"));
  free(buf);
  free(big);
  ts_script_free(&s);
}
END_TEST

static const unsigned char HDMV_REG[] = {0x05, 0x04, 'H', 'D', 'M', 'V'};
static const unsigned char D_AC3[] = {0x6A, 0x01, 0x00};
static const unsigned char D_EAC3[] = {0x7A, 0x01, 0x00};
static const unsigned char D_DTS[] = {0x7B, 0x01, 0x00};
static const unsigned char D_AC4[] = {0x7F, 0x02, 0x15, 0x00};
static const unsigned char D_OPUS[] = {0x05, 0x04, 'O', 'p', 'u', 's'};

START_TEST(every_supported_audio_codec_gets_a_track) {
  static const ts_es_spec_t set_a[] = {
      {0x03, 0x110, NULL, 0}, {0x0F, 0x111, NULL, 0}, {0x11, 0x112, NULL, 0}, {0x81, 0x113, NULL, 0},
      {0x87, 0x114, NULL, 0}, {0x06, 0x115, D_OPUS, sizeof D_OPUS}, {0x06, 0x116, D_DTS, sizeof D_DTS}, {0x06, 0x117, D_AC4, sizeof D_AC4},
  };
  static const ts_es_spec_t set_b[] = {
      {0x82, 0x120, NULL, 0}, {0x83, 0x121, NULL, 0}, {0x85, 0x122, NULL, 0}, {0x86, 0x123, NULL, 0},
      {0x06, 0x124, D_AC3, sizeof D_AC3}, {0x06, 0x125, D_EAC3, sizeof D_EAC3},
  };
  const ts_es_spec_t *sets[2] = {set_a, set_b};
  const size_t counts[2] = {NELEM(set_a), NELEM(set_b)};
  for (int c = 0; c < 2; c++) {
    rig_t r;
    ts_script_t s;
    unsigned char *buf;
    size_t len;
    int rc;
    memset(&s, 0, sizeof s);
    rig_open(&r, 0);
    ts_script_discovery(&s, 101, PMT_PID, c ? HDMV_REG : NULL, c ? sizeof HDMV_REG : 0, sets[c], counts[c], 1);
    rig_feed(&r, &s);
    ck_assert_int_eq(mkv_error(r.m), 0);
    buf = rig_finish(&r, &len, &rc);
    ck_assert_int_eq(rc, 0);
    free(buf);
    ts_script_free(&s);
  }
}
END_TEST

START_TEST(audio_track_selection_skips_other_tracks) {
  static const ts_es_spec_t two[] = {{0x0F, 0x110, NULL, 0}, {0x0F, 0x111, NULL, 0}};
  rig_t r;
  ts_script_t s;
  unsigned char adts[128];
  unsigned char *buf;
  size_t len;
  size_t alen = build_adts_frame(adts, 100);
  int rc;
  memset(&s, 0, sizeof s);
  rig_open(&r, 0);
  r.cfg.audio_all = 0;
  r.cfg.audio_track = 2;
  discovery(&s, two, 2, 1);
  for (unsigned i = 0; i < 3; i++) {
    audio_pes(&s, 0x110, 1, 90000 + i * 1920, adts, alen);
    audio_pes(&s, 0x111, 1, 90000 + i * 1920, adts, alen);
  }
  rig_feed(&r, &s);
  buf = rig_finish(&r, &len, &rc);
  ck_assert_int_eq(rc, 0);
  ck_assert_uint_ge(count_markers(buf, len), 1u);
  free(buf);
  ts_script_free(&s);
}
END_TEST

static const unsigned char TTX_DESC[] = {0x56, 0x05, 'e', 'n', 'g', (2 << 3) | 7, 0x77};
static const ts_es_spec_t ES_VIDEO_TTX[] = {{0x1B, VPID, NULL, 0}, {0x06, TPID, TTX_DESC, sizeof TTX_DESC}};
static const ts_es_spec_t ES_AUDIO_TTX[] = {{0x0F, APID, NULL, 0}, {0x06, TPID, TTX_DESC, sizeof TTX_DESC}};

static void ttx_pes_at(ts_script_t *s, unsigned page, unsigned pkt, const char *text, uint64_t pts_ms) {
  unsigned char es[64];
  size_t n = build_ttx_pes(es, page, pkt, text);
  pes_at(s, TPID, 0xBD, 1, pts_ms * 90, 0, 0, es, n);
}

static void ttx_cue(ts_script_t *s, const char *text, uint64_t start_ms, uint64_t end_ms) {
  ttx_pes_at(s, 777, 0, "", start_ms);
  ttx_pes_at(s, 777, 1, text, start_ms);
  ttx_pes_at(s, 777, 0, "", end_ms);
}

typedef struct {
  int video;
  uint64_t cue_start[3];
  uint64_t cue_end[3];
  unsigned ncue;
  int cues_first;
  int sub_lead;
} ttx_case_t;

START_TEST(teletext_cue_timing_variants) {
  static const ttx_case_t t[] = {
      {1, {1000, 3000, 5000}, {2000, 4000, 6000}, 3, 0, 0},
      {1, {1000, 3000, 5000}, {2000, 4000, 6000}, 3, 1, 0},
      {0, {1000, 3000, 5000}, {2000, 4000, 6000}, 3, 0, 0},
      {0, {1000, 3000, 5000}, {2000, 4000, 6000}, 3, 1, 0},
      {1, {500, 1000, 1500}, {900, 1800, 2400}, 3, 0, 700},
      {0, {100, 500, 3500}, {400, 3000, 3600}, 3, 0, 800},
  };
  for (size_t c = 0; c < NELEM(t); c++) {
    rig_t r;
    ts_script_t s;
    unsigned char au[256] = {0};
    unsigned char adts[128];
    unsigned char *buf;
    size_t len;
    size_t alen = build_adts_frame(adts, 100);
    int rc;
    memset(&s, 0, sizeof s);
    rig_open(&r, t[c].video);
    r.cfg.subs_srt = 1;
    r.cfg.sub_lead_ms = t[c].sub_lead;
    discovery(&s, t[c].video ? ES_VIDEO_TTX : ES_AUDIO_TTX, 2, 1);
    if (t[c].cues_first) {
      for (unsigned i = 0; i < t[c].ncue; i++) ttx_cue(&s, "HELLO", t[c].cue_start[i] + 90000 / 90, t[c].cue_end[i] + 1000);
    }
    for (unsigned i = 0; i < 6; i++) {
      uint64_t pts = 90000 + i * 90000;
      if (t[c].video) video_pes(&s, 1, pts, 1, pts, au, build_h264_au(au, i == 0));
      else audio_pes(&s, APID, 1, pts, adts, alen);
      if (!t[c].cues_first && i < t[c].ncue) ttx_cue(&s, "HELLO", t[c].cue_start[i] + 1000, t[c].cue_end[i] + 1000);
    }
    rig_feed(&r, &s);
    ck_assert_int_eq(mkv_error(r.m), 0);
    buf = rig_finish(&r, &len, &rc);
    ck_assert_int_eq(rc, 0);
    ck_assert(has_str(buf, len, "S_TEXT/UTF8"));
    free(buf);
    ts_script_free(&s);
  }
}
END_TEST

START_TEST(teletext_cue_text_edge_cases) {
  rig_t r;
  ts_script_t s;
  unsigned char adts[128];
  unsigned char *buf;
  size_t len;
  size_t alen = build_adts_frame(adts, 100);
  int rc;
  memset(&s, 0, sizeof s);
  rig_open(&r, 0);
  r.cfg.subs_srt = 1;
  discovery(&s, ES_AUDIO_TTX, 2, 1);
  for (unsigned i = 0; i < 4; i++) {
    audio_pes(&s, APID, 1, 90000 + i * 90000, adts, alen);
    ttx_cue(&s, "", 1000 + i * 1000, 1000 + i * 1000);
    ttx_cue(&s, "SAME", 1500 + i * 1000, 1500 + i * 1000);
  }
  rig_feed(&r, &s);
  buf = rig_finish(&r, &len, &rc);
  ck_assert_int_eq(rc, 0);
  free(buf);
  ts_script_free(&s);
}
END_TEST

START_TEST(teletext_track_needs_single_program_and_a_page) {
  static const unsigned char no_page_desc[] = {0x56, 0x05, 'e', 'n', 'g', 0x00, 0x00};
  static const ts_es_spec_t es[] = {{0x0F, APID, NULL, 0}, {0x06, TPID, no_page_desc, sizeof no_page_desc}, {0x06, TPID + 1, TTX_DESC, sizeof TTX_DESC}};
  rig_t r;
  ts_script_t s;
  unsigned char *buf;
  size_t len;
  int rc;
  memset(&s, 0, sizeof s);
  rig_open(&r, 0);
  r.cfg.subs_srt = 1;
  discovery(&s, es, 3, 1);
  rig_feed(&r, &s);
  buf = rig_finish(&r, &len, &rc);
  ck_assert_int_eq(rc, 0);
  free(buf);
  ts_script_free(&s);
}
END_TEST

START_TEST(write_failure_stops_further_processing) {
  mkv_opts_t cfg;
  mkv_t *m;
  ts_script_t s;
  unsigned char adts[128];
  unsigned long long bytes = 0;
  size_t alen = build_adts_frame(adts, 100);
  memset(&cfg, 0, sizeof cfg);
  cfg.audio_all = 1;
  cfg.app_name = "test";
  memset(&s, 0, sizeof s);
  m = mkv_new(-1, &cfg, 0, &bytes, NULL, 0);
  ck_assert_ptr_nonnull(m);
  discovery(&s, ES_AAC, 1, 1);
  for (unsigned i = 0; i < 40; i++) audio_pes(&s, APID, 1, 90000 + i * 1920, adts, alen);
  for (size_t i = 0; i < s.n; i++) mkv_feed(m, s.pkt + i * 188);
  ck_assert_int_ne(mkv_error(m), 0);
  ck_assert_int_eq(mkv_close(m), -1);
  ts_script_free(&s);
}
END_TEST

START_TEST(expired_grace_starts_without_service_name) {
  rig_t r;
  ts_script_t s;
  ts_script_t more;
  unsigned char au[256] = {0};
  unsigned char *buf;
  size_t len;
  int rc;
  memset(&s, 0, sizeof s);
  memset(&more, 0, sizeof more);
  rig_open(&r, 1);
  discovery(&s, ES_H264, 1, 0);
  video_pes(&s, 1, 90000, 1, 90000, au, build_h264_au(au, 1));
  video_pes(&s, 1, 93600, 1, 93600, au, build_h264_au(au, 0));
  rig_feed(&r, &s);
  ck_assert_uint_eq((unsigned)r.bytes, 0u);
  sleep_ms(2100);
  video_pes(&more, 1, 97200, 1, 97200, au, build_h264_au(au, 0));
  rig_feed(&r, &more);
  ck_assert_uint_gt((unsigned)r.bytes, 0u);
  buf = rig_finish(&r, &len, &rc);
  ck_assert_int_eq(rc, 0);
  free(buf);
  ts_script_free(&s);
  ts_script_free(&more);
}
END_TEST

START_TEST(incomplete_parameter_set_combinations) {
  static const unsigned char *const h264_sps_only[] = {h264_sps_1080p, BAD_IDR};
  static const size_t h264_sps_only_len[] = {sizeof h264_sps_1080p, sizeof BAD_IDR};
  static const unsigned char *const hevc_no_vps[] = {hevc_sps_1080p, hevc_pps, BAD_HEVC_IDR};
  static const size_t hevc_no_vps_len[] = {sizeof hevc_sps_1080p, sizeof hevc_pps, sizeof BAD_HEVC_IDR};
  static const unsigned char *const vvc_no_vps[] = {BAD_VVC_SPS, BAD_VVC_PPS, BAD_VVC_IDR};
  static const size_t vvc_no_vps_len[] = {sizeof BAD_VVC_SPS, sizeof BAD_VVC_PPS, sizeof BAD_VVC_IDR};
  static const unsigned char *const av1_frame_only[] = {BAD_AV1_FRAME};
  static const size_t av1_frame_only_len[] = {sizeof BAD_AV1_FRAME};
  static const unsigned char *const h264_no_key[] = {h264_sps_1080p, h264_pps, BAD_IDR};
  static const struct {
    const ts_es_spec_t *es;
    const unsigned char *const *nals;
    const size_t *lens;
    unsigned n;
  } t[] = {
      {ES_H264, h264_sps_only, h264_sps_only_len, 2},
      {ES_HEVC, hevc_no_vps, hevc_no_vps_len, 3},
      {ES_VVC, vvc_no_vps, vvc_no_vps_len, 3},
      {ES_AV1, av1_frame_only, av1_frame_only_len, 1},
      {ES_H264, h264_no_key, NULL, 0},
  };
  for (size_t c = 0; c < NELEM(t); c++) {
    rig_t r;
    ts_script_t s;
    unsigned char au[256] = {0};
    unsigned char *buf;
    size_t len;
    size_t alen;
    int rc;
    memset(&s, 0, sizeof s);
    rig_open(&r, 1);
    discovery(&s, t[c].es, 1, 1);
    for (unsigned i = 0; i < 3; i++) {
      if (t[c].lens) alen = build_annexb_au(au, t[c].nals, t[c].lens, t[c].n);
      else {
        unsigned char slice[8] = {0x41, 0x9A, 0xAB, 0xAB};
        const unsigned char *nals[3] = {h264_sps_1080p, h264_pps, slice};
        size_t lens[3] = {sizeof h264_sps_1080p, sizeof h264_pps, 4};
        alen = build_annexb_au(au, nals, lens, 3);
      }
      video_pes(&s, 1, 90000 + i * 3600, 1, 90000 + i * 3600, au, alen);
    }
    rig_feed(&r, &s);
    buf = rig_finish(&r, &len, &rc);
    ck_assert_int_eq(rc, 0);
    ck_assert_uint_eq(count_markers(buf, len), 0u);
    free(buf);
    ts_script_free(&s);
  }
}
END_TEST

START_TEST(mpeg2_sequence_header_with_zero_height) {
  static const unsigned char seq[] = {0x00, 0x00, 0x01, 0xB3, 0x78, 0x00, 0x00, 0x13, 0xFF, 0xFF, 0xE0, 0x18};
  rig_t r;
  ts_script_t s;
  unsigned char au[128] = {0};
  unsigned char *buf;
  size_t len;
  int rc;
  memcpy(au, seq, sizeof seq);
  memcpy(au + sizeof seq, M2_PIC_I, sizeof M2_PIC_I);
  memset(&s, 0, sizeof s);
  rig_open(&r, 1);
  discovery(&s, ES_MPEG2, 1, 1);
  for (unsigned i = 0; i < 3; i++) video_pes(&s, 1, 90000 + i * 3600, 0, 0, au, sizeof seq + sizeof M2_PIC_I);
  rig_feed(&r, &s);
  buf = rig_finish(&r, &len, &rc);
  ck_assert_int_eq(rc, 0);
  ck_assert_uint_eq(count_markers(buf, len), 0u);
  free(buf);
  ts_script_free(&s);
}
END_TEST

START_TEST(track_table_is_capped) {
  ts_es_spec_t es[10];
  rig_t r;
  ts_script_t s;
  unsigned char *buf;
  size_t len;
  int rc;
  for (unsigned i = 0; i < 10; i++) {
    es[i].stream_type = 0x0F;
    es[i].pid = 0x110 + i;
    es[i].desc = NULL;
    es[i].desc_len = 0;
  }
  memset(&s, 0, sizeof s);
  rig_open(&r, 0);
  discovery(&s, es, 10, 1);
  rig_feed(&r, &s);
  ck_assert_int_eq(mkv_error(r.m), 0);
  buf = rig_finish(&r, &len, &rc);
  ck_assert_int_eq(rc, 0);
  free(buf);
  ts_script_free(&s);
}
END_TEST

START_TEST(multi_program_skips_teletext_subtitles) {
  static const ts_es_spec_t a[] = {{0x0F, 0x110, NULL, 0}, {0x06, 0x112, TTX_DESC, sizeof TTX_DESC}};
  static const ts_es_spec_t b[] = {{0x0F, 0x111, NULL, 0}};
  static const unsigned pids[2] = {0x100, 0x101};
  psi_pat_entry_t progs[2] = {{101, 0x100}, {102, 0x101}};
  mkv_opts_t cfg;
  mkv_t *m;
  ts_script_t s;
  unsigned char sec[1024];
  unsigned long long bytes = 0;
  int fd;
  char path[] = "/tmp/dvbipitools_test_mkvfeed_XXXXXX";
  size_t slen;
  memset(&cfg, 0, sizeof cfg);
  cfg.audio_all = 1;
  cfg.subs_srt = 1;
  cfg.app_name = "test";
  memset(&s, 0, sizeof s);
  fd = mkstemp(path);
  ck_assert_int_ge(fd, 0);
  m = mkv_new(fd, &cfg, 0, &bytes, pids, 2);
  ck_assert_ptr_nonnull(m);
  slen = psi_build_pat_multi(0x1234, 0, progs, 2, sec, sizeof sec);
  ts_script_section(&s, 0x0000, sec, slen);
  slen = build_pmt_es_list(sec, 101, 0x110, NULL, 0, a, 2);
  ts_script_section(&s, 0x100, sec, slen);
  slen = build_pmt_es_list(sec, 102, 0x111, NULL, 0, b, 1);
  ts_script_section(&s, 0x101, sec, slen);
  for (size_t i = 0; i < s.n; i++) mkv_feed(m, s.pkt + i * 188);
  ck_assert_int_eq(mkv_error(m), 0);
  ck_assert_int_eq(mkv_close(m), 0);
  close(fd);
  unlink(path);
  ts_script_free(&s);
}
END_TEST

static Suite *mkv_feed_suite(void) {
  Suite *su = suite_create("mkv_feed");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 60);
  tcase_add_test(tc, video_without_pts_is_muxed);
  tcase_add_test(tc, video_timestamp_steps_and_jumps);
  tcase_add_test(tc, missing_service_name_delays_start_until_close);
  tcase_add_test(tc, unparsable_parameter_sets_never_start_the_track);
  tcase_add_test(tc, lcevc_stripping_option_still_muxes);
  tcase_add_test(tc, mpeg2_picture_and_header_variants);
  tcase_add_test(tc, audio_carry_over_and_resync);
  tcase_add_test(tc, ac4_frames_and_oversized_carry_over);
  tcase_add_test(tc, every_supported_audio_codec_gets_a_track);
  tcase_add_test(tc, audio_track_selection_skips_other_tracks);
  tcase_add_test(tc, teletext_cue_timing_variants);
  tcase_add_test(tc, teletext_cue_text_edge_cases);
  tcase_add_test(tc, teletext_track_needs_single_program_and_a_page);
  tcase_add_test(tc, write_failure_stops_further_processing);
  tcase_add_test(tc, expired_grace_starts_without_service_name);
  tcase_add_test(tc, incomplete_parameter_set_combinations);
  tcase_add_test(tc, mpeg2_sequence_header_with_zero_height);
  tcase_add_test(tc, track_table_is_capped);
  tcase_add_test(tc, multi_program_skips_teletext_subtitles);
  suite_add_tcase(su, tc);
  return su;
}

int main(void) {
  SRunner *sr = srunner_create(mkv_feed_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
