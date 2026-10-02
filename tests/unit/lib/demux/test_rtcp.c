/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/rtcp.h"
#include "lib/mux/rtcp_build.h"

#define RTCP_FMT_RAMS 6

static void wr32(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)(v >> 24);
  p[1] = (unsigned char)(v >> 16);
  p[2] = (unsigned char)(v >> 8);
  p[3] = (unsigned char)v;
}

static void wr16(unsigned char *p, uint16_t v) {
  p[0] = (unsigned char)(v >> 8);
  p[1] = (unsigned char)v;
}

/* RAMS header (16 bytes) + fci, fci_len must be a multiple of 4. returns total bytes written */
static size_t build_rams(unsigned char *out, unsigned sfmt, uint32_t sender_ssrc, uint32_t media_ssrc, const unsigned char *fci, size_t fci_len) {
  size_t total = 16 + fci_len;
  uint16_t words = (uint16_t)(total / 4 - 1);
  out[0] = (unsigned char)((2 << 6) | RTCP_FMT_RAMS);
  out[1] = 205; /* RTPFB */
  wr16(out + 2, words);
  wr32(out + 4, sender_ssrc);
  wr32(out + 8, media_ssrc);
  out[12] = (unsigned char)sfmt;
  out[13] = out[14] = out[15] = 0;
  if (fci_len)
    memcpy(out + 16, fci, fci_len);
  return total;
}

static rtcp_nack_t g_nack;
static int g_nack_calls;
static void nack_cb(const rtcp_nack_t *n, void *user) {
  (void)user;
  g_nack = *n;
  g_nack_calls++;
}

static rtcp_rams_r_t g_rams_r;
static int g_rams_r_calls;
static void rams_r_cb(const rtcp_rams_r_t *r, void *user) {
  (void)user;
  g_rams_r = *r;
  g_rams_r_calls++;
}

static rtcp_rams_t_t g_rams_t;
static int g_rams_t_calls;
static void rams_t_cb(const rtcp_rams_t_t *t, void *user) {
  (void)user;
  g_rams_t = *t;
  g_rams_t_calls++;
}

static unsigned g_malformed_sfmt;
static uint32_t g_malformed_sender_ssrc, g_malformed_media_ssrc;
static int g_malformed_calls;
static void malformed_cb(unsigned sfmt, uint32_t sender_ssrc, uint32_t media_ssrc, void *user) {
  (void)user;
  g_malformed_sfmt = sfmt;
  g_malformed_sender_ssrc = sender_ssrc;
  g_malformed_media_ssrc = media_ssrc;
  g_malformed_calls++;
}

static void reset_counters(void) {
  g_nack_calls = 0;
  g_rams_r_calls = 0;
  g_rams_t_calls = 0;
  g_malformed_calls = 0;
}

START_TEST(nack_is_parsed_via_builder_round_trip) {
  unsigned char buf[64];
  rtcp_nack_entry_t entries[1] = {{0x1234, 0}};
  size_t n = rtcp_build_ff(0xAAAA, 0xBBBB, entries, 1, buf, sizeof buf);

  reset_counters();
  ck_assert_uint_gt(n, 0u);
  rtcp_parse(buf, n, &(rtcp_cbs_t){.nack_cb = nack_cb});
  ck_assert_int_eq(g_nack_calls, 1);
  ck_assert_uint_eq(g_nack.sender_ssrc, 0xAAAAu);
  ck_assert_uint_eq(g_nack.media_ssrc, 0xBBBBu);
  ck_assert_uint_eq(g_nack.entry_count, 1u);
  ck_assert_uint_eq(g_nack.entry[0].pid, 0x1234u);
}
END_TEST

START_TEST(rams_r_ignore_media_ssrc_flag_is_parsed) {
  unsigned char pkt[64];
  unsigned char fci[4] = {1, 0, 0, 0}; /* type=1 (ignore media ssrc), length=0 */
  size_t n = build_rams(pkt, RTCP_SFMT_RAMS_R, 0x1111, 0x2222, fci, sizeof fci);

  reset_counters();
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.rams_r_cb = rams_r_cb});
  ck_assert_int_eq(g_rams_r_calls, 1);
  ck_assert_int_eq(g_rams_r.ignore_media_ssrc, 1);
  ck_assert_uint_eq(g_rams_r.sender_ssrc, 0x1111u);
  ck_assert_uint_eq(g_rams_r.media_ssrc, 0x2222u);
}
END_TEST

