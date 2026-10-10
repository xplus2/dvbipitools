/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/escodec/aubuild.h"
#include "lib/demux/escodec/escodec.h"

#include "../bitstream_test.h"

START_TEST(br_align_rounds_up_to_byte_boundary) {
  static const size_t in[] = {0, 1, 7, 8, 9, 15, 16, 17};
  static const size_t out[] = {0, 8, 8, 8, 16, 16, 16, 24};
  br_t b;

  memset(&b, 0, sizeof b);
  for (size_t i = 0; i < sizeof in / sizeof in[0]; i++) {
    b.bit = in[i];
    br_align(&b);
    ck_assert_uint_eq(b.bit, out[i]);
  }
}
END_TEST

START_TEST(skip_scaling_list_consumes_one_bit_per_zero_delta) {
  static const int sizes[] = {16, 64};
  stream_t s;

  for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
    stream_open(&s);
    put_repeat(&s, 1, (unsigned)sizes[i]);
    stream_read(&s);
    skip_scaling_list(&s.b, sizes[i]);
    ck_assert_uint_eq(s.b.bit, (size_t)sizes[i]);
    ck_assert_int_eq(s.b.err, 0);
    stream_close(&s);
  }
}
END_TEST

START_TEST(skip_scaling_list_stops_when_delta_reaches_zero) {
  stream_t s;

  stream_open(&s);
  put_se(&s, 0);
  put_se(&s, 0);
  put_se(&s, -8);
  put_repeat(&s, 1, 40);
  stream_read(&s);
  skip_scaling_list(&s.b, 16);
  ck_assert_uint_eq(s.b.bit, 11u);
  ck_assert_int_eq(s.b.err, 0);
  stream_close(&s);
}
END_TEST

START_TEST(skip_scaling_list_flags_truncated_data) {
  static const unsigned fills[] = {0, 1};
  stream_t s;

  for (size_t i = 0; i < sizeof fills / sizeof fills[0]; i++) {
    stream_open(&s);
    put_repeat(&s, fills[i], 8);
    stream_read(&s);
    skip_scaling_list(&s.b, 16);
    ck_assert_int_eq(s.b.err, 1);
    stream_close(&s);
  }
}
END_TEST

START_TEST(skip_scaling_matrices_reads_lists_only_for_set_flags) {
  stream_t s;

  stream_open(&s);
  put_bits(&s, 1, 1);
  put_repeat(&s, 1, 16);
  put_repeat(&s, 0, 5);
  put_bits(&s, 1, 1);
  put_repeat(&s, 1, 64);
  put_bits(&s, 0, 1);
  stream_read(&s);
  skip_scaling_matrices(&s.b, 8);
  ck_assert_uint_eq(s.b.bit, 88u);
  ck_assert_int_eq(s.b.err, 0);
  stream_close(&s);
}
END_TEST

START_TEST(skip_scaling_matrices_flags_truncated_data) {
  stream_t s;

  stream_open(&s);
  put_repeat(&s, 1, 32);
  stream_read(&s);
  skip_scaling_matrices(&s.b, 12);
  ck_assert_int_eq(s.b.err, 1);
  stream_close(&s);
}
END_TEST

typedef struct {
  const char *name;
  unsigned present;
  unsigned additional;
  size_t expect_bit;
} gci_case_t;

static const gci_case_t gci_cases[] = {
    {"absent", 0, 0, 8},
    {"present without additional bits", 1, 0, 80},
    {"present with five additional bits", 1, 5, 88},
    {"present with maximum additional bits", 1, 255, 336},
};

START_TEST(skip_vvc_gci_consumes_and_aligns) {
  const gci_case_t *c = &gci_cases[_i];
  stream_t s;

  stream_open(&s);
  put_bits(&s, c->present, 1);
  if (c->present) {
    put_repeat(&s, 0, 69);
    put_bits(&s, c->additional, 8);
    put_repeat(&s, 0, c->additional);
  }
  pad_to_byte(&s);
  put_repeat(&s, 1, 8);
  stream_read(&s);
  skip_vvc_gci(&s.b);
  ck_assert_msg(s.b.bit == c->expect_bit, "%s: bit %zu, want %zu", c->name, s.b.bit, c->expect_bit);
  ck_assert_msg(s.b.err == 0, "%s: unexpected error", c->name);
  stream_close(&s);
}
END_TEST

