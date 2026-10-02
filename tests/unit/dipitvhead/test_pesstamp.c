/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <string.h>

#include "dipitvhead/mux/pesstamp.h"

#define TS_MOD ((uint64_t)1 << 33)
#define ESCR_MOD (TS_MOD * 300ULL)

typedef struct {
  unsigned stream_id;
  int af_len;
  int has_pts;
  int has_dts;
  int has_escr;
  uint64_t pts;
  uint64_t dts;
  uint64_t escr27;
  unsigned hdr_extra;
  unsigned start_override;
} spec_t;

static void enc_ts(unsigned char *p, unsigned prefix, uint64_t v) {
  p[0] = (unsigned char)((prefix << 4) | ((v >> 29) & 0x0E) | 1);
  p[1] = (unsigned char)(v >> 22);
  p[2] = (unsigned char)(((v >> 14) & 0xFE) | 1);
  p[3] = (unsigned char)(v >> 7);
  p[4] = (unsigned char)(((v << 1) & 0xFE) | 1);
}

static void enc_escr(unsigned char *p, uint64_t v27) {
  uint64_t base = v27 / 300;
  uint64_t ext = v27 % 300;
  uint64_t w = (0x3ULL << 46) | (((base >> 30) & 0x7) << 43) | (1ULL << 42) | (((base >> 15) & 0x7FFF) << 27) | (1ULL << 26) |
               ((base & 0x7FFF) << 11) | (1ULL << 10) | (ext << 1) | 1;
  for (int i = 0; i < 6; i++) p[i] = (unsigned char)(w >> (8 * (5 - i)));
}

static void build(unsigned char pkt[188], const spec_t *s) {
  unsigned start = 4;
  unsigned char hdr[64];
  unsigned char *f = hdr + 9;
  unsigned flags = 0;
  unsigned fields = 0;
  unsigned room;
  memset(pkt, 0xA5, 188);
  pkt[0] = 0x47;
  pkt[1] = 0x41;
  pkt[2] = 0x00;
  pkt[3] = s->af_len >= 0 ? 0x31 : 0x11;
  if (s->af_len >= 0) {
    pkt[4] = (unsigned char)s->af_len;
    if (s->af_len > 0) pkt[5] = 0;
    for (int i = 1; i < s->af_len; i++) pkt[5 + i] = 0xFF;
    start += 1 + (unsigned)s->af_len;
  }
  if (s->start_override) {
    unsigned want = s->start_override;
    pkt[3] = 0x31;
    pkt[4] = (unsigned char)(want - 5);
    pkt[5] = 0;
    for (unsigned i = 6; i < want; i++) pkt[i] = 0xFF;
    start = want;
  }
  memset(hdr, 0xA5, sizeof hdr);
  hdr[0] = 0;
  hdr[1] = 0;
  hdr[2] = 1;
  hdr[3] = (unsigned char)s->stream_id;
  hdr[4] = 0;
  hdr[5] = 0;
  hdr[6] = 0x80;
  if (s->has_pts && s->has_dts) flags = 0xC0;
  else if (s->has_pts) flags = 0x80;
  if (s->has_escr) flags |= 0x20;
  if (s->has_pts) {
    enc_ts(f, s->has_dts ? 3 : 2, s->pts);
    f += 5;
    fields += 5;
  }
  if (s->has_dts) {
    enc_ts(f, 1, s->dts);
    f += 5;
    fields += 5;
  }
  if (s->has_escr) {
    enc_escr(f, s->escr27);
    fields += 6;
  }
  hdr[7] = (unsigned char)flags;
  hdr[8] = (unsigned char)(fields + s->hdr_extra);
  room = 188 - start;
  memcpy(pkt + start, hdr, room < sizeof hdr ? room : sizeof hdr);
}

START_TEST(reads_pts_only) {
  spec_t s = {.stream_id = 0xE0, .af_len = -1, .has_pts = 1, .pts = 0x123456789ULL & (TS_MOD - 1)};
  unsigned char pkt[188];
  pes_stamp_t st;
  build(pkt, &s);
  ck_assert_int_eq(pesstamp_read(pkt, &st), PESSTAMP_FOUND);
  ck_assert_int_eq(st.has_pts, 1);
  ck_assert_int_eq(st.has_dts, 0);
  ck_assert_int_eq(st.has_escr, 0);
  ck_assert_uint_eq(st.pts, s.pts);
}
END_TEST