START_TEST(rams_t_with_no_tlv_is_parsed) {
  unsigned char pkt[64];
  size_t n = build_rams(pkt, RTCP_SFMT_RAMS_T, 0x3333, 0x4444, NULL, 0);

  reset_counters();
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.rams_t_cb = rams_t_cb});
  ck_assert_int_eq(g_rams_t_calls, 1);
  ck_assert_int_eq(g_rams_t.has_first_mc_seqnum, 0);
  ck_assert_int_eq(g_malformed_calls, 0);
}
END_TEST

START_TEST(rams_t_with_seqnum_tlv_is_parsed) {
  unsigned char pkt[64];
  unsigned char fci[8];
  fci[0] = 61; /* type: extended seqnum of first mc packet */
  fci[1] = 0;
  wr16(fci + 2, 4); /* length 4 */
  wr32(fci + 4, 0xDEADBEEFu);
  size_t n = build_rams(pkt, RTCP_SFMT_RAMS_T, 0x5555, 0x6666, fci, sizeof fci);

  reset_counters();
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.rams_t_cb = rams_t_cb, .malformed_cb = malformed_cb});
  ck_assert_int_eq(g_rams_t_calls, 1);
  ck_assert_int_eq(g_rams_t.has_first_mc_seqnum, 1);
  ck_assert_uint_eq(g_rams_t.first_mc_seqnum, 0xDEADBEEFu);
  ck_assert_int_eq(g_malformed_calls, 0);
}
END_TEST

START_TEST(malformed_rams_t_tlv_reports_via_malformed_cb_and_still_delivers_partial) {
  unsigned char pkt[64];
  unsigned char fci[4];
  size_t n;

  fci[0] = 61;
  fci[1] = 0;
  wr16(fci + 2, 100); /* claims 100 bytes of value, only 0 follow: malformed */
  n = build_rams(pkt, RTCP_SFMT_RAMS_T, 0x7777, 0x8888, fci, sizeof fci);

  reset_counters();
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.rams_t_cb = rams_t_cb, .malformed_cb = malformed_cb});
  ck_assert_int_eq(g_rams_t_calls, 1); /* still delivered, no seqnum found before corruption */
  ck_assert_int_eq(g_rams_t.has_first_mc_seqnum, 0);
  ck_assert_int_eq(g_malformed_calls, 1);
  ck_assert_uint_eq(g_malformed_sfmt, RTCP_SFMT_RAMS_T);
  ck_assert_uint_eq(g_malformed_sender_ssrc, 0x7777u);
  ck_assert_uint_eq(g_malformed_media_ssrc, 0x8888u);
}
END_TEST

START_TEST(malformed_cb_not_called_for_well_formed_rams_t) {
  unsigned char pkt[64];
  size_t n = build_rams(pkt, RTCP_SFMT_RAMS_T, 0x9999, 0xAAAA, NULL, 0);

  reset_counters();
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.rams_t_cb = rams_t_cb, .malformed_cb = malformed_cb});
  ck_assert_int_eq(g_malformed_calls, 0);
}
END_TEST

START_TEST(malformed_cb_not_called_for_rams_r) {
  unsigned char pkt[64];
  unsigned char fci[4];
  size_t n;

  fci[0] = 2; /* min buffer fill */
  fci[1] = 0;
  wr16(fci + 2, 100); /* also malformed, but RAMS-R never reports via malformed_cb (400 handled elsewhere) */
  n = build_rams(pkt, RTCP_SFMT_RAMS_R, 0xBBBB, 0xCCCC, fci, sizeof fci);

  reset_counters();
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.rams_r_cb = rams_r_cb, .malformed_cb = malformed_cb});
  ck_assert_int_eq(g_malformed_calls, 0);
  ck_assert_int_eq(g_rams_r_calls, 1);
}
END_TEST

START_TEST(both_cb_and_malformed_cb_null_does_not_crash) {
  unsigned char pkt[64];
  size_t n = build_rams(pkt, RTCP_SFMT_RAMS_T, 1, 2, NULL, 0);
  rtcp_parse(pkt, n, &(rtcp_cbs_t){0});
}
END_TEST

static int g_rams_i_calls;
static rtcp_rams_i_t g_rams_i;

