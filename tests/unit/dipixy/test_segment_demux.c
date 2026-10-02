/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "../dipitvhead/psi_fixture.h"
#include "lib/demux/psi/section_asm.h"
#include "dipixy/segment/pidlock.h"
#include "dipixy/segment/priv.h"
#include "dipixy/segstore.h"

#define GROUP "239.255.42.42"
#define PORT 42142
#define MAX_STORES 8
#define SEG_TARGET 2.0
#define MAX_SEGS 3
#define PMT_PID 0x100
#define VIDEO_PID 0x101
#define AUDIO_PID 0x102
#define OTHER_PID 0x300
#define ST_H264 0x1B
#define ST_AAC 0x0F
#define ADTS_PAYLOAD 100
#define ADTS_RATE 48000
#define AAC_SAMPLES 1024

static const lcevc_select_t full = {LCEVC_SEL_FULL, 0, 0};
static const unsigned av_es[2][2] = {{ST_H264, VIDEO_PID}, {ST_AAC, AUDIO_PID}};
static const unsigned audio_only[1][2] = {{ST_AAC, AUDIO_PID}};

typedef struct {
  capture_ctx_t *ctx;
  hls_seg_ctx_t *s;
} seg_t;

static void seg_open(seg_t *g, const pid_filter_t *filter, seg_container_t container) {
  hls_store_init(MAX_STORES);
  hls_seg_init(MAX_STORES);
  g->ctx = capture_open(AF_INET, GROUP, PORT, NULL, 0, NULL, NULL, NULL, 0, 0);
  ck_assert_ptr_nonnull(g->ctx);
  ck_assert_int_eq(hls_seg_touch(capture_open(AF_INET, GROUP, PORT, NULL, 0, NULL, NULL, NULL, 0, 0), filter, 0, &full, SEG_TARGET, MAX_SEGS, container, 0.0), 1);
  hls_seg_registry_lock();
  g->s = hls_seg_find_locked(g->ctx, filter, 0, &full, container);
  hls_seg_registry_unlock();
  ck_assert_ptr_nonnull(g->s);
}

static void seg_close(seg_t *g) {
  atomic_store(&g->s->last_request_ms, 0);
  hls_seg_sweep_idle();
  capture_close(g->ctx);
}

static void feed_programme(const seg_t *g, const unsigned (*es)[2], unsigned n_es) {
  unsigned char pkts[2][188];

  fixture_programme_packets_es(1, VIDEO_PID, es, n_es, pkts);
  hls_seg_feed_all(g->ctx, pkts[0]);
  hls_seg_feed_all(g->ctx, pkts[1]);
}

static void feed_pid(const seg_t *g, unsigned pid) {
  unsigned char pkt[188];

  fixture_ts_packet(pkt, pid, 0, 0x55);
  hls_seg_feed_all(g->ctx, pkt);
}

START_TEST(program_lock_picks_pids_and_drops_the_probe_phase) {
  seg_t g;
  pid_filter_t none = {0};

  seg_open(&g, &none, SEG_CONTAINER_TS);
  ck_assert_int_eq(g.s->demux.video_pid_known, 0);
  feed_programme(&g, av_es, 2);
  ck_assert_int_eq(g.s->demux.video_pid_known, 1);
  ck_assert_uint_eq(g.s->demux.video_pid, (unsigned)VIDEO_PID);
  ck_assert_int_eq(g.s->demux.video_codec, CODEC_H264);
  ck_assert_int_eq(g.s->demux.audio_pid_known, 0);
  ck_assert_int_eq(g.s->demux.n_allowed, 5);
  ck_assert_int_eq(pidlock_allowed(g.s->demux.allowed_pids, g.s->demux.n_allowed, 0), 1);
  ck_assert_int_eq(pidlock_allowed(g.s->demux.allowed_pids, g.s->demux.n_allowed, PMT_PID), 1);
  ck_assert_int_eq(pidlock_allowed(g.s->demux.allowed_pids, g.s->demux.n_allowed, AUDIO_PID), 1);
  ck_assert_int_eq(pidlock_allowed(g.s->demux.allowed_pids, g.s->demux.n_allowed, OTHER_PID), 0);
  ck_assert_uint_eq(g.s->len, 188u);
  seg_close(&g);
}
END_TEST