START_TEST(skip_vvc_gci_flags_truncated_data) {
  stream_t s;

  stream_open(&s);
  put_bits(&s, 1, 1);
  put_repeat(&s, 0, 22);
  stream_read(&s);
  skip_vvc_gci(&s.b);
  ck_assert_int_eq(s.b.err, 1);
  stream_close(&s);
}
END_TEST

typedef struct {
  const char *name;
  unsigned max_sublayers_minus1;
  unsigned level_present_mask;
  unsigned n_sub_profiles;
  size_t expect_bit;
} ptl_case_t;

static const ptl_case_t ptl_cases[] = {
    {"no sublayers", 0, 0, 0, 32},
    {"two sublayers one with level", 2, 0x1, 2, 112},
    {"sublayer count clamped to seven", 20, 0, 0, 40},
    {"seven sublayers all with level", 7, 0x7F, 0, 96},
};

START_TEST(skip_vvc_ptl_consumes_optional_fields) {
  const ptl_case_t *c = &ptl_cases[_i];
  unsigned effective = c->max_sublayers_minus1 > 7 ? 7 : c->max_sublayers_minus1;
  stream_t s;

  stream_open(&s);
  put_repeat(&s, 0, 18);
  put_bits(&s, 0, 1);
  pad_to_byte(&s);
  for (unsigned i = 0; i < effective; i++) put_bits(&s, (c->level_present_mask >> i) & 1, 1);
  pad_to_byte(&s);
  for (unsigned i = 0; i < effective; i++)
    if ((c->level_present_mask >> i) & 1) put_bits(&s, 0xAA, 8);
  put_bits(&s, c->n_sub_profiles, 8);
  put_repeat(&s, 0, 32 * c->n_sub_profiles);
  put_repeat(&s, 1, 8);
  stream_read(&s);
  skip_vvc_ptl(&s.b, c->max_sublayers_minus1);
  ck_assert_msg(s.b.bit == c->expect_bit, "%s: bit %zu, want %zu", c->name, s.b.bit, c->expect_bit);
  ck_assert_msg(s.b.err == 0, "%s: unexpected error", c->name);
  stream_close(&s);
}
END_TEST

START_TEST(skip_vvc_ptl_flags_truncated_data) {
  static const size_t cuts[] = {1, 2, 3, 4};
  stream_t s;

  for (size_t i = 0; i < sizeof cuts / sizeof cuts[0]; i++) {
    stream_open(&s);
    put_repeat(&s, 0, 18);
    put_bits(&s, 0, 1);
    pad_to_byte(&s);
    put_bits(&s, 0, 8);
    put_bits(&s, 255, 8);
    stream_read(&s);
    s.b.len = cuts[i];
    skip_vvc_ptl(&s.b, 2);
    ck_assert_int_eq(s.b.err, 1);
    stream_close(&s);
  }
}
END_TEST

#define GARBAGE_KINDS 5
#define GARBAGE_SEEDS 48
#define GARBAGE_MAX_LEN 40

static void run_skipper(int kind, br_t *b) {
  switch (kind) {
    case 0:
      skip_scaling_list(b, 16);
      break;
    case 1:
      skip_scaling_list(b, 64);
      break;
    case 2:
      skip_scaling_matrices(b, 12);
      break;
    case 3:
      skip_vvc_gci(b);
      break;
    default:
      skip_vvc_ptl(b, 7);
      break;
  }
}