static void rams_i_cb(const rtcp_rams_i_t *info, void *user) {
  (void)user;
  g_rams_i_calls++;
  g_rams_i = *info;
}

START_TEST(rams_i_is_parsed_via_builder_round_trip) {
  unsigned char buf[64];
  rtcp_rams_i_tlvs_t tlvs;
  size_t n;

  memset(&tlvs, 0, sizeof tlvs);
  tlvs.has_earliest_join_time = 1;
  tlvs.earliest_join_time_ms = 0;
  tlvs.has_burst_duration = 1;
  tlvs.burst_duration_ms = 8000;

  n = rtcp_build_rams_i(0x77777777u, 0x88888888u, 5, 200, &tlvs, buf, sizeof buf);
  ck_assert_uint_gt(n, 0u);

  g_rams_i_calls = 0;
  rtcp_parse(buf, n, &(rtcp_cbs_t){.rams_i_cb = rams_i_cb});

  ck_assert_int_eq(g_rams_i_calls, 1);
  ck_assert_uint_eq(g_rams_i.sender_ssrc, 0x77777777u);
  ck_assert_uint_eq(g_rams_i.media_ssrc, 0x88888888u);
  ck_assert_uint_eq(g_rams_i.msn, 5u);
  ck_assert_uint_eq(g_rams_i.response, 200u);
  ck_assert_int_eq(g_rams_i.has_earliest_join_time, 1);
  ck_assert_int_eq(g_rams_i.has_burst_duration, 1);
  ck_assert_uint_eq(g_rams_i.burst_duration_ms, 8000u);
}
END_TEST

static rtcp_sdes_t g_sdes[4];
static int g_sdes_calls;

static void sdes_cb(const rtcp_sdes_t *sdes, void *user) {
  (void)user;
  if (g_sdes_calls < 4)
    g_sdes[g_sdes_calls] = *sdes;
  g_sdes_calls++;
}

static size_t build_hdr_pkt(unsigned char *out, unsigned count, unsigned pt, const unsigned char *body, size_t body_len) {
  size_t total = 4 + body_len;

  out[0] = (unsigned char)((2 << 6) | count);
  out[1] = (unsigned char)pt;
  wr16(out + 2, (uint16_t)(total / 4 - 1));
  if (body_len)
    memcpy(out + 4, body, body_len);
  return total;
}

static size_t build_sdes(unsigned char *out, unsigned sc, const unsigned char *body, size_t body_len) {
  return build_hdr_pkt(out, sc, 202, body, body_len);
}

static size_t put_cname_chunk(unsigned char *out, uint32_t ssrc, const char *cname, size_t cname_len, int terminate) {
  size_t off = 0;

  wr32(out, ssrc);
  off = 4;
  out[off++] = 1;
  out[off++] = (unsigned char)cname_len;
  memcpy(out + off, cname, cname_len);
  off += cname_len;
  if (terminate)
    out[off++] = 0;
  while (off % 4)
    out[off++] = 0;
  return off;
}

START_TEST(sdes_valid_cname_is_delivered) {
  unsigned char body[32];
  unsigned char pkt[64];
  size_t blen = put_cname_chunk(body, 0xA1B2C3D4u, "host1", 5, 1);
  size_t n = build_sdes(pkt, 1, body, blen);

  g_sdes_calls = 0;
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.sdes_cb = sdes_cb});
  ck_assert_int_eq(g_sdes_calls, 1);
  ck_assert_uint_eq(g_sdes[0].ssrc, 0xA1B2C3D4u);
  ck_assert_uint_eq(g_sdes[0].cname_len, 5u);
  ck_assert_str_eq(g_sdes[0].cname, "host1");
}
END_TEST

START_TEST(sdes_long_cname_is_clipped) {
  unsigned char body[160];
  unsigned char pkt[200];
  char cname[100];
  size_t blen;
  size_t n;

  memset(cname, 'x', sizeof cname);
  blen = put_cname_chunk(body, 7, cname, sizeof cname, 1);
  n = build_sdes(pkt, 1, body, blen);

  g_sdes_calls = 0;
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.sdes_cb = sdes_cb});
  ck_assert_int_eq(g_sdes_calls, 1);
  ck_assert_uint_eq(g_sdes[0].cname_len, (size_t)(RTCP_CNAME_MAX - 1));
  ck_assert_uint_eq(strlen(g_sdes[0].cname), (size_t)(RTCP_CNAME_MAX - 1));
}
END_TEST

