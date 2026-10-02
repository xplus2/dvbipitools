/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/fmp4/sample.h"
#include "lib/mux/fmp4/fmp4.h"

static fmp4_mux_t *build_mux(void) {
  fmp4_track_cfg_t cfg;
  static const unsigned char avcc[] = {0x01, 0x64, 0x00, 0x1F, 0xFF, 0xE1, 0x00, 0x00, 0x01, 0x00, 0x00};
  memset(&cfg, 0, sizeof cfg);
  cfg.codec = CODEC_H264;
  cfg.track_id = 7;
  cfg.timescale = 90000;
  cfg.width = 1280;
  cfg.height = 720;
  cfg.cpriv = avcc;
  cfg.cpriv_len = sizeof avcc;
  return fmp4_mux_new(&cfg, 1);
}

START_TEST(fmp4_parse_trun_samples_matches_real_writer_output) {
  fmp4_mux_t *m = build_mux();
  unsigned char *initbuf;
  unsigned char *segbuf;
  size_t initlen;
  size_t seglen;
  static const unsigned char frame1[] = {0x00, 0x00, 0x00, 0x05, 0x65, 0x11, 0x22, 0x33, 0x44};
  static const unsigned char frame2[] = {0x00, 0x00, 0x00, 0x03, 0x41, 0xAA, 0xBB};
  fmp4_sample_t s;
  fmp4_box_t moof;
  fmp4_box_t mfhd;
  fmp4_box_t traf;
  fmp4_box_t tfhd_box;
  fmp4_box_t tfdt_box;
  fmp4_box_t trun_box;
  fmp4_box_t mdat;
  fmp4_tfhd_t tfhd;
  uint64_t base_dts;
  fmp4_dec_sample_t out[8];
  unsigned n;

  ck_assert_ptr_nonnull(m);
  initlen = fmp4_init_segment(m, &initbuf);
  ck_assert_uint_gt(initlen, 0u);

  fmp4_segment_begin(m, 5);
  memset(&s, 0, sizeof s);
  s.track_idx = 0;
  s.data = frame1;
  s.size = sizeof frame1;
  s.duration = 3000;
  s.cts_offset = 0;
  s.keyframe = 1;
  fmp4_segment_add_sample(m, &s);

  s.data = frame2;
  s.size = sizeof frame2;
  s.duration = 3003;
  s.cts_offset = 6000;
  s.keyframe = 0;
  fmp4_segment_add_sample(m, &s);

  seglen = fmp4_segment_end(m, &segbuf);
  ck_assert_uint_gt(seglen, 0u);

  ck_assert_int_eq(fmp4_box_find(segbuf, seglen, "moof", &moof), 1);
  ck_assert_int_eq(fmp4_box_find(moof.body, moof.body_len, "mfhd", &mfhd), 1);
  ck_assert_uint_eq(fmp4_rb_u32(mfhd.body + 4), 5u);
  ck_assert_int_eq(fmp4_box_find(moof.body, moof.body_len, "traf", &traf), 1);
  ck_assert_int_eq(fmp4_box_find(traf.body, traf.body_len, "tfhd", &tfhd_box), 1);
  ck_assert_int_eq(fmp4_box_find(traf.body, traf.body_len, "tfdt", &tfdt_box), 1);
  ck_assert_int_eq(fmp4_box_find(traf.body, traf.body_len, "trun", &trun_box), 1);
  ck_assert_int_eq(fmp4_box_find(segbuf, seglen, "mdat", &mdat), 1);

  ck_assert_int_eq(fmp4_parse_tfhd(tfhd_box.body, tfhd_box.body_len, &tfhd), 1);
  ck_assert_uint_eq(tfhd.track_id, 7u);
  ck_assert_int_eq(tfhd.default_base_is_moof, 1);
  ck_assert_int_eq(tfhd.have_default_sample_duration, 0);

  ck_assert_int_eq(fmp4_parse_tfdt(tfdt_box.body, tfdt_box.body_len, &base_dts), 1);
  ck_assert_uint_eq((unsigned)base_dts, 0u);

  n = fmp4_parse_trun_samples(trun_box.body, trun_box.body_len, &tfhd, &moof, mdat.body, mdat.body_len, out, 8);
  ck_assert_uint_eq(n, 2u);

  ck_assert_uint_eq(out[0].size, sizeof frame1);
  ck_assert_int_eq(memcmp(out[0].data, frame1, sizeof frame1), 0);
  ck_assert_uint_eq(out[0].duration, 3000u);
  ck_assert_int_eq(out[0].cts_offset, 0);
  ck_assert_int_eq(fmp4_sample_is_keyframe(out[0].flags), 1);

  ck_assert_uint_eq(out[1].size, sizeof frame2);
  ck_assert_int_eq(memcmp(out[1].data, frame2, sizeof frame2), 0);
  ck_assert_uint_eq(out[1].duration, 3003u);
  ck_assert_int_eq(out[1].cts_offset, 6000);
  ck_assert_int_eq(fmp4_sample_is_keyframe(out[1].flags), 0);

  fmp4_mux_free(m);
}
END_TEST

