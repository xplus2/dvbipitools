/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dipitvhead/mux/pcrclock.h"
#include "dipitvhead/mux/pesstamp.h"
#include "dipitvhead/mux/remux.h"
#include "dipitvhead/mux/remux/priv.h"
#include "psi_fixture.h"
#include "lib/demux/crc32.h"
#include "lib/helper/beutil.h"
#include "lib/mux/psi_build.h"
#include "lib/sys/ioutil.h"

#define MAX_SEEN 64

static unsigned g_pids[MAX_SEEN];
static unsigned char g_cc[MAX_SEEN];
static unsigned char g_last_pkt[188];
static int g_count;

static void capture_cb(void *ctx, const unsigned char *pkt) {
  (void)ctx;
  memcpy(g_last_pkt, pkt, 188);
  if (g_count < MAX_SEEN) {
    g_pids[g_count] = (((unsigned)pkt[1] & 0x1F) << 8) | pkt[2];
    g_cc[g_count] = pkt[3] & 0x0F;
  }
  g_count++;
}

static int saw_pid(unsigned pid) {
  for (int i = 0; i < g_count && i < MAX_SEEN; i++)
    if (g_pids[i] == pid)
      return 1;
  return 0;
}

static void wrap_ts_packet(unsigned char pkt[188], unsigned pid, const unsigned char *section, size_t slen) {
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt[4] = 0x00;
  memcpy(pkt + 5, section, slen);
  for (size_t i = 5 + slen; i < 188; i++)
    pkt[i] = 0xFF;
}

/* discovery psi_t with one video (0x0101, H264) and one audio (0x0102, AAC/eng) ES,
 * PAT program_number 101 -> PMT pid 0x0100. optional descriptor bytes: PMT program_info,
 * video ES info, CAT body (CA_descriptor for the EMM pid) */
static psi_t *build_discovery_psi_with(const unsigned char *prog_info, size_t prog_info_len, const unsigned char *video_info, size_t video_info_len, const unsigned char *cat_desc, size_t cat_desc_len) {
  unsigned char section[256];
  unsigned char pkt[188];
  size_t slen;
  psi_t *psi = psi_new();

  slen = psi_build_pat(0x1234, 0, 101, 0x0100, section, sizeof section);
  wrap_ts_packet(pkt, 0x0000, section, slen);
  psi_feed(psi, pkt);

  {
    /* hand-build a 2-ES PMT: video H264 @0x101, audio AAC @0x102 lang "eng" */
    unsigned char body[128];
    size_t n = 0;
    size_t hdr;
    size_t crc_at;
    uint32_t crc;

    body[n++] = (unsigned char)(101 >> 8);
    body[n++] = (unsigned char)101;
    body[n++] = 0xC1;
    body[n++] = 0x00;
    body[n++] = 0x00;
    body[n++] = 0xE0 | ((0x0101 >> 8) & 0x1F); /* PCR pid = video */
    body[n++] = 0x01;
    body[n++] = (unsigned char)(0xF0 | ((prog_info_len >> 8) & 0x0F));
    body[n++] = (unsigned char)prog_info_len;
    if (prog_info_len) memcpy(body + n, prog_info, prog_info_len);
    n += prog_info_len;
    body[n++] = 0x1B; /* H264 */
    body[n++] = 0xE0 | ((0x0101 >> 8) & 0x1F);
    body[n++] = 0x01;
    body[n++] = (unsigned char)(0xF0 | ((video_info_len >> 8) & 0x0F));
    body[n++] = (unsigned char)video_info_len;
    if (video_info_len) memcpy(body + n, video_info, video_info_len);
    n += video_info_len;
    body[n++] = 0x0F; /* AAC */
    body[n++] = 0xE0 | ((0x0102 >> 8) & 0x1F);
    body[n++] = 0x02;
    body[n++] = 0xF0;
    body[n++] = 0x06; /* ES_info_length = 6: one ISO 639 descriptor */
    body[n++] = 0x0A;
    body[n++] = 4;
    body[n++] = 'e';
    body[n++] = 'n';
    body[n++] = 'g';
    body[n++] = 0x00;

    hdr = n + 4;
    section[0] = 0x02;
    section[1] = (unsigned char)(0xB0 | ((hdr >> 8) & 0x0F));
    section[2] = (unsigned char)hdr;
    memcpy(section + 3, body, n);
    crc_at = 3 + n;
    crc = crc32_mpeg(section, crc_at);
    section[crc_at + 0] = (unsigned char)(crc >> 24);
    section[crc_at + 1] = (unsigned char)(crc >> 16);
    section[crc_at + 2] = (unsigned char)(crc >> 8);
    section[crc_at + 3] = (unsigned char)crc;
    slen = crc_at + 4;
  }
  wrap_ts_packet(pkt, 0x0100, section, slen);
  psi_feed(psi, pkt);

  if (cat_desc_len) {
    slen = psi_build_cat(0, cat_desc, cat_desc_len, section, sizeof section);
    wrap_ts_packet(pkt, 0x0001, section, slen);
    psi_feed(psi, pkt);
  }

  return psi;
}

static psi_t *build_discovery_psi(void) {
  return build_discovery_psi_with(NULL, 0, NULL, 0, NULL, 0);
}

static void base_cfg(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
  cfg->nit_mode = TABLE_DROP;
  cfg->tsid = 1;
  cfg->onid = 2;
}

static void base_input(dipitvhead_input_t *input) {
  memset(input, 0, sizeof *input);
  input->sdt_mode = TABLE_DROP;
  input->hbbtv_url = NULL;
  input->sid = 101;
}

START_TEST(remux_forwards_mapped_es_and_sends_pat_pmt_on_first_feed) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];
  ts_metrics_t tsm;
  int video_idx = -1;

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  ck_assert_ptr_nonnull(r);
  memset(&tsm, 0, sizeof tsm);

  memset(pkt, 0xAB, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x00 | ((0x0101 >> 8) & 0x1F));
  pkt[2] = (unsigned char)0x0101;
  pkt[3] = 0x10;

  g_count = 0;
  remux_feed(r, 0.0, pkt, capture_cb, NULL, &tsm);

  ck_assert(saw_pid(0x0000));  /* PAT */
  ck_assert(saw_pid(0x1000));  /* our PMT pid */
  ck_assert(saw_pid(0x0100));  /* video remapped to pids.video_pid */
  ck_assert_uint_eq(tsm.psi_sections_total[PSI_TABLE_PAT], 1u);
  ck_assert_uint_eq(tsm.psi_sections_total[PSI_TABLE_PMT], 1u);
  ck_assert_uint_eq(tsm.psi_errors_total[PSI_TABLE_PAT], 0u);
  ck_assert_uint_eq(tsm.psi_errors_total[PSI_TABLE_PMT], 0u);

  for (int i = 0; i < g_count && i < MAX_SEEN; i++)
    if (g_pids[i] == 0x0100)
      video_idx = i;
  ck_assert_int_ge(video_idx, 0);
  ck_assert_uint_eq(g_cc[video_idx], 0u); /* source cc kept */

  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_es_exposes_output_pid_mapping) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  const out_es_t *es;
  int count;
  int saw_video = 0;
  int saw_audio = 0;

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  ck_assert_ptr_nonnull(r);

  es = remux_es(r, &count);
  ck_assert_int_eq(count, 2);
  for (int i = 0; i < count; i++) {
    if (es[i].src->cls == PID_VIDEO) {
      ck_assert_uint_eq(es[i].out_pid, pids.video_pid);
      ck_assert_uint_eq(es[i].in_pid, 0x0101);
      saw_video = 1;
    }
    if (es[i].src->cls == PID_AUDIO) {
      ck_assert_uint_eq(es[i].in_pid, 0x0102);
      saw_audio = 1;
    }
  }
  ck_assert(saw_video);
  ck_assert(saw_audio);

  remux_free(r);
  psi_free(psi);
}
END_TEST

static int feed_video_cc(remux_t *r, unsigned char *pkt, unsigned char afc_cc) {
  int video_idx = -1;
  pkt[3] = afc_cc;
  g_count = 0;
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  for (int i = 0; i < g_count && i < MAX_SEEN; i++)
    if (g_pids[i] == 0x0100) video_idx = i;
  return video_idx;
}

/* source cc is kept: duplicates and gaps stay visible downstream */
START_TEST(remux_keeps_source_cc_including_duplicates_and_gaps) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];
  int idx;

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  ck_assert_ptr_nonnull(r);

  memset(pkt, 0xAB, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((0x0101 >> 8) & 0x1F);
  pkt[2] = (unsigned char)0x0101;

  idx = feed_video_cc(r, pkt, 0x15);
  ck_assert_int_ge(idx, 0);
  ck_assert_uint_eq(g_cc[idx], 5u);
  idx = feed_video_cc(r, pkt, 0x15);
  ck_assert_int_ge(idx, 0);
  ck_assert_uint_eq(g_cc[idx], 5u); /* duplicate stays a duplicate */
  idx = feed_video_cc(r, pkt, 0x19);
  ck_assert_int_ge(idx, 0);
  ck_assert_uint_eq(g_cc[idx], 9u); /* gap stays a gap */
  pkt[4] = 183;
  idx = feed_video_cc(r, pkt, 0x20); /* AFC=10, no payload */
  ck_assert_int_ge(idx, 0);
  ck_assert_uint_eq(g_cc[idx], 0u);

  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_flags_discontinuity_on_first_adaptation_field_packet_after_new) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];
  int idx;

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  ck_assert_ptr_nonnull(r);
  memset(pkt, 0xAB, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((0x0101 >> 8) & 0x1F);
  pkt[2] = (unsigned char)0x0101;

  idx = feed_video_cc(r, pkt, 0x10); /* payload only: no room */
  ck_assert_int_ge(idx, 0);
  pkt[4] = 183;
  pkt[5] = 0x00;
  g_count = 0;
  pkt[3] = 0x31;
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  ck_assert_int_ge(g_count, 1);
  ck_assert_uint_eq(g_last_pkt[5] & 0x80, 0x80u);
  pkt[3] = 0x32;
  pkt[5] = 0x00;
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  ck_assert_uint_eq(g_last_pkt[5] & 0x80, 0u); /* once only */

  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_feed_counts_ts_packets_and_sync_errors) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];
  ts_metrics_t tsm;

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  ck_assert_ptr_nonnull(r);
  memset(&tsm, 0, sizeof tsm);

  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((0x0101 >> 8) & 0x1F);
  pkt[2] = (unsigned char)0x0101;
  pkt[3] = 0x10; /* AFC=01, cc=0 */
  remux_feed(r, 0.0, pkt, capture_cb, NULL, &tsm);
  ck_assert_uint_eq(tsm.ts_packets, 1u);
  ck_assert_uint_eq(tsm.ts_sync_errors, 0u);

  pkt[0] = 0xAA; /* bad sync byte */
  remux_feed(r, 0.0, pkt, capture_cb, NULL, &tsm);
  ck_assert_uint_eq(tsm.ts_packets, 1u); /* unchanged: sync error, not counted as a packet */
  ck_assert_uint_eq(tsm.ts_sync_errors, 1u);

  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_feed_detects_continuity_gap_and_signaled_discontinuity) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];
  ts_metrics_t tsm;

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  ck_assert_ptr_nonnull(r);
  memset(&tsm, 0, sizeof tsm);

  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((0x0101 >> 8) & 0x1F);
  pkt[2] = (unsigned char)0x0101;

  pkt[3] = 0x10; /* cc=0 */
  remux_feed(r, 0.0, pkt, capture_cb, NULL, &tsm);
  pkt[3] = 0x11; /* cc=1, sequential */
  remux_feed(r, 0.0, pkt, capture_cb, NULL, &tsm);
  ck_assert_uint_eq(tsm.ts_continuity_errors, 0u);
  ck_assert_uint_eq(tsm.ts_discontinuities, 0u);

  pkt[3] = 0x15; /* cc=5: skips 2..4, a real gap */
  remux_feed(r, 0.0, pkt, capture_cb, NULL, &tsm);
  ck_assert_uint_eq(tsm.ts_continuity_errors, 1u);
  ck_assert_uint_eq(tsm.ts_discontinuities, 0u);

  /* another jump, but discontinuity_indicator set: signaled, not an error */
  pkt[3] = 0x39; /* AFC=11, cc=9 */
  pkt[4] = 1;    /* adaptation_field_length */
  pkt[5] = 0x80; /* discontinuity_indicator */
  remux_feed(r, 0.0, pkt, capture_cb, NULL, &tsm);
  ck_assert_uint_eq(tsm.ts_continuity_errors, 1u); /* unchanged */
  ck_assert_uint_eq(tsm.ts_discontinuities, 1u);

  remux_free(r);
  psi_free(psi);
}
END_TEST