START_TEST(reads_pts_dts_and_escr) {
  spec_t s = {.stream_id = 0xC0, .af_len = -1, .has_pts = 1, .has_dts = 1, .has_escr = 1,
              .pts = 8000000000ULL, .dts = 7999990000ULL, .escr27 = 123456789012ULL};
  unsigned char pkt[188];
  pes_stamp_t st;
  build(pkt, &s);
  ck_assert_int_eq(pesstamp_read(pkt, &st), PESSTAMP_FOUND);
  ck_assert_uint_eq(st.pts, s.pts);
  ck_assert_uint_eq(st.dts, s.dts);
  ck_assert_uint_eq(st.escr27, s.escr27);
}
END_TEST

START_TEST(reads_through_an_adaptation_field) {
  spec_t s = {.stream_id = 0xE0, .af_len = 30, .has_pts = 1, .has_dts = 1, .pts = 1000, .dts = 900};
  unsigned char pkt[188];
  pes_stamp_t st;
  build(pkt, &s);
  ck_assert_int_eq(pesstamp_read(pkt, &st), PESSTAMP_FOUND);
  ck_assert_uint_eq(st.pts, 1000u);
  ck_assert_uint_eq(st.dts, 900u);
}
END_TEST

START_TEST(shift_matches_a_fresh_encoding_of_the_shifted_values) {
  spec_t s = {.stream_id = 0xE0, .af_len = 7, .has_pts = 1, .has_dts = 1, .has_escr = 1, .hdr_extra = 3,
              .pts = 5000000, .dts = 4990000, .escr27 = 3000000000ULL};
  spec_t e = s;
  unsigned char pkt[188];
  unsigned char want[188];
  build(pkt, &s);
  e.pts += 90000;
  e.dts += 90000;
  e.escr27 += 90000ULL * 300;
  build(want, &e);
  ck_assert_int_eq(pesstamp_shift(pkt, 90000), PESSTAMP_FOUND);
  ck_assert_mem_eq(pkt, want, 188);
}
END_TEST

START_TEST(shift_wraps_forward_and_backward) {
  spec_t s = {.stream_id = 0xE0, .af_len = -1, .has_pts = 1, .has_dts = 1, .pts = TS_MOD - 10, .dts = 5};
  unsigned char pkt[188];
  pes_stamp_t st;
  build(pkt, &s);
  ck_assert_int_eq(pesstamp_shift(pkt, 20), PESSTAMP_FOUND);
  ck_assert_int_eq(pesstamp_read(pkt, &st), PESSTAMP_FOUND);
  ck_assert_uint_eq(st.pts, 10u);
  ck_assert_uint_eq(st.dts, 25u);
  ck_assert_int_eq(pesstamp_shift(pkt, -30), PESSTAMP_FOUND);
  ck_assert_int_eq(pesstamp_read(pkt, &st), PESSTAMP_FOUND);
  ck_assert_uint_eq(st.pts, TS_MOD - 20);
  ck_assert_uint_eq(st.dts, TS_MOD - 5);
}
END_TEST

START_TEST(shift_handles_deltas_beyond_one_wrap) {
  spec_t s = {.stream_id = 0xE0, .af_len = -1, .has_pts = 1, .pts = 100};
  unsigned char pkt[188];
  pes_stamp_t st;
  build(pkt, &s);
  ck_assert_int_eq(pesstamp_shift(pkt, (int64_t)TS_MOD * 3 + 7), PESSTAMP_FOUND);
  ck_assert_int_eq(pesstamp_read(pkt, &st), PESSTAMP_FOUND);
  ck_assert_uint_eq(st.pts, 107u);
}
END_TEST

