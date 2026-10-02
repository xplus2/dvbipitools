/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

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
