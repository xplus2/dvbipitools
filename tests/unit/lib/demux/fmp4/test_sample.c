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
  unsigned char *initbuf, *segbuf;
  size_t initlen, seglen;
  static const unsigned char frame1[] = {0x00, 0x00, 0x00, 0x05, 0x65, 0x11, 0x22, 0x33, 0x44};
  static const unsigned char frame2[] = {0x00, 0x00, 0x00, 0x03, 0x41, 0xAA, 0xBB};
  fmp4_sample_t s;
  fmp4_box_t moof, mfhd, traf, tfhd_box, tfdt_box, trun_box, mdat;
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

static Suite *fmp4_sample_suite(void) {
  Suite *s = suite_create("fmp4_sample");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, fmp4_parse_trun_samples_matches_real_writer_output);
  tcase_add_test(tc, fmp4_parse_trun_samples_second_segment_has_nonzero_base_dts);
  tcase_add_test(tc, fmp4_sample_is_keyframe_matches_writer_convention);
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