START_TEST(escr_shift_wraps_at_its_own_modulus) {
  spec_t s = {.stream_id = 0xE0, .af_len = -1, .has_pts = 1, .has_escr = 1, .pts = 1, .escr27 = ESCR_MOD - 1};
  unsigned char pkt[188];
  pes_stamp_t st;
  build(pkt, &s);
  ck_assert_int_eq(pesstamp_shift(pkt, 1), PESSTAMP_FOUND);
  ck_assert_int_eq(pesstamp_read(pkt, &st), PESSTAMP_FOUND);
  ck_assert_uint_eq(st.escr27, 299u);
  ck_assert_uint_eq(st.pts, 2u);
}
END_TEST

START_TEST(shift_leaves_the_payload_and_stuffing_alone) {
  spec_t s = {.stream_id = 0xE0, .af_len = -1, .has_pts = 1, .hdr_extra = 4, .pts = 77};
  unsigned char pkt[188];
  unsigned char orig[188];
  build(pkt, &s);
  memcpy(orig, pkt, sizeof pkt);
  ck_assert_int_eq(pesstamp_shift(pkt, 1234), PESSTAMP_FOUND);
  ck_assert_mem_eq(pkt, orig, 9 + 4);
  ck_assert_mem_eq(pkt + 4 + 9 + 5, orig + 4 + 9 + 5, 188 - 4 - 9 - 5);
}
END_TEST

START_TEST(packets_that_do_not_start_a_pes_are_left_alone) {
  spec_t s = {.stream_id = 0xE0, .af_len = -1, .has_pts = 1, .pts = 42};
  unsigned char pkt[188];
  unsigned char orig[188];
  pes_stamp_t st;
  build(pkt, &s);
  pkt[1] &= (unsigned char)~0x40;
  memcpy(orig, pkt, sizeof pkt);
  ck_assert_int_eq(pesstamp_read(pkt, &st), PESSTAMP_NONE);
  ck_assert_int_eq(pesstamp_shift(pkt, 5), PESSTAMP_NONE);
  ck_assert_mem_eq(pkt, orig, 188);
  build(pkt, &s);
  pkt[4] = 0x02;
  memcpy(orig, pkt, sizeof pkt);
  ck_assert_int_eq(pesstamp_shift(pkt, 5), PESSTAMP_NONE);
  ck_assert_mem_eq(pkt, orig, 188);
}
END_TEST

START_TEST(adaptation_only_packets_are_left_alone) {
  unsigned char pkt[188];
  unsigned char orig[188];
  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = 0x41;
  pkt[2] = 0x00;
  pkt[3] = 0x20;
  pkt[4] = 183;
  memcpy(orig, pkt, sizeof pkt);
  ck_assert_int_eq(pesstamp_shift(pkt, 5), PESSTAMP_NONE);
  ck_assert_mem_eq(pkt, orig, 188);
}
END_TEST

START_TEST(stream_ids_without_a_pes_header_are_left_alone) {
  static const unsigned ids[] = {0xBC, 0xBE, 0xBF, 0xF0, 0xF1, 0xF2, 0xF8, 0xFF};
  for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
    spec_t s = {.stream_id = ids[i], .af_len = -1, .has_pts = 1, .pts = 42};
    unsigned char pkt[188];
    unsigned char orig[188];
    build(pkt, &s);
    memcpy(orig, pkt, sizeof pkt);
    ck_assert_int_eq(pesstamp_shift(pkt, 5), PESSTAMP_NONE);
    ck_assert_mem_eq(pkt, orig, 188);
  }
}
END_TEST

START_TEST(pes_without_stamps_is_reported_as_none) {
  spec_t s = {.stream_id = 0xE0, .af_len = -1};
  unsigned char pkt[188];
  pes_stamp_t st;
  build(pkt, &s);
  ck_assert_int_eq(pesstamp_read(pkt, &st), PESSTAMP_NONE);
  ck_assert_int_eq(st.has_pts, 0);
}
END_TEST

START_TEST(forbidden_pts_dts_flag_value_is_ignored) {
  spec_t s = {.stream_id = 0xE0, .af_len = -1, .has_pts = 1, .pts = 42};
  unsigned char pkt[188];
  unsigned char orig[188];
  build(pkt, &s);
  pkt[4 + 7] = 0x40;
  memcpy(orig, pkt, sizeof pkt);
  ck_assert_int_eq(pesstamp_shift(pkt, 5), PESSTAMP_NONE);
  ck_assert_mem_eq(pkt, orig, 188);
}
END_TEST