static void write_pcr_packet(unsigned char pkt[188], unsigned pid, unsigned char cc, uint64_t pcr27) {
  uint64_t base = pcr27 / 300;
  unsigned ext = (unsigned)(pcr27 % 300);
  memset(pkt, 0xFF, 188);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((pid >> 8) & 0x1F);
  pkt[2] = (unsigned char)pid;
  pkt[3] = (unsigned char)(0x20 | (cc & 0x0F)); /* AFC=10: adaptation only, no payload */
  pkt[4] = 183;
  pkt[5] = 0x10; /* PCR_flag */
  pkt[6] = (unsigned char)(base >> 25);
  pkt[7] = (unsigned char)(base >> 17);
  pkt[8] = (unsigned char)(base >> 9);
  pkt[9] = (unsigned char)(base >> 1);
  pkt[10] = (unsigned char)(((base & 1) << 7) | ((ext >> 8) & 1));
  pkt[11] = (unsigned char)ext;
}

START_TEST(remux_feed_detects_pcr_discontinuity_on_source_pcr_pid) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];
  ts_metrics_t tsm;

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1); /* PCR pid = video = 0x0101, per build_discovery_psi */
  ck_assert_ptr_nonnull(r);
  memset(&tsm, 0, sizeof tsm);

  write_pcr_packet(pkt, 0x0101, 0, 27000000ULL);
  remux_feed(r, 0.0, pkt, capture_cb, NULL, &tsm);
  ck_assert_uint_eq(tsm.pcr_discontinuities, 0u); /* first PCR: nothing to compare against yet */

  write_pcr_packet(pkt, 0x0101, 1, 27000000ULL + 27000000ULL / 10); /* +100ms of PCR ticks */
  remux_feed(r, 0.1, pkt, capture_cb, NULL, &tsm);                  /* +100ms wall clock: matches, plausible */
  ck_assert_uint_eq(tsm.pcr_discontinuities, 0u);

  write_pcr_packet(pkt, 0x0101, 2, 27000000ULL * 50); /* PCR jumps ~50s ahead */
  remux_feed(r, 0.2, pkt, capture_cb, NULL, &tsm);     /* wall clock only +100ms: implausible */
  ck_assert_uint_eq(tsm.pcr_discontinuities, 1u);

  remux_free(r);
  psi_free(psi);
}
END_TEST

static unsigned char g_last[188];

static void capture_last_cb(void *ctx, const unsigned char *pkt) {
  (void)ctx;
  memcpy(g_last, pkt, 188);
}

static void write_pes_packet(unsigned char pkt[188], unsigned pid, unsigned char cc, uint64_t pts) {
  memset(pkt, 0xFF, 188);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = (unsigned char)(0x10 | (cc & 0x0F));
  pkt[4] = 0;
  pkt[5] = 0;
  pkt[6] = 1;
  pkt[7] = 0xE0;
  pkt[8] = 0;
  pkt[9] = 0;
  pkt[10] = 0x80;
  pkt[11] = 0x80;
  pkt[12] = 5;
  pkt[13] = (unsigned char)(0x21 | ((pts >> 29) & 0x0E));
  pkt[14] = (unsigned char)(pts >> 22);
  pkt[15] = (unsigned char)(((pts >> 14) & 0xFE) | 1);
  pkt[16] = (unsigned char)(pts >> 7);
  pkt[17] = (unsigned char)(((pts << 1) & 0xFE) | 1);
}

static uint64_t last_pts(void) {
  pes_stamp_t st;
  ck_assert_int_eq(pesstamp_read(g_last, &st), PESSTAMP_FOUND);
  ck_assert_int_eq(st.has_pts, 1);
  return st.pts;
}

static uint64_t last_pcr(void) {
  uint64_t v;
  ck_assert_int_eq(pcr_packet_read(g_last, &v), 1);
  return v;
}

START_TEST(remux_rebase_shifts_pcr_and_timestamps_by_one_offset_after_a_source_jump) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];
  timemap_t tm;
  uint64_t pcr_before;
  uint64_t pcr_after;
  uint64_t k90;

  base_cfg(&cfg);
  cfg.pcr_mode = PCR_MODE_REBASE;
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  ck_assert_ptr_nonnull(r);
  timemap_init(&tm);
  remux_set_timemap(r, &tm);

  write_pcr_packet(pkt, 0x0101, 0, 27000000ULL * 100);
  remux_feed(r, 0.0, pkt, capture_last_cb, NULL, NULL);
  pcr_before = last_pcr();
  ck_assert_uint_eq(pcr_before, 27000000ULL * 100);
  write_pes_packet(pkt, 0x0101, 1, 9000000);
  remux_feed(r, 0.01, pkt, capture_last_cb, NULL, NULL);
  ck_assert_uint_eq(last_pts(), 9000000u);

  write_pcr_packet(pkt, 0x0101, 2, 27000000ULL * 5000);
  remux_feed(r, 0.04, pkt, capture_last_cb, NULL, NULL);
  pcr_after = last_pcr();
  ck_assert_uint_eq((unsigned)tm.relatches, 1u);
  {
    int64_t err = (int64_t)(pcr_after - pcr_before) - (int64_t)(27000000ULL * 4 / 100);
    ck_assert_int_le((int)(err < 0 ? -err : err), 150);
  }

  k90 = timemap_k90(&tm);
  write_pes_packet(pkt, 0x0101, 3, 123456789);
  remux_feed(r, 0.05, pkt, capture_last_cb, NULL, NULL);
  ck_assert_uint_eq(last_pts(), (123456789ULL + k90) % ((uint64_t)1 << 33));
  ck_assert_uint_eq(pcr_sub(pcr_after, 27000000ULL * 5000), k90 * 300u);

  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_rebase_applies_the_same_offset_to_every_elementary_stream) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];
  timemap_t tm;
  uint64_t video_pts;
  uint64_t audio_pts;

  base_cfg(&cfg);
  cfg.pcr_mode = PCR_MODE_REBASE;
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  ck_assert_ptr_nonnull(r);
  timemap_init(&tm);
  remux_set_timemap(r, &tm);

  write_pcr_packet(pkt, 0x0101, 0, 27000000ULL * 10);
  remux_feed(r, 0.0, pkt, capture_last_cb, NULL, NULL);
  write_pcr_packet(pkt, 0x0101, 1, 27000000ULL * 900);
  remux_feed(r, 0.04, pkt, capture_last_cb, NULL, NULL);
  ck_assert_uint_eq((unsigned)tm.relatches, 1u);

  write_pes_packet(pkt, 0x0101, 2, 1000000);
  remux_feed(r, 0.05, pkt, capture_last_cb, NULL, NULL);
  video_pts = last_pts();
  write_pes_packet(pkt, 0x0102, 0, 1000000);
  remux_feed(r, 0.05, pkt, capture_last_cb, NULL, NULL);
  audio_pts = last_pts();
  ck_assert_uint_eq(video_pts, audio_pts);
  ck_assert_uint_ne(video_pts, 1000000u);

  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_without_rebase_mode_leaves_timestamps_alone) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];
  timemap_t tm;

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  ck_assert_ptr_nonnull(r);
  timemap_init(&tm);
  remux_set_timemap(r, &tm);

  write_pcr_packet(pkt, 0x0101, 0, 27000000ULL * 10);
  remux_feed(r, 0.0, pkt, capture_last_cb, NULL, NULL);
  write_pcr_packet(pkt, 0x0101, 1, 27000000ULL * 900);
  remux_feed(r, 0.04, pkt, capture_last_cb, NULL, NULL);
  ck_assert_uint_eq(last_pcr(), 27000000ULL * 900);
  write_pes_packet(pkt, 0x0101, 2, 4242);
  remux_feed(r, 0.05, pkt, capture_last_cb, NULL, NULL);
  ck_assert_uint_eq(last_pts(), 4242u);
  ck_assert_uint_eq((unsigned)tm.relatches, 0u);

  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_rebase_keeps_the_offset_across_a_new_remux_on_the_same_timemap) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];
  timemap_t tm;
  uint64_t before;

  base_cfg(&cfg);
  cfg.pcr_mode = PCR_MODE_REBASE;
  base_input(&input);
  out_program_pids(0, &pids);
  timemap_init(&tm);

  r = remux_new(&cfg, &input, psi, &pids, 1);
  remux_set_timemap(r, &tm);
  write_pcr_packet(pkt, 0x0101, 0, 27000000ULL * 100);
  remux_feed(r, 0.0, pkt, capture_last_cb, NULL, NULL);
  before = last_pcr();
  remux_free(r);

  r = remux_new(&cfg, &input, psi, &pids, 1);
  remux_set_timemap(r, &tm);
  write_pcr_packet(pkt, 0x0101, 0, 27000000ULL * 7000);
  remux_feed(r, 2.0, pkt, capture_last_cb, NULL, NULL);
  ck_assert_uint_eq((unsigned)tm.relatches, 1u);
  ck_assert_uint_gt(last_pcr(), before);
  ck_assert_uint_lt(last_pcr() - before, 27000000ULL * 3);

  remux_free(r);
  psi_free(psi);
}
END_TEST

static uint64_t g_clock27;
static int g_clock_latched;

static int test_clock(void *ctx, uint64_t *pcr27) {
  (void)ctx;
  if (!g_clock_latched) return 0;
  *pcr27 = g_clock27;
  return 1;
}

static void test_latch(void *ctx, uint64_t pcr27) {
  (void)ctx;
  g_clock27 = pcr27;
  g_clock_latched = 1;
}

static void capture_both_cb(void *ctx, const unsigned char *pkt) {
  capture_cb(ctx, pkt);
  capture_last_cb(ctx, pkt);
}

static int es_seen(void) {
  int n = 0;
  for (int i = 0; i < g_count && i < MAX_SEEN; i++)
    if (g_pids[i] == 0x0100 || g_pids[i] == 0x0101) n++;
  return n;
}

static void write_cont_packet(unsigned char pkt[188], unsigned pid, unsigned char cc) {
  memset(pkt, 0xAA, 188);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((pid >> 8) & 0x1F);
  pkt[2] = (unsigned char)pid;
  pkt[3] = (unsigned char)(0x10 | (cc & 0x0F));
}

