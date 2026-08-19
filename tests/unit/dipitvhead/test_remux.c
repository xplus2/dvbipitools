/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dipitvhead/mux/remux.h"
#include "lib/demux/crc32.h"
#include "lib/mux/psi_build.h"

#define MAX_SEEN 64

static unsigned g_pids[MAX_SEEN];
static unsigned char g_cc[MAX_SEEN];
static int g_count;

static void capture_cb(void *ctx, const unsigned char *pkt) {
  (void)ctx;
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
 * PAT program_number 101 -> PMT pid 0x0100 */
static psi_t *build_discovery_psi(void) {
  unsigned char section[256], pkt[188];
  size_t slen;
  psi_t *psi = psi_new();

  slen = psi_build_pat(0x1234, 0, 101, 0x0100, section, sizeof section);
  wrap_ts_packet(pkt, 0x0000, section, slen);
  psi_feed(psi, pkt);

  {
    /* hand-build a 2-ES PMT: video H264 @0x101, audio AAC @0x102 lang "eng" */
    unsigned char body[64];
    size_t n = 0, hdr, crc_at;
    uint32_t crc;

    body[n++] = (unsigned char)(101 >> 8);
    body[n++] = (unsigned char)101;
    body[n++] = 0xC1;
    body[n++] = 0x00;
    body[n++] = 0x00;
    body[n++] = 0xE0 | ((0x0101 >> 8) & 0x1F); /* PCR pid = video */
    body[n++] = 0x01;
    body[n++] = 0xF0; /* program_info_length = 0 */
    body[n++] = 0x00;
    body[n++] = 0x1B; /* H264 */
    body[n++] = 0xE0 | ((0x0101 >> 8) & 0x1F);
    body[n++] = 0x01;
    body[n++] = 0xF0;
    body[n++] = 0x00;
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

  return psi;
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
  ck_assert_uint_eq(g_cc[video_idx], 1u); /* cc starts at 0, bumped once */

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
  int count, saw_video = 0, saw_audio = 0;

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

/* ISO 13818-1 2.4.3.3: cc must not advance on adaptation_field_control '10'
   (adaptation field only, no payload) - video's PCR pid uses these for pacing */
START_TEST(remux_does_not_advance_cc_on_payloadless_adaptation_field_packet) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];
  int i, video_idx;

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  r = remux_new(&cfg, &input, psi, &pids, 1);
  ck_assert_ptr_nonnull(r);

  memset(pkt, 0xAB, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((0x0101 >> 8) & 0x1F);
  pkt[2] = (unsigned char)0x0101;
  pkt[3] = 0x10; /* AFC=01, payload only */

  g_count = 0;
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  video_idx = -1;
  for (i = 0; i < g_count && i < MAX_SEEN; i++)
    if (g_pids[i] == 0x0100)
      video_idx = i;
  ck_assert_int_ge(video_idx, 0);
  ck_assert_uint_eq(g_cc[video_idx], 1u);

  pkt[3] = 0x20; /* AFC=10, adaptation field only, no payload */
  pkt[4] = 183;
  g_count = 0;
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  video_idx = -1;
  for (i = 0; i < g_count && i < MAX_SEEN; i++)
    if (g_pids[i] == 0x0100)
      video_idx = i;
  ck_assert_int_ge(video_idx, 0);
  ck_assert_uint_eq(g_cc[video_idx], 1u); /* unchanged: no payload to lose */

  pkt[3] = 0x10; /* AFC=01 again */
  g_count = 0;
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  video_idx = -1;
  for (i = 0; i < g_count && i < MAX_SEEN; i++)
    if (g_pids[i] == 0x0100)
      video_idx = i;
  ck_assert_int_ge(video_idx, 0);
  ck_assert_uint_eq(g_cc[video_idx], 2u); /* advanced once from the last payload packet, not skipped */

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

START_TEST(remux_eit_passthrough_respects_strip_eit) {
  psi_t *psi = build_discovery_psi();
  config_t cfg;
  dipitvhead_input_t input;
  remux_t *r;
  out_program_pids_t pids;
  unsigned char pkt[188];

  base_cfg(&cfg);
  base_input(&input);
  out_program_pids(0, &pids);
  input.strip_eit = 0;
  r = remux_new(&cfg, &input, psi, &pids, 1);

  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = 0x00;
  pkt[2] = 0x12; /* EIT */
  pkt[3] = 0x10;

  g_count = 0;
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  ck_assert(saw_pid(0x0012));

  remux_free(r);

  input.strip_eit = 1;
  r = remux_new(&cfg, &input, psi, &pids, 1);
  g_count = 0;
  remux_feed(r, 0.0, pkt, capture_cb, NULL, NULL);
  ck_assert(!saw_pid(0x0012));

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
  snprintf(input.sdt_text, sizeof input.sdt_text, "Test Service");
  cfg.nit_mode = TABLE_OVERRIDE;
  snprintf(cfg.nit_text, sizeof cfg.nit_text, "Test Network");
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
  snprintf(input.sdt_text, sizeof input.sdt_text, "Test Service");
  cfg.nit_mode = TABLE_OVERRIDE;
  snprintf(cfg.nit_text, sizeof cfg.nit_text, "Test Network");
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
  snprintf(input.sdt_text, sizeof input.sdt_text, "Test Service");

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
  return slen;
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
  unsigned char pkt[188], section[40];
  unsigned char cc = 0;
  size_t slen, n;
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
  unsigned char pkt[2][188], section[300];
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
  pkt[1][3] = 0x10;
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
  unsigned char pkt[188], section[40];

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
  unsigned char pkt[188], section[40];
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
  unsigned char pkt[188], section_a[40], section_b[40];
  unsigned char cc = 0;
  size_t slen_a, slen_b;
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
  ck_assert_mem_eq(g_eit_pkts[0] + off + 1, section_a, slen_a);
  ck_assert_int_eq(remux_eit_pending(r), 1); /* section_b still queued */

  g_eit_count = 0;
  ck_assert_uint_eq(remux_emit_eit(r, 0x0012, &cc, 1, eit_capture_cb, NULL), 1u);
  off = ((g_eit_pkts[0][3] >> 4) & 0x3) == 3 ? 5 + (size_t)g_eit_pkts[0][4] : 4;
  ck_assert_mem_eq(g_eit_pkts[0] + off + 1, section_b, slen_b);
  ck_assert_int_eq(remux_eit_pending(r), 0);

  remux_free(r);
  psi_free(psi);
}
END_TEST

static Suite *remux_suite(void) {
  Suite *s = suite_create("remux");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, remux_forwards_mapped_es_and_sends_pat_pmt_on_first_feed);
  tcase_add_test(tc, remux_es_exposes_output_pid_mapping);
  tcase_add_test(tc, remux_does_not_advance_cc_on_payloadless_adaptation_field_packet);
  tcase_add_test(tc, remux_feed_counts_ts_packets_and_sync_errors);
  tcase_add_test(tc, remux_feed_detects_continuity_gap_and_signaled_discontinuity);
  tcase_add_test(tc, remux_feed_detects_pcr_discontinuity_on_source_pcr_pid);
  tcase_add_test(tc, remux_drops_unrecognized_pid_and_does_not_resend_psi_immediately);
  tcase_add_test(tc, remux_resends_pat_after_interval_elapses_by_explicit_clock);
  tcase_add_test(tc, remux_pmt_updates_total_only_counts_real_content_changes);
  tcase_add_test(tc, remux_eit_passthrough_respects_strip_eit);
  tcase_add_test(tc, remux_sdt_nit_ait_sent_when_configured);
  tcase_add_test(tc, remux_non_standalone_only_sends_pmt_and_ait_directly);
  tcase_add_test(tc, remux_get_sdt_info_returns_service_regardless_of_mode);
  tcase_add_test(tc, remux_get_sdt_info_fails_when_sdt_dropped);
  tcase_add_test(tc, remux_non_standalone_emits_reassembled_eit);
  tcase_add_test(tc, remux_non_standalone_eit_spans_ticks_when_bounded);
  tcase_add_test(tc, remux_non_standalone_eit_drops_other_service_ids);
  tcase_add_test(tc, remux_non_standalone_eit_queues_distinct_sections);
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
