/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/mux/esbuild/remux.h"
#include "lib/mux/fmp4/fmp4.h"

typedef struct {
  unsigned char packets[64][188];
  int count;
} collected_t;

static void collect_cb(void *ctx, const unsigned char *pkt) {
  collected_t *c = ctx;
  if (c->count < 64) memcpy(c->packets[c->count], pkt, 188);
  c->count++;
}

static fmp4_mux_t *build_mux_ts(unsigned timescale) {
  fmp4_track_cfg_t cfg;
  static const unsigned char avcc[] = {0x01, 0x64, 0x00, 0x1F, 0xFF, 0xE1, 0x00, 0x00, 0x01, 0x00, 0x00};
  memset(&cfg, 0, sizeof cfg);
  cfg.codec = CODEC_H264;
  cfg.track_id = 1;
  cfg.timescale = timescale;
  cfg.width = 1280;
  cfg.height = 720;
  cfg.cpriv = avcc;
  cfg.cpriv_len = sizeof avcc;
  return fmp4_mux_new(&cfg, 1);
}

static fmp4_mux_t *build_mux(void) {
  return build_mux_ts(90000);
}

static size_t build_segment(fmp4_mux_t *m, unsigned seq, unsigned char **out) {
  static const unsigned char frame[] = {0x00, 0x00, 0x00, 0x03, 0x65, 0x11, 0x22};
  fmp4_sample_t s;
  fmp4_segment_begin(m, seq);
  memset(&s, 0, sizeof s);
  s.track_idx = 0;
  s.data = frame;
  s.size = sizeof frame;
  s.duration = 3000;
  s.keyframe = 1;
  fmp4_segment_add_sample(m, &s);
  return fmp4_segment_end(m, out);
}

static int pmt_version_of(const collected_t *c) {
  for (int i = 0; i < c->count && i < 64; i++) {
    const unsigned char *p = c->packets[i];
    unsigned pid = ((unsigned)(p[1] & 0x1F) << 8) | p[2];
    size_t off = 4;
    if (pid != ESBUILD_PMT_PID || !(p[1] & 0x40))
      continue;
    if (p[3] & 0x20)
      off += 1 + (size_t)p[4];
    off += 1 + (size_t)p[off];
    return (p[off + 5] >> 1) & 0x1F;
  }
  return -1;
}

static unsigned char *find_fourcc(unsigned char *buf, size_t len, const char *cc) {
  for (size_t i = 4; i + 4 <= len; i++)
    if (!memcmp(buf + i, cc, 4)) return buf + i - 4;
  return NULL;
}

static void bump_size(unsigned char *buf, size_t len, const char *cc, unsigned add) {
  unsigned char *b = find_fourcc(buf, len, cc);
  unsigned sz;
  ck_assert_ptr_nonnull(b);
  sz = ((unsigned)b[0] << 24) | ((unsigned)b[1] << 16) | ((unsigned)b[2] << 8) | b[3];
  sz += add;
  b[0] = (unsigned char)(sz >> 24);
  b[1] = (unsigned char)(sz >> 16);
  b[2] = (unsigned char)(sz >> 8);
  b[3] = (unsigned char)sz;
}

/* tkhd/mdhd to version 1: 64-bit times and duration. id_len = bytes kept after time fields before duration */
static unsigned char *to_v1(const unsigned char *in, size_t len, const char *cc, size_t id_len, int in_mdia, size_t *outlen) {
  unsigned char *out = calloc(1, len + 16);
  unsigned char *b;
  size_t boff, bsz, body, tail;
  ck_assert_ptr_nonnull(out);
  memcpy(out, in, len);
  b = find_fourcc(out, len, cc);
  ck_assert_ptr_nonnull(b);
  boff = (size_t)(b - out);
  bsz = ((size_t)b[0] << 24) | ((size_t)b[1] << 16) | ((size_t)b[2] << 8) | b[3];
  body = boff + 8;
  tail = body + 12 + id_len + 4;
  memset(out + body, 0, bsz - 8 + 12);
  out[body] = 1;
  memcpy(out + body + 20, in + body + 12, id_len);
  memcpy(out + body + 20 + id_len + 4, in + body + 12 + id_len, boff + bsz - tail + 4);
  memcpy(out + boff + bsz + 12, in + boff + bsz, len - boff - bsz);
  bump_size(out, len, cc, 12);
  bump_size(out, len, "trak", 12);
  bump_size(out, len, "moov", 12);
  if (in_mdia) bump_size(out, len, "mdia", 12);
  *outlen = len + 12;
  return out;
}

