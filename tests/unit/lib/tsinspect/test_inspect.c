/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/crc32.h"
#include "lib/tsinspect/inspect.h"

static void make_pkt(unsigned char *p, unsigned pid, unsigned afc, unsigned cc) {
  memset(p, 0xAA, 188);
  p[0] = 0x47;
  p[1] = (unsigned char)((pid >> 8) & 0x1F);
  p[2] = (unsigned char)pid;
  p[3] = (unsigned char)((afc << 4) | (cc & 0x0F));
  if (afc >= 2) {
    p[4] = afc == 2 ? 183 : 1;
    p[5] = 0;
  }
}

START_TEST(off_creates_nothing) {
  ck_assert_ptr_null(tsinspect_new(METRICS_INSPECT_TS_OFF));
}
END_TEST

START_TEST(counts_packet_classes) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  unsigned char p[188];
  const tsinspect_counters_t *c;

  ck_assert_ptr_nonnull(t);
  make_pkt(p, 0x100, 1, 0);
  tsinspect_packet(t, p);
  make_pkt(p, 0x100, 1, 1);
  p[3] |= 0x80;
  tsinspect_packet(t, p);
  make_pkt(p, 0x1FFF, 1, 0);
  tsinspect_packet(t, p);
  c = tsinspect_counters(t);
  ck_assert_uint_eq(c->packets, 3u);
  ck_assert_uint_eq(c->null_packets, 1u);
  ck_assert_uint_eq(c->scrambled_packets, 1u);
  ck_assert_uint_eq(c->clear_packets, 1u);
  ck_assert_uint_eq(c->continuity_errors, 0u);
  tsinspect_free(t);
}
END_TEST

START_TEST(transport_error_counted_and_skips_cc) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  unsigned char p[188];

  make_pkt(p, 0x100, 1, 0);
  tsinspect_packet(t, p);
  make_pkt(p, 0x100, 1, 9);
  p[1] |= 0x80;
  tsinspect_packet(t, p);
  make_pkt(p, 0x100, 1, 1);
  tsinspect_packet(t, p);
  ck_assert_uint_eq(tsinspect_counters(t)->transport_errors, 1u);
  ck_assert_uint_eq(tsinspect_counters(t)->continuity_errors, 0u);
  tsinspect_free(t);
}
END_TEST

START_TEST(cc_jump_is_error_and_resyncs_expectation) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  unsigned char p[188];

  make_pkt(p, 0x100, 1, 0);
  tsinspect_packet(t, p);
  make_pkt(p, 0x100, 1, 5);
  tsinspect_packet(t, p);
  make_pkt(p, 0x100, 1, 6);
  tsinspect_packet(t, p);
  ck_assert_uint_eq(tsinspect_counters(t)->continuity_errors, 1u);
  tsinspect_free(t);
}
END_TEST

START_TEST(cc_wraps_at_16) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  unsigned char p[188];

  for (unsigned i = 0; i < 40; i++) {
    make_pkt(p, 0x100, 1, i);
    tsinspect_packet(t, p);
  }
  ck_assert_uint_eq(tsinspect_counters(t)->continuity_errors, 0u);
  tsinspect_free(t);
}
END_TEST

START_TEST(one_duplicate_allowed_second_is_error) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  unsigned char p[188];

  make_pkt(p, 0x100, 1, 3);
  tsinspect_packet(t, p);
  tsinspect_packet(t, p);
  ck_assert_uint_eq(tsinspect_counters(t)->duplicate_packets, 1u);
  ck_assert_uint_eq(tsinspect_counters(t)->continuity_errors, 0u);
  tsinspect_packet(t, p);
  ck_assert_uint_eq(tsinspect_counters(t)->continuity_errors, 1u);
  tsinspect_free(t);
}
END_TEST

START_TEST(adaptation_only_keeps_cc) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  unsigned char p[188];

  make_pkt(p, 0x100, 1, 4);
  tsinspect_packet(t, p);
  make_pkt(p, 0x100, 2, 4);
  tsinspect_packet(t, p);
  make_pkt(p, 0x100, 1, 5);
  tsinspect_packet(t, p);
  ck_assert_uint_eq(tsinspect_counters(t)->continuity_errors, 0u);
  make_pkt(p, 0x100, 2, 9);
  tsinspect_packet(t, p);
  ck_assert_uint_eq(tsinspect_counters(t)->continuity_errors, 1u);
  tsinspect_free(t);
}
END_TEST

START_TEST(discontinuity_indicator_excuses_cc_jump) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  unsigned char p[188];

  make_pkt(p, 0x100, 1, 0);
  tsinspect_packet(t, p);
  make_pkt(p, 0x100, 3, 9);
  p[5] = 0x80;
  tsinspect_packet(t, p);
  ck_assert_uint_eq(tsinspect_counters(t)->continuity_errors, 0u);
  ck_assert_uint_eq(tsinspect_counters(t)->discontinuity_indicators, 1u);
  tsinspect_free(t);
}
END_TEST

START_TEST(pids_track_cc_independently) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  unsigned char p[188];

  for (unsigned i = 0; i < 20; i++) {
    make_pkt(p, 0x100, 1, i);
    tsinspect_packet(t, p);
    make_pkt(p, 0x200, 1, i + 7);
    tsinspect_packet(t, p);
  }
  ck_assert_uint_eq(tsinspect_counters(t)->continuity_errors, 0u);
  tsinspect_free(t);
}
END_TEST

START_TEST(non_sync_packet_is_ignored) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  unsigned char p[188];

  make_pkt(p, 0x100, 1, 0);
  p[0] = 0x00;
  tsinspect_packet(t, p);
  ck_assert_uint_eq(tsinspect_counters(t)->packets, 0u);
  tsinspect_free(t);
}
END_TEST

START_TEST(grid_counts_bad_sync_and_loss) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  unsigned char buf[188 * 9];

  for (unsigned i = 0; i < 9; i++)
    make_pkt(buf + 188 * i, 0x100, 1, i);
  tsinspect_grid(t, buf, 188 * 6);
  ck_assert_int_eq(tsinspect_sync(t)->in_sync, 1);
  buf[188 * 6] = 0;
  buf[188 * 7] = 0;
  tsinspect_grid(t, buf + 188 * 6, 188 * 3);
  ck_assert_uint_eq(tsinspect_sync(t)->byte_errors, 2u);
  ck_assert_uint_eq(tsinspect_sync(t)->losses, 1u);
  ck_assert_int_eq(tsinspect_sync(t)->in_sync, 0);
  ck_assert_uint_eq(tsinspect_counters(t)->packets, 7u);
  tsinspect_free(t);
}
END_TEST

START_TEST(single_bad_sync_does_not_lose_sync) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  unsigned char buf[188 * 8];

  for (unsigned i = 0; i < 8; i++)
    make_pkt(buf + 188 * i, 0x100, 1, i);
  buf[188 * 6] = 0;
  tsinspect_grid(t, buf, sizeof buf);
  ck_assert_uint_eq(tsinspect_sync(t)->byte_errors, 1u);
  ck_assert_uint_eq(tsinspect_sync(t)->losses, 0u);
  tsinspect_free(t);
}
END_TEST

