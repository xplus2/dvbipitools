/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/bitwriter.h"
#include "lib/demux/escodec/escodec.h"

#define NELEM(a) (sizeof(a) / sizeof((a)[0]))
#define FCAP 1024

#define SEQ "0000000000"
#define E0 "00 000 0 00 00"
#define PRES1_TAIL "000000"

static const char *cat(char *buf, ...) {
  va_list ap;
  const char *s;
  size_t n = 0;
  va_start(ap, buf);
  while ((s = va_arg(ap, const char *)) != NULL) {
    size_t l = strlen(s);
    ck_assert_uint_lt(n + l, FCAP);
    memcpy(buf + n, s, l);
    n += l;
  }
  va_end(ap);
  buf[n] = '\0';
  return buf;
}

static void put_bits(bitwriter_t *bw, const char *s) {
  for (; *s; s++) {
    if (*s == ' ') continue;
    ck_assert(*s == '0' || *s == '1');
    ck_assert_int_eq(bitwriter_put(bw, (uint64_t)(*s - '0'), 1), 0);
  }
}

static size_t mk(unsigned char *out, size_t cap, size_t pad, const char *bits) {
  bitwriter_t bw;
  size_t plen;
  const unsigned char *payload;
  size_t size;
  bitwriter_init(&bw);
  put_bits(&bw, bits);
  payload = bitwriter_data(&bw, &plen);
  size = plen + pad;
  ck_assert_uint_le(4 + size, cap);
  out[0] = 0xAC;
  out[1] = 0x40;
  out[2] = (unsigned char)(size >> 8);
  out[3] = (unsigned char)size;
  memcpy(out + 4, payload, plen);
  memset(out + 4 + plen, 0, pad);
  bitwriter_free(&bw);
  return 4 + size;
}

static int parse(const char *bits, esc_frame_t *f, esc_track_t *t) {
  unsigned char d[FCAP];
  size_t n = mk(d, sizeof d, 8, bits);
  memset(f, 0, sizeof *f);
  return next_frame(t, d, n, f);
}

static int parse_new(const char *bits, esc_frame_t *f) {
  esc_track_t t;
  memset(&t, 0, sizeof t);
  t.codec = CODEC_AC4;
  return parse(bits, f, &t);
}

static const char *head(char *buf, const char *ver, const char *wait, const char *fs, const char *fr, const char *tail) {
  return cat(buf, ver, SEQ, wait, fs, fr, "1", tail, (const char *)NULL);
}

static const char *mono_v0(char *buf, const char *mult, const char *emdf) {
  return cat(buf, "1", "0", "000", "0", mult, emdf, "0", (const char *)NULL);
}