static remux_t *hold_remux(psi_t **psi_out, uint64_t pcr90, int latched, unsigned lead_ms) {
  config_t cfg;
  dipitvhead_input_t input;
  out_program_pids_t pids;
  remux_t *r;
  base_cfg(&cfg);
  cfg.pcr_mode = PCR_MODE_REGENERATE;
  base_input(&input);
  out_program_pids(0, &pids);
  *psi_out = build_discovery_psi();
  r = remux_new(&cfg, &input, *psi_out, &pids, 1);
  ck_assert_ptr_nonnull(r);
  ck_assert_int_eq(remux_set_hold(r, test_clock, test_latch, NULL, lead_ms), 0);
  g_clock27 = pcr90 * 300;
  g_clock_latched = latched;
  g_count = 0;
  return r;
}

START_TEST(hold_keeps_a_packet_back_until_its_timestamp_is_within_the_lead) {
  psi_t *psi;
  unsigned char pkt[188];
  const uint64_t pts = 9000000;
  remux_t *r = hold_remux(&psi, pts - 90 * 1000, 1, 700);

  write_pes_packet(pkt, 0x0101, 0, pts);
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  ck_assert_int_eq(es_seen(), 0);
  ck_assert_int_ge(remux_hold_due_ms(r), 299);
  ck_assert_int_le(remux_hold_due_ms(r), 302);
  remux_release(r, 0.1, capture_cb, NULL, 0, NULL);
  ck_assert_int_eq(es_seen(), 0);

  g_clock27 = (pts - 90 * 600) * 300;
  ck_assert_int_eq(remux_hold_due_ms(r), 0);
  remux_release(r, 0.2, capture_cb, NULL, 0, NULL);
  ck_assert_int_eq(es_seen(), 1);
  ck_assert_int_eq(remux_hold_due_ms(r), -1);
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(hold_keeps_continuation_packets_with_their_pes_in_order) {
  psi_t *psi;
  unsigned char pkt[188];
  const uint64_t pts = 9000000;
  remux_t *r = hold_remux(&psi, pts - 90 * 1000, 1, 700);

  write_pes_packet(pkt, 0x0101, 0, pts);
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  for (unsigned char cc = 1; cc <= 3; cc++) {
    write_cont_packet(pkt, 0x0101, cc);
    remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  }
  ck_assert_int_eq(es_seen(), 0);
  g_clock27 = (pts - 90 * 600) * 300;
  remux_release(r, 0.2, capture_cb, NULL, 0, NULL);
  ck_assert_int_eq(es_seen(), 4);
  {
    int prev = -1;
    for (int i = 0; i < g_count; i++) {
      if (g_pids[i] != 0x0100) continue;
      if (prev >= 0) ck_assert_int_eq(g_cc[i], (prev + 1) & 0x0F);
      prev = g_cc[i];
    }
  }
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(hold_queues_are_per_stream_so_early_video_does_not_delay_audio) {
  psi_t *psi;
  unsigned char pkt[188];
  const uint64_t now90 = 9000000;
  remux_t *r = hold_remux(&psi, now90, 1, 700);

  write_pes_packet(pkt, 0x0101, 0, now90 + 90 * 2000);
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  write_pes_packet(pkt, 0x0102, 0, now90 + 90 * 300);
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  ck_assert_int_eq(es_seen(), 0);
  remux_release(r, 0.1, capture_cb, NULL, 0, NULL);
  ck_assert_int_eq(es_seen(), 1);
  ck_assert_uint_eq(g_pids[g_count - 1], 0x0101u);
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(hold_keeps_packets_queued_until_the_clock_is_latched) {
  psi_t *psi;
  unsigned char pkt[188];
  remux_t *r = hold_remux(&psi, 0, 0, 700);

  write_pes_packet(pkt, 0x0101, 0, 9000000);
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  remux_release(r, 0.0, capture_cb, NULL, 0, NULL);
  ck_assert_int_eq(es_seen(), 0);
  ck_assert_int_eq(remux_hold_due_ms(r), -1);
  g_clock27 = 9000000ULL * 300;
  g_clock_latched = 1;
  remux_release(r, 0.0, capture_cb, NULL, 0, NULL);
  ck_assert_int_eq(es_seen(), 1);
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(hold_releases_late_packets_immediately) {
  psi_t *psi;
  unsigned char pkt[188];
  const uint64_t pts = 9000000;
  remux_t *r = hold_remux(&psi, pts + 90 * 500, 1, 700);

  write_pes_packet(pkt, 0x0101, 0, pts);
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  ck_assert_int_eq(es_seen(), 0);
  ck_assert_int_eq(remux_hold_due_ms(r), 0);
  remux_release(r, 0.0, capture_cb, NULL, 0, NULL);
  ck_assert_int_eq(es_seen(), 1);
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(hold_release_all_flushes_everything_regardless_of_timestamps) {
  psi_t *psi;
  unsigned char pkt[188];
  remux_t *r = hold_remux(&psi, 0, 1, 700);

  for (int i = 0; i < 5; i++) {
    write_pes_packet(pkt, 0x0101, (unsigned char)i, 9000000 + 90000u * 100u * (unsigned)i);
    remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  }
  ck_assert_int_eq(es_seen(), 0);
  remux_release(r, 0.0, capture_cb, NULL, 1, NULL);
  ck_assert_int_eq(es_seen(), 5);
  ck_assert_int_eq(remux_hold_due_ms(r), -1);
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(hold_wraps_the_33_bit_timestamp_difference) {
  psi_t *psi;
  unsigned char pkt[188];
  const uint64_t mod = (uint64_t)1 << 33;
  remux_t *r = hold_remux(&psi, mod - 9000, 1, 700);

  write_pes_packet(pkt, 0x0101, 0, 90000);
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  ck_assert_int_eq(es_seen(), 0);
  remux_release(r, 0.0, capture_cb, NULL, 0, NULL);
  ck_assert_int_eq(es_seen(), 0);
  ck_assert_int_ge(remux_hold_due_ms(r), 399);
  ck_assert_int_le(remux_hold_due_ms(r), 402);
  g_clock27 = 36000ULL * 300;
  remux_release(r, 0.0, capture_cb, NULL, 0, NULL);
  ck_assert_int_eq(es_seen(), 1);
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(hold_releases_the_oldest_packet_when_a_queue_is_full) {
  psi_t *psi;
  unsigned char pkt[188];
  ts_metrics_t tsm;
  remux_t *r = hold_remux(&psi, 0, 1, 700);
  memset(&tsm, 0, sizeof tsm);

  write_pes_packet(pkt, 0x0101, 0, 9000000 + 90000u * 3600u);
  remux_feed(r, 0.0, pkt, capture_cb, NULL, &tsm);
  for (int i = 0; i < 32768; i++) {
    write_cont_packet(pkt, 0x0101, (unsigned char)(i + 1));
    remux_feed(r, 0.0, pkt, capture_cb, NULL, &tsm);
  }
  ck_assert_uint_eq((unsigned)remux_hold_forced(r), 1u);
  ck_assert_uint_eq((unsigned)tsm.hold_forced_total, 1u);
  ck_assert_int_eq(es_seen(), 1);
  remux_free(r);
  psi_free(psi);
}
END_TEST

static remux_t *regen_remux(psi_t **psi_out, timemap_t *tm, int latched, uint64_t p90, unsigned lead_ms) {
  remux_t *r = hold_remux(psi_out, p90, latched, lead_ms);
  remux_set_timemap(r, tm);
  return r;
}

START_TEST(regen_first_stamp_latches_the_clock_and_keeps_timestamps) {
  psi_t *psi;
  unsigned char pkt[188];
  timemap_t tm;
  remux_t *r;
  timemap_init(&tm);
  r = regen_remux(&psi, &tm, 0, 0, 700);

  write_pes_packet(pkt, 0x0101, 0, 9000000);
  remux_feed(r, 0.0, pkt, capture_both_cb, NULL, NULL);
  ck_assert_int_eq(g_clock_latched, 1);
  ck_assert_uint_eq(g_clock27, (9000000ULL - 63000ULL) * 300);
  ck_assert_uint_eq(timemap_k90(&tm), 0u);
  remux_release(r, 0.0, capture_both_cb, NULL, 0, NULL);
  ck_assert_int_eq(es_seen(), 1);
  ck_assert_uint_eq(last_pts(), 9000000u);
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(regen_later_program_gets_an_offset_matching_the_running_clock) {
  psi_t *psi;
  unsigned char pkt[188];
  timemap_t tm;
  remux_t *r;
  timemap_init(&tm);
  r = regen_remux(&psi, &tm, 1, 1000000, 700);

  write_pes_packet(pkt, 0x0101, 0, 50000000);
  remux_feed(r, 0.0, pkt, capture_both_cb, NULL, NULL);
  remux_release(r, 0.0, capture_both_cb, NULL, 0, NULL);
  ck_assert_int_eq(es_seen(), 1);
  ck_assert_uint_eq(last_pts(), 1000000u + 63000u);
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(regen_raises_the_lead_to_the_plausible_natural_lead_of_the_source) {
  psi_t *psi;
  unsigned char pkt[188];
  timemap_t tm;
  remux_t *r;
  timemap_init(&tm);
  r = regen_remux(&psi, &tm, 0, 0, 700);

  write_pcr_packet(pkt, 0x0101, 0, 27000000ULL * 100);
  remux_feed(r, 0.0, pkt, capture_both_cb, NULL, NULL);
  write_pes_packet(pkt, 0x0101, 1, 9000000 + 81000);
  remux_feed(r, 0.01, pkt, capture_both_cb, NULL, NULL);
  ck_assert_uint_eq(g_clock27, (9000000ULL + 81000ULL - 80100ULL) * 300);
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(regen_ignores_an_implausible_source_pcr_for_the_lead) {
  psi_t *psi;
  unsigned char pkt[188];
  timemap_t tm;
  remux_t *r;
  timemap_init(&tm);
  r = regen_remux(&psi, &tm, 0, 0, 700);

  write_pcr_packet(pkt, 0x0101, 0, 27000000ULL * 100);
  remux_feed(r, 0.0, pkt, capture_both_cb, NULL, NULL);
  write_pes_packet(pkt, 0x0101, 1, 9000000 - 450000);
  remux_feed(r, 0.01, pkt, capture_both_cb, NULL, NULL);
  ck_assert_uint_eq(g_clock27, (9000000ULL - 450000ULL - 63000ULL) * 300);
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(regen_without_metrics_still_tracks_the_source_pcr) {
  psi_t *psi;
  unsigned char pkt[188];
  timemap_t tm;
  remux_t *r;
  ts_metrics_t tsm;
  timemap_init(&tm);
  memset(&tsm, 0, sizeof tsm);
  tsm.ts_checks_off = 1;
  r = regen_remux(&psi, &tm, 0, 0, 700);

  write_pcr_packet(pkt, 0x0101, 0, 27000000ULL * 100);
  remux_feed(r, 0.0, pkt, capture_both_cb, NULL, &tsm);
  write_pes_packet(pkt, 0x0101, 1, 9000000 + 81000);
  remux_feed(r, 0.01, pkt, capture_both_cb, NULL, &tsm);
  ck_assert_uint_eq(g_clock27, (9000000ULL + 81000ULL - 80100ULL) * 300);
  ck_assert_uint_eq((unsigned)tsm.pcr_discontinuities, 0u);
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(regen_timestamp_jump_relatches_so_the_lead_is_restored) {
  psi_t *psi;
  unsigned char pkt[188];
  timemap_t tm;
  remux_t *r;
  timemap_init(&tm);
  r = regen_remux(&psi, &tm, 0, 0, 700);

  write_pes_packet(pkt, 0x0101, 0, 9000000);
  remux_feed(r, 0.0, pkt, capture_both_cb, NULL, NULL);
  write_pes_packet(pkt, 0x0101, 1, 9000000 + 3600);
  remux_feed(r, 0.04, pkt, capture_both_cb, NULL, NULL);
  ck_assert_uint_eq((unsigned)tm.rg_relatches, 0u);
  remux_release(r, 0.04, capture_both_cb, NULL, 1, NULL);
  g_clock27 = (9000000ULL - 63000ULL + 3600ULL) * 300;
  write_pes_packet(pkt, 0x0101, 2, 90000ULL * 5000);
  remux_feed(r, 0.08, pkt, capture_both_cb, NULL, NULL);
  ck_assert_uint_eq((unsigned)tm.rg_relatches, 1u);
  remux_release(r, 0.08, capture_both_cb, NULL, 1, NULL);
  ck_assert_uint_eq(last_pts(), g_clock27 / 300 + 63000u);
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(regen_all_streams_of_a_program_share_the_offset) {
  psi_t *psi;
  unsigned char pkt[188];
  timemap_t tm;
  remux_t *r;
  uint64_t video_pts;
  timemap_init(&tm);
  r = regen_remux(&psi, &tm, 1, 2000000, 700);

  write_pes_packet(pkt, 0x0101, 0, 70000000);
  remux_feed(r, 0.0, pkt, capture_both_cb, NULL, NULL);
  write_pes_packet(pkt, 0x0102, 0, 70000000);
  remux_feed(r, 0.0, pkt, capture_both_cb, NULL, NULL);
  remux_release(r, 0.0, capture_both_cb, NULL, 1, NULL);
  ck_assert_int_eq(es_seen(), 2);
  video_pts = last_pts();
  ck_assert_uint_eq(video_pts, 2000000u + 63000u);
  remux_free(r);
  psi_free(psi);
}
END_TEST

static void write_splice_packet(unsigned char pkt[188], unsigned pid, unsigned char cc, uint64_t next_dts) {
  memset(pkt, 0xAA, 188);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((pid >> 8) & 0x1F);
  pkt[2] = (unsigned char)pid;
  pkt[3] = (unsigned char)(0x30 | (cc & 0x0F));
  pkt[4] = 1 + 6 + 1 + 1 + 1 + 5 + 0;
  pkt[5] = 0x80 | 0x08 | 0x04 | 0x01;
  for (int i = 0; i < 6; i++) pkt[6 + i] = (unsigned char)(0x10 + i);
  pkt[12] = 3;
  pkt[13] = 6;
  pkt[14] = 0x20;
  pkt[15] = (unsigned char)(0x90 | ((next_dts >> 29) & 0x0E) | 1);
  pkt[16] = (unsigned char)(next_dts >> 22);
  pkt[17] = (unsigned char)(((next_dts >> 14) & 0xFE) | 1);
  pkt[18] = (unsigned char)(next_dts >> 7);
  pkt[19] = (unsigned char)(((next_dts << 1) & 0xFE) | 1);
}

static uint64_t splice_next_dts(const unsigned char *p) {
  return ((uint64_t)(p[15] & 0x0E) << 29) | ((uint64_t)p[16] << 22) | ((uint64_t)(p[17] & 0xFE) << 14) | ((uint64_t)p[18] << 7) | ((uint64_t)p[19] >> 1);
}

START_TEST(remux_rebase_and_regenerate_clear_the_discontinuity_flag_and_shift_dts_next_au) {
  static const pcr_mode_t modes[] = {PCR_MODE_REBASE, PCR_MODE_REGENERATE};
  for (size_t m = 0; m < sizeof modes / sizeof modes[0]; m++) {
    psi_t *psi = build_discovery_psi();
    config_t cfg;
    dipitvhead_input_t input;
    remux_t *r;
    out_program_pids_t pids;
    unsigned char pkt[188];
    unsigned char orig[188];
    timemap_t tm;

    base_cfg(&cfg);
    cfg.pcr_mode = modes[m];
    base_input(&input);
    out_program_pids(0, &pids);
    r = remux_new(&cfg, &input, psi, &pids, 1);
    ck_assert_ptr_nonnull(r);
    timemap_init(&tm);
    tm.k90 = 90000;
    remux_set_timemap(r, &tm);

    memset(pkt, 0xFF, sizeof pkt);
    pkt[0] = 0x47;
    pkt[1] = 0x01;
    pkt[2] = 0x01;
    pkt[3] = 0x20;
    pkt[4] = 183;
    pkt[5] = 0x00;
    remux_feed(r, 0.0, pkt, capture_last_cb, NULL, NULL); /* consumes the post-new discontinuity flag */

    write_splice_packet(pkt, 0x0101, 1, 5000000);
    memcpy(orig, pkt, sizeof pkt);
    remux_feed(r, 0.0, pkt, capture_last_cb, NULL, NULL);
    ck_assert_uint_eq(g_last[5], 0x0Du);
    ck_assert_mem_eq(g_last + 6, orig + 6, 6);
    ck_assert_uint_eq(splice_next_dts(g_last), 5000000u + 90000u);
    ck_assert_mem_eq(g_last + 20, orig + 20, 188 - 20);
    remux_free(r);
    psi_free(psi);
  }
}
END_TEST

START_TEST(remux_preserve_leaves_the_discontinuity_flag_and_dts_next_au_alone) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];
  timemap_t tm;

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  ck_assert_ptr_nonnull(r);
  timemap_init(&tm);
  tm.k90 = 90000;
  remux_set_timemap(r, &tm);

  write_splice_packet(pkt, 0x0101, 1, 5000000);
  remux_feed(r, 0.0, pkt, capture_last_cb, NULL, NULL);
  ck_assert_uint_eq(g_last[5], 0x8Du);
  ck_assert_uint_eq(splice_next_dts(g_last), 5000000u);
  remux_free(r);
  psi_free(psi);
}
END_TEST

static unsigned scte_section(unsigned char *s, uint64_t adj, unsigned body_len) {
  unsigned total = 21 + body_len + 4;
  uint32_t crc;
  memset(s, 0x5A, total);
  s[0] = 0xFC;
  s[1] = (unsigned char)(0x30 | (((total - 3) >> 8) & 0x0F));
  s[2] = (unsigned char)(total - 3);
  s[3] = 0;
  s[4] = (unsigned char)((adj >> 32) & 1);
  s[5] = (unsigned char)(adj >> 24);
  s[6] = (unsigned char)(adj >> 16);
  s[7] = (unsigned char)(adj >> 8);
  s[8] = (unsigned char)adj;
  s[19] = (unsigned char)(body_len >> 8);
  s[20] = (unsigned char)body_len;
  crc = crc32_mpeg(s, total - 4);
  s[total - 4] = (unsigned char)(crc >> 24);
  s[total - 3] = (unsigned char)(crc >> 16);
  s[total - 2] = (unsigned char)(crc >> 8);
  s[total - 1] = (unsigned char)crc;
  return total;
}

static int g_pk_count;
static unsigned char g_pk[8][188];

static void capture_all_cb(void *ctx, const unsigned char *pkt) {
  (void)ctx;
  if (g_pk_count < 8) memcpy(g_pk[g_pk_count], pkt, 188);
  g_pk_count++;
}

static void scte_run(pcr_mode_t mode, uint64_t k90, unsigned body_len, uint64_t *adj_out, uint32_t *crc_out, int *emitted, ts_metrics_t *tsm) {
  unsigned char pkts[2][188];
  psi_t *psi = psi_new();
  config_t cfg;
  dipitvhead_input_t input;
  out_program_pids_t pids;
  remux_t *r;
  timemap_t tm;
  unsigned char sec[4096];
  unsigned char got[4096] = {0};
  unsigned total = scte_section(sec, 1000, body_len);
  unsigned pos = 0;
  unsigned char pkt[188];
  unsigned cc = 0;

  fixture_programme_packets(1, 0x0200, 0x86, 0x0200, pkts);
  psi_feed(psi, pkts[0]);
  psi_feed(psi, pkts[1]);
  base_cfg(&cfg);
  cfg.pcr_mode = mode;
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  ck_assert_ptr_nonnull(r);
  timemap_init(&tm);
  tm.k90 = k90;
  remux_set_timemap(r, &tm);
  g_pk_count = 0;
  while (pos < total) {
    unsigned st = pos == 0 ? 5 : 4;
    unsigned take = 188 - st;
    if (take > total - pos) take = total - pos;
    memset(pkt, 0xFF, 188);
    pkt[0] = 0x47;
    pkt[1] = (unsigned char)((pos == 0 ? 0x40 : 0x00) | 0x02);
    pkt[2] = 0x00;
    pkt[3] = (unsigned char)(0x10 | (cc++ & 0x0F));
    if (pos == 0) pkt[4] = 0;
    memcpy(pkt + st, sec + pos, take);
    pos += take;
    remux_feed(r, 0.0, pkt, capture_all_cb, NULL, tsm);
  }
  {
    unsigned off = 0;
    int n = 0;
    for (int i = 0; i < g_pk_count && i < 8; i++) {
      unsigned pid = (((unsigned)g_pk[i][1] & 0x1F) << 8) | g_pk[i][2];
      unsigned st = n == 0 ? 5 : 4;
      unsigned take;
      if (pid != pids.es_pid_base) continue;
      take = 188 - st;
      if (take > total - off) take = total - off;
      memcpy(got + off, g_pk[i] + st, take);
      off += take;
      n++;
    }
    *emitted = n;
    *adj_out = ((uint64_t)(got[4] & 1) << 32) | ((uint64_t)got[5] << 24) | ((uint64_t)got[6] << 16) | ((uint64_t)got[7] << 8) | got[8];
    *crc_out = crc32_mpeg(got, total);
  }
  remux_free(r);
  psi_free(psi);
}

START_TEST(remux_rebase_and_regenerate_patch_scte35_pts_adjustment_with_a_valid_crc) {
  static const pcr_mode_t modes[] = {PCR_MODE_REBASE, PCR_MODE_REGENERATE};
  for (size_t m = 0; m < sizeof modes / sizeof modes[0]; m++) {
    uint64_t adj;
    uint32_t crc;
    int emitted;
    ts_metrics_t tsm;
    memset(&tsm, 0, sizeof tsm);
    scte_run(modes[m], 90000, 400, &adj, &crc, &emitted, &tsm);
    ck_assert_int_eq(emitted, 3);
    ck_assert_uint_eq(adj, 91000u);
    ck_assert_uint_eq(crc, 0u);
    ck_assert_uint_eq((unsigned)tsm.scte35_adjusted_total, 1u);
  }
}
END_TEST

START_TEST(remux_preserve_leaves_scte35_sections_alone) {
  uint64_t adj;
  uint32_t crc;
  int emitted;
  ts_metrics_t tsm;
  memset(&tsm, 0, sizeof tsm);
  scte_run(PCR_MODE_PRESERVE, 90000, 400, &adj, &crc, &emitted, &tsm);
  ck_assert_int_eq(emitted, 3);
  ck_assert_uint_eq(adj, 1000u);
  ck_assert_uint_eq(crc, 0u);
  ck_assert_uint_eq((unsigned)tsm.scte35_adjusted_total, 0u);
}
END_TEST

START_TEST(remux_uses_the_video_pid_as_pcr_pid_when_the_source_has_none) {
  unsigned char pkts[2][188];
  psi_t *psi = psi_new();
  config_t cfg;
  dipitvhead_input_t input;
  out_program_pids_t pids;
  remux_t *r;

  fixture_programme_packets(1, 0x1FFF, 0x1B, 0x0300, pkts);
  psi_feed(psi, pkts[0]);
  psi_feed(psi, pkts[1]);
  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  ck_assert_ptr_nonnull(r);
  ck_assert_uint_eq(remux_pcr_pid_out(r), pids.video_pid);
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_rebase_counts_rewrites_retimed_skipped_and_relatches) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];
  timemap_t tm;
  ts_metrics_t tsm;

  base_cfg(&cfg);
  cfg.pcr_mode = PCR_MODE_REBASE;
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  ck_assert_ptr_nonnull(r);
  timemap_init(&tm);
  remux_set_timemap(r, &tm);
  memset(&tsm, 0, sizeof tsm);

  write_pcr_packet(pkt, 0x0101, 0, 27000000ULL * 100);
  remux_feed(r, 0.0, pkt, capture_last_cb, NULL, &tsm);
  write_pcr_packet(pkt, 0x0101, 1, 27000000ULL * 5000);
  remux_feed(r, 0.04, pkt, capture_last_cb, NULL, &tsm);
  write_pes_packet(pkt, 0x0101, 2, 9000000);
  remux_feed(r, 0.05, pkt, capture_last_cb, NULL, &tsm);
  write_pes_packet(pkt, 0x0101, 3, 9000000);
  pkt[3] |= 0x80;
  remux_feed(r, 0.06, pkt, capture_last_cb, NULL, &tsm);

  ck_assert_uint_eq((unsigned)tsm.pcr_rewritten_total, 2u);
  ck_assert_uint_eq((unsigned)tsm.retime_relatches_total, 1u);
  ck_assert_uint_eq((unsigned)tsm.pes_retimed_total, 1u);
  ck_assert_uint_eq((unsigned)tsm.pes_retime_skipped_total, 1u);
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(regen_counts_retimed_pes_relatches_and_the_release_lead_range) {
  psi_t *psi;
  unsigned char pkt[188];
  timemap_t tm;
  remux_t *r;
  ts_metrics_t tsm;
  timemap_init(&tm);
  memset(&tsm, 0, sizeof tsm);
  r = regen_remux(&psi, &tm, 0, 0, 700);

  write_pes_packet(pkt, 0x0101, 0, 9000000);
  remux_feed(r, 0.0, pkt, capture_both_cb, NULL, &tsm);
  write_pes_packet(pkt, 0x0101, 1, 9000000 + 3600);
  remux_feed(r, 0.04, pkt, capture_both_cb, NULL, &tsm);
  remux_release(r, 0.04, capture_both_cb, NULL, 0, &tsm);
  ck_assert_uint_eq((unsigned)tsm.pes_retimed_total, 2u);
  ck_assert_int_eq(tsm.release_lead_seen, 1);
  ck_assert_int_eq((int)tsm.release_lead_max_us, 700000);
  ck_assert_int_le((int)tsm.release_lead_min_us, (int)tsm.release_lead_max_us);
  write_pes_packet(pkt, 0x0101, 2, 90000ULL * 5000);
  remux_feed(r, 0.08, pkt, capture_both_cb, NULL, &tsm);
  ck_assert_uint_eq((unsigned)tsm.retime_relatches_total, 1u);
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(regen_reconnect_keeps_the_clock_and_relatches_to_the_new_source_timeline) {
  psi_t *psi;
  unsigned char pkt[188];
  timemap_t tm;
  remux_t *r;
  uint64_t clock_before;
  timemap_init(&tm);
  r = regen_remux(&psi, &tm, 0, 0, 700);

  write_pes_packet(pkt, 0x0101, 0, 9000000);
  remux_feed(r, 0.0, pkt, capture_both_cb, NULL, NULL);
  write_pes_packet(pkt, 0x0101, 1, 9000000 + 3600);
  remux_feed(r, 0.04, pkt, capture_both_cb, NULL, NULL);
  ck_assert_uint_eq(timemap_k90(&tm), 0u);
  remux_release(r, 0.04, capture_both_cb, NULL, 1, NULL);
  remux_free(r);
  psi_free(psi);

  g_clock27 += 90000ULL * 5 * 300;
  clock_before = g_clock27;
  r = regen_remux(&psi, &tm, 1, g_clock27 / 300, 700);
  g_clock27 = clock_before;
  write_pes_packet(pkt, 0x0101, 0, 90000ULL * 40000);
  remux_feed(r, 5.0, pkt, capture_both_cb, NULL, NULL);
  ck_assert_uint_eq((unsigned)tm.rg_relatches, 1u);
  remux_release(r, 5.0, capture_both_cb, NULL, 1, NULL);
  ck_assert_uint_eq(last_pts(), g_clock27 / 300 + 63000u);
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_drops_unrecognized_pid_and_does_not_resend_psi_immediately) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1);

  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;
  pkt[3] = 0x10;
  g_count = 0;
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL); /* primes last_pat etc */

  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((0x0500 >> 8) & 0x1F); /* not carried in the ES map */
  pkt[2] = (unsigned char)0x0500;
  pkt[3] = 0x10;
  g_count = 0;
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);

  ck_assert_int_eq(g_count, 0); /* PSI not due yet, pid not recognized: nothing emitted */

  remux_free(r);
  psi_free(psi);
}
END_TEST

/* PSI due-checks now run off caller-supplied now_s, not an internal clock read - exercise the
 * interval gating with a controlled clock instead of relying on real elapsed time */
START_TEST(remux_resends_pat_after_interval_elapses_by_explicit_clock) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  ck_assert_ptr_nonnull(r);

  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;
  pkt[3] = 0x10;

  g_count = 0;
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  ck_assert(saw_pid(0x0000)); /* PAT sent on first feed */

  g_count = 0;
  remux_feed(r, 0.05, pkt, capture_cb, NULL, NULL); /* 50ms later: PAT interval is 100ms */
  ck_assert(!saw_pid(0x0000));

  g_count = 0;
  remux_feed(r, 0.2, pkt, capture_cb, NULL, NULL); /* 200ms later: past the interval */
  ck_assert(saw_pid(0x0000));

  remux_free(r);
  psi_free(psi);
}
END_TEST

/* PMT is rebuilt unconditionally every INTERVAL_PAT_PMT_S regardless of content -
   pmt_updates_total must only count actual content changes, not every rebuild */
START_TEST(remux_pmt_updates_total_only_counts_real_content_changes) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];
  ts_metrics_t tsm;

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  ck_assert_ptr_nonnull(r);
  memset(&tsm, 0, sizeof tsm);

  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;
  pkt[3] = 0x10;

  remux_feed(r, 0.0, pkt, capture_cb, NULL, &tsm);
  ck_assert_uint_eq(tsm.pmt_updates_total, 0u); /* first build: nothing to compare against */

  remux_feed(r, 0.2, pkt, capture_cb, NULL, &tsm); /* rebuilt (past interval), same ES set */
  ck_assert_uint_eq(tsm.pmt_updates_total, 0u);

  remux_feed(r, 0.4, pkt, capture_cb, NULL, &tsm); /* rebuilt again, still identical */
  ck_assert_uint_eq(tsm.pmt_updates_total, 0u);

  remux_free(r);
  psi_free(psi);
}
END_TEST

static unsigned char g_ver_pat;
static unsigned char g_ver_pmt;

static void version_cb(void *ctx, const unsigned char *pkt) {
  unsigned pid = (((unsigned)pkt[1] & 0x1F) << 8) | pkt[2];
  size_t sec = (pkt[3] & 0x20) ? 5 + pkt[4] + 1 : 5;
  (void)ctx;
  if (!(pkt[1] & 0x40)) return;
  if (pid == OUT_PID_PAT) g_ver_pat = (pkt[sec + 5] >> 1) & 0x1F;
  if (pid == 0x1000) g_ver_pmt = (pkt[sec + 5] >> 1) & 0x1F;
}

/* source PMT for program 101 on pid 0x0100: first es_n of video 0x101, audio 0x102, audio 0x103 */
static void src_pmt_packet(unsigned char pkt[188], unsigned version, unsigned pcr_pid, int es_n) {
  unsigned char section[128];
  unsigned char body[64];
  size_t n = 0;
  size_t crc_at;
  size_t hdr;
  uint32_t crc;
  static const unsigned es_list[3][2] = {{0x1B, 0x0101}, {0x0F, 0x0102}, {0x0F, 0x0103}};

  body[n++] = 0;
  body[n++] = 101;
  body[n++] = (unsigned char)(0xC1 | (version << 1));
  body[n++] = 0;
  body[n++] = 0;
  body[n++] = (unsigned char)(0xE0 | (pcr_pid >> 8));
  body[n++] = (unsigned char)pcr_pid;
  body[n++] = 0xF0;
  body[n++] = 0x00;
  for (int i = 0; i < es_n; i++) {
    body[n++] = (unsigned char)es_list[i][0];
    body[n++] = (unsigned char)(0xE0 | (es_list[i][1] >> 8));
    body[n++] = (unsigned char)es_list[i][1];
    body[n++] = 0xF0;
    body[n++] = 0x00;
  }
  hdr = n + 4;
  section[0] = 0x02;
  section[1] = (unsigned char)(0xB0 | ((hdr >> 8) & 0x0F));
  section[2] = (unsigned char)hdr;
  memcpy(section + 3, body, n);
  crc_at = 3 + n;
  crc = crc32_mpeg(section, crc_at);
  be32_put(section + crc_at, crc);
  wrap_ts_packet(pkt, 0x0100, section, crc_at + 4);
}

static void feed_src_pat(remux_t *r, double now, remux_packet_cb cb) {
  unsigned char section[64];
  unsigned char pkt[188];
  size_t slen = psi_build_pat(0x1234, 0, 101, 0x0100, section, sizeof section);
  wrap_ts_packet(pkt, 0x0000, section, slen);
  remux_feed(r, now, pkt, cb, NULL, NULL);
}

static void feed_es_packet(remux_t *r, double now, unsigned pid, remux_packet_cb cb) {
  unsigned char pkt[188];
  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((pid >> 8) & 0x1F);
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  remux_feed(r, now, pkt, cb, NULL, NULL);
}

START_TEST(remux_psi_versions_start_at_zero_and_bump_only_on_content_change) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  psi_versions_t pv;
  unsigned char pkt[188];

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  memset(&pv, 0, sizeof pv);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  ck_assert_ptr_nonnull(r);
  remux_set_psi_versions(r, &pv);

  g_ver_pat = g_ver_pmt = 0xFF;
  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;
  pkt[3] = 0x10;
  remux_feed(r, 0.0, pkt, version_cb, NULL, NULL);
  ck_assert_uint_eq(g_ver_pat, 0u);
  ck_assert_uint_eq(g_ver_pmt, 0u);
  remux_feed(r, 0.2, pkt, version_cb, NULL, NULL);
  ck_assert_uint_eq(g_ver_pmt, 0u);

  feed_src_pat(r, 0.3, version_cb);
  src_pmt_packet(pkt, 1, 0x0101, 3);
  remux_feed(r, 0.3, pkt, version_cb, NULL, NULL);
  remux_feed(r, 0.5, pkt, version_cb, NULL, NULL);
  ck_assert_uint_eq(g_ver_pmt, 1u);
  ck_assert_uint_eq(g_ver_pat, 0u);
  remux_feed(r, 0.7, pkt, version_cb, NULL, NULL);
  ck_assert_uint_eq(g_ver_pmt, 1u);
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_psi_versions_survive_a_reconnect) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  psi_versions_t pv;
  unsigned char pkt[188];

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  memset(&pv, 0, sizeof pv);
  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;
  pkt[3] = 0x10;

  r = remux_new(&cfg, &input, psi, &pids, 1);
  remux_set_psi_versions(r, &pv);
  remux_feed(r, 0.0, pkt, version_cb, NULL, NULL);
  remux_free(r);

  r = remux_new(&cfg, &input, psi, &pids, 1);
  remux_set_psi_versions(r, &pv);
  g_ver_pmt = 0xFF;
  remux_feed(r, 0.0, pkt, version_cb, NULL, NULL);
  ck_assert_uint_eq(g_ver_pmt, 0u);
  remux_free(r);

  psi_free(psi);
  psi = build_discovery_psi_with(NULL, 0, (const unsigned char[]){0x52, 0x01, 0x01}, 3, NULL, 0);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  remux_set_psi_versions(r, &pv);
  remux_feed(r, 0.0, pkt, version_cb, NULL, NULL);
  ck_assert_uint_eq(g_ver_pmt, 1u);
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_psi_versions_wrap_at_32) {
  psi_versions_t pv;
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  memset(&pv, 0, sizeof pv);
  pv.primed[PSI_TABLE_PMT] = 1;
  pv.ver[PSI_TABLE_PMT] = 31;
  r = remux_new(&cfg, &input, psi, &pids, 1);
  remux_set_psi_versions(r, &pv);
  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;
  pkt[3] = 0x10;
  remux_feed(r, 0.0, pkt, version_cb, NULL, NULL);
  ck_assert_uint_eq(g_ver_pmt, 0u);
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_follows_a_track_added_to_the_source_pmt) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];
  const out_es_t *es;
  int n;
  unsigned audio_out;

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  ck_assert_ptr_nonnull(r);
  es = remux_es(r, &n);
  ck_assert_int_eq(n, 2);
  audio_out = es[1].out_pid;

  g_count = 0;
  feed_es_packet(r, 0.0, 0x0103, capture_cb);
  ck_assert(!saw_pid(pids.es_pid_base + 1));

  feed_src_pat(r, 0.0, capture_cb);
  src_pmt_packet(pkt, 0, 0x0101, 2);
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  es = remux_es(r, &n);
  ck_assert_int_eq(n, 2);

  src_pmt_packet(pkt, 1, 0x0101, 3);
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  es = remux_es(r, &n);
  ck_assert_int_eq(n, 3);
  ck_assert_uint_eq(es[1].out_pid, audio_out);
  ck_assert_uint_eq(es[2].in_pid, 0x0103u);
  ck_assert_uint_ne(es[2].out_pid, es[0].out_pid);
  ck_assert_uint_ne(es[2].out_pid, es[1].out_pid);
  ck_assert(!remux_reconnect_wanted(r));

  g_count = 0;
  feed_es_packet(r, 0.0, 0x0103, capture_cb);
  ck_assert(saw_pid(es[2].out_pid));
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_follows_a_track_removed_from_the_source_pmt) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];
  const out_es_t *es;
  int n;
  unsigned video_out;

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  es = remux_es(r, &n);
  video_out = es[0].out_pid;
  feed_src_pat(r, 0.0, capture_cb);
  src_pmt_packet(pkt, 0, 0x0101, 2);
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);

  src_pmt_packet(pkt, 1, 0x0101, 3);
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  es = remux_es(r, &n);
  ck_assert_int_eq(n, 3);

  src_pmt_packet(pkt, 2, 0x0101, 1);
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  es = remux_es(r, &n);
  ck_assert_int_eq(n, 1);
  ck_assert_uint_eq(es[0].out_pid, video_out);

  g_count = 0;
  feed_es_packet(r, 0.0, 0x0102, capture_cb);
  ck_assert_int_eq(g_count, 0);
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_asks_for_a_reconnect_when_the_pcr_stream_moves) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  feed_src_pat(r, 0.0, capture_cb);
  src_pmt_packet(pkt, 0, 0x0101, 2);
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  ck_assert(!remux_reconnect_wanted(r));
  src_pmt_packet(pkt, 1, 0x0102, 2);
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  ck_assert(remux_reconnect_wanted(r));
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_sdt_nit_ait_sent_when_configured) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  input.sdt_mode = TABLE_OVERRIDE;
  bufcpy(input.sdt_text, sizeof input.sdt_text, "Test Service");
  cfg.nit_mode = TABLE_OVERRIDE;
  bufcpy(cfg.nit_text, sizeof cfg.nit_text, "Test Network");
  input.hbbtv_url = "http://example.invalid/app.html";
  input.hbbtv_org_id = 1;
  input.hbbtv_app_id = 2;

  r = remux_new(&cfg, &input, psi, &pids, 1);
  ck_assert_ptr_nonnull(r);

  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = 0x1F; /* pid 0x1FFF: null packet, not carried in the ES map */
  pkt[2] = 0xFF;
  pkt[3] = 0x10;

  g_count = 0;
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);

  ck_assert(saw_pid(0x0000)); /* PAT */
  ck_assert(saw_pid(0x1000)); /* PMT */
  ck_assert(saw_pid(0x0011)); /* SDT */
  ck_assert(saw_pid(0x0010)); /* NIT */
  ck_assert(saw_pid(pids.ait_pid)); /* AIT */

  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_non_standalone_only_sends_pmt_and_ait_directly) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  input.sdt_mode = TABLE_OVERRIDE;
  bufcpy(input.sdt_text, sizeof input.sdt_text, "Test Service");
  cfg.nit_mode = TABLE_OVERRIDE;
  bufcpy(cfg.nit_text, sizeof cfg.nit_text, "Test Network");
  input.hbbtv_url = "http://example.invalid/app.html";
  input.hbbtv_org_id = 1;
  input.hbbtv_app_id = 2;

  r = remux_new(&cfg, &input, psi, &pids, 0); /* MPTS program, not standalone */
  ck_assert_ptr_nonnull(r);

  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = 0x1F;
  pkt[2] = 0xFF;
  pkt[3] = 0x10;

  g_count = 0;
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);

  ck_assert(!saw_pid(0x0000)); /* PAT: mpts_t's job, not sent directly */
  ck_assert(!saw_pid(0x0011)); /* SDT: pulled via remux_get_sdt_info(), not sent directly */
  ck_assert(!saw_pid(0x0010)); /* NIT: mpts_t's job, not sent directly */
  ck_assert(saw_pid(pids.pmt_pid));    /* PMT: still this program's own job */
  ck_assert(saw_pid(pids.ait_pid));    /* AIT: still this program's own job */

  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_get_sdt_info_returns_service_regardless_of_mode) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  psi_sdt_entry_t info;

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  input.sdt_mode = TABLE_OVERRIDE;
  bufcpy(input.sdt_text, sizeof input.sdt_text, "Test Service");

  r = remux_new(&cfg, &input, psi, &pids, 0);
  ck_assert_ptr_nonnull(r);

  ck_assert_int_eq(remux_get_sdt_info(r, &info), 0);
  ck_assert_uint_eq(info.service_id, input.sid);
  ck_assert_str_eq(info.service_name, "Test Service");

  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_get_sdt_info_fails_when_sdt_dropped) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  psi_sdt_entry_t info;

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  input.sdt_mode = TABLE_DROP;

  r = remux_new(&cfg, &input, psi, &pids, 0);
  ck_assert_ptr_nonnull(r);

  ck_assert_int_ne(remux_get_sdt_info(r, &info), 0);

  remux_free(r);
  psi_free(psi);
}
END_TEST