START_TEST(sync_reacquired_after_five_good) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  unsigned char buf[188 * 20];

  for (unsigned i = 0; i < 20; i++)
    make_pkt(buf + 188 * i, 0x100, 1, i);
  tsinspect_grid(t, buf, 188 * 5);
  buf[188 * 5] = 0;
  buf[188 * 6] = 0;
  tsinspect_grid(t, buf + 188 * 5, 188 * 2);
  ck_assert_int_eq(tsinspect_sync(t)->in_sync, 0);
  tsinspect_grid(t, buf + 188 * 7, 188 * 4);
  ck_assert_int_eq(tsinspect_sync(t)->in_sync, 0);
  tsinspect_grid(t, buf + 188 * 11, 188);
  ck_assert_int_eq(tsinspect_sync(t)->in_sync, 1);
  ck_assert_uint_eq(tsinspect_sync(t)->losses, 1u);
  tsinspect_free(t);
}
END_TEST

static size_t seal(unsigned char *sec, size_t body_len) {
  size_t total = 3 + body_len;
  uint32_t crc;
  sec[1] = (unsigned char)(0xB0 | (((body_len + 4) >> 8) & 0x0F));
  sec[2] = (unsigned char)(body_len + 4);
  crc = crc32_mpeg(sec, total);
  sec[total] = (unsigned char)(crc >> 24);
  sec[total + 1] = (unsigned char)(crc >> 16);
  sec[total + 2] = (unsigned char)(crc >> 8);
  sec[total + 3] = (unsigned char)crc;
  return total + 4;
}

static size_t build_pat(unsigned char *sec, unsigned version, unsigned prog, unsigned pmt_pid) {
  size_t n = 3;
  sec[0] = 0x00;
  sec[n++] = 0x00;
  sec[n++] = 0x01;
  sec[n++] = (unsigned char)(0xC1 | (version << 1));
  sec[n++] = 0;
  sec[n++] = 0;
  sec[n++] = (unsigned char)(prog >> 8);
  sec[n++] = (unsigned char)prog;
  sec[n++] = (unsigned char)(0xE0 | (pmt_pid >> 8));
  sec[n++] = (unsigned char)pmt_pid;
  return seal(sec, n - 3);
}

static size_t build_pmt(unsigned char *sec, unsigned version, unsigned prog, unsigned pcr_pid, unsigned es_pid) {
  size_t n = 3;
  sec[0] = 0x02;
  sec[n++] = (unsigned char)(prog >> 8);
  sec[n++] = (unsigned char)prog;
  sec[n++] = (unsigned char)(0xC1 | (version << 1));
  sec[n++] = 0;
  sec[n++] = 0;
  sec[n++] = (unsigned char)(0xE0 | (pcr_pid >> 8));
  sec[n++] = (unsigned char)pcr_pid;
  sec[n++] = 0xF0;
  sec[n++] = 0;
  sec[n++] = 0x1B;
  sec[n++] = (unsigned char)(0xE0 | (es_pid >> 8));
  sec[n++] = (unsigned char)es_pid;
  sec[n++] = 0xF0;
  sec[n++] = 0;
  return seal(sec, n - 3);
}

static void section_pkt(unsigned char *pkt, unsigned pid, const unsigned char *sec, size_t len) {
  memset(pkt, 0xFF, 188);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | (pid >> 8));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt[4] = 0;
  memcpy(pkt + 5, sec, len);
}

static void pcr_pkt(unsigned char *pkt, unsigned pid, unsigned cc, uint64_t pcr27, int disc) {
  uint64_t base = pcr27 / 300, ext = pcr27 % 300;
  memset(pkt, 0xFF, 188);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(pid >> 8);
  pkt[2] = (unsigned char)pid;
  pkt[3] = (unsigned char)(0x20 | (cc & 0x0F));
  pkt[4] = 183;
  pkt[5] = (unsigned char)(0x10 | (disc ? 0x80 : 0));
  pkt[6] = (unsigned char)(base >> 25);
  pkt[7] = (unsigned char)(base >> 17);
  pkt[8] = (unsigned char)(base >> 9);
  pkt[9] = (unsigned char)(base >> 1);
  pkt[10] = (unsigned char)(((base & 1) << 7) | 0x7E | (ext >> 8));
  pkt[11] = (unsigned char)ext;
}

static void feed_psi(tsinspect_t *t, psi_t *psi, const unsigned char *pkt) {
  psi_feed(psi, pkt);
  tsinspect_packet(t, pkt);
}

START_TEST(pat_missing_counts_once_until_it_arrives) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  psi_t *psi = psi_new();
  unsigned char sec[64], pkt[188];
  size_t len = build_pat(sec, 0, 1, 0x100);

  tsinspect_bind_psi(t, psi);
  tsinspect_tick(t, 10.0);
  tsinspect_tick(t, 10.4);
  ck_assert_uint_eq(tsinspect_counters(t)->pat_errors, 0u);
  tsinspect_tick(t, 10.6);
  tsinspect_tick(t, 12.0);
  ck_assert_uint_eq(tsinspect_counters(t)->pat_errors, 1u);
  section_pkt(pkt, 0, sec, len);
  tsinspect_tick(t, 12.1);
  feed_psi(t, psi, pkt);
  tsinspect_tick(t, 12.3);
  tsinspect_tick(t, 12.7);
  ck_assert_uint_eq(tsinspect_counters(t)->pat_errors, 2u);
  ck_assert(tsinspect_psi(t)->t[PSI_OBS_PAT].last_seen == 12.1);
  psi_free(psi);
  tsinspect_free(t);
}
END_TEST

START_TEST(pat_crc_error_and_version_changes_counted) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  psi_t *psi = psi_new();
  unsigned char sec[64], pkt[188];
  size_t len = build_pat(sec, 0, 1, 0x100);

  tsinspect_bind_psi(t, psi);
  tsinspect_tick(t, 1.0);
  section_pkt(pkt, 0, sec, len);
  feed_psi(t, psi, pkt);
  len = build_pat(sec, 1, 1, 0x100);
  section_pkt(pkt, 0, sec, len);
  feed_psi(t, psi, pkt);
  len = build_pat(sec, 2, 1, 0x100);
  sec[len - 1] ^= 0xFF;
  section_pkt(pkt, 0, sec, len);
  feed_psi(t, psi, pkt);
  ck_assert_uint_eq(tsinspect_psi(t)->t[PSI_OBS_PAT].crc_errors, 1u);
  ck_assert_uint_eq(tsinspect_psi(t)->t[PSI_OBS_PAT].changes, 1u);
  psi_free(psi);
  tsinspect_free(t);
}
END_TEST

