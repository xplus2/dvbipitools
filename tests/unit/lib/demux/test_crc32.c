/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>

#include "lib/demux/crc32.h"

START_TEST(crc32_empty_input) {
  ck_assert_uint_eq(crc32_mpeg((const unsigned char *)"", 0), 0xFFFFFFFFu);
}
END_TEST

START_TEST(crc32_known_vector) {
  /* "123456789", MPEG-2/DVB CRC32 (poly 0x04C11DB7, init 0xFFFFFFFF, no
   * reflect, no final xor) - standard check value for this variant. */
  const unsigned char data[] = "123456789";
  ck_assert_uint_eq(crc32_mpeg(data, 9), 0x0376E6E7u);
}
END_TEST

START_TEST(crc32_appended_own_crc_is_zero) {
  unsigned char buf[13] = "123456789";
  uint32_t crc = crc32_mpeg(buf, 9);
  buf[9] = (unsigned char)(crc >> 24);
  buf[10] = (unsigned char)(crc >> 16);
  buf[11] = (unsigned char)(crc >> 8);
  buf[12] = (unsigned char)crc;
  ck_assert_uint_eq(crc32_mpeg(buf, 13), 0u);
}
END_TEST

static Suite *crc32_suite(void) {
  Suite *s = suite_create("crc32");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, crc32_empty_input);
  tcase_add_test(tc, crc32_known_vector);
  tcase_add_test(tc, crc32_appended_own_crc_is_zero);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(crc32_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
