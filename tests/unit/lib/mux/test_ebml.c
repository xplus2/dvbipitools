/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/mux/ebml.h"

START_TEST(eb_id_picks_length_from_leading_byte_group) {
  ebuf_t b = {0};
  eb_id(&b, 0x1A45DFA3u); /* 4-byte EBML header id */
  ck_assert_uint_eq(b.len, 4u);
  ck_assert_mem_eq(b.p, "\x1A\x45\xDF\xA3", 4);
  ebuf_free(&b);

  memset(&b, 0, sizeof b);
  eb_id(&b, 0x4286u); /* 2-byte id */
  ck_assert_uint_eq(b.len, 2u);
  ck_assert_mem_eq(b.p, "\x42\x86", 2);
  ebuf_free(&b);

  memset(&b, 0, sizeof b);
  eb_id(&b, 0x80u); /* 1-byte id */
  ck_assert_uint_eq(b.len, 1u);
  ck_assert_mem_eq(b.p, "\x80", 1);
  ebuf_free(&b);
}
END_TEST

START_TEST(eb_size_encodes_variable_length_with_marker_bit) {
  ebuf_t b = {0};
  eb_size(&b, 5);
  ck_assert_uint_eq(b.len, 1u);
  ck_assert_uint_eq(b.p[0], 0x85u); /* marker bit 7 + value 5 */
  ebuf_free(&b);

  memset(&b, 0, sizeof b);
  eb_size(&b, 200);
  ck_assert_uint_eq(b.len, 2u);
  ck_assert_uint_eq(b.p[0], 0x40u);
  ck_assert_uint_eq(b.p[1], 0xC8u);
  ebuf_free(&b);
}
END_TEST

START_TEST(eb_uint_writes_minimal_length_big_endian_value) {
  ebuf_t b = {0};
  eb_uint(&b, 0x80u, 0); /* value 0 still takes 1 byte */
  ck_assert_uint_eq(b.len, 3u); /* id(1) + size(1) + value(1) */
  ck_assert_mem_eq(b.p, "\x80\x81\x00", 3);
  ebuf_free(&b);

  memset(&b, 0, sizeof b);
  eb_uint(&b, 0x80u, 300); /* 0x012C, needs 2 bytes */
  ck_assert_uint_eq(b.len, 4u);
  ck_assert_mem_eq(b.p, "\x80\x82\x01\x2C", 4);
  ebuf_free(&b);
}
END_TEST

START_TEST(eb_str_and_eb_bin_write_id_size_and_raw_bytes) {
  ebuf_t b = {0};
  eb_str(&b, 0x80u, "ab");
  ck_assert_uint_eq(b.len, 4u); /* id(1) + size(1) + "ab"(2) */
  ck_assert_mem_eq(b.p, "\x80\x82" "ab", 4);
  ebuf_free(&b);
}
END_TEST

START_TEST(eb_float_writes_id_size_8_and_ieee754_bytes) {
  ebuf_t b = {0};
  union {
    double d;
    uint64_t u;
  } c;
  unsigned char expect[8];
  int i;
  c.d = 1.5;
  for (i = 0; i < 8; i++)
    expect[i] = (unsigned char)(c.u >> (8 * (7 - i)));

  eb_float(&b, 0x80u, 1.5);
  ck_assert_uint_eq(b.len, 10u); /* id(1) + size(1) + double(8) */
  ck_assert_mem_eq(b.p + 2, expect, 8);
  ebuf_free(&b);
}
END_TEST

START_TEST(eb_master_wraps_child_bytes_into_parent_and_frees_child) {
  ebuf_t parent = {0}, child = {0};
  eb_uint(&child, 0x80u, 7);       /* child.len = 3 */
  eb_master(&parent, 0x8Fu, &child);

  ck_assert_ptr_null(child.p); /* child freed */
  ck_assert_uint_eq(parent.len, 5u); /* id(1) + size(1) + child(3) */
  ck_assert_mem_eq(parent.p, "\x8F\x83" "\x80\x81\x07", 5);
  ebuf_free(&parent);
}
END_TEST

START_TEST(eb_bytes_grows_past_initial_capacity) {
  ebuf_t b = {0};
  unsigned char chunk[1000];
  int i;
  memset(chunk, 0xAB, sizeof chunk);
  for (i = 0; i < 5; i++) /* 5000 bytes total, forces realloc past the 4096 initial cap */
    eb_bytes(&b, chunk, sizeof chunk);
  ck_assert_uint_eq(b.len, 5000u);
  ck_assert_int_eq(b.err, 0);
  ck_assert_uint_eq(b.p[4999], 0xABu);
  ebuf_free(&b);
}
END_TEST

static Suite *ebml_suite(void) {
  Suite *s = suite_create("ebml");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, eb_id_picks_length_from_leading_byte_group);
  tcase_add_test(tc, eb_size_encodes_variable_length_with_marker_bit);
  tcase_add_test(tc, eb_uint_writes_minimal_length_big_endian_value);
  tcase_add_test(tc, eb_str_and_eb_bin_write_id_size_and_raw_bytes);
  tcase_add_test(tc, eb_float_writes_id_size_8_and_ieee754_bytes);
  tcase_add_test(tc, eb_master_wraps_child_bytes_into_parent_and_frees_child);
  tcase_add_test(tc, eb_bytes_grows_past_initial_capacity);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(ebml_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