START_TEST(pmt_late_repetition_counted) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  psi_t *psi = psi_new();
  unsigned char sec[128], pkt[188];
  size_t len;

  tsinspect_bind_psi(t, psi);
  tsinspect_tick(t, 1.0);
  len = build_pat(sec, 0, 1, 0x100);
  section_pkt(pkt, 0, sec, len);
  feed_psi(t, psi, pkt);
  len = build_pmt(sec, 0, 1, 0x101, 0x101);
  section_pkt(pkt, 0x100, sec, len);
  feed_psi(t, psi, pkt);
  tsinspect_tick(t, 1.3);
  ck_assert_uint_eq(tsinspect_counters(t)->pmt_errors, 0u);
  tsinspect_tick(t, 1.7);
  ck_assert_uint_eq(tsinspect_counters(t)->pmt_errors, 1u);
  psi_free(psi);
  tsinspect_free(t);
}
END_TEST

START_TEST(referenced_pid_missing_after_five_windows) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  psi_t *psi = psi_new();
  unsigned char sec[128], pkt[188], es[188];
  size_t len;

  tsinspect_bind_psi(t, psi);
  tsinspect_tick(t, 100.0);
  len = build_pat(sec, 0, 1, 0x100);
  section_pkt(pkt, 0, sec, len);
  feed_psi(t, psi, pkt);
  len = build_pmt(sec, 0, 1, 0x101, 0x101);
  section_pkt(pkt, 0x100, sec, len);
  feed_psi(t, psi, pkt);
  memset(es, 0xAA, sizeof es);
  es[0] = 0x47;
  es[1] = 0x01;
  es[2] = 0x01;
  es[3] = 0x10;
  feed_psi(t, psi, es);
  for (unsigned i = 1; i <= 5; i++)
    tsinspect_tick(t, 100.0 + i * 1.1);
  ck_assert_uint_eq(tsinspect_counters(t)->referenced_pid_missing, 0u);
  for (unsigned i = 6; i <= 11; i++)
    tsinspect_tick(t, 100.0 + i * 1.1);
  ck_assert_uint_eq(tsinspect_counters(t)->referenced_pid_missing, 1u);
  psi_free(psi);
  tsinspect_free(t);
}
END_TEST

START_TEST(pcr_repetition_and_discontinuity_checks) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  psi_t *psi = psi_new();
  unsigned char sec[128], pkt[188];
  size_t len;
  const tsinspect_counters_t *c = tsinspect_counters(t);

  tsinspect_bind_psi(t, psi);
  tsinspect_tick(t, 1.0);
  len = build_pat(sec, 0, 1, 0x100);
  section_pkt(pkt, 0, sec, len);
  feed_psi(t, psi, pkt);
  len = build_pmt(sec, 0, 1, 0x101, 0x101);
  section_pkt(pkt, 0x100, sec, len);
  feed_psi(t, psi, pkt);
  tsinspect_tick(t, 1.05);

  pcr_pkt(pkt, 0x101, 0, 27000000ULL, 0);
  tsinspect_packet(t, pkt);
  pcr_pkt(pkt, 0x101, 1, 27000000ULL + 1350000ULL, 0);
  tsinspect_packet(t, pkt);
  ck_assert_uint_eq(c->pcr_repetition_errors, 0u);
  ck_assert_uint_eq(c->pcr_discontinuity_errors, 0u);
  pcr_pkt(pkt, 0x101, 2, 27000000ULL + 1350000ULL + 5400000ULL, 0);
  tsinspect_packet(t, pkt);
  ck_assert_uint_eq(c->pcr_repetition_errors, 1u);
  ck_assert_uint_eq(c->pcr_discontinuity_errors, 1u);
  pcr_pkt(pkt, 0x101, 3, 1000, 0);
  tsinspect_packet(t, pkt);
  ck_assert_uint_eq(c->pcr_discontinuity_errors, 2u);
  ck_assert_uint_eq(c->pcr_repetition_errors, 1u);
  pcr_pkt(pkt, 0x101, 4, 90000000ULL, 1);
  tsinspect_packet(t, pkt);
  ck_assert_uint_eq(c->pcr_discontinuity_errors, 2u);
  ck_assert(tsinspect_pcr_max_interval(t) >= 0.19);
  psi_free(psi);
  tsinspect_free(t);
}
END_TEST

START_TEST(cat_missing_with_scrambled_packets) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  unsigned char p[188];

  tsinspect_tick(t, 1.0);
  make_pkt(p, 0x100, 1, 0);
  p[3] |= 0x80;
  tsinspect_packet(t, p);
  tsinspect_tick(t, 1.1);
  tsinspect_tick(t, 3.0);
  ck_assert_uint_eq(tsinspect_counters(t)->cat_errors, 0u);
  tsinspect_tick(t, 3.5);
  ck_assert_uint_eq(tsinspect_counters(t)->cat_errors, 1u);
  tsinspect_tick(t, 9.0);
  ck_assert_uint_eq(tsinspect_counters(t)->cat_errors, 1u);
  tsinspect_free(t);
}
END_TEST

START_TEST(pts_gap_over_700ms_counted_once_and_recovers) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  psi_t *psi = psi_new();
  unsigned char sec[128], pkt[188], pes[188];
  size_t len;

  tsinspect_bind_psi(t, psi);
  tsinspect_tick(t, 1.0);
  len = build_pat(sec, 0, 1, 0x100);
  section_pkt(pkt, 0, sec, len);
  feed_psi(t, psi, pkt);
  len = build_pmt(sec, 0, 1, 0x101, 0x101);
  section_pkt(pkt, 0x100, sec, len);
  feed_psi(t, psi, pkt);
  memset(pes, 0xFF, sizeof pes);
  pes[0] = 0x47;
  pes[1] = 0x41;
  pes[2] = 0x01;
  pes[3] = 0x10;
  pes[4] = 0;
  pes[5] = 0;
  pes[6] = 1;
  pes[7] = 0xE0;
  pes[8] = 0;
  pes[9] = 0;
  pes[10] = 0x80;
  pes[11] = 0x80;
  pes[12] = 5;

  tsinspect_tick(t, 1.05);
  tsinspect_tick(t, 1.5);
  ck_assert_uint_eq(tsinspect_counters(t)->pts_errors, 0u);
  tsinspect_tick(t, 1.9);
  ck_assert_uint_eq(tsinspect_counters(t)->pts_errors, 1u);
  tsinspect_tick(t, 1.95);
  tsinspect_packet(t, pes);
  tsinspect_tick(t, 2.0);
  tsinspect_tick(t, 2.6);
  ck_assert_uint_eq(tsinspect_counters(t)->pts_errors, 1u);
  tsinspect_tick(t, 2.8);
  ck_assert_uint_eq(tsinspect_counters(t)->pts_errors, 2u);
  psi_free(psi);
  tsinspect_free(t);
}
END_TEST

