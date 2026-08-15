/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>

#include "lib/bim/bitreader.h"
#include "lib/bim/bitwriter.h"

START_TEST(bitwriter_put_packs_msb_first) {
  bitwriter_t bw;
  const unsigned char *data;
  size_t len;
  bitwriter_init(&bw);

  ck_assert_int_eq(bitwriter_put(&bw, 5, 3), 0);  /* 101 */
  ck_assert_int_eq(bitwriter_put(&bw, 0x15, 5), 0); /* 10101 */

  data = bitwriter_data(&bw, &len);
  ck_assert_uint_eq(len, 1u);
  ck_assert_uint_eq(data[0], 0xB5u); /* 101 10101 */

  bitwriter_free(&bw);
}
END_TEST

START_TEST(bitwriter_put_round_trips_through_bitreader) {
  bitwriter_t bw;
  bitreader_t br;
  const unsigned char *data;
  size_t len;
  uint64_t v;
  bitwriter_init(&bw);

  ck_assert_int_eq(bitwriter_put(&bw, 1, 1), 0);
  ck_assert_int_eq(bitwriter_put(&bw, 0x2A, 7), 0);
  ck_assert_int_eq(bitwriter_put(&bw, 0xDEADBEEFu, 32), 0);
  ck_assert_int_eq(bitwriter_put(&bw, 0x1234567890ABCDEFull, 64), 0);

  data = bitwriter_data(&bw, &len);
  bitreader_init(&br, data, len);

  ck_assert_int_eq(bitreader_get(&br, 1, &v), 0);
  ck_assert_uint_eq(v, 1u);
  ck_assert_int_eq(bitreader_get(&br, 7, &v), 0);
  ck_assert_uint_eq(v, 0x2Au);
  ck_assert_int_eq(bitreader_get(&br, 32, &v), 0);
  ck_assert_uint_eq((uint32_t)v, 0xDEADBEEFu);
  ck_assert_int_eq(bitreader_get(&br, 64, &v), 0);
  ck_assert(v == 0x1234567890ABCDEFull);

  bitwriter_free(&bw);
}
END_TEST

START_TEST(bitwriter_put_bytes_round_trips) {
  bitwriter_t bw;
  bitreader_t br;
  const unsigned char *data;
  size_t len;
  static const unsigned char in[] = {0x00, 0xFF, 0x7A, 0x81};
  uint64_t v;
  size_t i;
  bitwriter_init(&bw);

  ck_assert_int_eq(bitwriter_put_bytes(&bw, in, sizeof in), 0);
  data = bitwriter_data(&bw, &len);
  ck_assert_uint_eq(len, sizeof in);
  ck_assert_mem_eq(data, in, sizeof in);

  bitreader_init(&br, data, len);
  for (i = 0; i < sizeof in; i++) {
    ck_assert_int_eq(bitreader_get(&br, 8, &v), 0);
    ck_assert_uint_eq(v, in[i]);
  }

  bitwriter_free(&bw);
}
END_TEST

START_TEST(bitwriter_vluimsbf8_round_trips_across_group_boundaries) {
  static const uint64_t values[] = {0, 5, 100, 127, 128, 20000, 1000000, UINT64_MAX};
  size_t i;
  for (i = 0; i < sizeof values / sizeof values[0]; i++) {
    bitwriter_t bw;
    bitreader_t br;
    const unsigned char *data;
    size_t len;
    uint64_t got;
    bitwriter_init(&bw);
    ck_assert_int_eq(bitwriter_put_vluimsbf8(&bw, values[i]), 0);
    data = bitwriter_data(&bw, &len);
    bitreader_init(&br, data, len);
    ck_assert_int_eq(bitreader_get_vluimsbf8(&br, &got), 0);
    ck_assert(got == values[i]);
    bitwriter_free(&bw);
  }
}
END_TEST

START_TEST(bitwriter_vluimsbf8_encodes_uint64_max_without_overflow) {
  bitwriter_t bw;
  const unsigned char *data;
  size_t len;
  bitwriter_init(&bw);

  ck_assert_int_eq(bitwriter_put_vluimsbf8(&bw, UINT64_MAX), 0);
  data = bitwriter_data(&bw, &len);
  ck_assert_ptr_nonnull(data);
  ck_assert_uint_eq(len, 10u);

  bitwriter_free(&bw);
}
END_TEST

START_TEST(bitwriter_vluimsbf8_matches_known_encoding) {
  bitwriter_t bw;
  const unsigned char *data;
  size_t len;
  bitwriter_init(&bw);

  ck_assert_int_eq(bitwriter_put_vluimsbf8(&bw, 200), 0); /* 2 groups: 1 0000001, 0 1001000 */
  data = bitwriter_data(&bw, &len);
  ck_assert_uint_eq(len, 2u);
  ck_assert_uint_eq(data[0], 0x81u);
  ck_assert_uint_eq(data[1], 0x48u);

  bitwriter_free(&bw);
}
END_TEST

START_TEST(bitwriter_vluimsbf4_round_trips_across_group_boundaries) {
  static const uint64_t values[] = {0, 5, 15, 16, 1000, 100000, UINT64_MAX};
  size_t i;
  for (i = 0; i < sizeof values / sizeof values[0]; i++) {
    bitwriter_t bw;
    bitreader_t br;
    const unsigned char *data;
    size_t len;
    uint64_t got;
    bitwriter_init(&bw);
    ck_assert_int_eq(bitwriter_put_vluimsbf4(&bw, values[i]), 0);
    data = bitwriter_data(&bw, &len);
    bitreader_init(&br, data, len);
    ck_assert_int_eq(bitreader_get_vluimsbf4(&br, &got), 0);
    ck_assert(got == values[i]);
    bitwriter_free(&bw);
  }
}
END_TEST

START_TEST(bitwriter_vluimsbf4_encodes_uint64_max_without_overflow) {
  bitwriter_t bw;
  const unsigned char *data;
  size_t len;
  bitwriter_init(&bw);

  ck_assert_int_eq(bitwriter_put_vluimsbf4(&bw, UINT64_MAX), 0);
  data = bitwriter_data(&bw, &len);
  ck_assert_ptr_nonnull(data);
  ck_assert_uint_eq(len, 10u);

  bitwriter_free(&bw);
}
END_TEST

static Suite *bitwriter_suite(void) {
  Suite *s = suite_create("bitwriter");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, bitwriter_put_packs_msb_first);
  tcase_add_test(tc, bitwriter_put_round_trips_through_bitreader);
  tcase_add_test(tc, bitwriter_put_bytes_round_trips);
  tcase_add_test(tc, bitwriter_vluimsbf8_round_trips_across_group_boundaries);
  tcase_add_test(tc, bitwriter_vluimsbf8_encodes_uint64_max_without_overflow);
  tcase_add_test(tc, bitwriter_vluimsbf8_matches_known_encoding);
  tcase_add_test(tc, bitwriter_vluimsbf4_round_trips_across_group_boundaries);
  tcase_add_test(tc, bitwriter_vluimsbf4_encodes_uint64_max_without_overflow);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(bitwriter_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