START_TEST(fragmented_containers_also_lock_the_audio_track) {
  seg_t g;
  pid_filter_t none = {0};

  seg_open(&g, &none, SEG_CONTAINER_FMP4);
  feed_programme(&g, av_es, 2);
  ck_assert_int_eq(g.s->demux.audio_pid_known, 1);
  ck_assert_uint_eq(g.s->demux.audio_pid, (unsigned)AUDIO_PID);
  ck_assert_int_eq(g.s->demux.audio_codec, CODEC_AAC);
  ck_assert_int_eq(g.s->audio.present, 1);
  seg_close(&g);
}
END_TEST

START_TEST(only_the_locked_programs_pids_are_buffered) {
  seg_t g;
  pid_filter_t none = {0};

  seg_open(&g, &none, SEG_CONTAINER_TS);
  feed_programme(&g, av_es, 2);
  feed_pid(&g, AUDIO_PID);
  ck_assert_uint_eq(g.s->len, 376u);
  feed_pid(&g, OTHER_PID);
  ck_assert_uint_eq(g.s->len, 376u);
  seg_close(&g);
}
END_TEST

START_TEST(excluded_pids_never_reach_discovery_or_the_buffer) {
  seg_t g;
  pid_filter_t drop_audio = {0};

  pid_filter_add(&drop_audio, AUDIO_PID);
  seg_open(&g, &drop_audio, SEG_CONTAINER_TS);
  feed_programme(&g, av_es, 2);
  feed_pid(&g, AUDIO_PID);
  ck_assert_uint_eq(g.s->len, 188u);
  seg_close(&g);
}
END_TEST

START_TEST(program_without_video_never_locks) {
  seg_t g;
  pid_filter_t none = {0};

  seg_open(&g, &none, SEG_CONTAINER_TS);
  feed_programme(&g, audio_only, 1);
  feed_pid(&g, AUDIO_PID);
  ck_assert_int_eq(g.s->demux.video_pid_known, 0);
  ck_assert_uint_eq(g.s->len, 0u);
  seg_close(&g);
}
END_TEST

static int has_es_entry(const unsigned char *buf, size_t len, unsigned type, unsigned pid) {
  const unsigned char needle[3] = {(unsigned char)type, (unsigned char)(0xE0 | (pid >> 8)), (unsigned char)pid};

  return memmem(buf, len, needle, sizeof needle) != NULL;
}

START_TEST(transport_pmt_keeps_all_streams_without_a_filter) {
  seg_t plain;
  pid_filter_t none = {0};

  seg_open(&plain, &none, SEG_CONTAINER_TS);
  feed_programme(&plain, av_es, 2);
  ck_assert_int_eq(has_es_entry(plain.s->buf, plain.s->len, ST_AAC, AUDIO_PID), 1);
  seg_close(&plain);
}
END_TEST

START_TEST(transport_pmt_loses_filtered_streams) {
  seg_t g;
  pid_filter_t drop_audio = {0};

  pid_filter_add(&drop_audio, AUDIO_PID);
  seg_open(&g, &drop_audio, SEG_CONTAINER_TS);
  feed_programme(&g, av_es, 2);
  ck_assert_uint_eq(g.s->len, 188u);
  ck_assert_int_eq(has_es_entry(g.s->buf, g.s->len, ST_AAC, AUDIO_PID), 0);
  ck_assert_int_eq(has_es_entry(g.s->buf, g.s->len, ST_H264, VIDEO_PID), 1);
  ck_assert_uint_eq(g.s->cc_pmt, 1u);
  seg_close(&g);
}
END_TEST

static size_t adts_frame(uint8_t *out, size_t payload) {
  size_t len = 7 + payload;

  out[0] = 0xFF;
  out[1] = 0xF1;
  out[2] = (uint8_t)((1 << 6) | (3 << 2));
  out[3] = (uint8_t)((2 << 6) | ((len >> 11) & 0x03));
  out[4] = (uint8_t)((len >> 3) & 0xFF);
  out[5] = (uint8_t)(((len & 7) << 5) | 0x1F);
  out[6] = 0xFC;
  memset(out + 7, 0x11, payload);
  return len;
}