START_TEST(fmp4_parse_trun_samples_second_segment_has_nonzero_base_dts) {
  fmp4_mux_t *m = build_mux();
  unsigned char *initbuf;
  unsigned char *segbuf;
  size_t initlen;
  size_t seglen;
  static const unsigned char frame[] = {0x00, 0x00, 0x00, 0x02, 0x65, 0x01};
  fmp4_sample_t s;
  fmp4_box_t moof;
  fmp4_box_t traf;
  fmp4_box_t tfdt_box;
  uint64_t base_dts;

  ck_assert_ptr_nonnull(m);
  initlen = fmp4_init_segment(m, &initbuf);
  ck_assert_uint_gt(initlen, 0u);

  fmp4_segment_begin(m, 1);
  memset(&s, 0, sizeof s);
  s.track_idx = 0;
  s.data = frame;
  s.size = sizeof frame;
  s.duration = 3000;
  s.keyframe = 1;
  fmp4_segment_add_sample(m, &s);
  seglen = fmp4_segment_end(m, &segbuf);
  ck_assert_uint_gt(seglen, 0u);

  fmp4_segment_begin(m, 2);
  fmp4_segment_add_sample(m, &s);
  seglen = fmp4_segment_end(m, &segbuf);
  ck_assert_uint_gt(seglen, 0u);

  ck_assert_int_eq(fmp4_box_find(segbuf, seglen, "moof", &moof), 1);
  ck_assert_int_eq(fmp4_box_find(moof.body, moof.body_len, "traf", &traf), 1);
  ck_assert_int_eq(fmp4_box_find(traf.body, traf.body_len, "tfdt", &tfdt_box), 1);
  ck_assert_int_eq(fmp4_parse_tfdt(tfdt_box.body, tfdt_box.body_len, &base_dts), 1);
  ck_assert_uint_eq((unsigned)base_dts, 3000u);

  fmp4_mux_free(m);
}
END_TEST

START_TEST(fmp4_sample_is_keyframe_matches_writer_convention) {
  ck_assert_int_eq(fmp4_sample_is_keyframe(0x02000000u), 1);
  ck_assert_int_eq(fmp4_sample_is_keyframe(0x01010000u), 0);
}
END_TEST

typedef struct {
  const char *name;
  unsigned char body[16];
  size_t len;
  int ret;
} tfhd_case_t;

static const tfhd_case_t tfhd_cases[] = {
    {"track id only", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07}, 8, 1},
    {"shorter than fixed header", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 7, 0},
    {"base data offset cut", {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00, 0x00}, 12, 0},
    {"sample description index cut", {0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00}, 11, 0},
    {"default duration cut", {0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00}, 11, 0},
    {"default size cut", {0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00}, 11, 0},
    {"default flags cut", {0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00}, 11, 0},
};

START_TEST(fmp4_parse_tfhd_rejects_truncated_bodies) {
  const tfhd_case_t *c = &tfhd_cases[_i];
  fmp4_tfhd_t out;
  int ret = fmp4_parse_tfhd(c->body, c->len, &out);

  ck_assert_msg(ret == c->ret, "%s: ret %d, want %d", c->name, ret, c->ret);
}
END_TEST

