/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

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

START_TEST(strrepo_reader_truncates_into_small_buffer) {
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *data;
  size_t len;
  char out[4];
  strrepo_writer_init(&sw);
  strrepo_writer_put(&sw, "abcdefgh");
  data = strrepo_writer_data(&sw, &len);
  strrepo_reader_init(&sr, data, len);

  ck_assert_int_eq(strrepo_reader_next(&sr, out, sizeof out), 0);
  ck_assert_str_eq(out, "abc"); /* 3 chars + NUL fits a 4-byte buffer */

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

static Suite *strrepo_suite(void) {
  Suite *s = suite_create("strrepo");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, strrepo_round_trips_multiple_strings_in_order);
  tcase_add_test(tc, strrepo_reader_truncates_into_small_buffer);
  tcase_add_test(tc, strrepo_reader_init_rejects_bad_encoding_and_empty);
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