static void audio_ready(seg_t *g) {
  g->s->demux.audio_codec = CODEC_AAC;
  g->s->audio.es.codec = CODEC_AAC;
}

START_TEST(first_adts_frame_fills_the_audio_parameters) {
  seg_t g;
  pid_filter_t none = {0};
  uint8_t frame[256];
  size_t n = adts_frame(frame, ADTS_PAYLOAD);

  seg_open(&g, &none, SEG_CONTAINER_FMP4);
  audio_ready(&g);
  handle_audio_pes(g.s, 0, 0, frame, n);
  ck_assert_int_eq(g.s->audio.ready, 1);
  ck_assert_uint_eq(g.s->audio.rate, (unsigned)ADTS_RATE);
  ck_assert_uint_eq(g.s->audio.channels, 2u);
  ck_assert_int_eq((int)g.s->audio.nominal_samples, AAC_SAMPLES);
  ck_assert_uint_eq(g.s->audio.remlen, 0u);
  seg_close(&g);
}
END_TEST

START_TEST(frames_split_across_pes_packets_are_reassembled) {
  seg_t g;
  pid_filter_t none = {0};
  uint8_t frame[256];
  size_t n = adts_frame(frame, ADTS_PAYLOAD);
  size_t cut = n * 6 / 10;

  seg_open(&g, &none, SEG_CONTAINER_FMP4);
  audio_ready(&g);
  handle_audio_pes(g.s, 0, 0, frame, cut);
  ck_assert_int_eq(g.s->audio.ready, 0);
  ck_assert_uint_eq(g.s->audio.remlen, cut);
  handle_audio_pes(g.s, 0, 0, frame + cut, n - cut);
  ck_assert_int_eq(g.s->audio.ready, 1);
  ck_assert_uint_eq(g.s->audio.remlen, 0u);
  ck_assert_int_eq((int)g.s->audio.nominal_samples, AAC_SAMPLES);
  seg_close(&g);
}
END_TEST

START_TEST(several_frames_in_one_pes_all_count) {
  seg_t g;
  pid_filter_t none = {0};
  uint8_t data[1024];
  size_t n = 0;

  seg_open(&g, &none, SEG_CONTAINER_FMP4);
  audio_ready(&g);
  for (int i = 0; i < 3; i++) n += adts_frame(data + n, ADTS_PAYLOAD);
  handle_audio_pes(g.s, 0, 0, data, n);
  ck_assert_int_eq((int)g.s->audio.nominal_samples, 3 * AAC_SAMPLES);
  seg_close(&g);
}
END_TEST

START_TEST(leading_garbage_is_skipped_to_the_next_frame) {
  seg_t g;
  pid_filter_t none = {0};
  uint8_t data[512];
  size_t n = 5;

  seg_open(&g, &none, SEG_CONTAINER_FMP4);
  audio_ready(&g);
  memset(data, 0x00, n);
  n += adts_frame(data + n, ADTS_PAYLOAD);
  handle_audio_pes(g.s, 0, 0, data, n);
  ck_assert_int_eq(g.s->audio.ready, 1);
  ck_assert_int_eq((int)g.s->audio.nominal_samples, AAC_SAMPLES);
  seg_close(&g);
}
END_TEST

START_TEST(pure_garbage_never_makes_audio_ready) {
  seg_t g;
  pid_filter_t none = {0};
  uint8_t data[200];

  seg_open(&g, &none, SEG_CONTAINER_FMP4);
  audio_ready(&g);
  memset(data, 0x00, sizeof data);
  handle_audio_pes(g.s, 0, 0, data, sizeof data);
  ck_assert_int_eq(g.s->audio.ready, 0);
  seg_close(&g);
}
END_TEST