static void fill_garbage(unsigned char *buf, size_t len, unsigned seed) {
  unsigned x = seed * 2654435761u + 12345u;

  for (size_t i = 0; i < len; i++) {
    x = x * 1664525u + 1013904223u;
    buf[i] = (unsigned char)(x >> 24);
  }
  if (seed % 3 == 0) memset(buf, 0, len);
  if (seed % 3 == 1) memset(buf, 0xFF, len);
}

START_TEST(skip_helpers_survive_garbage_of_every_length) {
  unsigned char buf[GARBAGE_MAX_LEN];
  br_t b;

  for (unsigned seed = 0; seed < GARBAGE_SEEDS; seed++) {
    for (size_t len = 0; len <= GARBAGE_MAX_LEN; len++) {
      unsigned char *exact = malloc(len ? len : 1);

      ck_assert_ptr_nonnull(exact);
      fill_garbage(buf, len, seed);
      if (len) memcpy(exact, buf, len);
      b.d = exact;
      b.len = len;
      b.bit = 0;
      b.err = 0;
      run_skipper(_i, &b);
      ck_assert_uint_le(b.bit, len * 8 + 7);
      free(exact);
    }
  }
}
END_TEST

static const unsigned char sps_1080p[] = {
    0x67, 0x64, 0x00, 0x28, 0xAC, 0xD9, 0x40, 0x78, 0x02, 0x27, 0xE5, 0xC0, 0x44, 0x00, 0x00, 0x03,
    0x00, 0x04, 0x00, 0x00, 0x03, 0x00, 0xF0, 0x3C, 0x60, 0xC6, 0x58};

START_TEST(h264_dims_reads_full_sps_and_survives_every_prefix) {
  unsigned w = 0;
  unsigned h = 0;

  ck_assert_int_eq(h264_dims(sps_1080p, sizeof sps_1080p, &w, &h), 0);
  ck_assert_uint_eq(w, 1920u);
  ck_assert_uint_eq(h, 1080u);
  for (size_t len = 0; len < sizeof sps_1080p; len++) {
    unsigned char *exact = malloc(len ? len : 1);
    int r;

    ck_assert_ptr_nonnull(exact);
    if (len) memcpy(exact, sps_1080p, len);
    w = 0;
    h = 0;
    r = h264_dims(exact, len, &w, &h);
    ck_assert_msg(r == 0 || r == -1, "prefix %zu: ret %d", len, r);
    if (r == 0) ck_assert_msg(w && h, "prefix %zu: zero dimensions", len);
    free(exact);
  }
}
END_TEST

START_TEST(dims_parsers_survive_garbage_of_every_length) {
  unsigned char buf[GARBAGE_MAX_LEN];
  unsigned char ptl[12];
  unsigned chroma;
  unsigned w;
  unsigned h;

  for (unsigned seed = 0; seed < GARBAGE_SEEDS; seed++) {
    for (size_t len = 0; len <= GARBAGE_MAX_LEN; len++) {
      unsigned char *exact = malloc(len ? len : 1);
      int r;

      ck_assert_ptr_nonnull(exact);
      fill_garbage(buf, len, seed);
      if (len) memcpy(exact, buf, len);
      r = h264_dims(exact, len, &w, &h);
      ck_assert_int_le(r, 0);
      r = hevc_info(exact, len, ptl, &chroma, &w, &h);
      ck_assert_int_le(r, 0);
      r = vvc_dims(exact, len, &w, &h);
      ck_assert_int_le(r, 0);
      free(exact);
    }
  }
}
END_TEST

static const unsigned char *stream_finish(stream_t *s, size_t *len) {
  pad_to_byte(s);
  put_repeat(s, 0, 32);
  return bitwriter_data(&s->bw, len);
}

typedef struct {
  const char *name;
  unsigned profile;
  unsigned chroma;
  unsigned poc;
  unsigned frame_mbs_only;
  unsigned wmbs;
  unsigned hmus;
  unsigned crop[4];
  int want_ret;
  unsigned want_w;
  unsigned want_h;
} h264_case_t;