START_TEST(stall_detected_counted_once_and_timed) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  unsigned char p[188];

  make_pkt(p, 0x100, 1, 0);
  tsinspect_tick(t, 1.0);
  tsinspect_packet(t, p);
  tsinspect_tick(t, 1.1);
  tsinspect_tick(t, 1.9);
  ck_assert_uint_eq(tsinspect_counters(t)->stalls, 0u);
  tsinspect_tick(t, 2.3);
  tsinspect_tick(t, 3.3);
  ck_assert_uint_eq(tsinspect_counters(t)->stalls, 1u);
  ck_assert_uint_ge(tsinspect_counters(t)->stall_ms, 1000u);
  make_pkt(p, 0x100, 1, 1);
  tsinspect_packet(t, p);
  tsinspect_tick(t, 3.4);
  ck_assert(tsinspect_max_gap_ms(t) >= 2200.0);
  ck_assert(tsinspect_last_packet(t) == 3.4);
  tsinspect_tick(t, 6.0);
  ck_assert_uint_eq(tsinspect_counters(t)->stalls, 2u);
  tsinspect_free(t);
}
END_TEST

START_TEST(pid_added_and_removed_on_pmt_change) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  psi_t *psi = psi_new();
  unsigned char sec[128], pkt[188];
  size_t len;

  tsinspect_bind_psi(t, psi);
  tsinspect_tick(t, 1.0);
  len = build_pat(sec, 0, 1, 0x100);
  section_pkt(pkt, 0, sec, len);
  feed_psi(t, psi, pkt);
  len = build_pmt(sec, 0, 1, 0x101, 0x101);
  section_pkt(pkt, 0x100, sec, len);
  feed_psi(t, psi, pkt);
  tsinspect_tick(t, 2.1);
  ck_assert_uint_eq(tsinspect_counters(t)->pid_added, 0u);
  len = build_pmt(sec, 1, 1, 0x102, 0x102);
  section_pkt(pkt, 0x100, sec, len);
  feed_psi(t, psi, pkt);
  tsinspect_tick(t, 3.2);
  ck_assert_uint_eq(tsinspect_counters(t)->pid_added, 1u);
  ck_assert_uint_eq(tsinspect_counters(t)->pid_removed, 1u);
  psi_free(psi);
  tsinspect_free(t);
}
END_TEST

static size_t build_si(unsigned char *sec, unsigned tid, unsigned ext, unsigned secno, size_t extra) {
  size_t n = 3;
  sec[0] = (unsigned char)tid;
  sec[n++] = (unsigned char)(ext >> 8);
  sec[n++] = (unsigned char)ext;
  sec[n++] = 0xC1;
  sec[n++] = (unsigned char)secno;
  sec[n++] = 1;
  memset(sec + n, 0, extra);
  n += extra;
  return seal(sec, n - 3);
}

START_TEST(eit_pf_missing_section_counted_once) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  unsigned char sec[64], pkt[188];
  size_t len;

  tsinspect_tick(t, 1.0);
  len = build_si(sec, 0x4E, 0x1234, 0, 6);
  section_pkt(pkt, 0x12, sec, len);
  tsinspect_packet(t, pkt);
  len = build_si(sec, 0x4E, 0x1234, 1, 6);
  section_pkt(pkt, 0x12, sec, len);
  tsinspect_packet(t, pkt);
  tsinspect_tick(t, 2.5);
  ck_assert_uint_eq(tsinspect_counters(t)->eit_errors, 0u);
  len = build_si(sec, 0x4E, 0x1234, 0, 6);
  section_pkt(pkt, 0x12, sec, len);
  tsinspect_packet(t, pkt);
  tsinspect_tick(t, 3.5);
  tsinspect_tick(t, 4.0);
  ck_assert_uint_eq(tsinspect_counters(t)->eit_errors, 1u);
  tsinspect_free(t);
}
END_TEST

START_TEST(tdt_missing_and_wrong_table_id) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  unsigned char sec[64], pkt[188];
  size_t len;

  tsinspect_tick(t, 1.0);
  tsinspect_tick(t, 20.0);
  ck_assert_uint_eq(tsinspect_counters(t)->tdt_errors, 0u);
  tsinspect_tick(t, 32.0);
  ck_assert_uint_eq(tsinspect_counters(t)->tdt_errors, 1u);
  len = build_si(sec, 0x70, 0, 0, 0);
  section_pkt(pkt, 0x14, sec, len);
  tsinspect_packet(t, pkt);
  tsinspect_tick(t, 33.0);
  tsinspect_tick(t, 50.0);
  ck_assert_uint_eq(tsinspect_counters(t)->tdt_errors, 1u);
  len = build_si(sec, 0x42, 0, 0, 0);
  section_pkt(pkt, 0x14, sec, len);
  tsinspect_packet(t, pkt);
  ck_assert_uint_eq(tsinspect_counters(t)->tdt_errors, 2u);
  tsinspect_free(t);
}
END_TEST

START_TEST(rst_wrong_table_id_counted) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  unsigned char sec[64], pkt[188];
  size_t len = build_si(sec, 0x71, 0, 0, 0);

  section_pkt(pkt, 0x13, sec, len);
  tsinspect_packet(t, pkt);
  ck_assert_uint_eq(tsinspect_counters(t)->rst_errors, 0u);
  len = build_si(sec, 0x42, 0, 0, 0);
  section_pkt(pkt, 0x13, sec, len);
  tsinspect_packet(t, pkt);
  ck_assert_uint_eq(tsinspect_counters(t)->rst_errors, 1u);
  tsinspect_free(t);
}
END_TEST

START_TEST(si_crc_checked_only_from_medium) {
  tsinspect_t *basic = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  tsinspect_t *medium = tsinspect_new(METRICS_INSPECT_TS_MEDIUM);
  unsigned char sec[64], pkt[188];
  size_t len = build_si(sec, 0x50, 0x1234, 0, 6);

  sec[len - 1] ^= 0xFF;
  section_pkt(pkt, 0x12, sec, len);
  tsinspect_packet(basic, pkt);
  tsinspect_packet(medium, pkt);
  ck_assert_uint_eq(tsinspect_counters(basic)->si_crc_errors, 0u);
  ck_assert_uint_eq(tsinspect_counters(medium)->si_crc_errors, 1u);
  tsinspect_free(basic);
  tsinspect_free(medium);
}
END_TEST

START_TEST(sdt_other_checked_only_once_seen) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  psi_t *psi = psi_new();
  unsigned char sec[64], pkt[188];
  size_t len;

  tsinspect_bind_psi(t, psi);
  tsinspect_tick(t, 1.0);
  tsinspect_tick(t, 30.0);
  ck_assert_uint_eq(tsinspect_counters(t)->sdt_other_errors, 0u);
  len = build_si(sec, 0x46, 0x1234, 0, 6);
  section_pkt(pkt, 0x11, sec, len);
  tsinspect_tick(t, 31.0);
  feed_psi(t, psi, pkt);
  tsinspect_tick(t, 35.0);
  ck_assert_uint_eq(tsinspect_counters(t)->sdt_other_errors, 0u);
  tsinspect_tick(t, 42.0);
  ck_assert_uint_eq(tsinspect_counters(t)->sdt_other_errors, 1u);
  psi_free(psi);
  tsinspect_free(t);
}
END_TEST