START_TEST(sdes_item_overrun_without_cname_yields_nothing) {
  unsigned char body[8] = {0, 0, 0, 9, 1, 200, 'a', 'b'};
  unsigned char pkt[32];
  size_t n = build_sdes(pkt, 1, body, sizeof body);

  g_sdes_calls = 0;
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.sdes_cb = sdes_cb});
  ck_assert_int_eq(g_sdes_calls, 0);
}
END_TEST

START_TEST(sdes_item_overrun_after_cname_keeps_cname) {
  unsigned char body[16] = {0, 0, 0, 9, 1, 2, 'o', 'k', 2, 200, 'a', 'b'};
  unsigned char pkt[32];
  size_t n = build_sdes(pkt, 1, body, sizeof body);

  g_sdes_calls = 0;
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.sdes_cb = sdes_cb});
  ck_assert_int_eq(g_sdes_calls, 1);
  ck_assert_str_eq(g_sdes[0].cname, "ok");
}
END_TEST

START_TEST(sdes_chunk_without_cname_is_skipped) {
  unsigned char body[12] = {0, 0, 0, 5, 2, 3, 'f', 'o', 'o', 0, 0, 0};
  unsigned char pkt[32];
  size_t n = build_sdes(pkt, 1, body, sizeof body);

  g_sdes_calls = 0;
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.sdes_cb = sdes_cb});
  ck_assert_int_eq(g_sdes_calls, 0);
}
END_TEST

START_TEST(sdes_multiple_chunks_each_delivered) {
  unsigned char body[64];
  unsigned char pkt[96];
  size_t blen = put_cname_chunk(body, 0x11, "aaa", 3, 1);
  size_t n;

  blen += put_cname_chunk(body + blen, 0x22, "bbbbbb", 6, 1);
  n = build_sdes(pkt, 2, body, blen);

  g_sdes_calls = 0;
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.sdes_cb = sdes_cb});
  ck_assert_int_eq(g_sdes_calls, 2);
  ck_assert_uint_eq(g_sdes[0].ssrc, 0x11u);
  ck_assert_str_eq(g_sdes[0].cname, "aaa");
  ck_assert_uint_eq(g_sdes[1].ssrc, 0x22u);
  ck_assert_str_eq(g_sdes[1].cname, "bbbbbb");
}
END_TEST

START_TEST(sdes_sc_larger_than_chunks_present_stops_cleanly) {
  unsigned char body[32];
  unsigned char pkt[64];
  size_t blen = put_cname_chunk(body, 0x33, "only", 4, 1);
  size_t n = build_sdes(pkt, 5, body, blen);

  g_sdes_calls = 0;
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.sdes_cb = sdes_cb});
  ck_assert_int_eq(g_sdes_calls, 1);
  ck_assert_uint_eq(g_sdes[0].ssrc, 0x33u);
}
END_TEST

START_TEST(sdes_missing_terminator_is_tolerated) {
  unsigned char body[32];
  unsigned char pkt[64];
  size_t blen = put_cname_chunk(body, 0x44, "abcdef", 6, 0);
  size_t n = build_sdes(pkt, 1, body, blen);

  ck_assert_uint_eq(blen, 12u);
  g_sdes_calls = 0;
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.sdes_cb = sdes_cb});
  ck_assert_int_eq(g_sdes_calls, 1);
  ck_assert_str_eq(g_sdes[0].cname, "abcdef");
}
END_TEST

START_TEST(sdes_first_cname_item_wins) {
  unsigned char body[16] = {0, 0, 0, 6, 1, 1, 'a', 1, 1, 'b', 0, 0};
  unsigned char pkt[32];
  size_t n = build_sdes(pkt, 1, body, sizeof body - 4);

  g_sdes_calls = 0;
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.sdes_cb = sdes_cb});
  ck_assert_int_eq(g_sdes_calls, 1);
  ck_assert_str_eq(g_sdes[0].cname, "a");
}
END_TEST

START_TEST(sdes_without_callback_does_not_crash) {
  unsigned char body[32];
  unsigned char pkt[64];
  size_t blen = put_cname_chunk(body, 1, "x", 1, 1);
  size_t n = build_sdes(pkt, 1, body, blen);

  rtcp_parse(pkt, n, &(rtcp_cbs_t){0});
}
END_TEST