static const h264_case_t h264_cases[] = {
  {"baseline 1080 cropped", 66, 1, 0, 1, 119, 67, {0, 0, 0, 4}, 0, 1920, 1080},
  {"high 4:4:4 interlaced poc1", 100, 3, 1, 0, 44, 17, {0, 0, 0, 0}, 0, 720, 576},
  {"high 4:0:0 poc2", 110, 0, 2, 1, 79, 44, {0, 0, 0, 0}, 0, 1280, 720},
  {"high 4:2:2 left crop", 122, 2, 0, 1, 44, 29, {2, 0, 0, 0}, 0, 716, 480},
  {"interlaced top crop", 66, 1, 0, 0, 44, 17, {0, 0, 2, 0}, 0, 720, 568},
  {"crop leaves nothing", 66, 1, 0, 1, 0, 0, {8, 0, 0, 0}, -1, 0, 0},
};

START_TEST(h264_dims_for_each_profile_chroma_and_poc_type) {
  const h264_case_t *c = &h264_cases[_i];
  stream_t s;
  const unsigned char *d;
  size_t len;
  unsigned w = 0;
  unsigned h = 0;
  int high = c->profile >= 100;
  int ret;

  stream_open(&s);
  put_bits(&s, 0x67, 8);
  put_bits(&s, c->profile, 8);
  put_bits(&s, 0, 16);
  put_ue(&s, 0);
  if (high) {
    put_ue(&s, c->chroma);
    if (c->chroma == 3) put_bits(&s, 0, 1);
    put_ue(&s, 0);
    put_ue(&s, 0);
    put_bits(&s, 0, 1);
    put_bits(&s, 0, 1);
  }
  put_ue(&s, 0);
  put_ue(&s, c->poc);
  if (c->poc == 0) {
    put_ue(&s, 0);
  } else if (c->poc == 1) {
    put_bits(&s, 0, 1);
    put_se(&s, 0);
    put_se(&s, 0);
    put_ue(&s, 2);
    put_se(&s, 1);
    put_se(&s, 1);
  }
  put_ue(&s, 1);
  put_bits(&s, 0, 1);
  put_ue(&s, c->wmbs);
  put_ue(&s, c->hmus);
  put_bits(&s, c->frame_mbs_only, 1);
  if (!c->frame_mbs_only) put_bits(&s, 0, 1);
  put_bits(&s, 0, 1);
  if (c->crop[0] || c->crop[1] || c->crop[2] || c->crop[3]) {
    put_bits(&s, 1, 1);
    for (int i = 0; i < 4; i++) put_ue(&s, c->crop[i]);
  } else {
    put_bits(&s, 0, 1);
  }
  d = stream_finish(&s, &len);
  ret = h264_dims(d, len, &w, &h);
  ck_assert_msg(ret == c->want_ret, "%s: ret %d", c->name, ret);
  if (!ret) {
    ck_assert_msg(w == c->want_w, "%s: w %u", c->name, w);
    ck_assert_msg(h == c->want_h, "%s: h %u", c->name, h);
  }
  stream_close(&s);
}
END_TEST

typedef struct {
  const char *name;
  unsigned maxsub;
  unsigned sub_profile_mask;
  unsigned sub_level_mask;
  unsigned chroma;
  unsigned w;
  unsigned h;
  unsigned crop[4];
  int want_ret;
  unsigned want_w;
  unsigned want_h;
} hevc_case_t;

static const hevc_case_t hevc_cases[] = {
  {"single layer cropped", 0, 0, 0, 1, 1920, 1088, {0, 0, 0, 4}, 0, 1920, 1080},
  {"two sublayers 4:4:4", 2, 1, 2, 3, 3840, 2160, {0, 0, 0, 0}, 0, 3840, 2160},
  {"4:2:2 left crop", 0, 0, 0, 2, 1280, 720, {2, 0, 0, 0}, 0, 1276, 720},
  {"zero width", 0, 0, 0, 1, 0, 1080, {0, 0, 0, 0}, -1, 0, 0},
};

