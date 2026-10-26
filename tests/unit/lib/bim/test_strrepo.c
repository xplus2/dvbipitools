/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/bim/strrepo.h"

START_TEST(strrepo_round_trips_multiple_strings_in_order) {
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *data;
  size_t len;
  char out[64];
  strrepo_writer_init(&sw);

  ck_assert_int_eq(strrepo_writer_put(&sw, "first"), 0);
  ck_assert_int_eq(strrepo_writer_put(&sw, ""), 0);
  ck_assert_int_eq(strrepo_writer_put(&sw, "third one"), 0);

  data = strrepo_writer_data(&sw, &len);
  ck_assert_int_eq(strrepo_reader_init(&sr, data, len), 0);

  ck_assert_int_eq(strrepo_reader_next(&sr, out, sizeof out), 0);
  ck_assert_str_eq(out, "first");
  ck_assert_int_eq(strrepo_reader_next(&sr, out, sizeof out), 0);
  ck_assert_str_eq(out, "");
  ck_assert_int_eq(strrepo_reader_next(&sr, out, sizeof out), 0);
  ck_assert_str_eq(out, "third one");
  ck_assert_int_eq(strrepo_reader_next(&sr, out, sizeof out), -1); /* no more strings */

  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(strrepo_reader_rejects_string_too_long_for_buffer) {
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *data;
  size_t len;
  char out[4];
  strrepo_writer_init(&sw);
  strrepo_writer_put(&sw, "abcdefgh");
  strrepo_writer_put(&sw, "abc");
  data = strrepo_writer_data(&sw, &len);
  strrepo_reader_init(&sr, data, len);

  ck_assert_int_eq(strrepo_reader_next(&sr, out, sizeof out), -1);

  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(strrepo_reader_accepts_exact_fit_with_nul) {
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *data;
  size_t len;
  char out[4];
  strrepo_writer_init(&sw);
  strrepo_writer_put(&sw, "abc");
  data = strrepo_writer_data(&sw, &len);
  strrepo_reader_init(&sr, data, len);

  ck_assert_int_eq(strrepo_reader_next(&sr, out, sizeof out), 0);
  ck_assert_str_eq(out, "abc");

  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(strrepo_reader_next_dup_returns_full_string) {
  strrepo_reader_t sr;
  static const unsigned char buf[] = {0x01, 'a', 'b', 'c', '\0', 'z', '\0', 'q'};
  char *s = NULL;
  ck_assert_int_eq(strrepo_reader_init(&sr, buf, sizeof buf), 0);
  ck_assert_int_eq(strrepo_reader_next_dup(&sr, &s), 0);
  ck_assert_str_eq(s, "abc");
  free(s);
  ck_assert_int_eq(strrepo_reader_next_dup(&sr, &s), 0);
  ck_assert_str_eq(s, "z");
  free(s);
  s = NULL;
  ck_assert_int_eq(strrepo_reader_next_dup(&sr, &s), -1);
  ck_assert_ptr_null(s);
}
END_TEST

START_TEST(strrepo_reader_seek_repositions_and_rejects_bad_offsets) {
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *data;
  size_t len;
  char out[16];
  strrepo_writer_init(&sw);
  strrepo_writer_put(&sw, "aa");
  strrepo_writer_put(&sw, "bb");
  data = strrepo_writer_data(&sw, &len);
  strrepo_reader_init(&sr, data, len);

  ck_assert_int_eq(strrepo_reader_seek(&sr, 4), 0);
  ck_assert_int_eq(strrepo_reader_next(&sr, out, sizeof out), 0);
  ck_assert_str_eq(out, "bb");
  ck_assert_int_eq(strrepo_reader_seek(&sr, 1), 0);
  ck_assert_int_eq(strrepo_reader_next(&sr, out, sizeof out), 0);
  ck_assert_str_eq(out, "aa");
  ck_assert_int_eq(strrepo_reader_seek(&sr, 0), -1); /* encoding_type byte */
  ck_assert_int_eq(strrepo_reader_seek(&sr, len + 1), -1);

  strrepo_writer_free(&sw);
}
END_TEST

START_TEST(strrepo_reader_init_rejects_bad_encoding_and_empty) {
  strrepo_reader_t sr;
  static const unsigned char bad[] = {0x02, 'x', '\0'};
  ck_assert_int_eq(strrepo_reader_init(&sr, bad, sizeof bad), -1);
  ck_assert_int_eq(strrepo_reader_init(&sr, bad, 0), -1);
}
END_TEST

START_TEST(strrepo_put_fails_on_writer_without_encoding_byte) {
  strrepo_writer_t sw = {.buf = NULL}; /* state after failed init */
  ck_assert_int_eq(strrepo_writer_put(&sw, "x"), -1);
  ck_assert_uint_eq(sw.len, 0u);
}
END_TEST

START_TEST(strrepo_resync_at_and_allocation_failures) {
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  bitwriter_t bw;
  bitreader_t br;
  const unsigned char *data;
  const unsigned char *bits;
  size_t n;
  size_t bn;
  char out[16];
  char *dup = NULL;
  int rc;
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_put(&sw, "first"), 0);
  sw.resync = &bw;
  ck_assert_int_eq(strrepo_writer_put(&sw, "second"), 0);
  ck_assert_ptr_null(sw.resync);
  bits = bitwriter_data(&bw, &bn);
  ck_assert_uint_eq(bn, 2u);
  data = strrepo_writer_data(&sw, &n);
  ck_assert_int_eq(strrepo_reader_init(&sr, data, n), 0);
  bitreader_init(&br, bits, bn);
  sr.resync = &br;
  ck_assert_int_eq(strrepo_reader_next(&sr, out, sizeof out), 0);
  ck_assert_str_eq(out, "second");
  ck_assert_ptr_null(sr.resync);
  bitreader_init(&br, bits, 1);
  sr.resync = &br;
  ck_assert_int_eq(strrepo_reader_next(&sr, out, sizeof out), -1);
  {
    unsigned char big[2] = {0xFF, 0xFF};
    bitreader_init(&br, big, 2);
    sr.resync = &br;
    ck_assert_int_eq(strrepo_reader_next_dup(&sr, &dup), -1);
  }
  ck_assert_ptr_nonnull(strrepo_at(data, n, 1));
  ck_assert_str_eq(strrepo_at(data, n, 1), "first");
  ck_assert_ptr_null(strrepo_at(NULL, n, 1));
  ck_assert_ptr_null(strrepo_at(data, n, 0));
  ck_assert_ptr_null(strrepo_at(data, n, n));
  ck_assert_ptr_null(strrepo_at((const unsigned char *)"\x01" "abc", 4, 1));
  ck_assert_int_eq(strrepo_reader_init(&sr, data, n), 0);
  sr.pos = n;
  ck_assert_int_eq(strrepo_reader_next(&sr, out, sizeof out), -1);
  ck_assert_int_eq(strrepo_reader_init(&sr, (const unsigned char *)"\x01" "abc", 4), 0);
  ck_assert_int_eq(strrepo_reader_next(&sr, out, sizeof out), -1);
  ck_assert_int_eq(strrepo_reader_init(&sr, data, n), 0);
  alloc_fail_at(0);
  rc = strrepo_reader_next_dup(&sr, &dup);
  alloc_fail_end();
  ck_assert_int_eq(rc, -1);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);

  alloc_fail_at(0);
  rc = strrepo_writer_init(&sw);
  alloc_fail_end();
  ck_assert_int_eq(rc, -1);
  ck_assert_int_eq(strrepo_writer_put(&sw, "x"), -1);
  strrepo_writer_free(&sw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  for (long k = 0; k < 1; k++) {
    char big[600];
    memset(big, 'z', sizeof big - 1);
    big[sizeof big - 1] = 0;
    alloc_fail_at(k);
    rc = strrepo_writer_put(&sw, big);
    alloc_fail_end();
    ck_assert_int_eq(rc, -1);
  }
  strrepo_writer_free(&sw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  bitwriter_init(&bw);
  sw.resync = &bw;
  alloc_fail_at(0);
  rc = strrepo_writer_put(&sw, "q");
  alloc_fail_end();
  (void)rc;
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

static Suite *strrepo_suite(void) {
  Suite *s = suite_create("strrepo");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, strrepo_round_trips_multiple_strings_in_order);
  tcase_add_test(tc, strrepo_reader_rejects_string_too_long_for_buffer);
  tcase_add_test(tc, strrepo_reader_accepts_exact_fit_with_nul);
  tcase_add_test(tc, strrepo_reader_next_dup_returns_full_string);
  tcase_add_test(tc, strrepo_reader_init_rejects_bad_encoding_and_empty);
  tcase_add_test(tc, strrepo_reader_seek_repositions_and_rejects_bad_offsets);
  tcase_add_test(tc, strrepo_put_fails_on_writer_without_encoding_byte);
  tcase_add_test(tc, strrepo_resync_at_and_allocation_failures);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(strrepo_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