START_TEST(header_length_too_small_for_the_flags_is_ignored) {
  spec_t s = {.stream_id = 0xE0, .af_len = -1, .has_pts = 1, .has_dts = 1, .pts = 42, .dts = 40};
  unsigned char pkt[188];
  unsigned char orig[188];
  build(pkt, &s);
  pkt[4 + 8] = 6;
  memcpy(orig, pkt, sizeof pkt);
  ck_assert_int_eq(pesstamp_shift(pkt, 5), PESSTAMP_NONE);
  ck_assert_mem_eq(pkt, orig, 188);
}
END_TEST

START_TEST(mpeg1_style_header_is_ignored) {
  spec_t s = {.stream_id = 0xE0, .af_len = -1, .has_pts = 1, .pts = 42};
  unsigned char pkt[188];
  unsigned char orig[188];
  build(pkt, &s);
  pkt[4 + 6] = 0x40;
  memcpy(orig, pkt, sizeof pkt);
  ck_assert_int_eq(pesstamp_shift(pkt, 5), PESSTAMP_NONE);
  ck_assert_mem_eq(pkt, orig, 188);
}
END_TEST

START_TEST(headers_cut_by_the_packet_end_are_reported_as_split) {
  spec_t a = {.stream_id = 0xE0, .has_pts = 1, .pts = 42, .start_override = 183};
  spec_t fit = {.stream_id = 0xE0, .has_pts = 1, .has_dts = 1, .pts = 42, .dts = 40, .start_override = 168};
  spec_t cut = {.stream_id = 0xE0, .has_pts = 1, .has_dts = 1, .pts = 42, .dts = 40, .start_override = 170};
  unsigned char pkt[188];
  unsigned char orig[188];
  pes_stamp_t st;
  build(pkt, &a);
  memcpy(orig, pkt, sizeof pkt);
  ck_assert_int_eq(pesstamp_read(pkt, &st), PESSTAMP_SPLIT);
  ck_assert_int_eq(pesstamp_shift(pkt, 5), PESSTAMP_SPLIT);
  ck_assert_mem_eq(pkt, orig, 188);
  build(pkt, &fit);
  ck_assert_int_eq(pesstamp_shift(pkt, 5), PESSTAMP_FOUND);
  build(pkt, &cut);
  memcpy(orig, pkt, sizeof pkt);
  ck_assert_int_eq(pesstamp_shift(pkt, 5), PESSTAMP_SPLIT);
  ck_assert_mem_eq(pkt, orig, 188);
}
END_TEST

START_TEST(scrambled_packets_are_reported_and_left_alone) {
  spec_t s = {.stream_id = 0xE0, .af_len = -1, .has_pts = 1, .pts = 42};
  unsigned char pkt[188];
  unsigned char orig[188];
  build(pkt, &s);
  pkt[3] |= 0x80;
  memcpy(orig, pkt, sizeof pkt);
  ck_assert_int_eq(pesstamp_shift(pkt, 5), PESSTAMP_SCRAMBLED);
  ck_assert_mem_eq(pkt, orig, 188);
}
END_TEST

typedef struct {
  unsigned flags;
  int pcr;
  int opcr;
  int countdown;
  int priv_len;
  unsigned eflags;
  int ltw;
  int piecewise;
  uint64_t next_dts;
  unsigned splice_type;
} af_spec_t;

static void build_af(unsigned char pkt[188], const af_spec_t *a, unsigned *ts_off) {
  unsigned o = 6;
  unsigned ext_start;
  memset(pkt, 0xEE, 188);
  pkt[0] = 0x47;
  pkt[1] = 0x01;
  pkt[2] = 0x00;
  pkt[3] = 0x30;
  pkt[5] = (unsigned char)a->flags;
  if (a->pcr) o += 6;
  if (a->opcr) o += 6;
  if (a->countdown) pkt[o++] = 5;
  if (a->priv_len >= 0) {
    pkt[o++] = (unsigned char)a->priv_len;
    o += (unsigned)a->priv_len;
  }
  ext_start = o;
  pkt[o++] = 0;
  pkt[o++] = (unsigned char)a->eflags;
  if (a->ltw) o += 2;
  if (a->piecewise) o += 3;
  *ts_off = o;
  if (a->eflags & 0x20) {
    enc_ts(pkt + o, a->splice_type, a->next_dts);
    o += 5;
  }
  pkt[ext_start] = (unsigned char)(o - ext_start - 1);
  pkt[4] = (unsigned char)(o - 5);
}