START_TEST(timestamps_anchor_once_ready_and_measure_drift) {
  seg_t g;
  pid_filter_t none = {0};
  uint8_t frame[256];
  size_t n = adts_frame(frame, ADTS_PAYLOAD);

  seg_open(&g, &none, SEG_CONTAINER_FMP4);
  audio_ready(&g);
  handle_audio_pes(g.s, 1, 90000, frame, n);
  ck_assert_int_eq(g.s->audio.pts_anchored, 0);
  handle_audio_pes(g.s, 1, 90000, frame, n);
  ck_assert_int_eq(g.s->audio.pts_anchored, 1);
  ck_assert_int_eq((int)g.s->audio.anchor_pts_ms, 1000);
  ck_assert_int_eq((int)g.s->audio.anchor_nominal_samples, AAC_SAMPLES);
  handle_audio_pes(g.s, 1, 90000 * 2, frame, n);
  ck_assert_int_eq((int)g.s->audio.pending_drift_samples, ADTS_RATE - AAC_SAMPLES);
  ck_assert_int_eq((int)g.s->audio.anchor_pts_ms, 2000);
  seg_close(&g);
}
END_TEST

START_TEST(transport_containers_ignore_audio_pes) {
  seg_t g;
  pid_filter_t none = {0};
  uint8_t frame[256];
  size_t n = adts_frame(frame, ADTS_PAYLOAD);

  seg_open(&g, &none, SEG_CONTAINER_TS);
  audio_ready(&g);
  handle_audio_pes(g.s, 1, 90000, frame, n);
  ck_assert_int_eq(g.s->audio.ready, 0);
  ck_assert_uint_eq(g.s->audio.remlen, 0u);
  seg_close(&g);
}
END_TEST

START_TEST(runaway_remainders_are_discarded) {
  seg_t g;
  pid_filter_t none = {0};
  uint8_t frame[256];
  size_t n = adts_frame(frame, ADTS_PAYLOAD);

  seg_open(&g, &none, SEG_CONTAINER_FMP4);
  audio_ready(&g);
  g.s->audio.remlen = 70000;
  handle_audio_pes(g.s, 0, 0, frame, n);
  ck_assert_int_eq(g.s->audio.ready, 1);
  ck_assert_uint_lt(g.s->audio.remlen, 1000u);
  seg_close(&g);
}
END_TEST

START_TEST(snapshot_lists_the_program_pids_in_order_and_honours_the_cap) {
  psi_t *psi = build_psi_with_program(1, VIDEO_PID, ST_H264, VIDEO_PID);
  unsigned allowed[8];
  int n = 0;

  pidlock_snapshot(psi, allowed, &n, 8);
  ck_assert_int_eq(n, 4);
  ck_assert_uint_eq(allowed[0], 0u);
  ck_assert_uint_eq(allowed[1], (unsigned)PMT_PID);
  ck_assert_uint_eq(allowed[2], (unsigned)VIDEO_PID);
  ck_assert_uint_eq(allowed[3], (unsigned)VIDEO_PID);
  pidlock_snapshot(psi, allowed, &n, 3);
  ck_assert_int_eq(n, 3);
  pidlock_snapshot(psi, allowed, &n, 4);
  ck_assert_int_eq(n, 4);
  psi_free(psi);
}
END_TEST

START_TEST(allowed_lookup_is_exact) {
  const unsigned list[] = {0, 0x100, 0x101};

  ck_assert_int_eq(pidlock_allowed(list, 3, 0x100), 1);
  ck_assert_int_eq(pidlock_allowed(list, 3, 0x102), 0);
  ck_assert_int_eq(pidlock_allowed(list, 0, 0), 0);
}
END_TEST

typedef struct {
  const char *name;
  lcevc_select_t sel;
  int expect_excluded[3];
} lcevc_case_t;

static const lcevc_case_t lcevc_cases[] = {
    {"base layer only", {LCEVC_SEL_BASE, 0, 0}, {1, 1, 1}},
    {"full", {LCEVC_SEL_FULL, 0, 0}, {0, 0, 0}},
    {"all", {LCEVC_SEL_ALL, 0, 0}, {0, 0, 0}},
    {"second by index", {LCEVC_SEL_N, 1, 0}, {1, 0, 1}},
    {"third by pid", {LCEVC_SEL_N, 0x202, 1}, {1, 1, 0}},
    {"index out of range", {LCEVC_SEL_N, 9, 0}, {0, 0, 0}},
};

