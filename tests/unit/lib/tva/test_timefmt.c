/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/tva/timefmt.h"

START_TEST(xmltv_time_to_iso8601_no_zone_is_utc) {
  char out[32];
  ck_assert_int_eq(xmltv_time_to_iso8601("20201215123045", out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-12-15T12:30:45Z");
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

START_TEST(xmltv_time_to_iso8601_accepts_reduced_precision) {
  char out[32];
  ck_assert_int_eq(xmltv_time_to_iso8601("202012151230 +0200", out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-12-15T12:30:00+02:00");
  ck_assert_int_eq(xmltv_time_to_iso8601("2020121512", out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-12-15T12:00:00Z");
  ck_assert_int_eq(xmltv_time_to_iso8601("20201215", out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-12-15T00:00:00Z");
  ck_assert_int_eq(xmltv_time_to_iso8601("202012", out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-12-01T00:00:00Z");
  ck_assert_int_eq(xmltv_time_to_iso8601("2020", out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-01-01T00:00:00Z");
}
END_TEST

START_TEST(xmltv_time_to_iso8601_maps_named_zones) {
  char out[32];
  ck_assert_int_eq(xmltv_time_to_iso8601("20201215123045 CET", out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-12-15T12:30:45+01:00");
  ck_assert_int_eq(xmltv_time_to_iso8601("20200715123045 CEST", out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-07-15T12:30:45+02:00");
  ck_assert_int_eq(xmltv_time_to_iso8601("20201215123045 UTC", out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-12-15T12:30:45Z");
  ck_assert_int_eq(xmltv_time_to_iso8601("20201215123045 EEST", out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-12-15T12:30:45+03:00");
}
END_TEST

START_TEST(xmltv_time_to_iso8601_reports_unknown_zone_name) {
  char out[32];
  ck_assert_int_eq(xmltv_time_to_iso8601("20201215123045 XYZ", out, sizeof out), 1);
  ck_assert_str_eq(out, "2020-12-15T12:30:45");
}
END_TEST

START_TEST(xmltv_time_to_iso8601_accepts_colon_and_trailing_space) {
  char out[32];
  ck_assert_int_eq(xmltv_time_to_iso8601("20201215123045 +01:00", out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-12-15T12:30:45+01:00");
  ck_assert_int_eq(xmltv_time_to_iso8601("20201215123045 -05:30", out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-12-15T12:30:45-05:30");
  ck_assert_int_eq(xmltv_time_to_iso8601("20201215123045 +0100 ", out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-12-15T12:30:45+01:00");
  ck_assert_int_eq(xmltv_time_to_iso8601("20201215123045 CET\t", out, sizeof out), 0);
  ck_assert_str_eq(out, "2020-12-15T12:30:45+01:00");
}
END_TEST

START_TEST(xmltv_time_to_iso8601_rejects_trailing_junk) {
  char out[32];
  ck_assert_int_eq(xmltv_time_to_iso8601("20201215123045 +01:0", out, sizeof out), -1);
  ck_assert_int_eq(xmltv_time_to_iso8601("20201215123045 +01:00:", out, sizeof out), -1);
}
END_TEST

START_TEST(xmltv_time_to_iso8601_rejects_short_offset) {
  char out[32];
  ck_assert_int_eq(xmltv_time_to_iso8601("20201215123045 +02", out, sizeof out), -1);
  ck_assert_int_eq(xmltv_time_to_iso8601("20201215123045+0200x", out, sizeof out), -1);
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

START_TEST(iso8601_to_xmltv_time_accepts_fractional_seconds) {
  char out[32];
  ck_assert_int_eq(iso8601_to_xmltv_time("2024-03-15T12:30:45.000Z", out, sizeof out), 0);
  ck_assert_str_eq(out, "20240315123045 +0000");
  ck_assert_int_eq(iso8601_to_xmltv_time("2024-03-15T12:30:45.25+02:00", out, sizeof out), 0);
  ck_assert_str_eq(out, "20240315123045 +0200");
  ck_assert_int_eq(iso8601_to_xmltv_time("2024-03-15T12:30:45.", out, sizeof out), -1);
  ck_assert_int_eq(iso8601_to_xmltv_time("2024-03-15T12:30:45.5x", out, sizeof out), -1);
}
END_TEST

START_TEST(timefmt_round_trips_utc) {
  char mid[32], back[32];
  ck_assert_int_eq(xmltv_time_to_iso8601("20201215123045 +0000", mid, sizeof mid), 0);
  ck_assert_int_eq(iso8601_to_xmltv_time(mid, back, sizeof back), 0);
  ck_assert_str_eq(back, "20201215123045 +0000");
}
END_TEST

START_TEST(timefmt_zone_and_length_edge_cases) {
  static const char *const rej[] = {"", "123", "12345", "123456789012345", "2024050110203 +0100", "20240501102030 +01", "20240501102030 +01:0x", "20240501102030 1230", "20240501102030 +0a00",
                                    "20240501102030 abcdefghijklmnopq", "20240501102030 +01:30:00", "20240501102030 -"};
  static const struct {
    const char *in;
    const char *want;
    int rc;
  } ok[] = {
    {"2024", "2024-01-01T00:00:00", 0}, {"20240501", "2024-05-01T00:00:00", 0}, {"202405011020", "2024-05-01T10:20:00", 0}, {"20240501102030", "2024-05-01T10:20:30", 0},
    {"20240501102030 +0100", "2024-05-01T10:20:30+01:00", 0}, {"20240501102030 -0930", "2024-05-01T10:20:30-09:30", 0}, {"20240501102030\t+01:30", "2024-05-01T10:20:30+01:30", 0},
    {"20240501102030 CEST", "2024-05-01T10:20:30+02:00", 0}, {"20240501102030 UTC  ", "2024-05-01T10:20:30Z", 0}, {"20240501102030 EEST", "2024-05-01T10:20:30+03:00", 0},
    {"20240501102030 FOO", "2024-05-01T10:20:30", 1}, {"20240501102030 +0000", "2024-05-01T10:20:30Z", 0},
  };
  char out[40];
  for (size_t i = 0; i < sizeof rej / sizeof *rej; i++) ck_assert_msg(xmltv_time_to_iso8601(rej[i], out, sizeof out) == -1, "accepted '%s'", rej[i]);
  for (size_t i = 0; i < sizeof ok / sizeof *ok; i++) {
    ck_assert_msg(xmltv_time_to_iso8601(ok[i].in, out, sizeof out) == ok[i].rc, "rc for '%s'", ok[i].in);
    ck_assert_msg(!strncmp(out, ok[i].want, strlen(ok[i].want)), "'%s' gave '%s'", ok[i].in, out);
  }
  ck_assert_int_eq(iso8601_to_xmltv_time("2024-05-01T10:20:30-09:30", out, sizeof out), 0);
  ck_assert_str_eq(out, "20240501102030 -0930");
  ck_assert_int_eq(iso8601_to_xmltv_time("2024-05-01T10:20:30+00:00", out, sizeof out), 0);
  ck_assert_str_eq(out, "20240501102030 +0000");
  ck_assert_int_eq(iso8601_to_xmltv_time("2024-05-01T10:20:30", out, 5), 0);
  ck_assert_int_eq(iso8601_to_xmltv_time("bad", out, sizeof out), -1);
}
END_TEST

static Suite *timefmt_suite(void) {
  Suite *s = suite_create("timefmt");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, xmltv_time_to_iso8601_no_zone_is_utc);
  tcase_add_test(tc, xmltv_time_to_iso8601_zero_offset_becomes_z);
  tcase_add_test(tc, xmltv_time_to_iso8601_nonzero_offset);
  tcase_add_test(tc, xmltv_time_to_iso8601_rejects_malformed);
  tcase_add_test(tc, xmltv_time_to_iso8601_accepts_reduced_precision);
  tcase_add_test(tc, xmltv_time_to_iso8601_maps_named_zones);
  tcase_add_test(tc, xmltv_time_to_iso8601_reports_unknown_zone_name);
  tcase_add_test(tc, xmltv_time_to_iso8601_accepts_colon_and_trailing_space);
  tcase_add_test(tc, xmltv_time_to_iso8601_rejects_short_offset);
  tcase_add_test(tc, xmltv_time_to_iso8601_rejects_trailing_junk);
  tcase_add_test(tc, iso8601_to_xmltv_time_z_becomes_plus_zero);
  tcase_add_test(tc, iso8601_to_xmltv_time_with_offset);
  tcase_add_test(tc, iso8601_to_xmltv_time_no_offset);
  tcase_add_test(tc, iso8601_to_xmltv_time_rejects_malformed);
  tcase_add_test(tc, iso8601_to_xmltv_time_accepts_fractional_seconds);
  tcase_add_test(tc, timefmt_round_trips_utc);
  tcase_add_test(tc, timefmt_zone_and_length_edge_cases);
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