START_TEST(eit_other_checked_only_once_seen) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  unsigned char sec[64], pkt[188];
  size_t len = build_si(sec, 0x4F, 0x1234, 0, 6);

  tsinspect_tick(t, 1.0);
  tsinspect_tick(t, 30.0);
  ck_assert_uint_eq(tsinspect_counters(t)->eit_other_errors, 0u);
  section_pkt(pkt, 0x12, sec, len);
  tsinspect_packet(t, pkt);
  tsinspect_tick(t, 35.0);
  ck_assert_uint_eq(tsinspect_counters(t)->eit_other_errors, 0u);
  tsinspect_tick(t, 41.0);
  ck_assert_uint_eq(tsinspect_counters(t)->eit_other_errors, 2u);
  tsinspect_free(t);
}
END_TEST

START_TEST(put_metrics_emits_stream_labeled_series_by_level) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  tsinspect_t *m = tsinspect_new(METRICS_INSPECT_TS_MEDIUM);
  psi_t *psi = psi_new();
  unsigned char p[188];
  metrics_writer_t w;
  metrics_hdr_t hdr;
  metrics_reader_t r;
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  uint64_t value;
  int seen_packets = 0, seen_crc = 0, seen_pat = 0, seen_sync = 0;

  memset(&hdr, 0, sizeof hdr);
  hdr.proto_version = METRICS_PROTO_VERSION;
  hdr.component = METRICS_COMPONENT_TVHEAD;
  hdr.metrics_id[0] = 'x';
  make_pkt(p, 0x100, 1, 0);
  tsinspect_tick(t, 5.0);
  tsinspect_packet(t, p);
  tsinspect_bind_psi(t, psi);
  tsinspect_tick(t, 5.5);

  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  tsinspect_put_metrics(t, &w, "input0", 6.0, 1000.0);
  ck_assert_int_eq(metrics_reader_init(&r, w.buf, w.len, &hdr), 0);
  while (metrics_reader_next(&r, &id, label, sizeof label, &value) == 1) {
    if (id == METRICS_ID_TS_PACKETS_TOTAL) {
      ck_assert_str_eq(label, "input0");
      ck_assert_uint_eq(value, 1u);
      seen_packets = 1;
    }
    seen_pat |= id == METRICS_ID_TS_PAT_ERRORS_TOTAL;
    seen_crc |= id == METRICS_ID_TS_SI_CRC_ERRORS_TOTAL;
    seen_sync |= id == METRICS_ID_TS_SYNC_LOSS_TOTAL;
    if (id == METRICS_ID_TS_LAST_PACKET_TIMESTAMP_SECONDS)
      ck_assert_uint_eq(value, 999u);
  }
  ck_assert_int_eq(seen_packets, 1);
  ck_assert_int_eq(seen_pat, 1);
  ck_assert_int_eq(seen_crc, 0);
  ck_assert_int_eq(seen_sync, 0);

  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  tsinspect_put_metrics(m, &w, "output0", 6.0, 1000.0);
  ck_assert_int_eq(metrics_reader_init(&r, w.buf, w.len, &hdr), 0);
  seen_pat = 0;
  while (metrics_reader_next(&r, &id, label, sizeof label, &value) == 1) {
    seen_crc |= id == METRICS_ID_TS_SI_CRC_ERRORS_TOTAL;
    seen_pat |= id == METRICS_ID_TS_PAT_ERRORS_TOTAL;
  }
  ck_assert_int_eq(seen_crc, 1);
  ck_assert_int_eq(seen_pat, 0);
  psi_free(psi);
  tsinspect_free(t);
  tsinspect_free(m);
}
END_TEST

START_TEST(aggregate_sums_live_and_retired_streams) {
  tsinspect_agg_t *a = tsinspect_agg_new(METRICS_INSPECT_TS_BASIC);
  tsinspect_t *t1 = tsinspect_agg_add(a);
  tsinspect_t *t2 = tsinspect_agg_add(a);
  unsigned char p[188];
  metrics_writer_t w;
  metrics_hdr_t hdr;
  metrics_reader_t r;
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  uint64_t value, packets = 0, cc = 0;

  memset(&hdr, 0, sizeof hdr);
  hdr.proto_version = METRICS_PROTO_VERSION;
  hdr.component = METRICS_COMPONENT_FCCRET;
  hdr.metrics_id[0] = 'x';
  ck_assert_ptr_nonnull(t1);
  make_pkt(p, 0x100, 1, 0);
  tsinspect_tick(t1, 1.0);
  tsinspect_packet(t1, p);
  make_pkt(p, 0x100, 1, 5);
  tsinspect_packet(t1, p);
  tsinspect_tick(t2, 1.0);
  make_pkt(p, 0x200, 1, 0);
  tsinspect_packet(t2, p);
  tsinspect_tick(t1, 1.5);
  tsinspect_tick(t2, 1.5);
  tsinspect_agg_remove(a, t2);

  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  tsinspect_agg_put(a, &w, "input0");
  ck_assert_int_eq(metrics_reader_init(&r, w.buf, w.len, &hdr), 0);
  while (metrics_reader_next(&r, &id, label, sizeof label, &value) == 1) {
    if (id == METRICS_ID_TS_PACKETS_TOTAL)
      packets = value;
    if (id == METRICS_ID_TS_CONTINUITY_ERRORS_TOTAL)
      cc = value;
  }
  ck_assert_uint_eq(packets, 3u);
  ck_assert_uint_eq(cc, 1u);
  tsinspect_agg_free(a);
}
END_TEST

START_TEST(light_inspector_skips_si_and_psi_state) {
  tsinspect_t *t = tsinspect_new_light(METRICS_INSPECT_TS_BASIC);
  unsigned char sec[64], pkt[188];
  size_t len = build_si(sec, 0x71, 0, 0, 0);

  ck_assert_ptr_null(tsinspect_psi(t));
  section_pkt(pkt, 0x13, sec, len);
  len = build_si(sec, 0x42, 0, 0, 0);
  section_pkt(pkt, 0x13, sec, len);
  tsinspect_packet(t, pkt);
  ck_assert_uint_eq(tsinspect_counters(t)->rst_errors, 0u);
  ck_assert_uint_eq(tsinspect_counters(t)->packets, 1u);
  tsinspect_free(t);
}
END_TEST

START_TEST(own_psi_follows_packets_without_a_tool_psi) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  unsigned char sec[128], pkt[188];
  size_t len;

  ck_assert_int_eq(tsinspect_enable_own_psi(t, 0), 0);
  tsinspect_tick(t, 1.0);
  len = build_pat(sec, 0, 1, 0x100);
  section_pkt(pkt, 0, sec, len);
  tsinspect_packet(t, pkt);
  len = build_pmt(sec, 0, 1, 0x101, 0x101);
  section_pkt(pkt, 0x100, sec, len);
  tsinspect_packet(t, pkt);
  ck_assert(tsinspect_psi(t)->t[PSI_OBS_PAT].last_seen == 1.0);
  ck_assert(tsinspect_psi(t)->t[PSI_OBS_PMT].last_seen == 1.0);
  tsinspect_free(t);
}
END_TEST