#define MAX_EIT_PKTS 8
static unsigned char g_eit_pkts[MAX_EIT_PKTS][188];
static int g_eit_count;

static void eit_capture_cb(void *ctx, const unsigned char *pkt) {
  (void)ctx;
  if (g_eit_count < MAX_EIT_PKTS)
    memcpy(g_eit_pkts[g_eit_count], pkt, 188);
  g_eit_count++;
}

/* service_id must match build_discovery_psi()'s 101 to pass remux's filter */
static size_t build_fake_eit_section(unsigned char *section_out, unsigned service_id, unsigned char section_number, size_t body_len) {
  size_t slen = 3 + body_len;

  section_out[0] = 0x4E; /* EIT actual_transport_stream, present/following */
  section_out[1] = (unsigned char)(0xF0 | ((body_len >> 8) & 0x0F));
  section_out[2] = (unsigned char)body_len;
  for (size_t i = 0; i < body_len; i++)
    section_out[3 + i] = (unsigned char)((0xA0 + i) & 0xFF);
  section_out[3] = (unsigned char)(service_id >> 8);
  section_out[4] = (unsigned char)service_id;
  section_out[6] = section_number;
  be32_put(section_out + slen - 4, crc32_mpeg(section_out, slen - 4));
  return slen;
}

/* expected output form: sid/tsid/onid rewritten to output values, CRC recomputed */
static void rewrite_eit_expected(unsigned char *sec, size_t slen, const config_t *cfg, const dipitvhead_input_t *input) {
  sec[3] = (unsigned char)(input->sid >> 8);
  sec[4] = (unsigned char)input->sid;
  sec[8] = (unsigned char)(cfg->tsid >> 8);
  sec[9] = (unsigned char)cfg->tsid;
  sec[10] = (unsigned char)(cfg->onid >> 8);
  sec[11] = (unsigned char)cfg->onid;
  be32_put(sec + slen - 4, crc32_mpeg(sec, slen - 4));
}