START_TEST(af_shift_moves_dts_next_au_and_keeps_the_splice_type) {
  af_spec_t a = {.flags = 0x01 | 0x04, .countdown = 1, .priv_len = -1, .eflags = 0x20, .next_dts = 1000000, .splice_type = 0x9};
  unsigned char pkt[188];
  unsigned char want[188];
  unsigned off;
  build_af(pkt, &a, &off);
  a.next_dts = 1000000 + 90000;
  build_af(want, &a, &off);
  ck_assert_int_eq(afstamp_shift(pkt, 90000), 1);
  ck_assert_mem_eq(pkt, want, 188);
  ck_assert_uint_eq(pkt[off] >> 4, 0x9u);
}
END_TEST

START_TEST(af_shift_skips_pcr_opcr_countdown_private_data_ltw_and_piecewise_rate) {
  af_spec_t a = {.flags = 0x01 | 0x10 | 0x08 | 0x04 | 0x02, .pcr = 1, .opcr = 1, .countdown = 1, .priv_len = 4, .eflags = 0x20 | 0x80 | 0x40,
                 .ltw = 1, .piecewise = 1, .next_dts = TS_MOD - 5, .splice_type = 0x3};
  unsigned char pkt[188];
  unsigned char want[188];
  unsigned off;
  build_af(pkt, &a, &off);
  a.next_dts = 15;
  build_af(want, &a, &off);
  ck_assert_int_eq(afstamp_shift(pkt, 20), 1);
  ck_assert_mem_eq(pkt, want, 188);
}
END_TEST

START_TEST(af_shift_accepts_negative_deltas) {
  af_spec_t a = {.flags = 0x01 | 0x04, .countdown = 1, .priv_len = -1, .eflags = 0x20, .next_dts = 10, .splice_type = 0x1};
  unsigned char pkt[188];
  unsigned char want[188];
  unsigned off;
  build_af(pkt, &a, &off);
  a.next_dts = TS_MOD - 40;
  build_af(want, &a, &off);
  ck_assert_int_eq(afstamp_shift(pkt, -50), 1);
  ck_assert_mem_eq(pkt, want, 188);
}
END_TEST

START_TEST(af_shift_ignores_adaptation_fields_without_dts_next_au) {
  af_spec_t cases[] = {
    {.flags = 0x00, .priv_len = -1},
    {.flags = 0x01, .priv_len = -1, .eflags = 0x00},
    {.flags = 0x01 | 0x04, .countdown = 1, .priv_len = -1, .eflags = 0x80, .ltw = 1},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    unsigned char pkt[188];
    unsigned char orig[188];
    unsigned off;
    build_af(pkt, &cases[i], &off);
    memcpy(orig, pkt, sizeof pkt);
    ck_assert_int_eq(afstamp_shift(pkt, 5), 0);
    ck_assert_mem_eq(pkt, orig, 188);
  }
}
END_TEST

START_TEST(af_shift_ignores_truncated_or_malformed_fields) {
  af_spec_t a = {.flags = 0x01 | 0x04, .countdown = 1, .priv_len = -1, .eflags = 0x20, .next_dts = 100, .splice_type = 0x1};
  unsigned char pkt[188];
  unsigned char orig[188];
  unsigned off;
  build_af(pkt, &a, &off);
  pkt[4] = (unsigned char)(pkt[4] - 3);
  memcpy(orig, pkt, sizeof pkt);
  ck_assert_int_eq(afstamp_shift(pkt, 5), 0);
  ck_assert_mem_eq(pkt, orig, 188);
  build_af(pkt, &a, &off);
  pkt[6 + 1] = 200;
  memcpy(orig, pkt, sizeof pkt);
  ck_assert_int_eq(afstamp_shift(pkt, 5), 0);
  ck_assert_mem_eq(pkt, orig, 188);
  build_af(pkt, &a, &off);
  pkt[3] = 0x10;
  memcpy(orig, pkt, sizeof pkt);
  ck_assert_int_eq(afstamp_shift(pkt, 5), 0);
  ck_assert_mem_eq(pkt, orig, 188);
  build_af(pkt, &a, &off);
  pkt[4] = 0;
  memcpy(orig, pkt, sizeof pkt);
  ck_assert_int_eq(afstamp_shift(pkt, 5), 0);
  ck_assert_mem_eq(pkt, orig, 188);
}
END_TEST