START_TEST(unreferenced_pids_only_from_full_level) {
  tsinspect_t *full = tsinspect_new(METRICS_INSPECT_TS_FULL);
  tsinspect_t *med = tsinspect_new(METRICS_INSPECT_TS_MEDIUM);
  tsinspect_t *both[2] = {full, med};
  unsigned char sec[128], pkt[188], p[188];
  size_t len;

  for (int i = 0; i < 2; i++) {
    tsinspect_enable_own_psi(both[i], 0);
    tsinspect_tick(both[i], 1.0);
    len = build_pat(sec, 0, 1, 0x100);
    section_pkt(pkt, 0, sec, len);
    tsinspect_packet(both[i], pkt);
    len = build_pmt(sec, 0, 1, 0x101, 0x101);
    section_pkt(pkt, 0x100, sec, len);
    tsinspect_packet(both[i], pkt);
    make_pkt(p, 0x300, 1, 0);
    tsinspect_packet(both[i], p);
    make_pkt(p, 0x300, 1, 1);
    tsinspect_packet(both[i], p);
    tsinspect_tick(both[i], 2.1);
  }
  ck_assert_uint_eq(tsinspect_counters(full)->unreferenced_packets, 2u);
  ck_assert_uint_eq(tsinspect_counters(full)->unreferenced_pids, 1u);
  ck_assert_uint_eq(tsinspect_counters(med)->unreferenced_packets, 0u);
  tsinspect_free(full);
  tsinspect_free(med);
}
END_TEST

START_TEST(bat_crc_checked_from_medium) {
  tsinspect_t *basic = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  tsinspect_t *med = tsinspect_new(METRICS_INSPECT_TS_MEDIUM);
  tsinspect_t *both[2] = {basic, med};
  unsigned char sec[64], pkt[188];
  size_t len = build_si(sec, 0x4A, 0x1234, 0, 6);

  sec[len - 1] ^= 0xFF;
  section_pkt(pkt, 0x11, sec, len);
  for (int i = 0; i < 2; i++) {
    tsinspect_enable_own_psi(both[i], 0);
    tsinspect_tick(both[i], 1.0);
    tsinspect_packet(both[i], pkt);
  }
  ck_assert_uint_eq(tsinspect_psi(basic)->t[PSI_OBS_BAT].crc_errors, 0u);
  ck_assert_uint_eq(tsinspect_psi(med)->t[PSI_OBS_BAT].crc_errors, 1u);
  tsinspect_free(basic);
  tsinspect_free(med);
}
END_TEST

static unsigned count_series(tsinspect_t *t, metrics_id_t want, const char *label_suffix) {
  metrics_writer_t w;
  metrics_hdr_t hdr;
  metrics_reader_t r;
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  uint64_t v;
  unsigned n = 0;

  memset(&hdr, 0, sizeof hdr);
  hdr.proto_version = METRICS_PROTO_VERSION;
  hdr.component = METRICS_COMPONENT_TVHEAD;
  hdr.metrics_id[0] = 'x';
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  tsinspect_put_metrics(t, &w, "input0", 6.0, 1000.0);
  ck_assert_int_eq(metrics_reader_init(&r, w.buf, w.len, &hdr), 0);
  while (metrics_reader_next(&r, &id, label, sizeof label, &v) == 1) {
    size_t ll = strlen(label), sl = label_suffix ? strlen(label_suffix) : 0;
    if (id == want && (!sl || (ll >= sl && !strcmp(label + ll - sl, label_suffix)))) n++;
  }
  return n;
}

START_TEST(exported_series_cover_tables_gap_stalls_and_bat_crc_by_level) {
  tsinspect_t *basic = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  tsinspect_t *med = tsinspect_new(METRICS_INSPECT_TS_MEDIUM);
  tsinspect_t *both[2] = {basic, med};
  unsigned char sec[64], pkt[188];
  size_t len = build_pat(sec, 0, 1, 0x100);

  section_pkt(pkt, 0, sec, len);
  for (int i = 0; i < 2; i++) {
    tsinspect_enable_own_psi(both[i], 0);
    tsinspect_tick(both[i], 1.0);
    tsinspect_packet(both[i], pkt);
    tsinspect_tick(both[i], 2.0);
  }
  for (int i = 0; i < 2; i++) {
    ck_assert_uint_eq(count_series(both[i], METRICS_ID_TS_TABLE_LAST_SEEN_TIMESTAMP_SECONDS, "pat"), 1u);
    ck_assert_uint_eq(count_series(both[i], METRICS_ID_TS_TABLE_VERSION_CHANGES_TOTAL, "pat"), 1u);
    ck_assert_uint_eq(count_series(both[i], METRICS_ID_TS_TABLE_CRC_ERRORS_TOTAL, "pat"), 1u);
    ck_assert_uint_eq(count_series(both[i], METRICS_ID_TS_TABLE_LAST_SEEN_TIMESTAMP_SECONDS, "pmt"), 0u);
    ck_assert_uint_eq(count_series(both[i], METRICS_ID_TS_MAX_INTERPACKET_GAP_MILLISECONDS, NULL), 1u);
    ck_assert_uint_eq(count_series(both[i], METRICS_ID_TS_INPUT_STALLS_TOTAL, NULL), 1u);
    ck_assert_uint_eq(count_series(both[i], METRICS_ID_TS_INPUT_STALL_MILLISECONDS_TOTAL, NULL), 1u);
  }
  ck_assert_uint_eq(count_series(basic, METRICS_ID_TS_TABLE_CRC_ERRORS_TOTAL, "bat"), 0u);
  ck_assert_uint_eq(count_series(med, METRICS_ID_TS_TABLE_CRC_ERRORS_TOTAL, "bat"), 1u);
  tsinspect_free(basic);
  tsinspect_free(med);
}
END_TEST