START_TEST(hevc_info_for_sublayers_chroma_and_cropping) {
  const hevc_case_t *c = &hevc_cases[_i];
  stream_t s;
  const unsigned char *d;
  size_t len;
  unsigned char ptl[12];
  unsigned chroma = 99;
  unsigned w = 0;
  unsigned h = 0;
  int ret;

  stream_open(&s);
  put_bits(&s, 0x4201, 16);
  put_bits(&s, 0, 4);
  put_bits(&s, c->maxsub, 3);
  put_bits(&s, 1, 1);
  for (unsigned i = 0; i < 12; i++) put_bits(&s, i + 1, 8);
  if (c->maxsub) {
    for (unsigned i = 0; i < c->maxsub; i++) {
      put_bits(&s, (c->sub_profile_mask >> i) & 1, 1);
      put_bits(&s, (c->sub_level_mask >> i) & 1, 1);
    }
    for (unsigned i = c->maxsub; i < 8; i++) put_bits(&s, 0, 2);
    for (unsigned i = 0; i < c->maxsub; i++) {
      if ((c->sub_profile_mask >> i) & 1) {
        put_bits(&s, 0, 32);
        put_bits(&s, 0, 32);
        put_bits(&s, 0, 24);
      }
      if ((c->sub_level_mask >> i) & 1) put_bits(&s, 0, 8);
    }
  }
  put_ue(&s, 0);
  put_ue(&s, c->chroma);
  if (c->chroma == 3) put_bits(&s, 0, 1);
  put_ue(&s, c->w);
  put_ue(&s, c->h);
  if (c->crop[0] || c->crop[1] || c->crop[2] || c->crop[3]) {
    put_bits(&s, 1, 1);
    for (int i = 0; i < 4; i++) put_ue(&s, c->crop[i]);
  } else {
    put_bits(&s, 0, 1);
  }
  d = stream_finish(&s, &len);
  ret = hevc_info(d, len, ptl, &chroma, &w, &h);
  ck_assert_msg(ret == c->want_ret, "%s: ret %d", c->name, ret);
  if (!ret) {
    ck_assert_msg(chroma == c->chroma, "%s: chroma %u", c->name, chroma);
    ck_assert_msg(w == c->want_w && h == c->want_h, "%s: %ux%u", c->name, w, h);
    for (unsigned i = 0; i < 12; i++) ck_assert_uint_eq(ptl[i], i + 1);
  }
  stream_close(&s);
}
END_TEST

typedef struct {
  const char *name;
  unsigned ptl_present;
  unsigned maxsub;
  unsigned resampling;
  unsigned w;
  unsigned h;
  int want_ret;
} vvc_case_t;

static const vvc_case_t vvc_cases[] = {
  {"single sublayer", 1, 0, 0, 1920, 1080, 0},
  {"sublayer levels and sub profiles with resampling", 1, 2, 1, 3840, 2160, 0},
  {"ptl not signalled in sps", 0, 0, 0, 1920, 1080, -1},
  {"zero width", 1, 0, 0, 0, 1080, -1},
};

START_TEST(vvc_dims_for_sublayers_and_unsupported_signalling) {
  const vvc_case_t *c = &vvc_cases[_i];
  stream_t s;
  const unsigned char *d;
  size_t len;
  unsigned w = 0;
  unsigned h = 0;
  int ret;

  stream_open(&s);
  put_bits(&s, 0, 24);
  put_bits(&s, c->maxsub, 3);
  put_bits(&s, 0, 4);
  put_bits(&s, c->ptl_present, 1);
  if (c->ptl_present) {
    put_repeat(&s, 0, 18);
    put_bits(&s, 0, 1);
    pad_to_byte(&s);
    for (unsigned i = 0; i < c->maxsub; i++) put_bits(&s, i == 0, 1);
    pad_to_byte(&s);
    if (c->maxsub) put_bits(&s, 0x33, 8);
    put_bits(&s, c->maxsub ? 1 : 0, 8);
    if (c->maxsub) put_bits(&s, 0, 32);
    put_bits(&s, 0, 1);
    put_bits(&s, c->resampling, 1);
    if (c->resampling) put_bits(&s, 0, 1);
    put_ue(&s, c->w);
    put_ue(&s, c->h);
  }
  d = stream_finish(&s, &len);
  ret = vvc_dims(d, len, &w, &h);
  ck_assert_msg(ret == c->want_ret, "%s: ret %d", c->name, ret);
  if (!ret) ck_assert_msg(w == c->w && h == c->h, "%s: %ux%u", c->name, w, h);
  stream_close(&s);
}
END_TEST

