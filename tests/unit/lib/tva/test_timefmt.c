/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/tva/timefmt.h"

START_TEST(xmltv_time_to_iso8601_no_offset) {
  char out[32];
  ck_assert_int_eq(xmltv_time_to_iso8601("20201215123045", out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-12-15T12:30:45");
}
END_TEST

START_TEST(xmltv_time_to_iso8601_zero_offset_becomes_z) {
  char out[32];
  ck_assert_int_eq(xmltv_time_to_iso8601("20201215123045 +0000", out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-12-15T12:30:45Z");
}
END_TEST

START_TEST(xmltv_time_to_iso8601_nonzero_offset) {
  char out[32];
  ck_assert_int_eq(xmltv_time_to_iso8601("20201215123045 +0200", out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-12-15T12:30:45+02:00");
  ck_assert_int_eq(xmltv_time_to_iso8601("20201215123045 -0500", out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-12-15T12:30:45-05:00");
}
END_TEST

START_TEST(xmltv_time_to_iso8601_rejects_malformed) {
  char out[32];
  ck_assert_int_eq(xmltv_time_to_iso8601("not-a-time", out, sizeof out), -1);
  ck_assert_int_eq(xmltv_time_to_iso8601("2020121512304", out, sizeof out), -1); /* 13 digits */
}
END_TEST

START_TEST(iso8601_to_xmltv_time_z_becomes_plus_zero) {
  char out[32];
  ck_assert_int_eq(iso8601_to_xmltv_time("2020-12-15T12:30:45Z", out, sizeof out), 0);
  ck_assert_str_eq(out, "20201215123045 +0000");
}
END_TEST

START_TEST(iso8601_to_xmltv_time_with_offset) {
  char out[32];
  ck_assert_int_eq(iso8601_to_xmltv_time("2020-12-15T12:30:45+02:00", out, sizeof out), 0);
  ck_assert_str_eq(out, "20201215123045 +0200");
}
END_TEST

START_TEST(iso8601_to_xmltv_time_no_offset) {
  char out[32];
  ck_assert_int_eq(iso8601_to_xmltv_time("2020-12-15T12:30:45", out, sizeof out), 0);
  ck_assert_str_eq(out, "20201215123045");
}
END_TEST

START_TEST(iso8601_to_xmltv_time_rejects_malformed) {
  char out[32];
  ck_assert_int_eq(iso8601_to_xmltv_time("not-a-time", out, sizeof out), -1);
}
END_TEST

START_TEST(timefmt_round_trips_utc) {
  char mid[32], back[32];
  ck_assert_int_eq(xmltv_time_to_iso8601("20201215123045 +0000", mid, sizeof mid), 0);
  ck_assert_int_eq(iso8601_to_xmltv_time(mid, back, sizeof back), 0);
  ck_assert_str_eq(back, "20201215123045 +0000");
}
END_TEST

static Suite *timefmt_suite(void) {
  Suite *s = suite_create("timefmt");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, xmltv_time_to_iso8601_no_offset);
  tcase_add_test(tc, xmltv_time_to_iso8601_zero_offset_becomes_z);
  tcase_add_test(tc, xmltv_time_to_iso8601_nonzero_offset);
  tcase_add_test(tc, xmltv_time_to_iso8601_rejects_malformed);
  tcase_add_test(tc, iso8601_to_xmltv_time_z_becomes_plus_zero);
  tcase_add_test(tc, iso8601_to_xmltv_time_with_offset);
  tcase_add_test(tc, iso8601_to_xmltv_time_no_offset);
  tcase_add_test(tc, iso8601_to_xmltv_time_rejects_malformed);
  tcase_add_test(tc, timefmt_round_trips_utc);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(timefmt_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