START_TEST(pcr_jitter_from_arrival_timestamps) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_MEDIUM);
  psi_t *psi = psi_new();
  unsigned char sec[128], pkt[188];
  size_t len;
  metrics_writer_t w;
  metrics_hdr_t hdr;
  metrics_reader_t r;
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  uint64_t value, jitter = 0;

  memset(&hdr, 0, sizeof hdr);
  hdr.proto_version = METRICS_PROTO_VERSION;
  hdr.component = METRICS_COMPONENT_TVHEAD;
  hdr.metrics_id[0] = 'x';
  ck_assert(tsinspect_wants_rx_ns(t));
  tsinspect_bind_psi(t, psi);
  tsinspect_tick(t, 1.0);
  len = build_pat(sec, 0, 1, 0x100);
  section_pkt(pkt, 0, sec, len);
  feed_psi(t, psi, pkt);
  len = build_pmt(sec, 0, 1, 0x101, 0x101);
  section_pkt(pkt, 0x100, sec, len);
  feed_psi(t, psi, pkt);
  tsinspect_tick(t, 1.05);
  for (unsigned i = 0; i < 4; i++) {
    tsinspect_set_rx_ns(t, 1000000000ULL + i * 40000000ULL + (i == 3 ? 3000000ULL : 0));
    pcr_pkt(pkt, 0x101, 0, 27000000ULL + i * 1080000ULL, 0);
    tsinspect_packet(t, pkt);
  }
  tsinspect_tick(t, 1.5);
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  tsinspect_put_metrics(t, &w, "input0", 1.5, 1000.0);
  ck_assert_int_eq(metrics_reader_init(&r, w.buf, w.len, &hdr), 0);
  while (metrics_reader_next(&r, &id, label, sizeof label, &value) == 1) {
    if (id == METRICS_ID_TS_PCR_JITTER_MAX_MICROSECONDS)
      jitter = value;
    if (id == METRICS_ID_TS_TRANSPORT_STREAM_ID)
      ck_assert_uint_eq(value, 1u);
  }
  ck_assert_uint_ge(jitter, 2900u);
  ck_assert_uint_le(jitter, 3100u);
  ck_assert_uint_eq(tsinspect_counters(t)->pcr_accuracy_errors, 1u);
  psi_free(psi);
  tsinspect_free(t);
}
END_TEST

START_TEST(relay_detects_pcr_without_psi_and_skips_si) {
  tsinspect_t *t = tsinspect_new_relay(METRICS_INSPECT_TS_MEDIUM);
  unsigned char buf[188 * 3], sec[64];
  size_t len = build_si(sec, 0x42, 0, 0, 0);

  ck_assert_ptr_null(tsinspect_psi(t));
  tsinspect_tick(t, 1.0);
  tsinspect_set_rx_ns(t, 1000000000ULL);
  pcr_pkt(buf, 0x101, 0, 27000000ULL, 0);
  section_pkt(buf + 188, 0x13, sec, len);
  tsinspect_grid(t, buf, 188 * 2);
  tsinspect_set_rx_ns(t, 1000000000ULL + 40000000ULL + 3000000ULL);
  pcr_pkt(buf, 0x101, 0, 27000000ULL + 1080000ULL, 0);
  tsinspect_grid(t, buf, 188);
  pcr_pkt(buf, 0x101, 0, 27000000ULL + 1080000ULL + 5400000ULL, 0);
  tsinspect_grid(t, buf, 188);
  ck_assert_uint_eq(tsinspect_counters(t)->pcr_repetition_errors, 1u);
  ck_assert_uint_ge(tsinspect_counters(t)->pcr_accuracy_errors, 1u);
  ck_assert_uint_eq(tsinspect_counters(t)->rst_errors, 0u);
  tsinspect_free(t);
}
END_TEST

START_TEST(other_tables_checked_per_section_and_service) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  psi_t *psi = psi_new();
  unsigned char sec[64], pkt[188];
  size_t len;

  tsinspect_bind_psi(t, psi);
  tsinspect_tick(t, 1.0);
  len = build_si(sec, 0x46, 0x0A01, 0, 6);
  section_pkt(pkt, 0x11, sec, len);
  feed_psi(t, psi, pkt);
  len = build_si(sec, 0x46, 0x0A02, 0, 6);
  section_pkt(pkt, 0x11, sec, len);
  feed_psi(t, psi, pkt);
  len = build_si(sec, 0x4F, 0x0B01, 0, 6);
  section_pkt(pkt, 0x12, sec, len);
  tsinspect_packet(t, pkt);
  len = build_si(sec, 0x4F, 0x0B02, 0, 6);
  section_pkt(pkt, 0x12, sec, len);
  tsinspect_packet(t, pkt);
  for (unsigned i = 1; i <= 8; i++) {
    double now = 1.0 + i * 1.5;
    tsinspect_tick(t, now);
    len = build_si(sec, 0x46, 0x0A01, 0, 6);
    section_pkt(pkt, 0x11, sec, len);
    feed_psi(t, psi, pkt);
    len = build_si(sec, 0x4F, 0x0B01, 0, 6);
    section_pkt(pkt, 0x12, sec, len);
    tsinspect_packet(t, pkt);
    len = build_si(sec, 0x4F, 0x0B01, 1, 6);
    section_pkt(pkt, 0x12, sec, len);
    tsinspect_packet(t, pkt);
  }
  ck_assert_uint_eq(tsinspect_counters(t)->sdt_other_errors, 1u);
  ck_assert_uint_ge(tsinspect_counters(t)->eit_other_errors, 2u);
  psi_free(psi);
  tsinspect_free(t);
}
END_TEST

START_TEST(known_pids_split_the_two_unreferenced_definitions) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_FULL);
  unsigned known[1] = {0x300};
  unsigned char sec[128], pkt[188], p[188];
  size_t len;

  tsinspect_enable_own_psi(t, 0);
  tsinspect_set_known_pids(t, known, 1);
  tsinspect_tick(t, 1.0);
  len = build_pat(sec, 0, 1, 0x100);
  section_pkt(pkt, 0, sec, len);
  tsinspect_packet(t, pkt);
  len = build_pmt(sec, 0, 1, 0x101, 0x101);
  section_pkt(pkt, 0x100, sec, len);
  tsinspect_packet(t, pkt);
  make_pkt(p, 0x300, 1, 0);
  tsinspect_packet(t, p);
  make_pkt(p, 0x301, 1, 0);
  tsinspect_packet(t, p);
  tsinspect_tick(t, 2.1);
  ck_assert_uint_eq(tsinspect_counters(t)->unreferenced_pids, 2u);
  ck_assert_uint_eq(tsinspect_counters(t)->unreferenced_unlisted_pids, 1u);
  tsinspect_free(t);
}
END_TEST