/* one TS packet, pusi=1, pointer_field=0 */
static void wrap_eit_packet(unsigned char pkt[188], const unsigned char *section, size_t slen) {
  memset(pkt, 0xFF, 188);
  pkt[0] = 0x47;
  pkt[1] = 0x40;
  pkt[2] = 0x12;
  pkt[3] = 0x10;
  pkt[4] = 0x00; /* pointer_field */
  memcpy(pkt + 5, section, slen);
}

static size_t build_fake_eit_packet(unsigned char pkt[188], unsigned char *section_out) {
  size_t slen = build_fake_eit_section(section_out, 101, 0x00, 20);
  wrap_eit_packet(pkt, section_out, slen);
  return slen;
}

START_TEST(remux_non_standalone_emits_reassembled_eit) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];
  unsigned char section[40];
  unsigned char cc = 0;
  size_t slen;
  size_t n;
  unsigned afc;
  size_t off;

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 0);
  ck_assert_ptr_nonnull(r);

  slen = build_fake_eit_packet(pkt, section);

  ck_assert_int_eq(remux_eit_pending(r), 0);
  g_count = 0;
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  ck_assert(!saw_pid(0x0012)); /* not forwarded directly */
  ck_assert_int_eq(remux_eit_pending(r), 1);

  g_eit_count = 0;
  n = remux_emit_eit(r, 0x0012, &cc, 1, eit_capture_cb, NULL);
  ck_assert_uint_eq(n, 1u);
  ck_assert_int_eq(g_eit_count, 1);
  ck_assert_int_eq(remux_eit_pending(r), 0);

  afc = (g_eit_pkts[0][3] >> 4) & 0x3;
  off = (afc == 3) ? 5 + (size_t)g_eit_pkts[0][4] : 4;
  ck_assert_uint_eq(g_eit_pkts[0][off], 0x00); /* pointer_field */
  rewrite_eit_expected(section, slen, &cfg, &input);
  ck_assert_mem_eq(g_eit_pkts[0] + off + 1, section, slen);

  ck_assert_uint_eq(remux_emit_eit(r, 0x0012, &cc, 1, eit_capture_cb, NULL), 0u);

  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_non_standalone_eit_spans_ticks_when_bounded) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[2][188];
  unsigned char section[300];
  unsigned char cc = 0;
  size_t slen;

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 0);
  ck_assert_ptr_nonnull(r);

  slen = build_fake_eit_section(section, 101, 0x00, 200); /* 2 packets */
  memset(pkt, 0xFF, sizeof pkt);
  pkt[0][0] = 0x47;
  pkt[0][1] = 0x40;
  pkt[0][2] = 0x12;
  pkt[0][3] = 0x10;
  pkt[0][4] = 0x00;
  memcpy(pkt[0] + 5, section, 183);
  pkt[1][0] = 0x47;
  pkt[1][1] = 0x00;
  pkt[1][2] = 0x12;
  pkt[1][3] = 0x11;
  memcpy(pkt[1] + 4, section + 183, slen - 183);

  for (size_t i = 0; i < 2; i++)
    remux_feed(r, 0.0, pkt[i], capture_cb, NULL, NULL);
  ck_assert_int_eq(remux_eit_pending(r), 1);

  g_eit_count = 0;
  ck_assert_uint_eq(remux_emit_eit(r, 0x0012, &cc, 1, eit_capture_cb, NULL), 1u);
  ck_assert_int_eq(remux_eit_pending(r), 1); /* not fully sent yet */

  ck_assert_uint_eq(remux_emit_eit(r, 0x0012, &cc, 1, eit_capture_cb, NULL), 1u);
  ck_assert_int_eq(remux_eit_pending(r), 0);
  ck_assert_int_eq(g_eit_count, 2);

  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_non_standalone_eit_drops_other_service_ids) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];
  unsigned char section[40];

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 0);
  ck_assert_ptr_nonnull(r);

  build_fake_eit_section(section, 999, 0x00, 20); /* wrong service_id */
  wrap_eit_packet(pkt, section, 23);
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  ck_assert_int_eq(remux_eit_pending(r), 0);

  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_non_standalone_eit_queue_full_counts_drops) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];
  unsigned char section[40];
  ts_metrics_t tsm;

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 0);
  ck_assert_ptr_nonnull(r);
  memset(&tsm, 0, sizeof tsm);

  /* EIT_QUEUE_CAP is 16: 16 distinct section_numbers fill it, the 17th must be dropped */
  for (int i = 0; i < 17; i++) {
    build_fake_eit_section(section, 101, (unsigned char)i, 20);
    wrap_eit_packet(pkt, section, 23);
    remux_feed(r, 0.0, pkt, capture_cb, NULL, &tsm);
  }

  ck_assert_uint_eq(tsm.eit_queue_drops_total, 1u);

  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_non_standalone_eit_queues_distinct_sections) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];
  unsigned char section_a[40];
  unsigned char section_b[40];
  unsigned char cc = 0;
  size_t slen_a;
  size_t slen_b;
  unsigned afc;
  size_t off;

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 0);
  ck_assert_ptr_nonnull(r);

  slen_a = build_fake_eit_section(section_a, 101, 0x00, 20);
  wrap_eit_packet(pkt, section_a, slen_a);
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);

  slen_b = build_fake_eit_section(section_b, 101, 0x01, 20); /* distinct section_number */
  wrap_eit_packet(pkt, section_b, slen_b);
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);

  ck_assert_int_eq(remux_eit_pending(r), 1); /* both queued, no clobber */

  g_eit_count = 0;
  ck_assert_uint_eq(remux_emit_eit(r, 0x0012, &cc, 1, eit_capture_cb, NULL), 1u);
  afc = (g_eit_pkts[0][3] >> 4) & 0x3;
  off = (afc == 3) ? 5 + (size_t)g_eit_pkts[0][4] : 4;
  rewrite_eit_expected(section_a, slen_a, &cfg, &input);
  ck_assert_mem_eq(g_eit_pkts[0] + off + 1, section_a, slen_a);
  ck_assert_int_eq(remux_eit_pending(r), 1); /* section_b still queued */

  g_eit_count = 0;
  ck_assert_uint_eq(remux_emit_eit(r, 0x0012, &cc, 1, eit_capture_cb, NULL), 1u);
  off = ((g_eit_pkts[0][3] >> 4) & 0x3) == 3 ? 5 + (size_t)g_eit_pkts[0][4] : 4;
  rewrite_eit_expected(section_b, slen_b, &cfg, &input);
  ck_assert_mem_eq(g_eit_pkts[0] + off + 1, section_b, slen_b);
  ck_assert_int_eq(remux_eit_pending(r), 0);

  remux_free(r);
  psi_free(psi);
}
END_TEST