START_TEST(avcc_hvcc_vvcc_av1c_respect_capacity_and_layout) {
  esc_track_t t;
  unsigned char o[64];
  av1_seq_hdr_t info;
  static const unsigned char sps[] = {0x67, 0x64, 0x00, 0x28, 0xAC};
  static const unsigned char pps[] = {0x68, 0xEB};
  static const unsigned char av1_sps[] = {0x0A, 0x01, 0x18, 0x00};

  memset(&t, 0, sizeof t);
  memcpy(t.sps, sps, sizeof sps);
  t.spslen = sizeof sps;
  memcpy(t.pps, pps, sizeof pps);
  t.ppslen = sizeof pps;
  ck_assert_uint_eq(build_avcc(&t, o, 17), 0u);
  ck_assert_uint_eq(build_avcc(&t, o, 18), 18u);
  ck_assert_uint_eq(o[0], 1u);
  ck_assert_uint_eq(o[1], 0x64u);
  ck_assert_uint_eq(o[3], 0x28u);
  ck_assert_uint_eq(o[5], 0xE1u);
  t.spslen = 3;
  ck_assert_uint_eq(build_avcc(&t, o, sizeof o), 0u);

  memset(&t, 0, sizeof t);
  memcpy(t.vps, sps, 2);
  t.vpslen = 2;
  memcpy(t.sps, sps, 3);
  t.spslen = 3;
  memcpy(t.pps, pps, 2);
  t.ppslen = 2;
  for (unsigned i = 0; i < 12; i++) t.ptl[i] = (unsigned char)(0x10 + i);
  t.chroma = 1;
  ck_assert_uint_eq(build_hvcc(&t, o, 44), 0u);
  ck_assert_uint_eq(build_hvcc(&t, o, 45), 45u);
  ck_assert_uint_eq(o[0], 1u);
  ck_assert_mem_eq(o + 1, t.ptl, 12);
  ck_assert_uint_eq(o[16], 0xFDu);
  ck_assert_uint_eq(o[23], 32u);
  ck_assert_uint_eq(build_vvcc(&t, o, 23), 0u);
  ck_assert_uint_eq(build_vvcc(&t, o, 24), 24u);
  ck_assert_uint_eq(o[0], 0xFEu);
  ck_assert_uint_eq(o[1], 3u);
  ck_assert_uint_eq(o[2], (unsigned)(0x80 | VVC_NAL_VPS));

  memset(&info, 0, sizeof info);
  info.seq_profile = 1;
  info.seq_level_idx0 = 9;
  info.seq_tier0 = 1;
  info.high_bitdepth = 1;
  info.subsampling_x = 1;
  info.subsampling_y = 1;
  info.chroma_sample_pos = 2;
  ck_assert_uint_eq(build_av1c(&info, av1_sps, sizeof av1_sps, o, 7), 0u);
  ck_assert_uint_eq(build_av1c(&info, av1_sps, sizeof av1_sps, o, 8), 8u);
  ck_assert_uint_eq(o[0], 0x81u);
  ck_assert_uint_eq(o[1], 0x29u);
  ck_assert_uint_eq(o[2], 0xCEu);
  ck_assert_mem_eq(o + 4, av1_sps, sizeof av1_sps);
}
END_TEST