START_TEST(discontinuity_indicator_is_cleared_and_other_flags_stay) {
  af_spec_t a = {.flags = 0x80 | 0x40 | 0x10, .pcr = 1, .priv_len = -1};
  unsigned char pkt[188];
  unsigned off;
  build_af(pkt, &a, &off);
  ck_assert_int_eq(afstamp_clear_discontinuity(pkt), 1);
  ck_assert_uint_eq(pkt[5], 0x50u);
  ck_assert_int_eq(afstamp_clear_discontinuity(pkt), 0);
  ck_assert_uint_eq(pkt[5], 0x50u);
}
END_TEST

START_TEST(discontinuity_clearing_ignores_packets_without_an_adaptation_field) {
  unsigned char pkt[188];
  unsigned char orig[188];
  memset(pkt, 0x80, sizeof pkt);
  pkt[0] = 0x47;
  pkt[3] = 0x10;
  memcpy(orig, pkt, sizeof pkt);
  ck_assert_int_eq(afstamp_clear_discontinuity(pkt), 0);
  ck_assert_mem_eq(pkt, orig, 188);
  pkt[3] = 0x30;
  pkt[4] = 0;
  memcpy(orig, pkt, sizeof pkt);
  ck_assert_int_eq(afstamp_clear_discontinuity(pkt), 0);
  ck_assert_mem_eq(pkt, orig, 188);
}
END_TEST

static Suite *pesstamp_suite(void) {
  Suite *s = suite_create("pesstamp");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, reads_pts_only);
  tcase_add_test(tc, reads_pts_dts_and_escr);
  tcase_add_test(tc, reads_through_an_adaptation_field);
  tcase_add_test(tc, shift_matches_a_fresh_encoding_of_the_shifted_values);
  tcase_add_test(tc, shift_wraps_forward_and_backward);
  tcase_add_test(tc, shift_handles_deltas_beyond_one_wrap);
  tcase_add_test(tc, escr_shift_wraps_at_its_own_modulus);
  tcase_add_test(tc, shift_leaves_the_payload_and_stuffing_alone);
  tcase_add_test(tc, packets_that_do_not_start_a_pes_are_left_alone);
  tcase_add_test(tc, adaptation_only_packets_are_left_alone);
  tcase_add_test(tc, stream_ids_without_a_pes_header_are_left_alone);
  tcase_add_test(tc, pes_without_stamps_is_reported_as_none);
  tcase_add_test(tc, forbidden_pts_dts_flag_value_is_ignored);
  tcase_add_test(tc, header_length_too_small_for_the_flags_is_ignored);
  tcase_add_test(tc, mpeg1_style_header_is_ignored);
  tcase_add_test(tc, headers_cut_by_the_packet_end_are_reported_as_split);
  tcase_add_test(tc, scrambled_packets_are_reported_and_left_alone);
  tcase_add_test(tc, af_shift_moves_dts_next_au_and_keeps_the_splice_type);
  tcase_add_test(tc, af_shift_skips_pcr_opcr_countdown_private_data_ltw_and_piecewise_rate);
  tcase_add_test(tc, af_shift_accepts_negative_deltas);
  tcase_add_test(tc, af_shift_ignores_adaptation_fields_without_dts_next_au);
  tcase_add_test(tc, af_shift_ignores_truncated_or_malformed_fields);
  tcase_add_test(tc, discontinuity_indicator_is_cleared_and_other_flags_stay);
  tcase_add_test(tc, discontinuity_clearing_ignores_packets_without_an_adaptation_field);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(pesstamp_suite());
  int failed;
  srunner_run_all(sr, CK_NORMAL);
  failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? 0 : 1;
}