typedef struct {
  unsigned sys_id;
  unsigned pid;
} ca_desc_t;

typedef struct {
  const char *name;
  ca_desc_t program;
  ca_desc_t video;
  ca_desc_t cat;
  unsigned strip_mask;
  int own_cas;
  ca_desc_t expect_ecm;
  ca_desc_t expect_emm;
  unsigned expect_ecm_out;
  unsigned expect_emm_out;
} ca_source_case_t;

static const ca_source_case_t ca_source_cases[] = {
    {"no source ca", {0, 0}, {0, 0}, {0, 0}, 0, 0, {0, 0}, {0, 0}, 0, 0},
    {"program level ecm", {0x4A75, 0x0200}, {0, 0}, {0, 0}, 0, 0, {0x4A75, 0x0200}, {0, 0}, 0x0102, 0},
    {"es level ecm", {0, 0}, {0x0963, 0x0201}, {0, 0}, 0, 0, {0x0963, 0x0201}, {0, 0}, 0x0102, 0},
    {"program level wins over es level", {0x4A75, 0x0200}, {0x0963, 0x0201}, {0, 0}, 0, 0, {0x4A75, 0x0200}, {0, 0}, 0x0102, 0},
    {"emm only", {0, 0}, {0, 0}, {0x4A75, 0x0300}, 0, 0, {0, 0}, {0x4A75, 0x0300}, 0, 0x0102},
    {"ecm and emm", {0x4A75, 0x0200}, {0, 0}, {0x4A75, 0x0300}, 0, 0, {0x4A75, 0x0200}, {0x4A75, 0x0300}, 0x0102, 0x0103},
    {"strip ecm removes both", {0x4A75, 0x0200}, {0, 0}, {0x4A75, 0x0300}, TVSTRIP_ECM, 0, {0, 0}, {0, 0}, 0, 0},
    {"strip data keeps both", {0x4A75, 0x0200}, {0, 0}, {0x4A75, 0x0300}, TVSTRIP_DATA, 0, {0x4A75, 0x0200}, {0x4A75, 0x0300}, 0x0102, 0x0103},
    {"own cas suppresses passthrough", {0x4A75, 0x0200}, {0, 0}, {0x4A75, 0x0300}, 0, 1, {0, 0}, {0, 0}, 0, 0},
};