typedef struct {
  const char *name;
  unsigned profile;
  unsigned reduced;
  unsigned level;
  unsigned tier;
  unsigned timing;
  unsigned dec_model;
  unsigned init_delay;
  unsigned frame_id;
  unsigned order_hint;
  unsigned choose_sct;
  unsigned high_bd;
  unsigned twelve;
  unsigned mono;
  unsigned color_desc;
  unsigned cp;
  unsigned tc;
  unsigned mc;
  unsigned want_ssx;
  unsigned want_ssy;
  unsigned want_twelve;
  unsigned want_tier;
} av1_case_t;

static const av1_case_t av1_cases[] = {
  {"reduced 4:2:0", 0, 1, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0},
  {"full with timing, decoder model, frame ids, srgb", 2, 0, 9, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 13, 0, 0, 0, 1, 1},
  {"profile 2 twelve bit 4:2:2", 2, 0, 4, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 1, 0, 1, 0},
  {"monochrome", 0, 0, 4, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 1, 0, 0},
  {"profile 1 4:4:4", 1, 0, 4, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 2, 2, 2, 0, 0, 0, 0},
};

static void build_av1_seq(stream_t *s, const av1_case_t *c) {
  int srgb = c->color_desc && c->cp == 1 && c->tc == 13 && c->mc == 0;

  put_bits(s, 0, 1);
  put_bits(s, 1, 4);
  put_bits(s, 0, 1);
  put_bits(s, 1, 1);
  put_bits(s, 0, 1);
  put_bits(s, 0x10, 8);
  put_bits(s, c->profile, 3);
  put_bits(s, 0, 1);
  put_bits(s, c->reduced, 1);
  if (c->reduced) {
    put_bits(s, c->level, 5);
  } else {
    put_bits(s, c->timing, 1);
    if (c->timing) {
      put_bits(s, 0, 32);
      put_bits(s, 0, 32);
      put_bits(s, 1, 1);
      put_ue(s, 0);
      put_bits(s, c->dec_model, 1);
      if (c->dec_model) {
        put_bits(s, 2, 5);
        put_bits(s, 0, 32);
        put_bits(s, 0, 5);
        put_bits(s, 0, 5);
      }
    }
    put_bits(s, c->init_delay, 1);
    put_bits(s, 0, 5);
    put_bits(s, 0, 12);
    put_bits(s, c->level, 5);
    if (c->level > 7) put_bits(s, c->tier, 1);
    if (c->dec_model) {
      put_bits(s, 1, 1);
      put_bits(s, 0, 3);
      put_bits(s, 0, 3);
      put_bits(s, 0, 1);
    }
    if (c->init_delay) {
      put_bits(s, 1, 1);
      put_bits(s, 0, 4);
    }
  }
  put_bits(s, 10, 4);
  put_bits(s, 10, 4);
  put_bits(s, 1919, 11);
  put_bits(s, 1079, 11);
  if (!c->reduced) {
    put_bits(s, c->frame_id, 1);
    if (c->frame_id) {
      put_bits(s, 0, 4);
      put_bits(s, 0, 3);
    }
  }
  put_bits(s, 0, 3);
  if (!c->reduced) {
    put_bits(s, 0, 4);
    put_bits(s, c->order_hint, 1);
    if (c->order_hint) put_bits(s, 0, 2);
    put_bits(s, c->choose_sct, 1);
    if (!c->choose_sct) put_bits(s, 1, 1);
    put_bits(s, 1, 1);
    if (c->order_hint) put_bits(s, 0, 3);
  }
  put_bits(s, 0, 3);
  put_bits(s, c->high_bd, 1);
  if (c->profile == 2 && c->high_bd) put_bits(s, c->twelve, 1);
  if (c->profile != 1) put_bits(s, c->mono, 1);
  put_bits(s, c->color_desc, 1);
  if (c->color_desc) {
    put_bits(s, c->cp, 8);
    put_bits(s, c->tc, 8);
    put_bits(s, c->mc, 8);
  }
  if (c->mono) {
    put_bits(s, 0, 1);
  } else if (!srgb) {
    put_bits(s, 0, 1);
    if (c->profile == 0) {
      put_bits(s, 2, 2);
    } else if (c->profile == 2 && c->twelve) {
      put_bits(s, 1, 1);
      put_bits(s, 0, 1);
    }
  }
}