START_TEST(nack_fci_exactly_at_cap_is_not_truncated) {
  unsigned char pkt[12 + 4 * RTCP_NACK_MAX_ENTRIES];
  unsigned char body[8 + 4 * RTCP_NACK_MAX_ENTRIES];
  size_t i;
  size_t n;

  memset(body, 0, sizeof body);
  for (i = 0; i < RTCP_NACK_MAX_ENTRIES; i++)
    wr16(body + 8 + 4 * i, (uint16_t)(100 + i));
  n = build_hdr_pkt(pkt, 1, 205, body, sizeof body);

  reset_counters();
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.nack_cb = nack_cb});
  ck_assert_int_eq(g_nack_calls, 1);
  ck_assert_uint_eq(g_nack.entry_count, (size_t)RTCP_NACK_MAX_ENTRIES);
  ck_assert_int_eq(g_nack.truncated, 0);
}
END_TEST

START_TEST(nack_fci_over_cap_sets_truncated) {
  unsigned char pkt[16 + 4 * RTCP_NACK_MAX_ENTRIES];
  unsigned char body[12 + 4 * RTCP_NACK_MAX_ENTRIES];
  size_t i;
  size_t n;

  memset(body, 0, sizeof body);
  for (i = 0; i < RTCP_NACK_MAX_ENTRIES + 1; i++)
    wr16(body + 8 + 4 * i, (uint16_t)(100 + i));
  n = build_hdr_pkt(pkt, 1, 205, body, sizeof body);

  reset_counters();
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.nack_cb = nack_cb});
  ck_assert_int_eq(g_nack_calls, 1);
  ck_assert_uint_eq(g_nack.entry_count, (size_t)RTCP_NACK_MAX_ENTRIES);
  ck_assert_int_eq(g_nack.truncated, 1);
  ck_assert_uint_eq(g_nack.entry[RTCP_NACK_MAX_ENTRIES - 1].pid, (uint16_t)(100 + RTCP_NACK_MAX_ENTRIES - 1));
}
END_TEST

START_TEST(nack_without_fci_entries_gives_no_callback) {
  unsigned char body[8] = {0};
  unsigned char pkt[16];
  size_t n = build_hdr_pkt(pkt, 1, 205, body, sizeof body);

  reset_counters();
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.nack_cb = nack_cb});
  ck_assert_int_eq(g_nack_calls, 0);
}
END_TEST

START_TEST(nack_without_callback_does_not_crash) {
  unsigned char body[12] = {0};
  unsigned char pkt[20];
  size_t n = build_hdr_pkt(pkt, 1, 205, body, sizeof body);

  rtcp_parse(pkt, n, &(rtcp_cbs_t){0});
}
END_TEST

START_TEST(rams_i_all_tlvs_are_parsed) {
  unsigned char fci[4 + 8 + 8 + 8 + 8 + 12];
  unsigned char pkt[96];
  size_t off = 0;
  size_t n;

  fci[off] = 31;
  fci[off + 1] = 0;
  wr16(fci + off + 2, 4);
  wr32(fci + off + 4, 0xCAFEF00Du);
  off += 8;
  fci[off] = 32;
  fci[off + 1] = 0;
  wr16(fci + off + 2, 2);
  wr16(fci + off + 4, 0xBEEF);
  fci[off + 6] = 0;
  fci[off + 7] = 0;
  off += 8;
  fci[off] = 33;
  fci[off + 1] = 0;
  wr16(fci + off + 2, 4);
  wr32(fci + off + 4, 1500);
  off += 8;
  fci[off] = 34;
  fci[off + 1] = 0;
  wr16(fci + off + 2, 4);
  wr32(fci + off + 4, 2500);
  off += 8;
  fci[off] = 35;
  fci[off + 1] = 0;
  wr16(fci + off + 2, 8);
  wr32(fci + off + 4, 0x00000001u);
  wr32(fci + off + 8, 0x00000002u);
  off += 12;
  n = build_rams(pkt, 2, 0x10, 0x20, fci, off);
  pkt[13] = 9;
  wr16(pkt + 14, 250);

  g_rams_i_calls = 0;
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.rams_i_cb = rams_i_cb});
  ck_assert_int_eq(g_rams_i_calls, 1);
  ck_assert_uint_eq(g_rams_i.msn, 9u);
  ck_assert_uint_eq(g_rams_i.response, 250u);
  ck_assert_int_eq(g_rams_i.has_media_ssrc_tlv, 1);
  ck_assert_uint_eq(g_rams_i.media_ssrc_tlv, 0xCAFEF00Du);
  ck_assert_int_eq(g_rams_i.has_first_packet_seqnum, 1);
  ck_assert_uint_eq(g_rams_i.first_packet_seqnum, 0xBEEFu);
  ck_assert_int_eq(g_rams_i.has_earliest_join_time, 1);
  ck_assert_uint_eq(g_rams_i.earliest_join_time_ms, 1500u);
  ck_assert_int_eq(g_rams_i.has_burst_duration, 1);
  ck_assert_uint_eq(g_rams_i.burst_duration_ms, 2500u);
  ck_assert_int_eq(g_rams_i.has_max_transmit_bitrate, 1);
  ck_assert_uint_eq(g_rams_i.max_transmit_bitrate_bps, 0x100000002ull);
}
END_TEST