static size_t build_ca_descriptor_bytes(const ca_desc_t *d, unsigned pid, unsigned char *out) {
  out[0] = 0x09;
  out[1] = 4;
  out[2] = (unsigned char)(d->sys_id >> 8);
  out[3] = (unsigned char)d->sys_id;
  out[4] = (unsigned char)(0xE0 | ((pid >> 8) & 0x1F));
  out[5] = (unsigned char)pid;
  return 6;
}

START_TEST(remux_source_ca_passthrough_descriptors_follow_source_and_strip_settings) {
  const ca_source_case_t *c = &ca_source_cases[_i];
  unsigned char prog_info[8];
  unsigned char video_info[8];
  unsigned char cat_desc[8];
  unsigned char want[8];
  unsigned char got[16];
  size_t prog_len = c->program.pid ? build_ca_descriptor_bytes(&c->program, c->program.pid, prog_info) : 0;
  size_t video_len = c->video.pid ? build_ca_descriptor_bytes(&c->video, c->video.pid, video_info) : 0;
  size_t cat_len = c->cat.pid ? build_ca_descriptor_bytes(&c->cat, c->cat.pid, cat_desc) : 0;
  psi_t *psi = build_discovery_psi_with(prog_info, prog_len, video_info, video_len, cat_desc, cat_len);
  config_t cfg;
  dipitvhead_input_t input;
  out_program_pids_t pids;
  remux_t *r;
  const out_es_t *ecm;
  const out_es_t *emm;
  int es_count;

  base_cfg(&cfg);
  base_input(&input);
  input.strip_mask = c->strip_mask;
  if (c->own_cas) cfg.cas_algo = CAS_ALGO_CSA2;
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  ck_assert_ptr_nonnull(r);
  remux_es(r, &es_count);
  ecm = find_ca_passthrough(r, CA_PASS_ECM);
  emm = find_ca_passthrough(r, CA_PASS_EMM);

  if (c->expect_ecm.pid) {
    size_t n = build_ca_descriptor_bytes(&c->expect_ecm, c->expect_ecm_out, want);

    ck_assert_msg(ecm != NULL, "%s: no ecm entry", c->name);
    ck_assert_msg(ecm->in_pid == c->expect_ecm.pid && ecm->out_pid == c->expect_ecm_out && ecm->ca_system_id == c->expect_ecm.sys_id, "%s: ecm entry wrong", c->name);
    ck_assert_msg(remux_source_ca_descriptor(r, got, sizeof got) == n, "%s: ecm descriptor length", c->name);
    ck_assert_msg(memcmp(got, want, n) == 0, "%s: ecm descriptor bytes", c->name);
    for (size_t cap = 0; cap < n; cap++) ck_assert_msg(remux_source_ca_descriptor(r, got, cap) == 0, "%s: ecm descriptor into %zu bytes", c->name, cap);
  } else {
    ck_assert_msg(ecm == NULL, "%s: unexpected ecm entry", c->name);
    ck_assert_msg(remux_source_ca_descriptor(r, got, sizeof got) == 0, "%s: ecm descriptor without ecm", c->name);
  }
  if (c->expect_emm.pid) {
    size_t n = build_ca_descriptor_bytes(&c->expect_emm, c->expect_emm_out, want);

    ck_assert_msg(emm != NULL, "%s: no emm entry", c->name);
    ck_assert_msg(emm->in_pid == c->expect_emm.pid && emm->out_pid == c->expect_emm_out && emm->ca_system_id == c->expect_emm.sys_id, "%s: emm entry wrong", c->name);
    ck_assert_msg(remux_source_emm_descriptor(r, got, sizeof got) == n, "%s: emm descriptor length", c->name);
    ck_assert_msg(memcmp(got, want, n) == 0, "%s: emm descriptor bytes", c->name);
    for (size_t cap = 0; cap < n; cap++) ck_assert_msg(remux_source_emm_descriptor(r, got, cap) == 0, "%s: emm descriptor into %zu bytes", c->name, cap);
  } else {
    ck_assert_msg(emm == NULL, "%s: unexpected emm entry", c->name);
    ck_assert_msg(remux_source_emm_descriptor(r, got, sizeof got) == 0, "%s: emm descriptor without emm", c->name);
  }
  ck_assert_msg(es_count == 2 + (ecm != NULL) + (emm != NULL), "%s: %d es entries", c->name, es_count);

  remux_free(r);
  psi_free(psi);
}
END_TEST

static unsigned char g_eit_only[MAX_EIT_PKTS][188];
static int g_eit_only_count;

static void eit_only_capture_cb(void *ctx, const unsigned char *pkt) {
  (void)ctx;
  if ((((unsigned)pkt[1] & 0x1F) << 8 | pkt[2]) != 0x0012) return;
  if (g_eit_only_count < MAX_EIT_PKTS) memcpy(g_eit_only[g_eit_only_count], pkt, 188);
  g_eit_only_count++;
}

