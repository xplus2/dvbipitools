/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/mux/mp4/mp4.h"
#include "lib/mux/psi_build.h"

#include "ts_test_util.h"

#define NELEM(a) (sizeof(a) / sizeof((a)[0]))
#define VPID 0x201
#define APID 0x101
#define TPID 0x301
#define PMT_PID 0x100

typedef struct {
  mp4_opts_t cfg;
  mp4_t *m;
  char path[48];
  int fd;
  unsigned long long bytes;
} rig_t;

static void rig_open(rig_t *r, int video_ok) {
  memset(r, 0, sizeof *r);
  r->cfg.audio_all = 1;
  strcpy(r->path, "/tmp/dvbipitools_test_mp4feed_XXXXXX");
  r->fd = mkstemp(r->path);
  ck_assert_int_ge(r->fd, 0);
  r->m = mp4_new(r->fd, &r->cfg, video_ok, &r->bytes, NULL, 0);
  ck_assert_ptr_nonnull(r->m);
}

static void rig_feed(rig_t *r, const ts_script_t *s) {
  for (size_t i = 0; i < s->n; i++) mp4_feed(r->m, s->pkt + i * 188);
}

static unsigned char *rig_finish(rig_t *r, size_t *len) {
  unsigned char *buf;
  mp4_close(r->m);
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

START_TEST(h264_timestamp_forms) {
  static const struct {
    int has_pts;
    int has_dts;
    uint64_t pts[6];
    uint64_t dts[6];
  } t[] = {
      {1, 1, {90000, 100800, 97200, 104400, 108000, 111600}, {90000, 93600, 97200, 100800, 104400, 108000}},
      {1, 0, {90000, 93600, 97200, 100800, 104400, 108000}, {0}},
      {0, 1, {0}, {90000, 93600, 97200, 100800, 104400, 108000}},
      {0, 0, {0}, {0}},
      {1, 1, {90000, 93600, 93600, 90000, 400000, 403600}, {90000, 93600, 93600, 90000, 400000, 403600}},
      {1, 1, {90000, 93600, 400000000, 400003600, 90000, 93600}, {90000, 93600, 400000000, 400003600, 90000, 93600}},
  };
  for (size_t c = 0; c < NELEM(t); c++) {
    rig_t r;
    ts_script_t s;
    unsigned char au[256] = {0};
    unsigned char *buf;
    size_t len;
    memset(&s, 0, sizeof s);
    rig_open(&r, 1);
    discovery(&s, ES_H264, 1, 1);
    for (size_t i = 0; i < 6; i++) video_pes(&s, t[c].has_pts, t[c].pts[i], t[c].has_dts, t[c].dts[i], au, build_h264_au(au, i == 0));
    rig_feed(&r, &s);
    ck_assert_int_eq(mp4_error(r.m), 0);
    buf = rig_finish(&r, &len);
    ck_assert(has_str(buf, len, "avc1"));
    free(buf);
    ts_script_free(&s);
  }
}
END_TEST

START_TEST(other_video_codecs_mux) {
  unsigned char au[256] = {0};
  const struct {
    const ts_es_spec_t *es;
    size_t (*build)(unsigned char *);
    const char *fourcc;
  } t[] = {{ES_VVC, build_vvc_au, "vvc1"}, {ES_AV1, build_av1_au, "av01"}};
  for (size_t c = 0; c < NELEM(t); c++) {
    rig_t r;
    ts_script_t s;
    unsigned char *buf;
    size_t len;
    memset(&s, 0, sizeof s);
    rig_open(&r, 1);
    discovery(&s, t[c].es, 1, 1);
    for (unsigned i = 0; i < 4; i++) video_pes(&s, 1, 90000 + i * 3600, 1, 90000 + i * 3600, au, t[c].build(au));
    rig_feed(&r, &s);
    buf = rig_finish(&r, &len);
    ck_assert(has_str(buf, len, t[c].fourcc));
    free(buf);
    ts_script_free(&s);
  }
}
END_TEST

START_TEST(hevc_with_lcevc_strip_mux) {
  rig_t r;
  ts_script_t s;
  unsigned char au[256] = {0};
  unsigned char *buf;
  size_t len;
  memset(&s, 0, sizeof s);
  rig_open(&r, 1);
  r.cfg.strip_lcevc = 1;
  discovery(&s, ES_HEVC, 1, 1);
  video_pes(&s, 1, 90000, 1, 90000, au, build_hevc_au(au, 1));
  video_pes(&s, 1, 93600, 1, 93600, au, build_hevc_au(au, 0));
  video_pes(&s, 1, 97200, 1, 97200, au, build_hevc_au(au, 0));
  rig_feed(&r, &s);
  buf = rig_finish(&r, &len);
  ck_assert(has_str(buf, len, "hvc1"));
  free(buf);
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
      {ES_H264, h264_bad, h264_bad_len, 3},       {ES_H264, h264_sps_only, h264_sps_only_len, 2}, {ES_HEVC, hevc_bad, hevc_bad_len, 4},
      {ES_HEVC, hevc_no_vps, hevc_no_vps_len, 3}, {ES_VVC, vvc_bad, vvc_bad_len, 4},              {ES_VVC, vvc_no_vps, vvc_no_vps_len, 3},
      {ES_AV1, av1_bad, av1_bad_len, 2},          {ES_AV1, av1_frame_only, av1_frame_only_len, 1},
  };
  for (size_t c = 0; c < NELEM(t); c++) {
    rig_t r;
    ts_script_t s;
    unsigned char au[256] = {0};
    unsigned char *buf;
    size_t len;
    memset(&s, 0, sizeof s);
    rig_open(&r, 1);
    discovery(&s, t[c].es, 1, 1);
    for (unsigned i = 0; i < 3; i++) video_pes(&s, 1, 90000 + i * 3600, 1, 90000 + i * 3600, au, build_annexb_au(au, t[c].nals, t[c].lens, t[c].n));
    rig_feed(&r, &s);
    ck_assert_int_eq(mp4_error(r.m), 0);
    buf = rig_finish(&r, &len);
    free(buf);
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
  unsigned char *buf;
  size_t len;
  memset(&s, 0, sizeof s);
  rig_open(&r, 1);
  discovery(&s, ES_H264, 1, 1);
  for (unsigned i = 0; i < 3; i++) video_pes(&s, 1, 90000 + i * 3600, 1, 90000 + i * 3600, au, build_annexb_au(au, nals, lens, 3));
  rig_feed(&r, &s);
  buf = rig_finish(&r, &len);
  free(buf);
  ts_script_free(&s);
}
END_TEST

START_TEST(missing_service_name_delays_start_then_grace_expires) {
  rig_t r;
  ts_script_t s;
  ts_script_t more;
  unsigned char au[256] = {0};
  unsigned char *buf;
  size_t len;
  memset(&s, 0, sizeof s);
  memset(&more, 0, sizeof more);
  rig_open(&r, 1);
  discovery(&s, ES_H264, 1, 0);
  video_pes(&s, 1, 90000, 1, 90000, au, build_h264_au(au, 1));
  video_pes(&s, 1, 93600, 1, 93600, au, build_h264_au(au, 0));
  rig_feed(&r, &s);
  ck_assert_uint_eq((unsigned)r.bytes, 0u);
  usleep(2100000);
  video_pes(&more, 1, 97200, 1, 97200, au, build_h264_au(au, 0));
  video_pes(&more, 1, 100800, 1, 100800, au, build_h264_au(au, 0));
  rig_feed(&r, &more);
  buf = rig_finish(&r, &len);
  ck_assert(has_str(buf, len, "avc1"));
  free(buf);
  ts_script_free(&s);
  ts_script_free(&more);
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
  buf = rig_finish(&r, &len);
  ck_assert(has_str(buf, len, "mp4a"));
  free(buf);
  ts_script_free(&s);
}
END_TEST

static const unsigned char AC4_MONO[] = {0xac, 0x40, 0x00, 0x06, 0x00, 0x05, 0xb4, 0x00, 0x00, 0x00};
static const unsigned char AC4_DESC[] = {0x7F, 0x02, 0x15, 0x00};
static const unsigned char DTS_FRAME_HDR[] = {0x7F, 0xFE, 0x80, 0x01, 0x00, 0x3C, 0x06, 0x00, 0xB4};
static const unsigned char TRUEHD_SYNC[] = {0x00, 0x0B, 0x00, 0x00, 0xF8, 0x72, 0x6F, 0xBA, 0x00, 0x00, 0x80, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x81};
static const unsigned char HDMV_REG[] = {0x05, 0x04, 'H', 'D', 'M', 'V'};

static size_t ac3_frame(unsigned char *d, unsigned acmod, unsigned lfe) {
  memset(d, 0, 128);
  d[0] = 0x0B;
  d[1] = 0x77;
  d[5] = 8 << 3;
  d[6] = (unsigned char)(acmod << 5);
  d[6] |= (unsigned char)(lfe << (7 - 3));
  return 128;
}

static size_t eac3_frame(unsigned char *d) {
  memset(d, 0, 128);
  d[0] = 0x0B;
  d[1] = 0x77;
  d[3] = 63;
  d[4] = 0x30 | (2 << 1);
  d[5] = 16 << 3;
  return 128;
}

START_TEST(audio_codec_specific_headers) {
  static const unsigned char dts_desc[] = {0x7B, 0x01, 0x00};
  static const struct {
    unsigned char stream_type;
    const unsigned char *desc;
    size_t desc_len;
    int hdmv;
    const char *fourcc;
  } t[] = {
      {0x81, NULL, 0, 0, "ac-3"},
      {0x87, NULL, 0, 0, "ec-3"},
      {0x83, NULL, 0, 1, "mlpa"},
      {0x06, dts_desc, sizeof dts_desc, 0, "dtsc"},
      {0x06, AC4_DESC, sizeof AC4_DESC, 0, "ac-4"},
  };
  for (size_t c = 0; c < NELEM(t); c++) {
    rig_t r;
    ts_script_t s;
    ts_es_spec_t es;
    unsigned char fr[512];
    unsigned char *buf;
    size_t len;
    size_t flen;
    es.stream_type = t[c].stream_type;
    es.pid = APID;
    es.desc = t[c].desc;
    es.desc_len = t[c].desc_len;
    memset(&s, 0, sizeof s);
    rig_open(&r, 0);
    ts_script_discovery(&s, 101, PMT_PID, t[c].hdmv ? HDMV_REG : NULL, t[c].hdmv ? sizeof HDMV_REG : 0, &es, 1, 1);
    memset(fr, 0, sizeof fr);
    switch (c) {
      case 0: flen = ac3_frame(fr, 2, 1); break;
      case 1: flen = eac3_frame(fr); break;
      case 2:
        memcpy(fr, TRUEHD_SYNC, sizeof TRUEHD_SYNC);
        flen = sizeof TRUEHD_SYNC;
        break;
      case 3:
        memcpy(fr, DTS_FRAME_HDR, sizeof DTS_FRAME_HDR);
        flen = 97;
        break;
      default:
        memcpy(fr, AC4_MONO, sizeof AC4_MONO);
        flen = sizeof AC4_MONO;
        break;
    }
    for (unsigned i = 0; i < 4; i++) audio_pes(&s, APID, 1, 90000 + i * 2880, fr, flen);
    rig_feed(&r, &s);
    ck_assert_int_eq(mp4_error(r.m), 0);
    buf = rig_finish(&r, &len);
    ck_assert_msg(has_str(buf, len, t[c].fourcc), "case %zu", c);
    free(buf);
    ts_script_free(&s);
  }
}
END_TEST

START_TEST(every_supported_audio_codec_gets_a_track) {
  static const unsigned char d_opus[] = {0x05, 0x04, 'O', 'p', 'u', 's'};
  static const unsigned char d_dts[] = {0x7B, 0x01, 0x00};
  static const unsigned char d_ac3[] = {0x6A, 0x01, 0x00};
  static const unsigned char d_eac3[] = {0x7A, 0x01, 0x00};
  static const ts_es_spec_t set_a[] = {
      {0x03, 0x110, NULL, 0}, {0x0F, 0x111, NULL, 0}, {0x11, 0x112, NULL, 0}, {0x81, 0x113, NULL, 0},
      {0x87, 0x114, NULL, 0}, {0x06, 0x115, d_opus, sizeof d_opus}, {0x06, 0x116, d_dts, sizeof d_dts}, {0x06, 0x117, AC4_DESC, sizeof AC4_DESC},
  };
  static const ts_es_spec_t set_b[] = {
      {0x82, 0x120, NULL, 0}, {0x83, 0x121, NULL, 0}, {0x85, 0x122, NULL, 0}, {0x86, 0x123, NULL, 0},
      {0x06, 0x124, d_ac3, sizeof d_ac3}, {0x06, 0x125, d_eac3, sizeof d_eac3},
  };
  const ts_es_spec_t *sets[2] = {set_a, set_b};
  const size_t counts[2] = {NELEM(set_a), NELEM(set_b)};
  for (int c = 0; c < 2; c++) {
    rig_t r;
    ts_script_t s;
    unsigned char *buf;
    size_t len;
    memset(&s, 0, sizeof s);
    rig_open(&r, 0);
    ts_script_discovery(&s, 101, PMT_PID, c ? HDMV_REG : NULL, c ? sizeof HDMV_REG : 0, sets[c], counts[c], 1);
    rig_feed(&r, &s);
    ck_assert_int_eq(mp4_error(r.m), 0);
    buf = rig_finish(&r, &len);
    free(buf);
    ts_script_free(&s);
  }
}
END_TEST

START_TEST(unsupported_video_and_track_cap) {
  ts_es_spec_t many[10];
  rig_t r;
  ts_script_t s;
  unsigned char *buf;
  size_t len;
  for (unsigned i = 0; i < 10; i++) {
    many[i].stream_type = 0x0F;
    many[i].pid = 0x110 + i;
    many[i].desc = NULL;
    many[i].desc_len = 0;
  }
  memset(&s, 0, sizeof s);
  rig_open(&r, 1);
  discovery(&s, ES_MPEG2, 1, 1);
  rig_feed(&r, &s);
  buf = rig_finish(&r, &len);
  free(buf);
  ts_script_free(&s);
  memset(&s, 0, sizeof s);
  rig_open(&r, 0);
  discovery(&s, many, 10, 1);
  rig_feed(&r, &s);
  ck_assert_int_eq(mp4_error(r.m), 0);
  buf = rig_finish(&r, &len);
  free(buf);
  ts_script_free(&s);
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
  buf = rig_finish(&r, &len);
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

START_TEST(teletext_cue_gap_and_timing_variants) {
  static const struct {
    int video;
    uint64_t start[3];
    uint64_t end[3];
    int cues_first;
    long lead;
  } t[] = {
      {1, {1000, 3000, 5000}, {2000, 4000, 6000}, 0, 0},
      {1, {1000, 3000, 5000}, {2000, 4000, 6000}, 1, 0},
      {0, {1000, 3000, 5000}, {2000, 4000, 6000}, 0, 0},
      {0, {1000, 1500, 5000}, {2000, 4000, 6000}, 0, 700},
      {0, {1000, 2000, 3000}, {1500, 2500, 3500}, 1, 0},
  };
  for (size_t c = 0; c < NELEM(t); c++) {
    rig_t r;
    ts_script_t s;
    unsigned char au[256] = {0};
    unsigned char adts[128];
    unsigned char *buf;
    size_t len;
    size_t alen = build_adts_frame(adts, 100);
    memset(&s, 0, sizeof s);
    rig_open(&r, t[c].video);
    r.cfg.subs_srt = 1;
    r.cfg.sub_lead_ms = t[c].lead;
    discovery(&s, t[c].video ? ES_VIDEO_TTX : ES_AUDIO_TTX, 2, 1);
    if (t[c].cues_first) {
      for (unsigned i = 0; i < 3; i++) ttx_cue(&s, "HELLO", t[c].start[i], t[c].end[i]);
    }
    for (unsigned i = 0; i < 6; i++) {
      uint64_t pts = 90000 + i * 90000;
      if (t[c].video) video_pes(&s, 1, pts, 1, pts, au, build_h264_au(au, i == 0));
      else audio_pes(&s, APID, 1, pts, adts, alen);
      if (!t[c].cues_first && i < 3) ttx_cue(&s, "HELLO", t[c].start[i] + 1000, t[c].end[i] + 1000);
    }
    rig_feed(&r, &s);
    ck_assert_int_eq(mp4_error(r.m), 0);
    buf = rig_finish(&r, &len);
    ck_assert(has_str(buf, len, "tx3g"));
    free(buf);
    ts_script_free(&s);
  }
}
END_TEST

START_TEST(teletext_edge_cues_and_missing_page) {
  static const unsigned char no_page_desc[] = {0x56, 0x05, 'e', 'n', 'g', 0x00, 0x00};
  static const ts_es_spec_t no_page[] = {{0x0F, APID, NULL, 0}, {0x06, TPID, no_page_desc, sizeof no_page_desc}};
  rig_t r;
  ts_script_t s;
  unsigned char adts[128];
  unsigned char *buf;
  size_t len;
  size_t alen = build_adts_frame(adts, 100);
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
  buf = rig_finish(&r, &len);
  free(buf);
  ts_script_free(&s);
  memset(&s, 0, sizeof s);
  rig_open(&r, 0);
  r.cfg.subs_srt = 1;
  discovery(&s, no_page, 2, 1);
  rig_feed(&r, &s);
  buf = rig_finish(&r, &len);
  free(buf);
  ts_script_free(&s);
}
END_TEST

START_TEST(multi_program_skips_teletext_subtitles) {
  static const ts_es_spec_t a[] = {{0x0F, 0x110, NULL, 0}, {0x06, 0x112, TTX_DESC, sizeof TTX_DESC}};
  static const ts_es_spec_t b[] = {{0x0F, 0x111, NULL, 0}};
  static const unsigned pids[2] = {0x100, 0x101};
  psi_pat_entry_t progs[2] = {{101, 0x100}, {102, 0x101}};
  mp4_opts_t cfg;
  mp4_t *m;
  ts_script_t s;
  unsigned char sec[1024];
  unsigned long long bytes = 0;
  int fd;
  char path[] = "/tmp/dvbipitools_test_mp4feed_XXXXXX";
  size_t slen;
  memset(&cfg, 0, sizeof cfg);
  cfg.audio_all = 1;
  cfg.subs_srt = 1;
  memset(&s, 0, sizeof s);
  fd = mkstemp(path);
  ck_assert_int_ge(fd, 0);
  m = mp4_new(fd, &cfg, 0, &bytes, pids, 2);
  ck_assert_ptr_nonnull(m);
  slen = psi_build_pat_multi(0x1234, 0, progs, 2, sec, sizeof sec);
  ts_script_section(&s, 0x0000, sec, slen);
  slen = build_pmt_es_list(sec, 101, 0x110, NULL, 0, a, 2);
  ts_script_section(&s, 0x100, sec, slen);
  slen = build_pmt_es_list(sec, 102, 0x111, NULL, 0, b, 1);
  ts_script_section(&s, 0x101, sec, slen);
  for (size_t i = 0; i < s.n; i++) mp4_feed(m, s.pkt + i * 188);
  ck_assert_int_eq(mp4_error(m), 0);
  mp4_close(m);
  close(fd);
  unlink(path);
  ts_script_free(&s);
}
END_TEST

static Suite *mp4_feed_suite(void) {
  Suite *su = suite_create("mp4_feed");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 60);
  tcase_add_test(tc, h264_timestamp_forms);
  tcase_add_test(tc, other_video_codecs_mux);
  tcase_add_test(tc, hevc_with_lcevc_strip_mux);
  tcase_add_test(tc, unparsable_or_incomplete_parameter_sets_emit_nothing);
  tcase_add_test(tc, header_without_keyframe_waits);
  tcase_add_test(tc, missing_service_name_delays_start_then_grace_expires);
  tcase_add_test(tc, audio_carry_over_and_resync);
  tcase_add_test(tc, audio_codec_specific_headers);
  tcase_add_test(tc, every_supported_audio_codec_gets_a_track);
  tcase_add_test(tc, unsupported_video_and_track_cap);
  tcase_add_test(tc, audio_track_selection_skips_other_tracks);
  tcase_add_test(tc, teletext_cue_gap_and_timing_variants);
  tcase_add_test(tc, teletext_edge_cues_and_missing_page);
  tcase_add_test(tc, multi_program_skips_teletext_subtitles);
  suite_add_tcase(su, tc);
  return su;
}

int main(void) {
  SRunner *sr = srunner_create(mp4_feed_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
