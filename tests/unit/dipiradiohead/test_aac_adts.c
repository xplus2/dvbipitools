/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipiradiohead/framer/aac_adts.h"

/* 7-byte ADTS header, sample_rate index 4 (44100 Hz), frame_length 200, raw_data_blocks=0 */
static void build_header(unsigned char p[7], unsigned sr_idx, unsigned frame_length, unsigned raw_blocks) {
  p[0] = 0xFF;
  p[1] = 0xF1; /* MPEG-4, layer 00, protection_absent=1 */
  p[2] = (unsigned char)(0x40 | ((sr_idx & 0x0F) << 2));
  p[3] = (unsigned char)((frame_length >> 11) & 0x03);
  p[4] = (unsigned char)((frame_length >> 3) & 0xFF);
  p[5] = (unsigned char)((frame_length & 0x07) << 5);
  p[6] = (unsigned char)(raw_blocks & 0x03);
}

START_TEST(aac_adts_probe_extracts_rate_and_length) {
  unsigned char p[7];
  aac_adts_info_t info;
  build_header(p, 4, 200, 0);
  ck_assert_int_eq(aac_adts_probe(p, sizeof p, &info), 1);
  ck_assert_uint_eq(info.sample_rate, 44100u);
  ck_assert_uint_eq(info.samples_per_frame, 1024u);
  ck_assert_uint_eq(info.frame_len, 200u);
}
END_TEST

START_TEST(aac_adts_is_sync_checks_header_bits) {
  unsigned char good[2] = {0xFF, 0xF1};
  unsigned char bad[2] = {0xFF, 0x00};
  ck_assert_int_eq(aac_adts_is_sync(good, 2), 1);
  ck_assert_int_eq(aac_adts_is_sync(bad, 2), 0);
}
END_TEST

START_TEST(aac_adts_probe_needs_more_bytes) {
  unsigned char p[6];
  aac_adts_info_t info;
  memset(p, 0, sizeof p);
  p[0] = 0xFF;
  p[1] = 0xF1;
  ck_assert_int_eq(aac_adts_probe(p, sizeof p, &info), 0);
}
END_TEST

START_TEST(aac_adts_probe_rejects_bad_sample_rate_index) {
  unsigned char p[7];
  aac_adts_info_t info;
  build_header(p, 13, 200, 0); /* only 0..12 valid */
  ck_assert_int_eq(aac_adts_probe(p, sizeof p, &info), -1);
}
END_TEST

START_TEST(aac_adts_probe_rejects_multi_raw_block) {
  unsigned char p[7];
  aac_adts_info_t info;
  build_header(p, 4, 200, 1); /* only single-frame ADTS (raw_blocks=0) supported */
  ck_assert_int_eq(aac_adts_probe(p, sizeof p, &info), -1);
}
END_TEST

START_TEST(aac_adts_probe_rejects_too_short_frame_length) {
  unsigned char p[7];
  aac_adts_info_t info;
  build_header(p, 4, 3, 0); /* frame_length must be >= 7 (header size) */
  ck_assert_int_eq(aac_adts_probe(p, sizeof p, &info), -1);
}
END_TEST

static Suite *aac_adts_suite(void) {
  Suite *s = suite_create("aac_adts");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, aac_adts_probe_extracts_rate_and_length);
  tcase_add_test(tc, aac_adts_is_sync_checks_header_bits);
  tcase_add_test(tc, aac_adts_probe_needs_more_bytes);
  tcase_add_test(tc, aac_adts_probe_rejects_bad_sample_rate_index);
  tcase_add_test(tc, aac_adts_probe_rejects_multi_raw_block);
  tcase_add_test(tc, aac_adts_probe_rejects_too_short_frame_length);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(aac_adts_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
