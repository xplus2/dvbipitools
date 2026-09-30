/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/fmp4/box.h"
#include "lib/mux/fmp4/fmp4.h"

static fmp4_mux_t *build_one_video_track_mux(void) {
  fmp4_track_cfg_t cfg;
  static const unsigned char avcc[] = {0x01, 0x64, 0x00, 0x1F, 0xFF, 0xE1, 0x00, 0x00, 0x01, 0x00, 0x00};
  memset(&cfg, 0, sizeof cfg);
  cfg.codec = CODEC_H264;
  cfg.track_id = 1;
  cfg.timescale = 90000;
  cfg.width = 1920;
  cfg.height = 1080;
  cfg.cpriv = avcc;
  cfg.cpriv_len = sizeof avcc;
  return fmp4_mux_new(&cfg, 1);
}

START_TEST(fmp4_box_read_locates_ftyp_and_moov_at_top_level) {
  fmp4_mux_t *m = build_one_video_track_mux();
  unsigned char *out;
  size_t len;
  fmp4_box_t ftyp, moov;
  ck_assert_ptr_nonnull(m);
  len = fmp4_init_segment(m, &out);
  ck_assert_uint_gt(len, 0u);

  ck_assert_int_eq(fmp4_box_find(out, len, "ftyp", &ftyp), 1);
  ck_assert_int_eq(fmp4_box_find(out, len, "moov", &moov), 1);
  ck_assert_uint_gt(moov.body_len, 0u);
  fmp4_mux_free(m);
}
END_TEST

START_TEST(fmp4_box_read_walks_moov_trak_mdia_minf_stbl_stsd) {
  fmp4_mux_t *m = build_one_video_track_mux();
  unsigned char *out;
  size_t len;
  fmp4_box_t moov, trak, tkhd, mdia, mdhd, hdlr, minf, stbl, stsd;
  ck_assert_ptr_nonnull(m);
  len = fmp4_init_segment(m, &out);

  ck_assert_int_eq(fmp4_box_find(out, len, "moov", &moov), 1);
  ck_assert_int_eq(fmp4_box_find(moov.body, moov.body_len, "trak", &trak), 1);
  ck_assert_int_eq(fmp4_box_find(trak.body, trak.body_len, "tkhd", &tkhd), 1);
  ck_assert_int_eq(fmp4_box_find(trak.body, trak.body_len, "mdia", &mdia), 1);
  ck_assert_int_eq(fmp4_box_find(mdia.body, mdia.body_len, "mdhd", &mdhd), 1);
  ck_assert_int_eq(fmp4_box_find(mdia.body, mdia.body_len, "hdlr", &hdlr), 1);
  ck_assert_int_eq(fmp4_box_find(mdia.body, mdia.body_len, "minf", &minf), 1);
  ck_assert_int_eq(fmp4_box_find(minf.body, minf.body_len, "stbl", &stbl), 1);
  ck_assert_int_eq(fmp4_box_find(stbl.body, stbl.body_len, "stsd", &stsd), 1);

  /* tkhd: version(1)+flags(3)+ctime(4)+mtime(4)+track_ID(4) -> track_ID at offset 12 */
  ck_assert_uint_eq(fmp4_rb_u32(tkhd.body + 12), 1u);
  /* mdhd: version(1)+flags(3)+ctime(4)+mtime(4)+timescale(4) -> timescale at offset 12 */
  ck_assert_uint_eq(fmp4_rb_u32(mdhd.body + 12), 90000u);

  fmp4_mux_free(m);
}
END_TEST

START_TEST(fmp4_box_read_walks_moov_mvhd) {
  fmp4_mux_t *m = build_one_video_track_mux();
  unsigned char *out;
  size_t len;
  fmp4_box_t moov, mvhd;
  ck_assert_ptr_nonnull(m);
  len = fmp4_init_segment(m, &out);
  ck_assert_int_eq(fmp4_box_find(out, len, "moov", &moov), 1);
  ck_assert_int_eq(fmp4_box_find(moov.body, moov.body_len, "mvhd", &mvhd), 1);
  fmp4_mux_free(m);
}
END_TEST

START_TEST(fmp4_box_read_walks_moof_mfhd_traf_tfhd_tfdt_trun_and_mdat) {
  fmp4_mux_t *m = build_one_video_track_mux();
  unsigned char *initbuf, *out;
  size_t initlen, len;
  fmp4_sample_t s;
  static const unsigned char frame[] = {0x00, 0x00, 0x00, 0x04, 0x65, 0xAA, 0xBB, 0xCC};
  fmp4_box_t moof, mfhd, traf, tfhd, tfdt, trun, mdat;

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
  len = fmp4_segment_end(m, &out);
  ck_assert_uint_gt(len, 0u);

  ck_assert_int_eq(fmp4_box_find(out, len, "moof", &moof), 1);
  ck_assert_int_eq(fmp4_box_find(moof.body, moof.body_len, "mfhd", &mfhd), 1);
  ck_assert_int_eq(fmp4_box_find(moof.body, moof.body_len, "traf", &traf), 1);
  ck_assert_int_eq(fmp4_box_find(traf.body, traf.body_len, "tfhd", &tfhd), 1);
  ck_assert_int_eq(fmp4_box_find(traf.body, traf.body_len, "tfdt", &tfdt), 1);
  ck_assert_int_eq(fmp4_box_find(traf.body, traf.body_len, "trun", &trun), 1);
  ck_assert_int_eq(fmp4_box_find(out, len, "mdat", &mdat), 1);

  /* mfhd: version(1)+flags(3)+sequence_number(4) */
  ck_assert_uint_eq(fmp4_rb_u32(mfhd.body + 4), 1u);
  /* tfhd: version(1)+flags(3)+track_ID(4) */
  ck_assert_uint_eq(fmp4_rb_u32(tfhd.body + 4), 1u);
  /* trun v1: version(1)+flags(3)+sample_count(4) */
  ck_assert_uint_eq(fmp4_rb_u32(trun.body + 4), 1u);
  ck_assert_uint_eq(mdat.body_len, sizeof frame);
  ck_assert_int_eq(memcmp(mdat.body, frame, sizeof frame), 0);

  fmp4_mux_free(m);
}
END_TEST

START_TEST(fmp4_box_read_rejects_truncated_header) {
  unsigned char tiny[4] = {0, 0, 0, 8};
  fmp4_box_t b;
  ck_assert_int_eq(fmp4_box_read(tiny, tiny + sizeof tiny, &b), 0);
}
END_TEST

START_TEST(fmp4_box_find_returns_zero_when_absent) {
  unsigned char buf[8] = {0, 0, 0, 8, 'x', 'x', 'x', 'x'};
  fmp4_box_t b;
  ck_assert_int_eq(fmp4_box_find(buf, sizeof buf, "moov", &b), 0);
}
END_TEST

static Suite *fmp4_box_suite(void) {
  Suite *s = suite_create("fmp4_box");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, fmp4_box_read_locates_ftyp_and_moov_at_top_level);
  tcase_add_test(tc, fmp4_box_read_walks_moov_trak_mdia_minf_stbl_stsd);
  tcase_add_test(tc, fmp4_box_read_walks_moov_mvhd);
  tcase_add_test(tc, fmp4_box_read_walks_moof_mfhd_traf_tfhd_tfdt_trun_and_mdat);
  tcase_add_test(tc, fmp4_box_read_rejects_truncated_header);
  tcase_add_test(tc, fmp4_box_find_returns_zero_when_absent);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(fmp4_box_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
