/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>

#include "lib/demux/bitreader.h"

START_TEST(br_u_reads_msb_first_across_bytes) {
  static const unsigned char d[] = {0xF0, 0x0F};
  br_t b = {d, sizeof d, 0, 0};
  ck_assert_uint_eq(br_u(&b, 12), 0xF00u);
  ck_assert_int_eq(b.err, 0);
}
END_TEST

START_TEST(br_u_sets_err_on_short_read) {
  static const unsigned char d[] = {0xAB};
  br_t b = {d, sizeof d, 0, 0};
  ck_assert_uint_eq(br_u(&b, 16), 0xABu);
  ck_assert_int_eq(b.err, 1);
}
END_TEST

START_TEST(br_ue_matches_known_codes) {
  /* "1" -> 0, "010" -> 1, "011" -> 2, "00100" -> 3 (Exp-Golomb table) */
  static const unsigned char d1[] = {0x80};       /* 1000 0000 */
  static const unsigned char d2[] = {0x40};       /* 0100 0000 */
  static const unsigned char d3[] = {0x60};       /* 0110 0000 */
  static const unsigned char d4[] = {0x20};       /* 0010 0000 */
  br_t b1 = {d1, sizeof d1, 0, 0};
  br_t b2 = {d2, sizeof d2, 0, 0};
  br_t b3 = {d3, sizeof d3, 0, 0};
  br_t b4 = {d4, sizeof d4, 0, 0};
  ck_assert_uint_eq(br_ue(&b1), 0u);
  ck_assert_uint_eq(br_ue(&b2), 1u);
  ck_assert_uint_eq(br_ue(&b3), 2u);
  ck_assert_uint_eq(br_ue(&b4), 3u);
}
END_TEST

START_TEST(br_se_maps_ue_to_signed) {
  static const unsigned char d1[] = {0x40}; /* ue=1 -> se=1 */
  static const unsigned char d2[] = {0x60}; /* ue=2 -> se=-1 */
  static const unsigned char d3[] = {0x20}; /* ue=3 -> se=2 */
  br_t b1 = {d1, sizeof d1, 0, 0};
  br_t b2 = {d2, sizeof d2, 0, 0};
  br_t b3 = {d3, sizeof d3, 0, 0};
  ck_assert_int_eq(br_se(&b1), 1);
  ck_assert_int_eq(br_se(&b2), -1);
  ck_assert_int_eq(br_se(&b3), 2);
}
END_TEST

START_TEST(rbsp_unescape_strips_emulation_prevention) {
  static const unsigned char in[] = {0x00, 0x00, 0x03, 0x01, 0x00, 0x00, 0x03, 0x02};
  static const unsigned char want[] = {0x00, 0x00, 0x01, 0x00, 0x00, 0x02};
  unsigned char out[16];
  size_t n = rbsp_unescape(in, sizeof in, out, sizeof out);
  ck_assert_uint_eq(n, sizeof want);
  ck_assert_mem_eq(out, want, sizeof want);
}
END_TEST

START_TEST(rbsp_unescape_leaves_non_escape_zeros_alone) {
  static const unsigned char in[] = {0x00, 0x00, 0x05};
  unsigned char out[8];
  size_t n = rbsp_unescape(in, sizeof in, out, sizeof out);
  ck_assert_uint_eq(n, sizeof in);
  ck_assert_mem_eq(out, in, sizeof in);
}
END_TEST

START_TEST(find_startcode_finds_3_and_4_byte_codes) {
  static const unsigned char three[] = {0x01, 0x02, 0x00, 0x00, 0x01, 0x03};
  static const unsigned char four[] = {0x00, 0x00, 0x00, 0x01};
  static const unsigned char none[] = {0x01, 0x02, 0x03};
  size_t sclen = 0;

  ck_assert_uint_eq(find_startcode(three, sizeof three, 0, &sclen), 2u);
  ck_assert_uint_eq(sclen, 3u);

  ck_assert_uint_eq(find_startcode(four, sizeof four, 0, &sclen), 0u);
  ck_assert_uint_eq(sclen, 4u);

  ck_assert_uint_eq(find_startcode(none, sizeof none, 0, &sclen), sizeof none);
}
END_TEST

static Suite *bitreader_suite(void) {
  Suite *s = suite_create("bitreader");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, br_u_reads_msb_first_across_bytes);
  tcase_add_test(tc, br_u_sets_err_on_short_read);
  tcase_add_test(tc, br_ue_matches_known_codes);
  tcase_add_test(tc, br_se_maps_ue_to_signed);
  tcase_add_test(tc, rbsp_unescape_strips_emulation_prevention);
  tcase_add_test(tc, rbsp_unescape_leaves_non_escape_zeros_alone);
  tcase_add_test(tc, find_startcode_finds_3_and_4_byte_codes);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(bitreader_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