START_TEST(esbuild_remux_init_reads_version1_tkhd_mdhd) {
  fmp4_mux_t *m = build_mux();
  unsigned char *init, *v1a, *v1b;
  size_t len, len_a, len_b;
  esbuild_remux_t r;

  ck_assert_ptr_nonnull(m);
  len = fmp4_init_segment(m, &init);
  v1a = to_v1(init, len, "tkhd", 8, 0, &len_a);
  v1b = to_v1(v1a, len_a, "mdhd", 4, 1, &len_b);
  ck_assert_int_eq(esbuild_remux_init(&r, v1b, len_b), 1);
  ck_assert_uint_eq(r.n_tracks, 1u);
  ck_assert_uint_eq(r.tracks[0].fmp4_track_id, 1u);
  ck_assert_uint_eq(r.tracks[0].timescale, 90000u);
  esbuild_remux_free(&r);
  free(v1a);
  free(v1b);
  fmp4_mux_free(m);
}
END_TEST

START_TEST(esbuild_remux_produces_pat_pmt_and_pes_for_one_sample) {
  fmp4_mux_t *m = build_mux();
  unsigned char *initbuf, *segbuf;
  size_t initlen, seglen;
  esbuild_remux_t r;
  collected_t c;
  int saw_pat = 0, saw_pmt = 0, saw_es_pid = 0;
  int i;

  ck_assert_ptr_nonnull(m);
  initlen = fmp4_init_segment(m, &initbuf);
  ck_assert_uint_gt(initlen, 0u);
  ck_assert_int_eq(esbuild_remux_init(&r, initbuf, initlen), 1);
  ck_assert_uint_eq(r.n_tracks, 1u);
  ck_assert_uint_eq(r.tracks[0].fmp4_track_id, 1u);
  ck_assert_int_eq(r.tracks[0].track.codec, CODEC_H264);

  seglen = build_segment(m, 1, &segbuf);
  ck_assert_uint_gt(seglen, 0u);

  memset(&c, 0, sizeof c);
  esbuild_remux_feed(&r, 0, segbuf, seglen, collect_cb, &c);
  ck_assert_int_gt(c.count, 0);

  for (i = 0; i < c.count; i++) {
    unsigned pid = ((unsigned)(c.packets[i][1] & 0x1F) << 8) | c.packets[i][2];
    if (pid == 0x0000) saw_pat = 1;
    if (pid == ESBUILD_PMT_PID) saw_pmt = 1;
    if (pid == r.tracks[0].es.pid) saw_es_pid = 1;
  }
  ck_assert_int_eq(saw_pat, 1);
  ck_assert_int_eq(saw_pmt, 1);
  ck_assert_int_eq(saw_es_pid, 1);

  esbuild_remux_free(&r);
  fmp4_mux_free(m);
}
END_TEST

static fmp4_mux_t *build_aac_mux(const unsigned char *asc, size_t asc_len) {
  fmp4_track_cfg_t cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.codec = CODEC_AAC;
  cfg.track_id = 1;
  cfg.timescale = 48000;
  cfg.rate = 48000;
  cfg.channels = 2;
  cfg.cpriv = asc;
  cfg.cpriv_len = asc_len;
  return fmp4_mux_new(&cfg, 1);
}