START_TEST(av1_seq_hdr_info_for_profiles_and_optional_blocks) {
  const av1_case_t *c = &av1_cases[_i];
  stream_t s;
  const unsigned char *d;
  size_t len;
  av1_seq_hdr_t info;
  unsigned w = 0;
  unsigned h = 0;

  stream_open(&s);
  build_av1_seq(&s, c);
  d = stream_finish(&s, &len);
  memset(&info, 0, sizeof info);
  ck_assert_msg(av1_seq_hdr_info(d, len, &info, &w, &h) == 0, "%s: rejected", c->name);
  ck_assert_msg(w == 1920 && h == 1080, "%s: %ux%u", c->name, w, h);
  ck_assert_msg(info.seq_profile == c->profile, "%s: profile", c->name);
  ck_assert_msg(info.seq_level_idx0 == c->level, "%s: level", c->name);
  ck_assert_msg(info.seq_tier0 == c->want_tier, "%s: tier", c->name);
  ck_assert_msg(info.twelve_bit == c->want_twelve, "%s: twelve", c->name);
  ck_assert_msg(info.monochrome == c->mono, "%s: mono", c->name);
  ck_assert_msg(info.subsampling_x == c->want_ssx && info.subsampling_y == c->want_ssy, "%s: subsampling %u/%u", c->name, info.subsampling_x, info.subsampling_y);
  ck_assert_int_eq(av1_seq_hdr_info(d, 3, &info, &w, &h), -1);
  stream_close(&s);
}
END_TEST

static Suite *video_suite(void) {
  Suite *s = suite_create("escodec_video");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, br_align_rounds_up_to_byte_boundary);
  tcase_add_test(tc, skip_scaling_list_consumes_one_bit_per_zero_delta);
  tcase_add_test(tc, skip_scaling_list_stops_when_delta_reaches_zero);
  tcase_add_test(tc, skip_scaling_list_flags_truncated_data);
  tcase_add_test(tc, skip_scaling_matrices_reads_lists_only_for_set_flags);
  tcase_add_test(tc, skip_scaling_matrices_flags_truncated_data);
  tcase_add_loop_test(tc, skip_vvc_gci_consumes_and_aligns, 0, (int)(sizeof gci_cases / sizeof gci_cases[0]));
  tcase_add_test(tc, skip_vvc_gci_flags_truncated_data);
  tcase_add_loop_test(tc, skip_vvc_ptl_consumes_optional_fields, 0, (int)(sizeof ptl_cases / sizeof ptl_cases[0]));
  tcase_add_test(tc, skip_vvc_ptl_flags_truncated_data);
  tcase_add_loop_test(tc, skip_helpers_survive_garbage_of_every_length, 0, GARBAGE_KINDS);
  tcase_add_test(tc, h264_dims_reads_full_sps_and_survives_every_prefix);
  tcase_add_test(tc, dims_parsers_survive_garbage_of_every_length);
  tcase_add_loop_test(tc, h264_dims_for_each_profile_chroma_and_poc_type, 0, (int)(sizeof h264_cases / sizeof h264_cases[0]));
  tcase_add_loop_test(tc, hevc_info_for_sublayers_chroma_and_cropping, 0, (int)(sizeof hevc_cases / sizeof hevc_cases[0]));
  tcase_add_loop_test(tc, vvc_dims_for_sublayers_and_unsupported_signalling, 0, (int)(sizeof vvc_cases / sizeof vvc_cases[0]));
  tcase_add_test(tc, avcc_hvcc_vvcc_av1c_respect_capacity_and_layout);
  tcase_add_loop_test(tc, av1_seq_hdr_info_for_profiles_and_optional_blocks, 0, (int)(sizeof av1_cases / sizeof av1_cases[0]));
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(video_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
