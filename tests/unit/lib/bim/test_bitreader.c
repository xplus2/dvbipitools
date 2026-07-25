/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>

#include "lib/bim/bitreader.h"

START_TEST(bitreader_get_reads_msb_first) {
  static const unsigned char d[] = {0xB5}; /* 101 10101 */
  bitreader_t br;
  uint64_t v;
  bitreader_init(&br, d, sizeof d);

  ck_assert_int_eq(bitreader_get(&br, 3, &v), 0);
  ck_assert_uint_eq(v, 5u);
  ck_assert_int_eq(bitreader_get(&br, 5, &v), 0);
  ck_assert_uint_eq(v, 0x15u);
}
END_TEST

START_TEST(bitreader_bits_left_tracks_consumption) {
  static const unsigned char d[] = {0xFF, 0xFF};
  bitreader_t br;
  uint64_t v;
  bitreader_init(&br, d, sizeof d);

  ck_assert_uint_eq(bitreader_bits_left(&br), 16u);
  bitreader_get(&br, 5, &v);
  ck_assert_uint_eq(bitreader_bits_left(&br), 11u);
  bitreader_get(&br, 11, &v);
  ck_assert_uint_eq(bitreader_bits_left(&br), 0u);
}
END_TEST

START_TEST(bitreader_get_rejects_short_read) {
  static const unsigned char d[] = {0xFF};
  bitreader_t br;
  uint64_t v;
  bitreader_init(&br, d, sizeof d);
  ck_assert_int_eq(bitreader_get(&br, 9, &v), -1);
}
END_TEST

START_TEST(bitreader_vluimsbf8_matches_known_encoding) {
  /* value=200: continuation-tagged 7-bit groups, see bitwriter's own known-encoding test */
  static const unsigned char d[] = {0x81, 0x48};
  bitreader_t br;
  uint64_t v;
  bitreader_init(&br, d, sizeof d);
  ck_assert_int_eq(bitreader_get_vluimsbf8(&br, &v), 0);
  ck_assert(v == 200u);
}
END_TEST

START_TEST(bitreader_vluimsbf4_matches_known_encoding) {
  /* value=20 needs 2 nibbles (1 nibble caps at 15): unary prefix "10" (continue
     once, then stop), then n*4=8 payload bits = 20 -> bits 1 0 0001 0100 */
  static const unsigned char d[] = {0x85, 0x00}; /* 1,0 prefix + 00010100 (20) payload */
  bitreader_t br;
  uint64_t v;
  bitreader_init(&br, d, sizeof d);
  ck_assert_int_eq(bitreader_get_vluimsbf4(&br, &v), 0);
  ck_assert(v == 20u);
}
END_TEST

static Suite *bitreader_suite(void) {
  Suite *s = suite_create("bim_bitreader");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, bitreader_get_reads_msb_first);
  tcase_add_test(tc, bitreader_bits_left_tracks_consumption);
  tcase_add_test(tc, bitreader_get_rejects_short_read);
  tcase_add_test(tc, bitreader_vluimsbf8_matches_known_encoding);
  tcase_add_test(tc, bitreader_vluimsbf4_matches_known_encoding);
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
