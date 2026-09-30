/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
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

static fmp4_mux_t *build_mux(void) {
  fmp4_track_cfg_t cfg;
  static const unsigned char avcc[] = {0x01, 0x64, 0x00, 0x1F, 0xFF, 0xE1, 0x00, 0x00, 0x01, 0x00, 0x00};
  memset(&cfg, 0, sizeof cfg);
  cfg.codec = CODEC_H264;
  cfg.track_id = 1;
  cfg.timescale = 90000;
  cfg.width = 1280;
  cfg.height = 720;
  cfg.cpriv = avcc;
  cfg.cpriv_len = sizeof avcc;
  return fmp4_mux_new(&cfg, 1);
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

  fmp4_mux_free(vm);
  fmp4_mux_free(am);
}
END_TEST

START_TEST(esbuild_remux_pmt_version_changes_only_with_track_set) {
  fmp4_mux_t *vm = build_mux();
  fmp4_mux_t *am = build_audio_mux();
  unsigned char *vinit, *ainit, *segbuf;
  size_t vinit_len, ainit_len, seglen;
  esbuild_remux_t r;
  collected_t c;
  int v1, v2, v3;

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

  fmp4_mux_free(vm);
  fmp4_mux_free(am);
}
END_TEST

static Suite *esbuild_remux_suite(void) {
  Suite *s = suite_create("esbuild_remux");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, esbuild_remux_produces_pat_pmt_and_pes_for_one_sample);
  tcase_add_test(tc, esbuild_remux_add_init_appends_tracks_without_disturbing_existing_pids);
  tcase_add_test(tc, esbuild_remux_pmt_version_changes_only_with_track_set);
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