START_TEST(pmt_pids_get_packet_and_scrambled_counters_with_detail_flag) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_FULL);
  unsigned listed[1] = {0x300};
  unsigned char p[188], sec[128], pkt[188];
  size_t len;
  metrics_writer_t w;
  metrics_hdr_t hdr;
  metrics_reader_t r;
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  const char sep[2] = {METRICS_LABEL_SEP, '\0'};
  uint64_t v;
  uint64_t pkts101 = 0, scr101 = 0, pkts100 = 0, other = 0;
  uint64_t svc_pkts = 0, svc_scr = 0;
  unsigned n_svc = 0;

  tsinspect_enable_own_psi(t, 0);
  tsinspect_set_known_pids(t, listed, 1);
  tsinspect_tick(t, 1.0);
  len = build_pat(sec, 0, 1, 0x100);
  section_pkt(pkt, 0, sec, len);
  tsinspect_packet(t, pkt);
  len = build_pmt(sec, 0, 1, 0x101, 0x101);
  section_pkt(pkt, 0x100, sec, len);
  tsinspect_packet(t, pkt);
  for (unsigned i = 0; i < 5; i++) {
    make_pkt(p, 0x101, 1, i);
    tsinspect_packet(t, p);
  }
  make_pkt(p, 0x101, 1, 5);
  p[3] |= 0x80;
  tsinspect_packet(t, p);
  make_pkt(p, 0x300, 1, 0);
  tsinspect_packet(t, p);
  make_pkt(p, 0x400, 1, 0);
  tsinspect_packet(t, p);
  tsinspect_tick(t, 2.0);

  memset(&hdr, 0, sizeof hdr);
  hdr.proto_version = METRICS_PROTO_VERSION;
  hdr.component = METRICS_COMPONENT_TVHEAD;
  hdr.metrics_id[0] = 'x';
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  tsinspect_put_metrics(t, &w, "input0", 6.0, 1000.0);
  ck_assert_int_eq(metrics_reader_init(&r, w.buf, w.len, &hdr), 0);
  while (metrics_reader_next(&r, &id, label, sizeof label, &v) == 1) {
    const char *pid = strstr(label, sep);
    if (!pid) continue;
    pid++;
    if (id == METRICS_ID_TS_PID_PACKETS_TOTAL) {
      if (!strcmp(pid, "257")) pkts101 = v;
      else if (!strcmp(pid, "256")) pkts100 = v;
      else other++;
    }
    if (id == METRICS_ID_TS_PID_SCRAMBLED_PACKETS_TOTAL && !strcmp(pid, "257")) scr101 = v;
    if (id == METRICS_ID_TS_SERVICE_PACKETS_TOTAL) {
      ck_assert_str_eq(pid, "1");
      svc_pkts = v;
      n_svc++;
    }
    if (id == METRICS_ID_TS_SERVICE_SCRAMBLED_PACKETS_TOTAL) svc_scr = v;
  }
  ck_assert_uint_eq(pkts101, 6u);
  ck_assert_uint_eq(scr101, 1u);
  ck_assert_uint_eq(pkts100, 1u);
  ck_assert_uint_eq(other, 0u);
  ck_assert_uint_eq(n_svc, 1u);
  ck_assert_uint_eq(svc_pkts, 7u);
  ck_assert_uint_eq(svc_scr, 1u);
  tsinspect_free(t);
}
END_TEST

START_TEST(mgb1_and_mgb2_bitrates_follow_the_packet_rate) {
  tsinspect_t *t = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  unsigned char p[188];
  metrics_writer_t w;
  metrics_hdr_t hdr;
  metrics_reader_t r;
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  uint64_t value, m1 = 0, m2 = 0;
  double now = 10.0;

  memset(&hdr, 0, sizeof hdr);
  hdr.proto_version = METRICS_PROTO_VERSION;
  hdr.component = METRICS_COMPONENT_TVHEAD;
  hdr.metrics_id[0] = 'x';
  tsinspect_tick(t, now);
  for (unsigned i = 0; i < 340; i++) {
    for (unsigned k = 0; k < 10; k++) {
      make_pkt(p, 0x100, 1, (i * 10 + k));
      tsinspect_packet(t, p);
    }
    now += 0.01;
    tsinspect_tick(t, now);
  }
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  tsinspect_put_metrics(t, &w, "input0", now, 1000.0);
  ck_assert_int_eq(metrics_reader_init(&r, w.buf, w.len, &hdr), 0);
  while (metrics_reader_next(&r, &id, label, sizeof label, &value) == 1) {
    if (id == METRICS_ID_TS_BITRATE_MGB1_BITS_PER_SECOND) m1 = value;
    if (id == METRICS_ID_TS_BITRATE_MGB2_BITS_PER_SECOND) m2 = value;
  }
  ck_assert_uint_ge(m1, 1400000u);
  ck_assert_uint_le(m1, 1600000u);
  ck_assert_uint_ge(m2, 1400000u);
  ck_assert_uint_le(m2, 1600000u);
  tsinspect_free(t);
}
END_TEST

static Suite *inspect_suite(void) {
  Suite *s = suite_create("tsinspect");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, off_creates_nothing);
  tcase_add_test(tc, counts_packet_classes);
  tcase_add_test(tc, transport_error_counted_and_skips_cc);
  tcase_add_test(tc, cc_jump_is_error_and_resyncs_expectation);
  tcase_add_test(tc, cc_wraps_at_16);
  tcase_add_test(tc, one_duplicate_allowed_second_is_error);
  tcase_add_test(tc, adaptation_only_keeps_cc);
  tcase_add_test(tc, discontinuity_indicator_excuses_cc_jump);
  tcase_add_test(tc, pids_track_cc_independently);
  tcase_add_test(tc, non_sync_packet_is_ignored);
  tcase_add_test(tc, grid_counts_bad_sync_and_loss);
  tcase_add_test(tc, single_bad_sync_does_not_lose_sync);
  tcase_add_test(tc, sync_reacquired_after_five_good);
  tcase_add_test(tc, pat_missing_counts_once_until_it_arrives);
  tcase_add_test(tc, pat_crc_error_and_version_changes_counted);
  tcase_add_test(tc, pmt_late_repetition_counted);
  tcase_add_test(tc, referenced_pid_missing_after_five_windows);
  tcase_add_test(tc, pcr_repetition_and_discontinuity_checks);
  tcase_add_test(tc, cat_missing_with_scrambled_packets);
  tcase_add_test(tc, pts_gap_over_700ms_counted_once_and_recovers);
  tcase_add_test(tc, stall_detected_counted_once_and_timed);
  tcase_add_test(tc, pid_added_and_removed_on_pmt_change);
  tcase_add_test(tc, eit_pf_missing_section_counted_once);
  tcase_add_test(tc, tdt_missing_and_wrong_table_id);
  tcase_add_test(tc, rst_wrong_table_id_counted);
  tcase_add_test(tc, si_crc_checked_only_from_medium);
  tcase_add_test(tc, sdt_other_checked_only_once_seen);
  tcase_add_test(tc, eit_other_checked_only_once_seen);
  tcase_add_test(tc, put_metrics_emits_stream_labeled_series_by_level);
  tcase_add_test(tc, aggregate_sums_live_and_retired_streams);
  tcase_add_test(tc, light_inspector_skips_si_and_psi_state);
  tcase_add_test(tc, own_psi_follows_packets_without_a_tool_psi);
  tcase_add_test(tc, unreferenced_pids_only_from_full_level);
  tcase_add_test(tc, bat_crc_checked_from_medium);
  tcase_add_test(tc, pcr_jitter_from_arrival_timestamps);
  tcase_add_test(tc, relay_detects_pcr_without_psi_and_skips_si);
  tcase_add_test(tc, other_tables_checked_per_section_and_service);
  tcase_add_test(tc, known_pids_split_the_two_unreferenced_definitions);
  tcase_add_test(tc, pmt_pids_get_packet_and_scrambled_counters_with_detail_flag);
  tcase_add_test(tc, exported_series_cover_tables_gap_stalls_and_bat_crc_by_level);
  tcase_add_test(tc, mgb1_and_mgb2_bitrates_follow_the_packet_rate);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(inspect_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