START_TEST(fmp4_parse_tfhd_reads_every_optional_field) {
  static const unsigned char body[] = {
      0x00, 0x02, 0x00, 0x3B, 0x00, 0x00, 0x00, 0x09,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
      0x00, 0x00, 0x00, 0x01,
      0x00, 0x00, 0x0B, 0xB8,
      0x00, 0x00, 0x00, 0x10,
      0x01, 0x01, 0x00, 0x00};
  fmp4_tfhd_t out;

  ck_assert_int_eq(fmp4_parse_tfhd(body, sizeof body, &out), 1);
  ck_assert_uint_eq(out.track_id, 9u);
  ck_assert_int_eq(out.have_base_data_offset, 1);
  ck_assert_uint_eq((unsigned)out.base_data_offset, 256u);
  ck_assert_int_eq(out.have_default_sample_duration, 1);
  ck_assert_uint_eq(out.default_sample_duration, 3000u);
  ck_assert_int_eq(out.have_default_sample_size, 1);
  ck_assert_uint_eq(out.default_sample_size, 16u);
  ck_assert_int_eq(out.have_default_sample_flags, 1);
  ck_assert_uint_eq(out.default_sample_flags, 0x01010000u);
  ck_assert_int_eq(out.default_base_is_moof, 1);
}
END_TEST

typedef struct {
  const char *name;
  unsigned char body[12];
  size_t len;
  int ret;
  unsigned long long value;
} tfdt_case_t;

static const tfdt_case_t tfdt_cases[] = {
    {"shorter than version and flags", {0x00, 0x00, 0x00}, 3, 0, 0},
    {"v0 time cut", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 7, 0, 0},
    {"v0 time", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0B, 0xB8}, 8, 1, 3000},
    {"v1 time cut", {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00}, 11, 0, 0},
    {"v1 time", {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02}, 12, 1, 0x100000002ull},
};

START_TEST(fmp4_parse_tfdt_handles_short_and_wide_times) {
  const tfdt_case_t *c = &tfdt_cases[_i];
  uint64_t value = 0;
  int ret = fmp4_parse_tfdt(c->body, c->len, &value);

  ck_assert_msg(ret == c->ret, "%s: ret %d, want %d", c->name, ret, c->ret);
  ck_assert_msg(value == c->value, "%s: value %llu", c->name, (unsigned long long)value);
}
END_TEST

#define TRUN_MOOF_LEN 16
#define TRUN_MDAT_MAX 64

typedef struct {
  const char *name;
  unsigned char trun[32];
  size_t trun_len;
  size_t mdat_len;
  unsigned max;
  int base_offset_not_moof;
  unsigned default_size;
  unsigned expect_n;
  size_t expect_first_size;
} trun_case_t;