START_TEST(rams_i_unknown_and_short_and_overrun_tlvs_are_tolerated) {
  unsigned char fci[4 + 4 + 8 + 4];
  unsigned char pkt[64];
  size_t n;

  memset(fci, 0, sizeof fci);
  fci[0] = 99;
  wr16(fci + 2, 0);
  fci[4] = 33;
  wr16(fci + 6, 2);
  fci[12] = 34;
  wr16(fci + 14, 400);
  n = build_rams(pkt, 2, 1, 2, fci, sizeof fci);

  g_rams_i_calls = 0;
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.rams_i_cb = rams_i_cb});
  ck_assert_int_eq(g_rams_i_calls, 1);
  ck_assert_int_eq(g_rams_i.has_earliest_join_time, 0);
  ck_assert_int_eq(g_rams_i.has_burst_duration, 0);
}
END_TEST

START_TEST(rams_i_without_callback_does_not_crash) {
  unsigned char pkt[32];
  size_t n = build_rams(pkt, 2, 1, 2, NULL, 0);

  rtcp_parse(pkt, n, &(rtcp_cbs_t){0});
}
END_TEST

START_TEST(rams_r_all_tlvs_and_unknown_type_are_parsed) {
  unsigned char fci[8 + 8 + 8 + 12];
  unsigned char pkt[96];
  size_t n;

  memset(fci, 0, sizeof fci);
  fci[0] = 77;
  wr16(fci + 2, 1);
  fci[8] = 2;
  wr16(fci + 10, 4);
  wr32(fci + 12, 111);
  fci[16] = 3;
  wr16(fci + 18, 4);
  wr32(fci + 20, 222);
  fci[24] = 4;
  wr16(fci + 26, 8);
  wr32(fci + 28, 1);
  wr32(fci + 32, 3);
  n = build_rams(pkt, RTCP_SFMT_RAMS_R, 5, 6, fci, sizeof fci);

  reset_counters();
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.rams_r_cb = rams_r_cb});
  ck_assert_int_eq(g_rams_r_calls, 1);
  ck_assert_int_eq(g_rams_r.has_min_buffer_fill, 1);
  ck_assert_uint_eq(g_rams_r.min_buffer_fill_ms, 111u);
  ck_assert_int_eq(g_rams_r.has_max_buffer_fill, 1);
  ck_assert_uint_eq(g_rams_r.max_buffer_fill_ms, 222u);
  ck_assert_int_eq(g_rams_r.has_max_bitrate, 1);
  ck_assert_uint_eq(g_rams_r.max_bitrate_bps, 0x100000003ull);
  ck_assert_int_eq(g_rams_r.ignore_media_ssrc, 0);
}
END_TEST

START_TEST(rams_r_short_value_tlvs_are_ignored) {
  unsigned char fci[12];
  unsigned char pkt[48];
  size_t n;

  memset(fci, 0, sizeof fci);
  fci[0] = 2;
  wr16(fci + 2, 2);
  fci[4] = 4;
  wr16(fci + 6, 4);
  n = build_rams(pkt, RTCP_SFMT_RAMS_R, 5, 6, fci, sizeof fci);
  fci[8] = 3;
  wr16(fci + 10, 2);

  reset_counters();
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.rams_r_cb = rams_r_cb});
  ck_assert_int_eq(g_rams_r_calls, 1);
  ck_assert_int_eq(g_rams_r.has_min_buffer_fill, 0);
  ck_assert_int_eq(g_rams_r.has_max_bitrate, 0);
}
END_TEST