START_TEST(esbuild_remux_skips_aac_track_without_asc) {
  static const unsigned char asc[2] = {0x11, 0x90}; /* LC, 48 kHz, stereo */
  fmp4_mux_t *with = build_aac_mux(asc, sizeof asc);
  fmp4_mux_t *without = build_aac_mux(NULL, 0);
  unsigned char *init;
  size_t len;
  esbuild_remux_t r;

  ck_assert_ptr_nonnull(with);
  ck_assert_ptr_nonnull(without);
  len = fmp4_init_segment(with, &init);
  ck_assert_int_eq(esbuild_remux_init(&r, init, len), 1);
  ck_assert_uint_eq(r.tracks[0].track.aac_sr_index, 3u);
  esbuild_remux_free(&r);
  len = fmp4_init_segment(without, &init);
  ck_assert_int_eq(esbuild_remux_init(&r, init, len), 0);
  ck_assert_uint_eq(r.n_tracks, 0u);
  esbuild_remux_free(&r);
  fmp4_mux_free(with);
  fmp4_mux_free(without);
}
END_TEST

START_TEST(esbuild_remux_feeds_every_fragment_of_a_chunked_segment) {
  fmp4_mux_t *m = build_mux();
  unsigned char *initbuf, *segbuf, *both;
  size_t initlen, len1, len2;
  esbuild_remux_t r;
  collected_t c;
  int pes_starts = 0, pats = 0;

  ck_assert_ptr_nonnull(m);
  initlen = fmp4_init_segment(m, &initbuf);
  ck_assert_int_eq(esbuild_remux_init(&r, initbuf, initlen), 1);
  len1 = build_segment(m, 1, &segbuf);
  both = malloc(2 * len1 + 64);
  ck_assert_ptr_nonnull(both);
  memcpy(both, segbuf, len1);
  len2 = build_segment(m, 2, &segbuf);
  ck_assert_uint_le(len2, len1 + 64);
  memcpy(both + len1, segbuf, len2);

  memset(&c, 0, sizeof c);
  esbuild_remux_feed(&r, 0, both, len1 + len2, collect_cb, &c);
  for (int i = 0; i < c.count && i < 64; i++) {
    unsigned pid = ((unsigned)(c.packets[i][1] & 0x1F) << 8) | c.packets[i][2];
    if (pid == 0x0000) pats++;
    if (pid == r.tracks[0].es.pid && (c.packets[i][1] & 0x40)) pes_starts++;
  }
  ck_assert_int_eq(pats, 1);
  ck_assert_int_eq(pes_starts, 2);
  free(both);
  esbuild_remux_free(&r);
  fmp4_mux_free(m);
}
END_TEST

static fmp4_mux_t *build_audio_mux(void) {
  fmp4_track_cfg_t cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.codec = CODEC_AC3;
  cfg.track_id = 1;
  cfg.timescale = 48000;
  cfg.rate = 48000;
  cfg.channels = 2;
  return fmp4_mux_new(&cfg, 1);
}

START_TEST(esbuild_remux_add_init_appends_tracks_without_disturbing_existing_pids) {
  fmp4_mux_t *vm = build_mux();
  fmp4_mux_t *am = build_audio_mux();
  unsigned char *vinit, *ainit;
  size_t vinit_len, ainit_len;
  esbuild_remux_t r;

  ck_assert_ptr_nonnull(vm);
  ck_assert_ptr_nonnull(am);
  vinit_len = fmp4_init_segment(vm, &vinit);
  ck_assert_int_eq(esbuild_remux_init(&r, vinit, vinit_len), 1);
  ck_assert_uint_eq(r.n_tracks, 1u);
  ck_assert_uint_eq(r.tracks[0].es.pid, ESBUILD_FIRST_ES_PID);

  ainit_len = fmp4_init_segment(am, &ainit);
  ck_assert_int_eq(esbuild_remux_add_init(&r, 1, ainit, ainit_len), 1);
  ck_assert_uint_eq(r.n_tracks, 2u);
  ck_assert_uint_eq(r.tracks[0].es.pid, ESBUILD_FIRST_ES_PID);
  ck_assert_int_eq(r.tracks[0].track.codec, CODEC_H264);
  ck_assert_uint_eq(r.tracks[0].stream_idx, 0u);
  ck_assert_uint_eq(r.tracks[1].es.pid, ESBUILD_FIRST_ES_PID + 1);
  ck_assert_int_eq(r.tracks[1].track.codec, CODEC_AC3);
  ck_assert_uint_eq(r.tracks[1].fmp4_track_id, 1u);
  ck_assert_uint_eq(r.tracks[1].stream_idx, 1u);

  esbuild_remux_free(&r);
  fmp4_mux_free(vm);
  fmp4_mux_free(am);
}
END_TEST