static size_t reassemble_emitted_section(const unsigned char (*pkts)[188], int count, unsigned char *out, size_t cap) {
  size_t have = 0;

  for (int i = 0; i < count; i++) {
    unsigned afc = (pkts[i][3] >> 4) & 0x3;
    size_t off = (afc == 3) ? 5 + (size_t)pkts[i][4] : 4;

    if (pkts[i][1] & 0x40) off += 1 + pkts[i][off];
    for (; off < 188 && have < cap; off++) out[have++] = pkts[i][off];
  }
  return have;
}

static size_t split_section_into_packets(const unsigned char *section, size_t slen, unsigned char (*pkts)[188]) {
  size_t used = 0;
  size_t n = 0;

  while (used < slen) {
    size_t room = n == 0 ? 183 : 184;
    size_t take = slen - used < room ? slen - used : room;
    unsigned char *pkt = pkts[n];

    memset(pkt, 0xFF, 188);
    pkt[0] = 0x47;
    pkt[1] = (unsigned char)((n == 0 ? 0x40 : 0x00) | 0x00);
    pkt[2] = 0x12;
    pkt[3] = (unsigned char)(0x10 | (n & 0x0F));
    if (n == 0) pkt[4] = 0x00;
    memcpy(pkt + (n == 0 ? 5 : 4), section + used, take);
    used += take;
    n++;
  }
  return n;
}

START_TEST(remux_standalone_eit_rewritten_and_filtered) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];
  unsigned char other[188];
  unsigned char idle[188];
  unsigned char section[40];
  unsigned char osec[40];
  unsigned afc;
  size_t slen;
  size_t oslen;
  size_t off;

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  input.strip_eit = 0;
  r = remux_new(&cfg, &input, psi, &pids, 1);

  slen = build_fake_eit_packet(pkt, section);
  oslen = build_fake_eit_section(osec, 999, 0x00, 20);
  wrap_eit_packet(other, osec, oslen);

  memset(idle, 0xFF, sizeof idle);
  idle[0] = 0x47;
  idle[1] = 0x1F;
  idle[2] = 0xFF;
  idle[3] = 0x10;
  g_eit_only_count = 0;
  remux_feed(r, 0.0, other, eit_only_capture_cb, NULL, NULL);
  remux_feed(r, 0.0, other, eit_only_capture_cb, NULL, NULL);
  ck_assert_int_eq(g_eit_only_count, 0);

  remux_feed(r, 0.0, pkt, eit_only_capture_cb, NULL, NULL);
  remux_feed(r, 0.0, other, eit_only_capture_cb, NULL, NULL);
  ck_assert_int_eq(g_eit_only_count, 1);
  afc = (g_eit_only[0][3] >> 4) & 0x3;
  off = (afc == 3) ? 5 + (size_t)g_eit_only[0][4] : 4;
  rewrite_eit_expected(section, slen, &cfg, &input);
  ck_assert_mem_eq(g_eit_only[0] + off + 1, section, slen);
  remux_free(r);

  input.strip_eit = 1;
  r = remux_new(&cfg, &input, psi, &pids, 1);
  g_eit_only_count = 0;
  remux_feed(r, 0.0, pkt, eit_only_capture_cb, NULL, NULL);
  remux_feed(r, 0.0, other, eit_only_capture_cb, NULL, NULL);
  ck_assert_int_eq(g_eit_only_count, 0);

  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(remux_eit_section_content_survives_every_path) {
  static const size_t body_lens[] = {20, 160, 181, 182, 200, 360, 500};
  size_t body_len = body_lens[_i];
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  out_program_pids_t pids;
  unsigned char section[600];
  unsigned char in_pkts[MAX_EIT_PKTS][188];
  unsigned char got[1024];
  size_t slen = build_fake_eit_section(section, 101, 0x00, body_len);
  size_t n_pkts = split_section_into_packets(section, slen, in_pkts);
  remux_t *r;
  unsigned char cc = 0;
  unsigned char idle[188];
  size_t have;

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  memset(idle, 0xFF, sizeof idle);
  idle[0] = 0x47;
  idle[1] = 0x1F;
  idle[2] = 0xFF;
  idle[3] = 0x10;

  r = remux_new(&cfg, &input, psi, &pids, 1);
  ck_assert_ptr_nonnull(r);
  g_eit_only_count = 0;
  for (size_t i = 0; i < n_pkts; i++) remux_feed(r, 0.0, in_pkts[i], eit_only_capture_cb, NULL, NULL);
  for (size_t i = 0; i < n_pkts + 1; i++) remux_feed(r, 0.0, idle, eit_only_capture_cb, NULL, NULL);
  ck_assert_int_eq(g_eit_only_count, (int)n_pkts);
  have = reassemble_emitted_section(g_eit_only, g_eit_only_count, got, sizeof got);
  ck_assert_uint_ge(have, slen);
  rewrite_eit_expected(section, slen, &cfg, &input);
  ck_assert_mem_eq(got, section, slen);
  remux_free(r);

  r = remux_new(&cfg, &input, psi, &pids, 0);
  ck_assert_ptr_nonnull(r);
  for (size_t i = 0; i < n_pkts; i++) remux_feed(r, 0.0, in_pkts[i], capture_cb, NULL, NULL);
  ck_assert_int_eq(remux_eit_pending(r), 1);
  g_eit_only_count = 0;
  while (remux_eit_pending(r)) ck_assert_uint_eq(remux_emit_eit(r, 0x0012, &cc, 1, eit_only_capture_cb, NULL), 1u);
  ck_assert_int_eq(g_eit_only_count, (int)n_pkts);
  have = reassemble_emitted_section(g_eit_only, g_eit_only_count, got, sizeof got);
  ck_assert_uint_ge(have, slen);
  rewrite_eit_expected(section, slen, &cfg, &input);
  ck_assert_mem_eq(got, section, slen);
  remux_free(r);

  psi_free(psi);
}
END_TEST

static Suite *remux_suite(void) {
  Suite *s = suite_create("remux");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, remux_forwards_mapped_es_and_sends_pat_pmt_on_first_feed);
  tcase_add_test(tc, remux_es_exposes_output_pid_mapping);
  tcase_add_test(tc, remux_keeps_source_cc_including_duplicates_and_gaps);
  tcase_add_test(tc, remux_flags_discontinuity_on_first_adaptation_field_packet_after_new);
  tcase_add_test(tc, remux_feed_counts_ts_packets_and_sync_errors);
  tcase_add_test(tc, remux_feed_detects_continuity_gap_and_signaled_discontinuity);
  tcase_add_test(tc, remux_feed_detects_pcr_discontinuity_on_source_pcr_pid);
  tcase_add_test(tc, remux_rebase_shifts_pcr_and_timestamps_by_one_offset_after_a_source_jump);
  tcase_add_test(tc, remux_rebase_applies_the_same_offset_to_every_elementary_stream);
  tcase_add_test(tc, remux_without_rebase_mode_leaves_timestamps_alone);
  tcase_add_test(tc, remux_rebase_keeps_the_offset_across_a_new_remux_on_the_same_timemap);
  tcase_add_test(tc, hold_keeps_a_packet_back_until_its_timestamp_is_within_the_lead);
  tcase_add_test(tc, hold_keeps_continuation_packets_with_their_pes_in_order);
  tcase_add_test(tc, hold_queues_are_per_stream_so_early_video_does_not_delay_audio);
  tcase_add_test(tc, hold_keeps_packets_queued_until_the_clock_is_latched);
  tcase_add_test(tc, hold_releases_late_packets_immediately);
  tcase_add_test(tc, hold_release_all_flushes_everything_regardless_of_timestamps);
  tcase_add_test(tc, hold_wraps_the_33_bit_timestamp_difference);
  tcase_add_test(tc, hold_releases_the_oldest_packet_when_a_queue_is_full);
  tcase_add_test(tc, regen_first_stamp_latches_the_clock_and_keeps_timestamps);
  tcase_add_test(tc, regen_later_program_gets_an_offset_matching_the_running_clock);
  tcase_add_test(tc, regen_raises_the_lead_to_the_plausible_natural_lead_of_the_source);
  tcase_add_test(tc, regen_ignores_an_implausible_source_pcr_for_the_lead);
  tcase_add_test(tc, regen_without_metrics_still_tracks_the_source_pcr);
  tcase_add_test(tc, regen_timestamp_jump_relatches_so_the_lead_is_restored);
  tcase_add_test(tc, regen_all_streams_of_a_program_share_the_offset);
  tcase_add_test(tc, remux_rebase_and_regenerate_clear_the_discontinuity_flag_and_shift_dts_next_au);
  tcase_add_test(tc, remux_preserve_leaves_the_discontinuity_flag_and_dts_next_au_alone);
  tcase_add_test(tc, remux_rebase_and_regenerate_patch_scte35_pts_adjustment_with_a_valid_crc);
  tcase_add_test(tc, remux_preserve_leaves_scte35_sections_alone);
  tcase_add_test(tc, remux_uses_the_video_pid_as_pcr_pid_when_the_source_has_none);
  tcase_add_test(tc, remux_rebase_counts_rewrites_retimed_skipped_and_relatches);
  tcase_add_test(tc, regen_counts_retimed_pes_relatches_and_the_release_lead_range);
  tcase_add_test(tc, regen_reconnect_keeps_the_clock_and_relatches_to_the_new_source_timeline);
  tcase_add_test(tc, remux_drops_unrecognized_pid_and_does_not_resend_psi_immediately);
  tcase_add_test(tc, remux_resends_pat_after_interval_elapses_by_explicit_clock);
  tcase_add_test(tc, remux_pmt_updates_total_only_counts_real_content_changes);
  tcase_add_test(tc, remux_psi_versions_start_at_zero_and_bump_only_on_content_change);
  tcase_add_test(tc, remux_psi_versions_survive_a_reconnect);
  tcase_add_test(tc, remux_psi_versions_wrap_at_32);
  tcase_add_test(tc, remux_follows_a_track_added_to_the_source_pmt);
  tcase_add_test(tc, remux_follows_a_track_removed_from_the_source_pmt);
  tcase_add_test(tc, remux_asks_for_a_reconnect_when_the_pcr_stream_moves);
  tcase_add_test(tc, remux_standalone_eit_rewritten_and_filtered);
  tcase_add_test(tc, remux_sdt_nit_ait_sent_when_configured);
  tcase_add_test(tc, remux_non_standalone_only_sends_pmt_and_ait_directly);
  tcase_add_test(tc, remux_get_sdt_info_returns_service_regardless_of_mode);
  tcase_add_test(tc, remux_get_sdt_info_fails_when_sdt_dropped);
  tcase_add_test(tc, remux_non_standalone_emits_reassembled_eit);
  tcase_add_test(tc, remux_non_standalone_eit_spans_ticks_when_bounded);
  tcase_add_test(tc, remux_non_standalone_eit_drops_other_service_ids);
  tcase_add_test(tc, remux_non_standalone_eit_queues_distinct_sections);
  tcase_add_loop_test(tc, remux_source_ca_passthrough_descriptors_follow_source_and_strip_settings, 0, (int)(sizeof ca_source_cases / sizeof ca_source_cases[0]));
  tcase_add_loop_test(tc, remux_eit_section_content_survives_every_path, 0, 7);
  tcase_add_test(tc, remux_non_standalone_eit_queue_full_counts_drops);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(remux_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