START_TEST(rams_r_without_callback_does_not_crash) {
  unsigned char pkt[32];
  size_t n = build_rams(pkt, RTCP_SFMT_RAMS_R, 1, 2, NULL, 0);

  rtcp_parse(pkt, n, &(rtcp_cbs_t){0});
}
END_TEST

START_TEST(rams_t_unknown_tlv_is_tolerated_and_not_malformed) {
  unsigned char fci[8];
  unsigned char pkt[32];
  size_t n;

  memset(fci, 0, sizeof fci);
  fci[0] = 99;
  wr16(fci + 2, 4);
  n = build_rams(pkt, RTCP_SFMT_RAMS_T, 1, 2, fci, sizeof fci);

  reset_counters();
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.rams_t_cb = rams_t_cb, .malformed_cb = malformed_cb});
  ck_assert_int_eq(g_rams_t_calls, 1);
  ck_assert_int_eq(g_rams_t.has_first_mc_seqnum, 0);
  ck_assert_int_eq(g_malformed_calls, 0);
}
END_TEST

START_TEST(rams_t_malformed_without_main_callback_still_reports) {
  unsigned char fci[4];
  unsigned char pkt[32];
  size_t n;

  fci[0] = 61;
  fci[1] = 0;
  wr16(fci + 2, 100);
  n = build_rams(pkt, RTCP_SFMT_RAMS_T, 1, 2, fci, sizeof fci);

  reset_counters();
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.malformed_cb = malformed_cb});
  ck_assert_int_eq(g_malformed_calls, 1);
}
END_TEST

START_TEST(rams_unknown_sfmt_is_skipped) {
  unsigned char pkt[32];
  size_t n = build_rams(pkt, 9, 1, 2, NULL, 0);

  reset_counters();
  g_rams_i_calls = 0;
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.rams_r_cb = rams_r_cb, .rams_i_cb = rams_i_cb, .rams_t_cb = rams_t_cb});
  ck_assert_int_eq(g_rams_r_calls + g_rams_t_calls + g_rams_i_calls, 0);
}
END_TEST

START_TEST(rtpfb_unknown_fmt_is_skipped) {
  unsigned char body[12] = {0};
  unsigned char pkt[32];
  size_t n = build_hdr_pkt(pkt, 15, 205, body, sizeof body);

  reset_counters();
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.nack_cb = nack_cb, .rams_r_cb = rams_r_cb});
  ck_assert_int_eq(g_nack_calls + g_rams_r_calls, 0);
}
END_TEST

START_TEST(short_rams_packet_is_guarded) {
  unsigned char body[8] = {0};
  unsigned char pkt[32];
  size_t n = build_hdr_pkt(pkt, RTCP_FMT_RAMS, 205, body, sizeof body);

  reset_counters();
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.rams_r_cb = rams_r_cb, .rams_t_cb = rams_t_cb, .malformed_cb = malformed_cb});
  ck_assert_int_eq(g_rams_r_calls + g_rams_t_calls + g_malformed_calls, 0);
}
END_TEST

START_TEST(short_nack_packet_is_guarded) {
  unsigned char body[4] = {0};
  unsigned char pkt[32];
  size_t n = build_hdr_pkt(pkt, 1, 205, body, sizeof body);

  reset_counters();
  rtcp_parse(pkt, n, &(rtcp_cbs_t){.nack_cb = nack_cb});
  ck_assert_int_eq(g_nack_calls, 0);
}
END_TEST

START_TEST(framing_guards_stop_parsing) {
  unsigned char body[16] = {0};
  unsigned char pkt[64];
  size_t n = build_hdr_pkt(pkt, 1, 205, body, sizeof body);
  rtcp_cbs_t cbs = {.nack_cb = nack_cb};

  body[8] = 0;
  wr16(body + 10, 7);
  n = build_hdr_pkt(pkt, 1, 205, body, sizeof body);
  reset_counters();
  rtcp_parse(pkt, 3, &cbs);
  rtcp_parse(pkt, n - 4, &cbs);
  ck_assert_int_eq(g_nack_calls, 0);
  pkt[0] = (unsigned char)((1 << 6) | 1);
  rtcp_parse(pkt, n, &cbs);
  ck_assert_int_eq(g_nack_calls, 0);
  pkt[0] = (unsigned char)((2 << 6) | 1);
  rtcp_parse(pkt, n, &cbs);
  ck_assert_int_eq(g_nack_calls, 1);
}
END_TEST