START_TEST(esbuild_remux_pmt_version_changes_only_with_track_set) {
  fmp4_mux_t *vm = build_mux();
  fmp4_mux_t *am = build_audio_mux();
  unsigned char *vinit;
  unsigned char *ainit;
  unsigned char *segbuf;
  size_t vinit_len;
  size_t ainit_len;
  size_t seglen;
  esbuild_remux_t r;
  collected_t c;
  int v1;
  int v2;
  int v3;

  ck_assert_ptr_nonnull(vm);
  ck_assert_ptr_nonnull(am);
  vinit_len = fmp4_init_segment(vm, &vinit);
  ck_assert_int_eq(esbuild_remux_init(&r, vinit, vinit_len), 1);

  seglen = build_segment(vm, 1, &segbuf);
  memset(&c, 0, sizeof c);
  esbuild_remux_feed(&r, 0, segbuf, seglen, collect_cb, &c);
  v1 = pmt_version_of(&c);
  ck_assert_int_ge(v1, 0);

  seglen = build_segment(vm, 2, &segbuf);
  memset(&c, 0, sizeof c);
  esbuild_remux_feed(&r, 0, segbuf, seglen, collect_cb, &c);
  v2 = pmt_version_of(&c);
  ck_assert_int_eq(v2, v1);

  ainit_len = fmp4_init_segment(am, &ainit);
  ck_assert_int_eq(esbuild_remux_add_init(&r, 1, ainit, ainit_len), 1);
  seglen = build_segment(vm, 3, &segbuf);
  memset(&c, 0, sizeof c);
  esbuild_remux_feed(&r, 0, segbuf, seglen, collect_cb, &c);
  v3 = pmt_version_of(&c);
  ck_assert_int_ge(v3, 0);
  ck_assert_int_ne(v3, v1);

  esbuild_remux_free(&r);
  fmp4_mux_free(vm);
  fmp4_mux_free(am);
}
END_TEST

typedef struct {
  unsigned es_pid;
  unsigned es_pkts;
  int pcr_pkts;
  int first_es_seen;
  unsigned char first_es[188];
} counted_t;

static void count_cb(void *ctx, const unsigned char *pkt) {
  counted_t *c = ctx;
  unsigned pid = ((unsigned)(pkt[1] & 0x1F) << 8) | pkt[2];
  if (pid != c->es_pid) return;
  c->es_pkts++;
  if ((pkt[3] & 0x20) && pkt[4] >= 7 && (pkt[5] & 0x10)) c->pcr_pkts++;
  if (!c->first_es_seen) {
    memcpy(c->first_es, pkt, 188);
    c->first_es_seen = 1;
  }
}

START_TEST(esbuild_remux_big_sample_not_dropped_and_pcr_present) {
  enum { SZ = 100000 };
  fmp4_mux_t *m = build_mux();
  unsigned char *initbuf, *segbuf;
  unsigned char *frame = malloc(SZ + 4);
  size_t initlen, seglen;
  esbuild_remux_t r;
  counted_t c;
  fmp4_sample_t s;
  const unsigned char *p;
  const unsigned char *pes;
  uint64_t dts, pcr;

  ck_assert_ptr_nonnull(m);
  ck_assert_ptr_nonnull(frame);
  frame[0] = 0;
  frame[1] = (SZ >> 16) & 0xFF;
  frame[2] = (SZ >> 8) & 0xFF;
  frame[3] = SZ & 0xFF;
  memset(frame + 4, 0x55, SZ);
  frame[4] = 0x65;
  initlen = fmp4_init_segment(m, &initbuf);
  ck_assert_int_eq(esbuild_remux_init(&r, initbuf, initlen), 1);
  fmp4_segment_begin(m, 1);
  memset(&s, 0, sizeof s);
  s.data = frame;
  s.size = SZ + 4;
  s.duration = 3000;
  s.keyframe = 1;
  fmp4_segment_add_sample(m, &s);
  seglen = fmp4_segment_end(m, &segbuf);

  memset(&c, 0, sizeof c);
  c.es_pid = r.tracks[0].es.pid;
  esbuild_remux_feed(&r, 0, segbuf, seglen, count_cb, &c);
  ck_assert_uint_ge(c.es_pkts, (unsigned)(SZ / 184));
  ck_assert_int_eq(c.pcr_pkts, 1);
  p = c.first_es;
  ck_assert_int_ne(p[1] & 0x40, 0);
  ck_assert_int_ne(p[3] & 0x20, 0);
  pes = p + 5 + p[4];
  dts = ((uint64_t)(pes[14] & 0x0E) << 29) | ((uint64_t)pes[15] << 22) | ((uint64_t)(pes[16] & 0xFE) << 14) | ((uint64_t)pes[17] << 7) | (pes[18] >> 1);
  pcr = ((uint64_t)p[6] << 25) | ((uint64_t)p[7] << 17) | ((uint64_t)p[8] << 9) | ((uint64_t)p[9] << 1) | (p[10] >> 7);
  ck_assert_uint_eq(pcr, (dts - 63000) & 0x1FFFFFFFFULL);
  esbuild_remux_free(&r);
  free(frame);
  fmp4_mux_free(m);
}
END_TEST