START_TEST(lcevc_selection_excludes_the_unwanted_layers) {
  const lcevc_case_t *lc = &lcevc_cases[_i];
  static const unsigned pids[3] = {0x200, 0x201, 0x202};
  pid_filter_t filter = {0};

  pidlock_apply_lcevc(&lc->sel, &filter, pids, 3);
  for (int i = 0; i < 3; i++) ck_assert_int_eq(pid_filter_excludes(&filter, pids[i]), lc->expect_excluded[i]);
}
END_TEST

START_TEST(pmt_rewrite_falls_back_silently_when_it_cannot_apply) {
  psi_t *psi = build_psi_with_program(1, VIDEO_PID, ST_H264, VIDEO_PID);
  psi_t *empty = psi_new();
  pid_filter_t none = {0};
  pid_filter_t drop = {0};
  unsigned char pkt[188];
  unsigned char rw[PSI_SECTION_ASM_BUF_LEN];
  unsigned char out[188];
  unsigned char cc = 4;

  pid_filter_add(&drop, AUDIO_PID);
  fixture_ts_packet(pkt, PMT_PID, 0, 0x55);
  ck_assert_ptr_eq(pidlock_rewrite_pmt(NULL, &drop, &cc, pkt, PMT_PID, rw, out), pkt);
  ck_assert_ptr_eq(pidlock_rewrite_pmt(psi, &none, &cc, pkt, PMT_PID, rw, out), pkt);
  ck_assert_ptr_eq(pidlock_rewrite_pmt(empty, &drop, &cc, pkt, PMT_PID, rw, out), pkt);
  ck_assert_ptr_eq(pidlock_rewrite_pmt(psi, &drop, &cc, pkt, OTHER_PID, rw, out), pkt);
  ck_assert_uint_eq(cc, 4u);
  ck_assert_ptr_eq(pidlock_rewrite_pmt(psi, &drop, &cc, pkt, PMT_PID, rw, out), out);
  ck_assert_uint_eq(cc, 5u);
  psi_free(psi);
  psi_free(empty);
}
END_TEST

static Suite *demux_suite(void) {
  Suite *s = suite_create("dipixy_segment_demux");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, program_lock_picks_pids_and_drops_the_probe_phase);
  tcase_add_test(tc, fragmented_containers_also_lock_the_audio_track);
  tcase_add_test(tc, only_the_locked_programs_pids_are_buffered);
  tcase_add_test(tc, excluded_pids_never_reach_discovery_or_the_buffer);
  tcase_add_test(tc, program_without_video_never_locks);
  tcase_add_test(tc, transport_pmt_keeps_all_streams_without_a_filter);
  tcase_add_test(tc, transport_pmt_loses_filtered_streams);
  tcase_add_test(tc, first_adts_frame_fills_the_audio_parameters);
  tcase_add_test(tc, frames_split_across_pes_packets_are_reassembled);
  tcase_add_test(tc, several_frames_in_one_pes_all_count);
  tcase_add_test(tc, leading_garbage_is_skipped_to_the_next_frame);
  tcase_add_test(tc, pure_garbage_never_makes_audio_ready);
  tcase_add_test(tc, timestamps_anchor_once_ready_and_measure_drift);
  tcase_add_test(tc, transport_containers_ignore_audio_pes);
  tcase_add_test(tc, runaway_remainders_are_discarded);
  tcase_add_test(tc, snapshot_lists_the_program_pids_in_order_and_honours_the_cap);
  tcase_add_test(tc, allowed_lookup_is_exact);
  tcase_add_loop_test(tc, lcevc_selection_excludes_the_unwanted_layers, 0, (int)(sizeof lcevc_cases / sizeof lcevc_cases[0]));
  tcase_add_test(tc, pmt_rewrite_falls_back_silently_when_it_cannot_apply);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(demux_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
