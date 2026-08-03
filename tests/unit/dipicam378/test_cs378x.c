/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipicam378/cs378x.h"

START_TEST(crc32_matches_standard_check_value) {
  /* "123456789" -> 0xCBF43926 is the universal CRC-32/ISO-HDLC check value,
     confirms this is the same reflected/0xEDB88320/0xFFFFFFFF variant oscam uses */
  const unsigned char data[] = "123456789";
  cs378x_crc32_init_table();
  ck_assert_uint_eq(cs378x_crc32(data, 9), 0xCBF43926u);
}
END_TEST

START_TEST(crc32_of_empty_is_zero) {
  cs378x_crc32_init_table();
  ck_assert_uint_eq(cs378x_crc32((const unsigned char *)"", 0), 0);
}
END_TEST

START_TEST(md5_matches_known_vectors) {
  unsigned char out[16];
  const unsigned char md5_empty[16] = {
      0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04,
      0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e};
  const unsigned char md5_abc[16] = {
      0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0,
      0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1, 0x7f, 0x72};

  ck_assert_int_eq(cs378x_md5((const unsigned char *)"", 0, out), 0);
  ck_assert_mem_eq(out, md5_empty, 16);

  ck_assert_int_eq(cs378x_md5((const unsigned char *)"abc", 3, out), 0);
  ck_assert_mem_eq(out, md5_abc, 16);
}
END_TEST

START_TEST(aes128_ecb_roundtrip) {
  unsigned char key[16];
  unsigned char buf[32], orig[32];
  int i;

  for (i = 0; i < 16; i++)
    key[i] = (unsigned char)(i * 11);
  for (i = 0; i < 32; i++)
    buf[i] = (unsigned char)(i ^ 0x5A);
  memcpy(orig, buf, sizeof buf);

  ck_assert_int_eq(cs378x_aes128_ecb(key, buf, sizeof buf, 1), 0);
  ck_assert_mem_ne(buf, orig, sizeof buf); /* actually changed */
  ck_assert_int_eq(cs378x_aes128_ecb(key, buf, sizeof buf, 0), 0);
  ck_assert_mem_eq(buf, orig, sizeof buf); /* recovered */
}
END_TEST

START_TEST(aes128_ecb_rejects_non_block_length) {
  unsigned char key[16] = {0};
  unsigned char buf[10] = {0};
  ck_assert_int_eq(cs378x_aes128_ecb(key, buf, sizeof buf, 1), -1);
}
END_TEST

START_TEST(frame_boundary_rounds_up_to_16) {
  ck_assert_uint_eq(cs378x_frame_boundary(1), 16);
  ck_assert_uint_eq(cs378x_frame_boundary(16), 16);
  ck_assert_uint_eq(cs378x_frame_boundary(17), 32);
  ck_assert_uint_eq(cs378x_frame_boundary(32), 32);
  ck_assert_uint_eq(cs378x_frame_boundary(36), 48);
}
END_TEST

static Suite *cs378x_suite(void) {
  Suite *s = suite_create("cs378x");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, crc32_matches_standard_check_value);
  tcase_add_test(tc, crc32_of_empty_is_zero);
  tcase_add_test(tc, md5_matches_known_vectors);
  tcase_add_test(tc, aes128_ecb_roundtrip);
  tcase_add_test(tc, aes128_ecb_rejects_non_block_length);
  tcase_add_test(tc, frame_boundary_rounds_up_to_16);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(cs378x_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