START_TEST(esbuild_remux_epoch_tfdt_high_timescale_no_overflow) {
  enum { TS = 1000000 };
  const uint64_t secs = 1760000000ULL;
  static const unsigned char frame[] = {0x00, 0x00, 0x00, 0x03, 0x65, 0x11, 0x22};
  fmp4_mux_t *m = build_mux_ts(TS);
  unsigned char *initbuf, *segbuf;
  size_t initlen, seglen;
  esbuild_remux_t r;
  counted_t c;
  fmp4_sample_t s;
  const unsigned char *p;
  uint64_t pts;
  size_t off;

  ck_assert_ptr_nonnull(m);
  fmp4_track_seed_dts(m, 0, secs * TS);
  initlen = fmp4_init_segment(m, &initbuf);
  ck_assert_int_eq(esbuild_remux_init(&r, initbuf, initlen), 1);
  fmp4_segment_begin(m, 1);
  memset(&s, 0, sizeof s);
  s.data = frame;
  s.size = sizeof frame;
  s.duration = TS / 30;
  s.keyframe = 1;
  fmp4_segment_add_sample(m, &s);
  seglen = fmp4_segment_end(m, &segbuf);

  memset(&c, 0, sizeof c);
  c.es_pid = r.tracks[0].es.pid;
  esbuild_remux_feed(&r, 0, segbuf, seglen, count_cb, &c);
  ck_assert_int_eq(c.first_es_seen, 1);
  p = c.first_es;
  off = 4;
  if (p[3] & 0x20) off += 1 + (size_t)p[4];
  ck_assert_int_eq(p[off + 7] & 0x80, 0x80);
  pts = ((uint64_t)((p[off + 9] >> 1) & 7) << 30) | ((uint64_t)p[off + 10] << 22) | ((uint64_t)(p[off + 11] >> 1) << 15) | ((uint64_t)p[off + 12] << 7) | (p[off + 13] >> 1);
  ck_assert_uint_eq(pts, (secs * 90000ULL) & 0x1FFFFFFFFULL);
  esbuild_remux_free(&r);
  fmp4_mux_free(m);
}
END_TEST

static Suite *esbuild_remux_suite(void) {
  Suite *s = suite_create("esbuild_remux");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, esbuild_remux_produces_pat_pmt_and_pes_for_one_sample);
  tcase_add_test(tc, esbuild_remux_feeds_every_fragment_of_a_chunked_segment);
  tcase_add_test(tc, esbuild_remux_skips_aac_track_without_asc);
  tcase_add_test(tc, esbuild_remux_init_reads_version1_tkhd_mdhd);
  tcase_add_test(tc, esbuild_remux_add_init_appends_tracks_without_disturbing_existing_pids);
  tcase_add_test(tc, esbuild_remux_pmt_version_changes_only_with_track_set);
  tcase_add_test(tc, esbuild_remux_big_sample_not_dropped_and_pcr_present);
  tcase_add_test(tc, esbuild_remux_epoch_tfdt_high_timescale_no_overflow);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(esbuild_remux_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
