/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipibcg/container.h"

START_TEST(container_build_round_trips_through_parse) {
  const unsigned char au[] = {0x01, 0x02, 0x03, 0x04, 0x05};
  const unsigned char sr[] = {0xAA, 0xBB, 0xCC};
  unsigned char *buf;
  size_t buf_len;
  const unsigned char *out_au, *out_sr;
  size_t out_au_len, out_sr_len;

  ck_assert_int_eq(container_build(au, sizeof(au), sr, sizeof(sr), &buf, &buf_len), 0);
  ck_assert_int_eq(buf_len, 17 + sizeof(au) + sizeof(sr));

  ck_assert_int_eq(container_parse(buf, buf_len, &out_au, &out_au_len, &out_sr, &out_sr_len), 0);
  ck_assert_uint_eq(out_au_len, sizeof(au));
  ck_assert_mem_eq(out_au, au, sizeof(au));
  ck_assert_uint_eq(out_sr_len, sizeof(sr));
  ck_assert_mem_eq(out_sr, sr, sizeof(sr));

  free(buf);
}
END_TEST

START_TEST(container_build_handles_empty_payloads) {
  const unsigned char empty[1] = {0};
  unsigned char *buf;
  size_t buf_len;
  const unsigned char *out_au, *out_sr;
  size_t out_au_len, out_sr_len;

  ck_assert_int_eq(container_build(empty, 0, empty, 0, &buf, &buf_len), 0);
  ck_assert_int_eq(container_parse(buf, buf_len, &out_au, &out_au_len, &out_sr, &out_sr_len), 0);
  ck_assert_uint_eq(out_au_len, 0);
  ck_assert_uint_eq(out_sr_len, 0);

  free(buf);
}
END_TEST

START_TEST(container_build_rejects_oversized_24bit_fields) {
  static const unsigned char dummy[1] = {0};
  unsigned char *buf = NULL;
  size_t buf_len = 0;

  ck_assert_int_eq(container_build(dummy, 0x1000000u, dummy, 0, &buf, &buf_len), -1);
  ck_assert_ptr_null(buf);
  ck_assert_uint_eq(buf_len, 0);

  ck_assert_int_eq(container_build(dummy, 0, dummy, 0x1000000u, &buf, &buf_len), -1);
  ck_assert_ptr_null(buf);
  ck_assert_uint_eq(buf_len, 0);

  ck_assert_int_eq(container_build(dummy, 0x1000000u - 17u + 1u, dummy, 0, &buf, &buf_len), -1);
  ck_assert_ptr_null(buf);
  ck_assert_uint_eq(buf_len, 0);
}
END_TEST

START_TEST(container_parse_rejects_empty_buffer) {
  const unsigned char *out_au, *out_sr;
  size_t out_au_len, out_sr_len;
  ck_assert_int_eq(container_parse(NULL, 0, &out_au, &out_au_len, &out_sr, &out_sr_len), -1);
}
END_TEST

START_TEST(container_parse_rejects_truncated_header) {
  const unsigned char buf[] = {2, 0x02, 0x01, 0, 0, 17, 0, 0};
  const unsigned char *out_au, *out_sr;
  size_t out_au_len, out_sr_len;
  ck_assert_int_eq(container_parse(buf, sizeof(buf), &out_au, &out_au_len, &out_sr, &out_sr_len), -1);
}
END_TEST

START_TEST(container_parse_rejects_out_of_bounds_pointer) {
  unsigned char buf[17];
  const unsigned char *out_au, *out_sr;
  size_t out_au_len, out_sr_len;

  buf[0] = 2;
  buf[1] = CONTAINER_STRUCT_DATA_REPOSITORY;
  buf[2] = CONTAINER_DATAREPO_BINARY;
  buf[3] = 0; buf[4] = 0; buf[5] = 100; /* ptr = 100, way past len */
  buf[6] = 0; buf[7] = 0; buf[8] = 1;
  buf[9] = CONTAINER_STRUCT_DATA_REPOSITORY;
  buf[10] = CONTAINER_DATAREPO_STRINGS;
  buf[11] = 0; buf[12] = 0; buf[13] = 17;
  buf[14] = 0; buf[15] = 0; buf[16] = 0;

  ck_assert_int_eq(container_parse(buf, sizeof(buf), &out_au, &out_au_len, &out_sr, &out_sr_len), -1);
}
END_TEST

START_TEST(container_parse_rejects_length_past_end) {
  unsigned char buf[17];
  const unsigned char *out_au, *out_sr;
  size_t out_au_len, out_sr_len;

  buf[0] = 2;
  buf[1] = CONTAINER_STRUCT_DATA_REPOSITORY;
  buf[2] = CONTAINER_DATAREPO_BINARY;
  buf[3] = 0; buf[4] = 0; buf[5] = 17; /* ptr = 17, at end */
  buf[6] = 0; buf[7] = 0; buf[8] = 5;  /* length = 5, but nothing follows */
  buf[9] = CONTAINER_STRUCT_DATA_REPOSITORY;
  buf[10] = CONTAINER_DATAREPO_STRINGS;
  buf[11] = 0; buf[12] = 0; buf[13] = 17;
  buf[14] = 0; buf[15] = 0; buf[16] = 0;

  ck_assert_int_eq(container_parse(buf, sizeof(buf), &out_au, &out_au_len, &out_sr, &out_sr_len), -1);
}
END_TEST

START_TEST(container_parse_ignores_unknown_structure_types) {
  unsigned char buf[25];
  const unsigned char *out_au, *out_sr;
  size_t out_au_len, out_sr_len;

  buf[0] = 2;
  buf[1] = 0x99; /* unknown structure type, skipped */
  buf[2] = 0;
  buf[3] = 0; buf[4] = 0; buf[5] = 17;
  buf[6] = 0; buf[7] = 0; buf[8] = 3;
  buf[9] = CONTAINER_STRUCT_DATA_REPOSITORY;
  buf[10] = CONTAINER_DATAREPO_STRINGS;
  buf[11] = 0; buf[12] = 0; buf[13] = 20;
  buf[14] = 0; buf[15] = 0; buf[16] = 5;
  memset(buf + 17, 0, 3);
  memset(buf + 20, 0, 5);

  ck_assert_int_eq(container_parse(buf, sizeof(buf), &out_au, &out_au_len, &out_sr, &out_sr_len), -1);
  ck_assert_ptr_null(out_au);
  ck_assert_uint_eq(out_sr_len, 5);
}
END_TEST

static Suite *container_suite(void) {
  Suite *s = suite_create("dipibcg_container");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, container_build_round_trips_through_parse);
  tcase_add_test(tc, container_build_handles_empty_payloads);
  tcase_add_test(tc, container_build_rejects_oversized_24bit_fields);
  tcase_add_test(tc, container_parse_rejects_empty_buffer);
  tcase_add_test(tc, container_parse_rejects_truncated_header);
  tcase_add_test(tc, container_parse_rejects_out_of_bounds_pointer);
  tcase_add_test(tc, container_parse_rejects_length_past_end);
  tcase_add_test(tc, container_parse_ignores_unknown_structure_types);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(container_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
