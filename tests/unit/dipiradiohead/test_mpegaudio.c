/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>

#include "dipiradiohead/framer/mpegaudio.h"

START_TEST(mpegaudio_probe_mpeg1_layer3_128kbps_44100) {
  /* MPEG1 Layer III, 128 kbps, 44100 Hz, no padding */
  unsigned char p[4] = {0xFF, 0xFA, 0x90, 0x00};
  mpegaudio_info_t info;
  ck_assert_int_eq(mpegaudio_probe(p, sizeof p, &info), 1);
  ck_assert_uint_eq(info.sample_rate, 44100u);
  ck_assert_uint_eq(info.samples_per_frame, 1152u);
  ck_assert_uint_eq(info.frame_len, 417u);
}
END_TEST

START_TEST(mpegaudio_probe_mpeg1_layer1_128kbps_44100) {
  /* MPEG1 Layer I, 128 kbps, 44100 Hz, no padding */
  unsigned char p[4] = {0xFF, 0xFE, 0x40, 0x00};
  mpegaudio_info_t info;
  ck_assert_int_eq(mpegaudio_probe(p, sizeof p, &info), 1);
  ck_assert_uint_eq(info.sample_rate, 44100u);
  ck_assert_uint_eq(info.samples_per_frame, 384u);
  ck_assert_uint_eq(info.frame_len, 136u);
}
END_TEST

START_TEST(mpegaudio_is_sync_checks_first_two_bytes) {
  unsigned char good[2] = {0xFF, 0xE0};
  unsigned char bad[2] = {0xFF, 0x00};
  ck_assert_int_eq(mpegaudio_is_sync(good, 2), 1);
  ck_assert_int_eq(mpegaudio_is_sync(bad, 2), 0);
  ck_assert_int_eq(mpegaudio_is_sync(good, 1), 0); /* not enough bytes */
}
END_TEST

START_TEST(mpegaudio_probe_needs_more_bytes) {
  unsigned char p[3] = {0xFF, 0xFA, 0x80};
  mpegaudio_info_t info;
  ck_assert_int_eq(mpegaudio_probe(p, sizeof p, &info), 0);
}
END_TEST

START_TEST(mpegaudio_probe_rejects_reserved_version) {
  unsigned char p[4] = {0xFF, 0xE8, 0x80, 0x00}; /* version bits = 01 (reserved) */
  mpegaudio_info_t info;
  ck_assert_int_eq(mpegaudio_probe(p, sizeof p, &info), -1);
}
END_TEST

START_TEST(mpegaudio_probe_rejects_free_and_bad_bitrate_index) {
  mpegaudio_info_t info;
  unsigned char free_bw[4] = {0xFF, 0xFA, 0x00, 0x00};  /* br_idx = 0 (free format, unsupported here) */
  unsigned char bad_bw[4] = {0xFF, 0xFA, 0xF0, 0x00};   /* br_idx = 15 (reserved) */
  ck_assert_int_eq(mpegaudio_probe(free_bw, sizeof free_bw, &info), -1);
  ck_assert_int_eq(mpegaudio_probe(bad_bw, sizeof bad_bw, &info), -1);
}
END_TEST

static Suite *mpegaudio_suite(void) {
  Suite *s = suite_create("mpegaudio");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, mpegaudio_probe_mpeg1_layer3_128kbps_44100);
  tcase_add_test(tc, mpegaudio_probe_mpeg1_layer1_128kbps_44100);
  tcase_add_test(tc, mpegaudio_is_sync_checks_first_two_bytes);
  tcase_add_test(tc, mpegaudio_probe_needs_more_bytes);
  tcase_add_test(tc, mpegaudio_probe_rejects_reserved_version);
  tcase_add_test(tc, mpegaudio_probe_rejects_free_and_bad_bitrate_index);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(mpegaudio_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