START_TEST(compound_packet_skips_sr_and_delivers_following_sdes) {
  unsigned char sr[8] = {0x80, 200, 0, 1, 0, 0, 0, 1};
  unsigned char body[32];
  unsigned char pkt[64];
  size_t blen = put_cname_chunk(body, 0x55, "cmp", 3, 1);
  size_t n = build_sdes(pkt + sizeof sr, 1, body, blen);

  memcpy(pkt, sr, sizeof sr);
  g_sdes_calls = 0;
  rtcp_parse(pkt, sizeof sr + n, &(rtcp_cbs_t){.sdes_cb = sdes_cb});
  ck_assert_int_eq(g_sdes_calls, 1);
  ck_assert_uint_eq(g_sdes[0].ssrc, 0x55u);
}
END_TEST

static Suite *rtcp_suite(void) {
  Suite *s = suite_create("rtcp");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, nack_is_parsed_via_builder_round_trip);
  tcase_add_test(tc, rams_r_ignore_media_ssrc_flag_is_parsed);
  tcase_add_test(tc, rams_t_with_no_tlv_is_parsed);
  tcase_add_test(tc, rams_t_with_seqnum_tlv_is_parsed);
  tcase_add_test(tc, malformed_rams_t_tlv_reports_via_malformed_cb_and_still_delivers_partial);
  tcase_add_test(tc, malformed_cb_not_called_for_well_formed_rams_t);
  tcase_add_test(tc, malformed_cb_not_called_for_rams_r);
  tcase_add_test(tc, both_cb_and_malformed_cb_null_does_not_crash);
  tcase_add_test(tc, rams_i_is_parsed_via_builder_round_trip);
  tcase_add_test(tc, sdes_valid_cname_is_delivered);
  tcase_add_test(tc, sdes_long_cname_is_clipped);
  tcase_add_test(tc, sdes_item_overrun_without_cname_yields_nothing);
  tcase_add_test(tc, sdes_item_overrun_after_cname_keeps_cname);
  tcase_add_test(tc, sdes_chunk_without_cname_is_skipped);
  tcase_add_test(tc, sdes_multiple_chunks_each_delivered);
  tcase_add_test(tc, sdes_sc_larger_than_chunks_present_stops_cleanly);
  tcase_add_test(tc, sdes_missing_terminator_is_tolerated);
  tcase_add_test(tc, sdes_first_cname_item_wins);
  tcase_add_test(tc, sdes_without_callback_does_not_crash);
  tcase_add_test(tc, nack_fci_exactly_at_cap_is_not_truncated);
  tcase_add_test(tc, nack_fci_over_cap_sets_truncated);
  tcase_add_test(tc, nack_without_fci_entries_gives_no_callback);
  tcase_add_test(tc, nack_without_callback_does_not_crash);
  tcase_add_test(tc, rams_i_all_tlvs_are_parsed);
  tcase_add_test(tc, rams_i_unknown_and_short_and_overrun_tlvs_are_tolerated);
  tcase_add_test(tc, rams_i_without_callback_does_not_crash);
  tcase_add_test(tc, rams_r_all_tlvs_and_unknown_type_are_parsed);
  tcase_add_test(tc, rams_r_short_value_tlvs_are_ignored);
  tcase_add_test(tc, rams_r_without_callback_does_not_crash);
  tcase_add_test(tc, rams_t_unknown_tlv_is_tolerated_and_not_malformed);
  tcase_add_test(tc, rams_t_malformed_without_main_callback_still_reports);
  tcase_add_test(tc, rams_unknown_sfmt_is_skipped);
  tcase_add_test(tc, rtpfb_unknown_fmt_is_skipped);
  tcase_add_test(tc, short_rams_packet_is_guarded);
  tcase_add_test(tc, short_nack_packet_is_guarded);
  tcase_add_test(tc, framing_guards_stop_parsing);
  tcase_add_test(tc, compound_packet_skips_sr_and_delivers_following_sdes);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(rtcp_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