static const trun_case_t trun_cases[] = {
    {"shorter than fixed header", {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 7, 32, 8, 0, 0, 0, 0},
    {"empty sample table", {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10}, 12, 32, 8, 0, 0, 0, 0},
    {"single sample size in trun", {0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x08}, 16, 32, 8, 0, 0, 1, 8},
    {"single sample default size", {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x10}, 12, 32, 8, 0, 12, 1, 12},
    {"data offset cut", {0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01}, 8, 32, 8, 0, 0, 0, 0},
    {"first sample flags cut", {0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x10}, 12, 32, 8, 0, 0, 0, 0},
    {"data offset before mdat", {0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08}, 16, 32, 8, 0, 0, 0, 0},
    {"data offset past mdat end", {0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x08}, 16, 32, 8, 0, 0, 0, 0},
    {"sample larger than mdat", {0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x40}, 16, 32, 8, 0, 0, 0, 0},
    {"second record cut", {0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x08}, 16, 32, 8, 0, 0, 1, 8},
    {"second sample larger than mdat", {0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x40}, 20, 32, 8, 0, 0, 1, 8},
    {"huge count with one record", {0x00, 0x00, 0x02, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x08}, 16, 32, 8, 0, 0, 1, 8},
    {"output limit", {0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04}, 24, 32, 2, 0, 0, 2, 4},
    {"base data offset without default-base-is-moof", {0x00, 0x00, 0x02, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x08}, 16, 32, 8, 1, 0, 0, 0},
};

typedef struct {
  unsigned char buf[TRUN_MOOF_LEN + TRUN_MDAT_MAX];
  fmp4_box_t moof;
  fmp4_tfhd_t tfhd;
} trun_fixture_t;

static void init_trun_fixture(trun_fixture_t *fx) {
  memset(fx, 0, sizeof *fx);
  for (size_t i = 0; i < sizeof fx->buf; i++) fx->buf[i] = (unsigned char)i;
  fx->moof.start = fx->buf;
  fx->tfhd.default_base_is_moof = 1;
}

START_TEST(fmp4_parse_trun_samples_edge_cases) {
  const trun_case_t *c = &trun_cases[_i];
  trun_fixture_t fx;
  fmp4_dec_sample_t out[8];
  unsigned n;

  init_trun_fixture(&fx);
  if (c->default_size) {
    fx.tfhd.have_default_sample_size = 1;
    fx.tfhd.default_sample_size = c->default_size;
  }
  if (c->base_offset_not_moof) {
    fx.tfhd.have_base_data_offset = 1;
    fx.tfhd.default_base_is_moof = 0;
  }
  n = fmp4_parse_trun_samples(c->trun, c->trun_len, &fx.tfhd, &fx.moof, fx.buf + TRUN_MOOF_LEN, c->mdat_len, out, c->max);
  ck_assert_msg(n == c->expect_n, "%s: %u samples, want %u", c->name, n, c->expect_n);
  if (n) {
    ck_assert_msg(out[0].size == c->expect_first_size, "%s: first size %zu", c->name, out[0].size);
    ck_assert_msg(out[0].data == fx.buf + TRUN_MOOF_LEN, "%s: first sample position", c->name);
  }
}
END_TEST

START_TEST(fmp4_parse_trun_samples_applies_first_flags_and_signed_cts) {
  static const unsigned char trun[] = {
      0x00, 0x00, 0x0A, 0x05,
      0x00, 0x00, 0x00, 0x02,
      0x00, 0x00, 0x00, 0x10,
      0x02, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x04, 0xFF, 0xFF, 0xFF, 0xFA,
      0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x07};
  trun_fixture_t fx;
  fmp4_dec_sample_t out[8];
  unsigned n;

  init_trun_fixture(&fx);
  fx.tfhd.have_default_sample_flags = 1;
  fx.tfhd.default_sample_flags = 0x01010000u;
  n = fmp4_parse_trun_samples(trun, sizeof trun, &fx.tfhd, &fx.moof, fx.buf + TRUN_MOOF_LEN, 32, out, 8);
  ck_assert_uint_eq(n, 2u);
  ck_assert_uint_eq(out[0].flags, 0x02000000u);
  ck_assert_int_eq(out[0].cts_offset, -6);
  ck_assert_uint_eq(out[1].flags, 0x01010000u);
  ck_assert_int_eq(out[1].cts_offset, 7);
  ck_assert_ptr_eq(out[1].data, fx.buf + TRUN_MOOF_LEN + 4);
}
END_TEST

static Suite *fmp4_sample_suite(void) {
  Suite *s = suite_create("fmp4_sample");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, fmp4_parse_trun_samples_matches_real_writer_output);
  tcase_add_test(tc, fmp4_parse_trun_samples_second_segment_has_nonzero_base_dts);
  tcase_add_test(tc, fmp4_sample_is_keyframe_matches_writer_convention);
  tcase_add_loop_test(tc, fmp4_parse_tfhd_rejects_truncated_bodies, 0, (int)(sizeof tfhd_cases / sizeof tfhd_cases[0]));
  tcase_add_test(tc, fmp4_parse_tfhd_reads_every_optional_field);
  tcase_add_loop_test(tc, fmp4_parse_tfdt_handles_short_and_wide_times, 0, (int)(sizeof tfdt_cases / sizeof tfdt_cases[0]));
  tcase_add_loop_test(tc, fmp4_parse_trun_samples_edge_cases, 0, (int)(sizeof trun_cases / sizeof trun_cases[0]));
  tcase_add_test(tc, fmp4_parse_trun_samples_applies_first_flags_and_signed_cts);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(fmp4_sample_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
