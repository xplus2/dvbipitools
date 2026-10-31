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
#include "dipixy/segstore_int.h"

#define GROUP "239.255.42.42"
#define PORT 42142
#define MAX_STORES 8
#define SEG_TARGET 2.0
#define MAX_SEGS 3
#define PMT_PID 0x100
#define VIDEO_PID 0x101
#define AUDIO_PID 0x102
#define NEW_VIDEO_PID 0x201
#define NEW_AUDIO_PID 0x202
#define NEW_PMT_PID 0x300
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
  g->ctx = capture_open(AF_INET, GROUP, NULL, PORT, NULL, 0, NULL, NULL, NULL, 0, 0);
  ck_assert_ptr_nonnull(g->ctx);
  ck_assert_int_eq(hls_seg_touch(capture_open(AF_INET, GROUP, NULL, PORT, NULL, 0, NULL, NULL, NULL, 0, 0), filter, 0, &full, SEG_TARGET, MAX_SEGS, container, 0.0), 1);
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

static const struct {
  unsigned stream_type;
  codec_t codec;
} audio_stream_types[] = {
    {0x03, CODEC_MP2A},
    {0x11, CODEC_AAC_LATM},
    {0x81, CODEC_AC3},
    {0x87, CODEC_EAC3},
};

START_TEST(fragmented_containers_lock_every_supported_audio_codec) {
  const unsigned es[2][2] = {{ST_H264, VIDEO_PID}, {audio_stream_types[_i].stream_type, AUDIO_PID}};
  seg_t g;
  pid_filter_t none = {0};

  seg_open(&g, &none, SEG_CONTAINER_FMP4);
  feed_programme(&g, es, 2);
  ck_assert_int_eq(g.s->demux.audio_pid_known, 1);
  ck_assert_int_eq(g.s->demux.audio_codec, audio_stream_types[_i].codec);
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

START_TEST(low_rate_aac_waits_for_the_probe) {
  seg_t g;
  pid_filter_t none = {0};
  uint8_t frame[256];
  size_t n = adts_frame(frame, ADTS_PAYLOAD);
  int frames = 0;

  frame[2] = (uint8_t)((frame[2] & 0xC3) | (6u << 2)); /* 24 kHz: SBR possible */
  seg_open(&g, &none, SEG_CONTAINER_FMP4);
  audio_ready(&g);
  while (!g.s->audio.ready && frames < 64) {
    handle_audio_pes(g.s, 0, 0, frame, n);
    frames++;
  }
  ck_assert_int_eq(g.s->audio.ready, 1);
  ck_assert_int_gt(frames, 1);
  ck_assert_uint_eq(g.s->audio.rate, 24000u);
  ck_assert_uint_eq(g.s->audio.out_rate, 0u); /* undecided: core rate stands */
  ck_assert_uint_eq(g.s->audio.aac_cfg_len, 0u);
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
  ck_assert_int_eq((int)g.s->audio.anchor_pts, 90000);
  ck_assert_int_eq((int)g.s->audio.anchor_nominal_samples, AAC_SAMPLES);
  handle_audio_pes(g.s, 1, 90000 * 2, frame, n);
  ck_assert_int_eq((int)g.s->audio.pending_drift_samples, ADTS_RATE - AAC_SAMPLES);
  ck_assert_int_eq((int)g.s->audio.anchor_pts, 180000);
  seg_close(&g);
}
END_TEST

START_TEST(splice_sized_drift_is_not_corrected) {
  seg_t g;
  pid_filter_t none = {0};
  uint8_t frame[256];
  size_t n = adts_frame(frame, ADTS_PAYLOAD);

  seg_open(&g, &none, SEG_CONTAINER_FMP4);
  audio_ready(&g);
  handle_audio_pes(g.s, 1, 90000, frame, n);
  handle_audio_pes(g.s, 1, 90000, frame, n);
  handle_audio_pes(g.s, 1, 90000 * 10, frame, n);
  ck_assert_int_eq((int)g.s->audio.pending_drift_samples, 0);
  ck_assert_int_eq((int)g.s->audio.anchor_pts, 90000 * 10);
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
  unsigned char out[PMT_FILTER_PKT_MAX * 188];
  unsigned char cc = 4;
  size_t n = 0;

  pid_filter_add(&drop, AUDIO_PID);
  fixture_ts_packet(pkt, PMT_PID, 0, 0x55);
  pkt[1] |= 0x40;
  ck_assert_ptr_eq(pidlock_rewrite_pmt(NULL, &drop, &cc, pkt, PMT_PID, rw, out, &n), pkt);
  ck_assert_uint_eq(n, 1u);
  ck_assert_ptr_eq(pidlock_rewrite_pmt(psi, &none, &cc, pkt, PMT_PID, rw, out, &n), pkt);
  ck_assert_ptr_eq(pidlock_rewrite_pmt(empty, &drop, &cc, pkt, PMT_PID, rw, out, &n), pkt);
  ck_assert_ptr_eq(pidlock_rewrite_pmt(psi, &drop, &cc, pkt, OTHER_PID, rw, out, &n), pkt);
  ck_assert_uint_eq(cc, 4u);
  ck_assert_ptr_eq(pidlock_rewrite_pmt(psi, &drop, &cc, pkt, PMT_PID, rw, out, &n), out);
  ck_assert_uint_eq(n, 1u);
  ck_assert_uint_eq(cc, 5u);
  pkt[1] &= (unsigned char)~0x40;
  ck_assert_ptr_eq(pidlock_rewrite_pmt(psi, &drop, &cc, pkt, PMT_PID, rw, out, &n), NULL);
  ck_assert_uint_eq(n, 0u);
  ck_assert_uint_eq(cc, 5u);
  psi_free(psi);
  psi_free(empty);
}
END_TEST

START_TEST(pmt_rewrite_spans_packets_when_the_section_outgrows_one) {
  enum { N_ES = 40, DROP_PID = 0x205 };
  unsigned char body[9 + N_ES * 5];
  unsigned char section[3 + sizeof body + 4];
  unsigned char pat[64];
  unsigned char pat_pkt[188];
  unsigned char src[2 * 188];
  unsigned char rw[PSI_SECTION_ASM_BUF_LEN];
  unsigned char out[PMT_FILTER_PKT_MAX * 188];
  unsigned char cc_src = 0;
  unsigned char cc = 4;
  size_t n = 0;
  size_t o = 0;
  size_t hdr;
  size_t pat_len;
  psi_t *psi = psi_new();
  pid_filter_t drop = {0};

  body[o++] = 0x00;
  body[o++] = 0x01;
  body[o++] = 0xC1;
  body[o++] = 0x00;
  body[o++] = 0x00;
  body[o++] = 0xE2;
  body[o++] = 0x00;
  body[o++] = 0xF0;
  body[o++] = 0x00;
  for (unsigned i = 0; i < N_ES; i++) {
    body[o++] = ST_H264;
    body[o++] = (unsigned char)(0xE0 | (((0x200 + i) >> 8) & 0x1F));
    body[o++] = (unsigned char)(0x200 + i);
    body[o++] = 0xF0;
    body[o++] = 0x00;
  }
  hdr = o + 4;
  section[0] = 0x02;
  section[1] = (unsigned char)(0xB0 | ((hdr >> 8) & 0x0F));
  section[2] = (unsigned char)hdr;
  memcpy(section + 3, body, o);
  be32_put(section + 3 + o, crc32_mpeg(section, 3 + o));
  ck_assert_uint_eq(pmt_filter_emit_packets(src, sizeof src, PMT_PID, &cc_src, section, sizeof section), 2u);

  pat_len = psi_build_pat(0x1234, 0, 1, PMT_PID, pat, sizeof pat);
  fixture_wrap_section(pat_pkt, 0x0000, pat, pat_len);
  psi_feed(psi, pat_pkt);
  psi_feed(psi, src);
  psi_feed(psi, src + 188);
  ck_assert_int_eq(psi_have_pmt(psi), 1);

  pid_filter_add(&drop, DROP_PID);
  ck_assert_ptr_eq(pidlock_rewrite_pmt(psi, &drop, &cc, src, PMT_PID, rw, out, &n), out);
  ck_assert_uint_eq(n, 2u);
  ck_assert_uint_eq(cc, 6u);
  ck_assert_uint_eq(out[1] & 0x40, 0x40u);
  ck_assert_uint_eq(out[3] & 0x0F, 5u);
  ck_assert_uint_eq(out[188 + 1] & 0x40, 0u);
  ck_assert_uint_eq(out[188 + 3] & 0x0F, 6u);
  ck_assert_int_eq(has_es_entry(out, n * 188, ST_H264, DROP_PID), 0);
  ck_assert_int_eq(has_es_entry(out, n * 188, ST_H264, 0x204), 1);
  ck_assert_int_eq(has_es_entry(out, n * 188, ST_H264, 0x206), 1);

  ck_assert_ptr_eq(pidlock_rewrite_pmt(psi, &drop, &cc, src + 188, PMT_PID, rw, out, &n), NULL);
  ck_assert_uint_eq(n, 0u);
  ck_assert_uint_eq(cc, 6u);
  psi_free(psi);
}
END_TEST

START_TEST(pat_rewrite_narrows_to_the_locked_program) {
  psi_t *psi = build_psi_with_program(1, VIDEO_PID, ST_H264, VIDEO_PID);
  psi_t *empty = psi_new();
  unsigned char pkt[188];
  unsigned char out[188];
  unsigned char cc = 4;

  fixture_ts_packet(pkt, 0, 0, 0x55);
  pkt[1] |= 0x40;
  ck_assert_ptr_eq(pidlock_rewrite_pat(NULL, &cc, pkt, 0, out), pkt);
  ck_assert_ptr_eq(pidlock_rewrite_pat(empty, &cc, pkt, 0, out), pkt);
  ck_assert_ptr_eq(pidlock_rewrite_pat(psi, &cc, pkt, OTHER_PID, out), pkt);
  ck_assert_uint_eq(cc, 4u);
  ck_assert_ptr_eq(pidlock_rewrite_pat(psi, &cc, pkt, 0, out), out);
  ck_assert_uint_eq(cc, 5u);
  ck_assert_uint_eq(out[0], 0x47u);
  ck_assert_uint_eq(out[3] & 0x0F, 5u);
  ck_assert_uint_eq(out[5], 0u);
  ck_assert_uint_eq(((unsigned)out[15] & 0x1F) << 8 | out[16], (unsigned)PMT_PID);
  ck_assert_uint_eq(psi_program_number(psi), ((unsigned)out[13] << 8) | out[14]);
  pkt[1] &= (unsigned char)~0x40;
  ck_assert_ptr_eq(pidlock_rewrite_pat(psi, &cc, pkt, 0, out), NULL);
  ck_assert_uint_eq(cc, 5u);
  psi_free(psi);
  psi_free(empty);
}
END_TEST

static const unsigned new_av_es[2][2] = {{ST_H264, NEW_VIDEO_PID}, {ST_AAC, NEW_AUDIO_PID}};

static void feed_programme_pcr(const seg_t *g, unsigned pcr_pid, const unsigned (*es)[2], unsigned n_es) {
  unsigned char pkts[2][188];

  fixture_programme_packets_es(1, pcr_pid, es, n_es, pkts);
  hls_seg_feed_all(g->ctx, pkts[0]);
  hls_seg_feed_all(g->ctx, pkts[1]);
}

static const hls_snapshot_t *store_snap(const seg_t *g) {
  return atomic_load(&g->s->store->snap);
}

START_TEST(changed_pmt_relocks_onto_the_new_pids) {
  seg_t g;
  pid_filter_t none = {0};

  seg_open(&g, &none, SEG_CONTAINER_TS);
  feed_programme(&g, av_es, 2);
  feed_programme_pcr(&g, NEW_VIDEO_PID, new_av_es, 2);
  ck_assert_int_eq(g.s->demux.video_pid_known, 1);
  ck_assert_uint_eq(g.s->demux.video_pid, (unsigned)NEW_VIDEO_PID);
  ck_assert_int_eq(pidlock_allowed(g.s->demux.allowed_pids, g.s->demux.n_allowed, NEW_AUDIO_PID), 1);
  ck_assert_int_eq(pidlock_allowed(g.s->demux.allowed_pids, g.s->demux.n_allowed, AUDIO_PID), 0);
  ck_assert_uint_eq(g.s->len, 188u);
  feed_pid(&g, VIDEO_PID);
  ck_assert_uint_eq(g.s->len, 188u);
  feed_pid(&g, NEW_VIDEO_PID);
  ck_assert_uint_eq(g.s->len, 376u);
  seg_close(&g);
}
END_TEST

START_TEST(repeated_identical_pmt_keeps_the_lock_state) {
  seg_t g;
  pid_filter_t none = {0};

  seg_open(&g, &none, SEG_CONTAINER_TS);
  feed_programme(&g, av_es, 2);
  feed_programme(&g, av_es, 2);
  ck_assert_uint_eq(g.s->len, 564u);
  ck_assert_int_eq(store_snap(&g) == NULL || store_snap(&g)->epoch == 0, 1);
  seg_close(&g);
}
END_TEST

START_TEST(relock_follows_codec_and_audio_changes_for_fragmented_output) {
  const unsigned hevc_es[2][2] = {{0x24, VIDEO_PID}, {0x81, AUDIO_PID}};
  seg_t g;
  pid_filter_t none = {0};

  seg_open(&g, &none, SEG_CONTAINER_FMP4);
  feed_programme(&g, av_es, 2);
  ck_assert_int_eq(g.s->demux.video_codec, CODEC_H264);
  ck_assert_int_eq(g.s->demux.audio_codec, CODEC_AAC);
  feed_programme(&g, hevc_es, 2);
  ck_assert_int_eq(g.s->demux.video_codec, CODEC_HEVC);
  ck_assert_int_eq(g.s->video.es.codec, CODEC_HEVC);
  ck_assert_int_eq(g.s->demux.audio_codec, CODEC_AC3);
  ck_assert_int_eq(g.s->audio.es.codec, CODEC_AC3);
  ck_assert_int_eq(g.s->audio.ready, 0);
  feed_programme(&g, new_av_es, 1);
  ck_assert_uint_eq(g.s->demux.video_pid, (unsigned)NEW_VIDEO_PID);
  ck_assert_int_eq(g.s->demux.audio_pid_known, 0);
  ck_assert_int_eq(g.s->audio.present, 0);
  seg_close(&g);
}
END_TEST

START_TEST(relock_restarts_the_published_window) {
  const uint8_t data[188] = {0x47};
  seg_t g;
  pid_filter_t none = {0};

  seg_open(&g, &none, SEG_CONTAINER_TS);
  feed_programme(&g, av_es, 2);
  ck_assert_int_eq(hls_push_segment_at(g.s->store, data, sizeof data, 2.0), 0);
  ck_assert_int_eq(hls_push_segment_at(g.s->store, data, sizeof data, 2.0), 0);
  ck_assert_int_eq(store_snap(&g)->count, 2);
  feed_programme_pcr(&g, NEW_VIDEO_PID, new_av_es, 2);
  ck_assert_uint_eq(store_snap(&g)->epoch, 1u);
  ck_assert_int_eq(store_snap(&g)->count, 0);
  ck_assert_uint_eq(store_snap(&g)->oldest_seq, 3u);
  ck_assert_uint_eq(store_snap(&g)->epoch_base_ms, 4000u);
  seg_close(&g);
}
END_TEST

START_TEST(relock_follows_a_program_moved_to_another_pmt_pid) {
  unsigned char pkts[2][188];
  unsigned char pat[64];
  unsigned char pat_pkt[188];
  unsigned char pmt_pkt[188];
  size_t pat_len = psi_build_pat(0x1234, 1, 1, NEW_PMT_PID, pat, sizeof pat);
  size_t pmt_len;
  seg_t g;
  pid_filter_t none = {0};

  seg_open(&g, &none, SEG_CONTAINER_TS);
  feed_programme(&g, av_es, 2);
  fixture_programme_packets_es(1, NEW_VIDEO_PID, new_av_es, 2, pkts);
  pmt_len = 3 + (((size_t)(pkts[1][6] & 0x0F) << 8) | pkts[1][7]);
  fixture_wrap_section(pat_pkt, 0x0000, pat, pat_len);
  fixture_wrap_section(pmt_pkt, NEW_PMT_PID, pkts[1] + 5, pmt_len);
  hls_seg_feed_all(g.ctx, pat_pkt);
  hls_seg_feed_all(g.ctx, pmt_pkt);
  ck_assert_uint_eq(g.s->demux.video_pid, (unsigned)NEW_VIDEO_PID);
  ck_assert_int_eq(pidlock_allowed(g.s->demux.allowed_pids, g.s->demux.n_allowed, NEW_PMT_PID), 1);
  ck_assert_int_eq(pidlock_allowed(g.s->demux.allowed_pids, g.s->demux.n_allowed, PMT_PID), 0);
  seg_close(&g);
}
END_TEST

START_TEST(lcevc_excludes_never_change_the_lookup_key) {
  seg_t g;
  pid_filter_t none = {0};
  pid_filter_t key;

  seg_open(&g, &none, SEG_CONTAINER_TS);
  key = g.s->filter;
  pid_filter_add(&g.s->drop, OTHER_PID);
  ck_assert(pid_filter_equal(&g.s->filter, &key));
  seg_close(&g);
}
END_TEST

static Suite *demux_suite(void) {
  Suite *s = suite_create("dipixy_segment_demux");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, program_lock_picks_pids_and_drops_the_probe_phase);
  tcase_add_test(tc, fragmented_containers_also_lock_the_audio_track);
  tcase_add_loop_test(tc, fragmented_containers_lock_every_supported_audio_codec, 0, 4);
  tcase_add_test(tc, only_the_locked_programs_pids_are_buffered);
  tcase_add_test(tc, excluded_pids_never_reach_discovery_or_the_buffer);
  tcase_add_test(tc, program_without_video_never_locks);
  tcase_add_test(tc, transport_pmt_keeps_all_streams_without_a_filter);
  tcase_add_test(tc, transport_pmt_loses_filtered_streams);
  tcase_add_test(tc, first_adts_frame_fills_the_audio_parameters);
  tcase_add_test(tc, frames_split_across_pes_packets_are_reassembled);
  tcase_add_test(tc, low_rate_aac_waits_for_the_probe);
  tcase_add_test(tc, several_frames_in_one_pes_all_count);
  tcase_add_test(tc, leading_garbage_is_skipped_to_the_next_frame);
  tcase_add_test(tc, pure_garbage_never_makes_audio_ready);
  tcase_add_test(tc, timestamps_anchor_once_ready_and_measure_drift);
  tcase_add_test(tc, splice_sized_drift_is_not_corrected);
  tcase_add_test(tc, transport_containers_ignore_audio_pes);
  tcase_add_test(tc, runaway_remainders_are_discarded);
  tcase_add_test(tc, snapshot_lists_the_program_pids_in_order_and_honours_the_cap);
  tcase_add_test(tc, allowed_lookup_is_exact);
  tcase_add_loop_test(tc, lcevc_selection_excludes_the_unwanted_layers, 0, (int)(sizeof lcevc_cases / sizeof lcevc_cases[0]));
  tcase_add_test(tc, pmt_rewrite_falls_back_silently_when_it_cannot_apply);
  tcase_add_test(tc, pmt_rewrite_spans_packets_when_the_section_outgrows_one);
  tcase_add_test(tc, pat_rewrite_narrows_to_the_locked_program);
  tcase_add_test(tc, changed_pmt_relocks_onto_the_new_pids);
  tcase_add_test(tc, repeated_identical_pmt_keeps_the_lock_state);
  tcase_add_test(tc, relock_follows_codec_and_audio_changes_for_fragmented_output);
  tcase_add_test(tc, relock_restarts_the_published_window);
  tcase_add_test(tc, relock_follows_a_program_moved_to_another_pmt_pid);
  tcase_add_test(tc, lcevc_excludes_never_change_the_lookup_key);
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