START_TEST(header_length_and_sync_checks) {
  unsigned char d[16] = {0xAC, 0x40, 0x00, 0x06, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
  esc_track_t t;
  esc_frame_t f;
  memset(&t, 0, sizeof t);
  t.codec = CODEC_AC4;
  ck_assert_int_eq(next_frame(&t, d, 3, &f), 1);
  ck_assert_int_eq(next_frame(&t, d, 9, &f), 1);
  d[1] = 0x42;
  ck_assert_int_eq(next_frame(&t, d, 10, &f), -1);
  d[1] = 0x40;
  d[0] = 0xAB;
  ck_assert_int_eq(next_frame(&t, d, 10, &f), -1);
  d[0] = 0xAC;
  d[2] = 0;
  d[3] = 0;
  ck_assert_int_eq(next_frame(&t, d, 16, &f), -1);
}
END_TEST

START_TEST(header_crc_variant_counts_trailer) {
  unsigned char d[32] = {0xAC, 0x41, 0x00, 0x06, 0x00, 0x05, 0xb4, 0x00, 0x00, 0x00, 0x11, 0x22};
  esc_track_t t;
  esc_frame_t f;
  memset(&t, 0, sizeof t);
  t.codec = CODEC_AC4;
  ck_assert_int_eq(next_frame(&t, d, 11, &f), 1);
  ck_assert_int_eq(next_frame(&t, d, 12, &f), 0);
  ck_assert_uint_eq(f.consumed, 12);
  ck_assert_uint_eq(f.outlen, 6);
}
END_TEST

START_TEST(header_extended_size_form) {
  unsigned char d[64];
  esc_track_t t;
  esc_frame_t f;
  memset(d, 0, sizeof d);
  memset(&t, 0, sizeof t);
  t.codec = CODEC_AC4;
  d[0] = 0xAC;
  d[1] = 0x40;
  d[2] = 0xFF;
  d[3] = 0xFF;
  ck_assert_int_eq(next_frame(&t, d, 6, &f), 1);
  d[4] = d[5] = d[6] = 0;
  ck_assert_int_eq(next_frame(&t, d, 7, &f), -1);
  d[6] = 20;
  ck_assert_int_eq(next_frame(&t, d, 26, &f), 1);
  d[7] = 0x00;
  d[8] = 0x05;
  d[9] = 0xb4;
  ck_assert_int_eq(next_frame(&t, d, 27, &f), 0);
  ck_assert_uint_eq(f.consumed, 27);
  ck_assert_uint_eq(f.outlen, 20);
}
END_TEST

START_TEST(toc_rejects_bad_rate_indices) {
  static const struct {
    const char *fs;
    const char *fr;
  } t[] = {{"1", "1110"}, {"1", "1111"}, {"0", "0000"}, {"0", "1100"}};
  for (size_t i = 0; i < NELEM(t); i++) {
    char buf[FCAP];
    char b2[FCAP];
    esc_frame_t f;
    ck_assert_int_eq(parse_new(head(b2, "00", "0", t[i].fs, t[i].fr, cat(buf, "1 0", (const char *)NULL)), &f), -1);
  }
}
END_TEST

START_TEST(toc_rejects_body_shorter_than_header) {
  unsigned char d[5] = {0xAC, 0x40, 0x00, 0x01, 0x00};
  esc_track_t t;
  esc_frame_t f;
  memset(&t, 0, sizeof t);
  t.codec = CODEC_AC4;
  ck_assert_int_eq(next_frame(&t, d, 5, &f), -1);
}
END_TEST

START_TEST(toc_44k_rate_and_codec_private_data) {
  char b1[FCAP];
  char b2[FCAP];
  esc_frame_t f;
  esc_track_t t;
  memset(&t, 0, sizeof t);
  t.codec = CODEC_AC4;
  ck_assert_int_eq(parse(head(b2, "00", "0", "0", "1101", cat(b1, "1 0", (const char *)NULL)), &f, &t), 0);
  ck_assert_uint_eq(f.rate, 44100u);
  ck_assert_uint_eq(f.samples, 2048u);
  ck_assert_uint_gt(t.cpriv_len, 0u);
  t.cpriv[0] = 0x5A;
  ck_assert_int_eq(parse(head(b2, "00", "0", "0", "1101", cat(b1, "1 0", (const char *)NULL)), &f, &t), 0);
  ck_assert_uint_eq(t.cpriv[0], 0x5Au);
}
END_TEST

START_TEST(toc_wait_frames_and_payload_base) {
  static const struct {
    const char *wait;
    const char *payload;
  } t[] = {{"1 000", "0"}, {"1 001 00", "0"}, {"1 111 11", "1 00000"}, {"0", "1 11111 00000"}, {"0", "1 11111 11111 10000"}};
  for (size_t i = 0; i < NELEM(t); i++) {
    char b1[FCAP];
    char b2[FCAP];
    esc_frame_t f;
    ck_assert_int_eq(parse_new(head(b2, "00", t[i].wait, "1", "0011", cat(b1, "0 0 ", t[i].payload, (const char *)NULL)), &f), 0);
    ck_assert_uint_eq(f.samples, 1536u);
  }
}
END_TEST

START_TEST(toc_presentation_count_forms) {
  static const char *const tail[] = {"1 0", "0 0 0", "0 1 00 0 0", "0 1 11 0 1 00 0", "0 1 01 0 0"};
  for (size_t i = 0; i < NELEM(tail); i++) {
    char b1[FCAP];
    char b2[FCAP];
    esc_frame_t f;
    ck_assert_int_eq(parse_new(head(b2, "00", "0", "1", "0011", cat(b1, tail[i], (const char *)NULL)), &f), 0);
  }
}
END_TEST

static const struct {
  const char *bits;
  unsigned ch;
  unsigned v0_2011;
  unsigned v1_groups;
} v0_modes[] = {
    {"0", 1, 0x4000, 0x2},
    {"10", 2, 0xA000, 0x1},
    {"11 00", 3, 0xE000, 0x3},
    {"11 01", 5, 0xF800, 0x7},
    {"11 10", 6, 0xF802, 0x47},
    {"11 11 000", 7, 0xFA00, 0xF},
    {"11 11 001", 8, 0xFA02, 0x4F},
    {"11 11 010", 7, 0xF820, 0x20007},
    {"11 11 011", 8, 0xF822, 0x20047},
    {"11 11 100", 7, 0xF810, 0x17},
    {"11 11 101", 8, 0xF812, 0x57},
};

START_TEST(v0_single_substream_channel_modes) {
  for (int ver = 0; ver <= 1; ver++) {
    char b1[FCAP];
    char b2[FCAP];
    esc_frame_t f;
    ck_assert_int_eq(parse_new(head(b2, ver ? "01" : "00", "0", "1", "0011", cat(b1, "1 0 1 0 000 0 0 ", E0, v0_modes[_i].bits, (const char *)NULL)), &f), 0);
    ck_assert_uint_eq(f.ch, v0_modes[_i].ch);
    ck_assert_uint_eq(f.ac4_presentation_version, 0u);
    ck_assert_int_eq(f.chcfg_scheme, ver ? ESC_CHCFG_DOLBY_2015 : ESC_CHCFG_DOLBY_2011);
    ck_assert_uint_eq(f.chcfg_value, ver ? v0_modes[_i].v1_groups : v0_modes[_i].v0_2011);
  }
}
END_TEST

START_TEST(v0_reserved_channel_modes_are_rejected) {
  static const char *const m[] = {"11 11 110", "11 11 111 00 0", "11 11 111 11 0 0 0 0"};
  for (size_t i = 0; i < NELEM(m); i++) {
    char b1[FCAP];
    char b2[FCAP];
    esc_frame_t f;
    ck_assert_int_eq(parse_new(head(b2, "00", "0", "1", "0011", cat(b1, "1 0 1 0 000 0 0 ", E0, m[i], (const char *)NULL)), &f), 0);
    ck_assert_uint_eq(f.ch, 0u);
    ck_assert_int_eq(f.chcfg_scheme, 0);
  }
}
END_TEST

START_TEST(v0_presentation_version_counts_leading_ones) {
  static const struct {
    const char *ones;
    unsigned pver;
  } t[] = {{"0", 0}, {"10", 1}, {"110", 2}, {"1110", 3}};
  for (size_t i = 0; i < NELEM(t); i++) {
    char b1[FCAP];
    char b2[FCAP];
    esc_frame_t f;
    ck_assert_int_eq(parse_new(head(b2, "00", "0", "1", "0011", cat(b1, "1 0 1 ", t[i].ones, " 000 0 0 ", E0, "10", (const char *)NULL)), &f), 0);
    ck_assert_uint_eq(f.ac4_presentation_version, t[i].pver);
    ck_assert_uint_eq(f.ch, 2u);
    ck_assert_int_eq(f.chcfg_scheme, t[i].pver ? 0 : ESC_CHCFG_DOLBY_2011);
  }
}
END_TEST

START_TEST(v0_multi_substream_configs) {
  static const struct {
    const char *cfg;
    const char *rest;
    unsigned ch;
  } t[] = {
      {"000", "1", 2},
      {"101", "1", 2},
      {"110", "1", 0},
      {"111 00 0", "1", 0},
      {"111 00 0", "0", 0},
  };
  for (size_t i = 0; i < NELEM(t); i++) {
    char b1[FCAP];
    char b2[FCAP];
    esc_frame_t f;
    ck_assert_int_eq(parse_new(head(b2, "00", "0", "1", "0011", cat(b1, "1 0 0 ", t[i].cfg, " 0 000 0 0 ", E0, t[i].rest, "10", (const char *)NULL)), &f), 0);
    ck_assert_uint_eq(f.ch, t[i].ch);
  }
}
END_TEST

START_TEST(v0_frame_rate_multiplier_bits_per_index) {
  static const struct {
    const char *fs;
    const char *fr;
    const char *mult;
  } t[] = {
      {"1", "0000", "1"}, {"1", "0001", "0"}, {"1", "0010", "11"}, {"1", "0011", "10"}, {"1", "0100", "0"},  {"1", "0101", ""},
      {"1", "0110", ""},  {"1", "0111", "1"}, {"1", "1000", "0"},  {"1", "1001", "1"},  {"1", "1010", ""},   {"1", "1011", ""},
      {"1", "1100", ""},  {"0", "1101", ""},
  };
  for (size_t i = 0; i < NELEM(t); i++) {
    char b1[FCAP];
    char b3[FCAP];
    esc_frame_t f;
    ck_assert_int_eq(parse_new(head(b3, "00", "0", t[i].fs, t[i].fr, mono_v0(b1, t[i].mult, E0)), &f), 0);
    ck_assert_uint_eq(f.ch, 1u);
  }
}
END_TEST

START_TEST(emdf_info_variants_keep_alignment) {
  static const char *const emdf[] = {
      "00 000 0 00 00",
      "11 00 000 0 00 00",
      "00 111 00 0 00 00",
      "00 000 1 00 00 00",
      "00 000 1 11 00 00 00",
      "00 000 0 01 00 00000000",
      "00 000 0 00 01 00000000",
      "00 000 0 10 10 0000000000000000",
      "00 000 0 11 11 0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000",
  };
  char buf[FCAP];
  char b1[FCAP];
  esc_frame_t f;
  ck_assert_int_eq(parse_new(head(buf, "00", "0", "1", "0011", mono_v0(b1, "0", emdf[_i])), &f), 0);
  ck_assert_uint_eq(f.ch, 1u);
}
END_TEST

START_TEST(v0_truncation_leaves_no_channel_info) {
  char b1[FCAP];
  char b2[FCAP];
  unsigned char d[FCAP];
  esc_track_t t;
  esc_frame_t f;
  const char *bits = head(b2, "00", "0", "1", "0011", cat(b1, "1 0 1 0 000 0 0 ", E0, "11 11 000", (const char *)NULL));
  size_t n = mk(d, sizeof d, 0, bits);
  memset(&t, 0, sizeof t);
  t.codec = CODEC_AC4;
  for (size_t cut = 1; cut + 4 < n; cut++) {
    unsigned char c[FCAP];
    memcpy(c, d, n);
    c[2] = (unsigned char)((n - 4 - cut) >> 8);
    c[3] = (unsigned char)(n - 4 - cut);
    memset(&f, 0, sizeof f);
    ck_assert_int_ne(next_frame(&t, c, n - cut, &f), 1);
  }
}
END_TEST

static const char *const V1_MODES[] = {"0", "10", "11 00", "11 01", "11 10", "11 11 000", "11 11 001", "11 11 010", "11 11 011", "11 11 100", "11 11 101"};

static const char *info_chan(char *buf, const char *mode, const char *flags, int mode7to10, const char *factor_bits, const char *sf, const char *subs) {
  return cat(buf, mode, flags, sf, "0", mode7to10 ? "0" : "", factor_bits, subs, (const char *)NULL);
}

static const char *group1(char *buf, const char *mode, const char *flags, int mode7to10) {
  char ic[FCAP];
  info_chan(ic, mode, flags, mode7to10, "0", "0", "00");
  return cat(buf, "1 0 1 1 ", ic, " 0", (const char *)NULL);
}

static const char *v1_frame(char *buf, const char *fr, const char *pres, const char *groups) {
  char h[FCAP];
  char p[FCAP];
  head(h, "10", "0", "1", fr, "1 0");
  return cat(buf, h, " 0 ", cat(p, pres, (const char *)NULL), groups, (const char *)NULL);
}

static const char *pres_single(char *buf, const char *sgi) {
  return cat(buf, "1 0 000 0 0 ", E0, " 0 ", sgi, " ", PRES1_TAIL, (const char *)NULL);
}

START_TEST(v1_channel_modes_0_to_10_map_to_speaker_groups) {
  static const unsigned groups[] = {0x2, 0x1, 0x3, 0x7, 0x47, 0xF, 0x4F, 0x20007, 0x20047, 0x17, 0x57};
  static const unsigned chans[] = {1, 2, 3, 5, 6, 7, 8, 7, 8, 7, 8};
  char p[FCAP];
  char g[FCAP];
  char b[FCAP];
  esc_frame_t f;
  int m7 = _i >= 7 && _i <= 10;
  ck_assert_int_eq(parse_new(v1_frame(b, "0011", pres_single(p, "000"), group1(g, V1_MODES[_i], "", m7)), &f), 0);
  ck_assert_int_eq(f.chcfg_scheme, ESC_CHCFG_DOLBY_2015);
  ck_assert_uint_eq(f.chcfg_value, groups[_i]);
  ck_assert_uint_eq(f.ch, chans[_i]);
}
END_TEST

START_TEST(v1_channel_modes_11_to_15) {
  static const struct {
    const char *mode;
    const char *flags;
    unsigned groups;
    unsigned ch;
  } t[] = {
      {"11 11 110 0", "0 0 00", 0x5, 4},
      {"11 11 110 0", "0 1 00", 0x7, 5},
      {"11 11 110 0", "1 0 00", 0xD, 6},
      {"11 11 110 0", "0 0 01", 0x85, 6},
      {"11 11 110 0", "0 0 10", 0x85, 6},
      {"11 11 110 0", "0 0 11", 0x35, 8},
      {"11 11 110 1", "1 1 11", 0x7F, 12},
      {"11 11 111 00", "0 0 00", 0x10005, 6},
      {"11 11 111 01", "0 0 00", 0x10045, 7},
      {"11 11 111 10", "0 0 00", 0x2FEFF, 24},
  };
  char p[FCAP];
  char g[FCAP];
  char b[FCAP];
  esc_frame_t f;
  const char *flags = _i == 9 ? "" : t[_i].flags;
  ck_assert_int_eq(parse_new(v1_frame(b, "0011", pres_single(p, "000"), group1(g, t[_i].mode, flags, 0)), &f), 0);
  ck_assert_int_eq(f.chcfg_scheme, ESC_CHCFG_DOLBY_2015);
  ck_assert_uint_eq(f.chcfg_value, t[_i].groups);
  ck_assert_uint_eq(f.ch, t[_i].ch);
}
END_TEST

START_TEST(v1_reserved_channel_modes_fail) {
  static const char *const m[] = {"11 11 111 11 00 0", "11 11 111 11 11 0 0 0"};
  char p[FCAP];
  char g[FCAP];
  char b[FCAP];
  esc_frame_t f;
  ck_assert_int_eq(parse_new(v1_frame(b, "0011", pres_single(p, "000"), group1(g, m[_i], "", 0)), &f), 0);
  ck_assert_int_eq(f.chcfg_scheme, 0);
  ck_assert_uint_eq(f.ch, 0u);
}
END_TEST

START_TEST(v1_object_based_group) {
  char p[FCAP];
  char b[FCAP];
  esc_frame_t f;
  ck_assert_int_eq(parse_new(v1_frame(b, "0011", pres_single(p, "000"), "1 0 1 0"), &f), 0);
  ck_assert_uint_eq(f.chcfg_value, 0x800000u);
  ck_assert_int_eq(parse_new(v1_frame(b, "0011", pres_single(p, "000"), "1 0 1 0"), &f), 0);
}
END_TEST

START_TEST(v1_program_id_forms) {
  static const char *const pid[] = {"0", "1 0000000000000000 0", "1 0000000000000000 1 00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"};
  char p[FCAP];
  char g[FCAP];
  char h[FCAP];
  char b[FCAP];
  esc_frame_t f;
  head(h, "10", "0", "1", "0011", "1 0");
  cat(b, h, " ", pid[_i], " ", pres_single(p, "000"), group1(g, "10", "", 0), (const char *)NULL);
  ck_assert_int_eq(parse_new(b, &f), 0);
  ck_assert_uint_eq(f.ch, _i == 2 ? 0u : 2u);
}
END_TEST

START_TEST(v1_rate_dependent_presentation_bits) {
  static const struct {
    const char *fs;
    const char *fr;
    const char *mult;
    const char *frac;
    unsigned factor_bits;
  } t[] = {
      {"1", "0000", "1", "", 2}, {"1", "0001", "0", "", 1}, {"1", "0010", "11", "", 4}, {"1", "0011", "10", "", 2}, {"1", "0100", "0", "", 1},
      {"1", "0101", "", "0", 1}, {"1", "0110", "", "1", 1}, {"1", "0111", "1", "", 2},  {"1", "0111", "0", "0", 1}, {"1", "1000", "1", "", 2},
      {"1", "1001", "0", "1", 1}, {"1", "1010", "", "0", 1}, {"1", "1011", "", "1 0", 1}, {"1", "1100", "", "1 1", 1}, {"0", "1101", "", "", 1},
  };
  char h[FCAP];
  char b[FCAP];
  char ic[FCAP];
  char zeros[8] = "0000";
  esc_frame_t f;
  char pres[FCAP];
  char gp[FCAP];
  zeros[t[_i].factor_bits] = '\0';
  info_chan(ic, "10", "", 0, zeros, t[_i].fs[0] == '1' ? "0" : "", "00");
  cat(gp, "1 0 1 1 ", ic, " 0", (const char *)NULL);
  cat(pres, "1 0 000 0 ", t[_i].mult, " ", t[_i].frac, " ", E0, " 0 000 ", PRES1_TAIL, (const char *)NULL);
  head(h, "10", "0", t[_i].fs, t[_i].fr, "1 0");
  cat(b, h, " 0 ", pres, gp, (const char *)NULL);
  ck_assert_int_eq(parse_new(b, &f), 0);
  ck_assert_uint_eq(f.ch, 2u);
}
END_TEST

START_TEST(v1_presentation_filter_extra_bit) {
  char gp[FCAP];
  char b[FCAP];
  char h[FCAP];
  esc_frame_t f;
  group1(gp, "10", "", 0);
  head(h, "10", "0", "1", "0011", "1 0");
  cat(b, h, " 0 1 0 000 0 0 ", E0, " 1 1 000 ", PRES1_TAIL, gp, (const char *)NULL);
  ck_assert_int_eq(parse_new(b, &f), 0);
  ck_assert_uint_eq(f.ch, 2u);
}
END_TEST

START_TEST(v1_presentation_pver_and_trailer_variants) {
  char gp[FCAP];
  char b[FCAP];
  char h[FCAP];
  esc_frame_t f;
  group1(gp, "10", "", 0);
  head(h, "10", "0", "1", "0011", "1 0");
  cat(b, h, " 0 1 110 000 1 00 0 ", E0, " 0 000 000011 00", gp, (const char *)NULL);
  ck_assert_int_eq(parse_new(b, &f), 0);
  cat(b, h, " 0 1 0 000 0 0 ", E0, " 0 000 0 0 0 0 11 00 0", gp, (const char *)NULL);
  ck_assert_int_eq(parse_new(b, &f), 0);
  ck_assert_uint_eq(f.ch, 2u);
}
END_TEST

static const char *pres_multi(char *buf, const char *cfg, const char *extra, const char *sgis) {
  return cat(buf, "0 ", cfg, " 0 000 0 0 ", E0, " 0 0 ", extra, " ", sgis, " ", PRES1_TAIL, (const char *)NULL);
}

START_TEST(v1_multi_substream_group_counts) {
  static const struct {
    const char *cfg;
    const char *extra;
    const char *sgis;
    unsigned ngroups;
  } t[] = {
      {"000", "", "000 001", 2},
      {"001", "", "000 001", 2},
      {"010", "", "000 001", 2},
      {"011", "", "000 001 010", 3},
      {"100", "", "000 001 010", 3},
      {"101", "00", "000 001", 2},
      {"101", "01", "000 001 010", 3},
      {"101", "10", "000 001 010 011", 4},
      {"101", "11 00 0", "000 001 010 011 100", 5},
  };
  char p[FCAP];
  char g[FCAP];
  char gi[FCAP];
  char b[FCAP];
  char h[FCAP];
  esc_frame_t f;
  const char *grp = group1(gi, "10", "", 0);
  head(h, "10", "0", "1", "0011", "1 0");
  cat(g, grp, grp, grp, grp, grp, (const char *)NULL);
  cat(b, h, " 0 ", pres_multi(p, t[_i].cfg, t[_i].extra, t[_i].sgis), g, (const char *)NULL);
  ck_assert_int_eq(parse_new(b, &f), 0);
  ck_assert_uint_eq(f.ch, 2u);
}
END_TEST

START_TEST(v1_unreferenced_group_is_parsed_and_skipped) {
  char p[FCAP];
  char gi[FCAP];
  char gj[FCAP];
  char g[FCAP];
  char b[FCAP];
  char h[FCAP];
  esc_frame_t f;
  const char *g0 = group1(gi, "10", "", 0);
  head(h, "10", "0", "1", "0011", "1 0");
  cat(g, g0, " ", group1(gj, "0", "", 0), (const char *)NULL);
  cat(b, h, " 0 ", pres_multi(p, "001", "", "000 001"), g, (const char *)NULL);
  ck_assert_int_eq(parse_new(b, &f), 0);
  ck_assert_uint_eq(f.ch, 2u);
}
END_TEST

START_TEST(v1_unreferenced_object_group_fails) {
  char p[FCAP];
  char b[FCAP];
  char h[FCAP];
  esc_frame_t f;
  head(h, "10", "0", "1", "0011", "1 0");
  cat(b, h, " 0 ", pres_single(p, "001"), "1 0 1 0 ", (const char *)NULL);
  ck_assert_int_eq(parse_new(b, &f), 0);
  ck_assert_int_eq(f.chcfg_scheme, 0);
}
END_TEST

START_TEST(v1_group_index_extension_bits) {
  char p[FCAP];
  char gi[FCAP];
  char g[FCAP];
  char b[FCAP];
  char h[FCAP];
  esc_frame_t f;
  const char *g0 = group1(gi, "10", "", 0);
  head(h, "10", "0", "1", "0011", "1 0");
  cat(g, g0, g0, g0, g0, g0, g0, g0, g0, g0, (const char *)NULL);
  cat(b, h, " 0 ", pres_single(p, "111 00 0"), g, (const char *)NULL);
  ck_assert_int_eq(parse_new(b, &f), 0);
}
END_TEST

START_TEST(v1_second_presentation_forms) {
  static const struct {
    const char *second;
  } t[] = {
      {"1 0 000 0 0 00 000 0 00 00 0 001 000000"},
      {"0 110 0 000 0 0 00 000 0 00 00 0 0"},
      {"0 110 0 0 10 0 0 0"},
      {"0 110 0 1 0"},
      {"0 111 00 0 0 000 0 0 00 000 0 00 00 0 0 0 0"},
      {"0 000 0 000 0 0 00 000 0 00 00 0 0 0 001 001 000000"},
  };
  char gi[FCAP];
  char p[FCAP];
  char g[FCAP];
  char h[FCAP];
  char b[FCAP];
  esc_frame_t f;
  const char *g0 = group1(gi, "10", "", 0);
  cat(g, g0, g0, (const char *)NULL);
  cat(h, "10", SEQ, "0", "1", "0011", "1", "0 1 0 1 00 0", (const char *)NULL);
  cat(b, h, " 0 ", pres_single(p, "000"), t[_i].second, g, (const char *)NULL);
  ck_assert_int_ne(parse_new(b, &f), 1);
}
END_TEST

START_TEST(v1_group_info_extension_forms) {
  static const struct {
    const char *head_bits;
    const char *tail_bits;
    unsigned ch;
  } t[] = {
      {"1 0 1 1", "1 000 0", 2},
      {"1 0 1 1", "1 000 1 1 0 0000000000000000", 2},
      {"1 0 1 1", "1 000 1 0 000010 00000000 00000000", 2},
      {"1 1 1 1", "0", 2},
      {"0 0 1 1", "0", 2},
      {"1 0 0 00 1", "0", 2},
      {"1 0 0 01 1", "0", 2},
      {"1 0 0 11 00 0 1", "0", 2},
  };
  char p[FCAP];
  char ic[FCAP];
  char g[FCAP];
  char b[FCAP];
  char one[FCAP];
  char h[FCAP];
  esc_frame_t f;
  unsigned reps = 1;
  const char *tailb = t[_i].tail_bits;
  const char *hdr = t[_i].head_bits;
  if (_i == 5) reps = 2;
  if (_i == 6) reps = 3;
  if (_i == 7) reps = 5;
  info_chan(ic, "10", "", 0, "0", "0", t[_i].head_bits[0] == '1' ? "00" : "");
  cat(one, ic, _i == 3 ? " 00" : "", (const char *)NULL);
  g[0] = '\0';
  cat(g, hdr, " ", (const char *)NULL);
  for (unsigned i = 0; i < reps; i++) {
    char tmp[FCAP];
    cat(tmp, g, one, " ", (const char *)NULL);
    strcpy(g, tmp);
  }
  {
    char tmp[FCAP];
    cat(tmp, g, tailb, (const char *)NULL);
    strcpy(g, tmp);
  }
  head(h, "10", "0", "1", "0011", "1 0");
  cat(b, h, " 0 ", pres_single(p, "000"), g, (const char *)NULL);
  ck_assert_int_ne(parse_new(b, &f), 1);
}
END_TEST

START_TEST(v1_info_chan_optional_fields) {
  char p[FCAP];
  char b[FCAP];
  char h[FCAP];
  char g[FCAP];
  esc_frame_t f;
  head(h, "10", "0", "1", "0011", "1 0");
  cat(g, "1 0 1 1 10 1 0 1 0 0 0 00 0", (const char *)NULL);
  cat(b, h, " 0 ", pres_single(p, "000"), g, (const char *)NULL);
  ck_assert_int_eq(parse_new(b, &f), 0);
  cat(g, "1 0 1 1 10 1 1 1 1 111 11 0 00 0", (const char *)NULL);
  cat(b, h, " 0 ", pres_single(p, "000"), g, (const char *)NULL);
  ck_assert_int_eq(parse_new(b, &f), 0);
  cat(g, "1 0 1 1 10 0 0 0 00 11 00 0 0", (const char *)NULL);
  cat(b, h, " 0 ", pres_single(p, "000"), g, (const char *)NULL);
  ck_assert_int_eq(parse_new(b, &f), 0);
}
END_TEST

START_TEST(v1_no_presentations_is_ignored) {
  char h[FCAP];
  esc_frame_t f;
  head(h, "10", "0", "1", "0011", "0 0 0");
  ck_assert_int_eq(parse_new(h, &f), 0);
  ck_assert_uint_eq(f.ch, 0u);
}
END_TEST

static int parse_exact(const char *bits, esc_frame_t *f) {
  unsigned char d[FCAP];
  esc_track_t t;
  size_t n = mk(d, sizeof d, 0, bits);
  memset(&t, 0, sizeof t);
  t.codec = CODEC_AC4;
  memset(f, 0, sizeof *f);
  return next_frame(&t, d, n, f);
}

START_TEST(toc_payload_base_cut_short_skips_presentations) {
  char h[FCAP];
  esc_frame_t f;
  head(h, "00", "0", "1", "0011", "1 1");
  ck_assert_int_eq(parse_exact(h, &f), 0);
  ck_assert_uint_eq(f.ch, 0u);
}
END_TEST

START_TEST(v0_presentation_version_run_out_of_data) {
  char h[FCAP];
  esc_frame_t f;
  head(h, "00", "0", "1", "0011", "1 0 1 11");
  ck_assert_int_eq(parse_exact(h, &f), 0);
  ck_assert_uint_eq(f.ch, 0u);
  ck_assert_uint_eq(f.ac4_presentation_version, 0u);
}
END_TEST

START_TEST(v1_truncation_sweep_never_overreads) {
  char p[FCAP];
  char g[FCAP];
  char b[FCAP];
  unsigned char d[FCAP];
  esc_track_t t;
  esc_frame_t f;
  size_t n = mk(d, sizeof d, 0, v1_frame(b, "0011", pres_single(p, "000"), group1(g, "11 11 110 1", "1 1 11", 0)));
  memset(&t, 0, sizeof t);
  t.codec = CODEC_AC4;
  for (size_t cut = 1; cut + 4 < n; cut++) {
    unsigned char c[FCAP];
    memcpy(c, d, n);
    c[2] = (unsigned char)((n - 4 - cut) >> 8);
    c[3] = (unsigned char)(n - 4 - cut);
    memset(&f, 0, sizeof f);
    ck_assert_int_ne(next_frame(&t, c, n - cut, &f), 1);
  }
}
END_TEST

START_TEST(bit_flip_sweep_stays_in_bounds) {
  char p[FCAP];
  char g[FCAP];
  char b[FCAP];
  unsigned char d[FCAP];
  size_t n = mk(d, sizeof d, 6, v1_frame(b, "0011", pres_single(p, "000"), group1(g, "11 11 110 1", "1 1 11", 0)));
  for (size_t bit = 32; bit < n * 8; bit++) {
    unsigned char c[FCAP];
    esc_track_t t;
    esc_frame_t f;
    memcpy(c, d, n);
    c[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
    memset(&t, 0, sizeof t);
    t.codec = CODEC_AC4;
    memset(&f, 0, sizeof f);
    ck_assert_int_ne(next_frame(&t, c, n, &f), 1);
  }
}
END_TEST

START_TEST(random_toc_sweep_stays_in_bounds) {
  unsigned long long s = 0x9E3779B97F4A7C15ull;
  for (int iter = 0; iter < 60000; iter++) {
    unsigned char d[64];
    esc_track_t t;
    esc_frame_t f;
    int rc;
    d[0] = 0xAC;
    d[1] = 0x40;
    d[2] = 0;
    d[3] = 40;
    for (size_t i = 4; i < sizeof d; i++) {
      s = s * 6364136223846793005ull + 1442695040888963407ull;
      d[i] = (unsigned char)(s >> 56);
    }
    d[4] = (unsigned char)((d[4] & 0x3F) | ((iter & 1) ? 0x80 : 0x40));
    memset(&t, 0, sizeof t);
    t.codec = CODEC_AC4;
    memset(&f, 0, sizeof f);
    rc = next_frame(&t, d, sizeof d, &f);
    ck_assert_int_ne(rc, 1);
    if (rc == 0) ck_assert_uint_le(f.consumed, sizeof d);
  }
}
END_TEST

static Suite *ac4_suite(void) {
  Suite *s = suite_create("ac4");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 60);
  tcase_add_test(tc, header_length_and_sync_checks);
  tcase_add_test(tc, header_crc_variant_counts_trailer);
  tcase_add_test(tc, header_extended_size_form);
  tcase_add_test(tc, toc_rejects_bad_rate_indices);
  tcase_add_test(tc, toc_rejects_body_shorter_than_header);
  tcase_add_test(tc, toc_44k_rate_and_codec_private_data);
  tcase_add_test(tc, toc_wait_frames_and_payload_base);
  tcase_add_test(tc, toc_presentation_count_forms);
  tcase_add_loop_test(tc, v0_single_substream_channel_modes, 0, NELEM(v0_modes));
  tcase_add_test(tc, v0_reserved_channel_modes_are_rejected);
  tcase_add_test(tc, v0_presentation_version_counts_leading_ones);
  tcase_add_test(tc, v0_multi_substream_configs);
  tcase_add_test(tc, v0_frame_rate_multiplier_bits_per_index);
  tcase_add_loop_test(tc, emdf_info_variants_keep_alignment, 0, 9);
  tcase_add_test(tc, v0_truncation_leaves_no_channel_info);
  tcase_add_loop_test(tc, v1_channel_modes_0_to_10_map_to_speaker_groups, 0, 11);
  tcase_add_loop_test(tc, v1_channel_modes_11_to_15, 0, 10);
  tcase_add_loop_test(tc, v1_reserved_channel_modes_fail, 0, 2);
  tcase_add_test(tc, v1_object_based_group);
  tcase_add_loop_test(tc, v1_program_id_forms, 0, 3);
  tcase_add_loop_test(tc, v1_rate_dependent_presentation_bits, 0, 15);
  tcase_add_test(tc, v1_presentation_filter_extra_bit);
  tcase_add_test(tc, v1_presentation_pver_and_trailer_variants);
  tcase_add_loop_test(tc, v1_multi_substream_group_counts, 0, 9);
  tcase_add_test(tc, v1_unreferenced_group_is_parsed_and_skipped);
  tcase_add_test(tc, v1_unreferenced_object_group_fails);
  tcase_add_test(tc, v1_group_index_extension_bits);
  tcase_add_loop_test(tc, v1_second_presentation_forms, 0, 6);
  tcase_add_loop_test(tc, v1_group_info_extension_forms, 0, 8);
  tcase_add_test(tc, v1_info_chan_optional_fields);
  tcase_add_test(tc, v1_no_presentations_is_ignored);
  tcase_add_test(tc, toc_payload_base_cut_short_skips_presentations);
  tcase_add_test(tc, v0_presentation_version_run_out_of_data);
  tcase_add_test(tc, v1_truncation_sweep_never_overreads);
  tcase_add_test(tc, bit_flip_sweep_stays_in_bounds);
  tcase_add_test(tc, random_toc_sweep_stays_in_bounds);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(ac4_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
