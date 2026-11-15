/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/dateutil.h"

START_TEST(iso8601_forms) {
  static const struct {
    const char *in;
    int rc;
    iso8601_offset_kind_t kind;
    int off;
  } t[] = {
      {"2020-12-15T12:30:45Z", 0, ISO8601_OFF_Z, 0},
      {"2020-12-15T12:30:45", 0, ISO8601_OFF_NONE, 0},
      {"2020-12-15T12:30:45.123Z", 0, ISO8601_OFF_Z, 0},
      {"2020-12-15T12:30:45.5", 0, ISO8601_OFF_NONE, 0},
      {"2020-12-15T12:30:45+01:30", 0, ISO8601_OFF_NUMERIC, 90},
      {"2020-12-15T12:30:45-05:00", 0, ISO8601_OFF_NUMERIC, -300},
      {"2020-12-15T12:30:45.9+00:00", 0, ISO8601_OFF_NUMERIC, 0},
      {"2020-12-15T12:30:45.Z", -1, 0, 0},
      {"2020-12-15T12:30:45z", -1, 0, 0},
      {"2020-12-15T12:30:45ZZ", -1, 0, 0},
      {"2020-12-15T12:30:45+0100", -1, 0, 0},
      {"2020-12-15T12:30:45+24:00", -1, 0, 0},
      {"2020-12-15T12:30:45+01:60", -1, 0, 0},
      {"2020-12-15T12:30:45+0a:00", -1, 0, 0},
      {"2020-12-15T12:30:45 ", -1, 0, 0},
      {"2020-12-15 12:30:45", -1, 0, 0},
      {"2020-12-15T12:30:4", -1, 0, 0},
      {"2020-1-15T12:30:45Z", -1, 0, 0},
      {"2020-12-1ST12:30:45Z", -1, 0, 0},
      {"2020-13-15T12:30:45Z", -1, 0, 0},
      {"2020-00-15T12:30:45Z", -1, 0, 0},
      {"2020-12-00T12:30:45Z", -1, 0, 0},
      {"2020-12-32T12:30:45Z", -1, 0, 0},
      {"2021-02-29T12:30:45Z", -1, 0, 0},
      {"2020-02-29T12:30:45Z", 0, ISO8601_OFF_Z, 0},
      {"1900-02-29T12:30:45Z", -1, 0, 0},
      {"2000-02-29T12:30:45Z", 0, ISO8601_OFF_Z, 0},
      {"2020-04-31T12:30:45Z", -1, 0, 0},
      {"2020-12-15T24:30:45Z", -1, 0, 0},
      {"2020-12-15T12:60:45Z", -1, 0, 0},
      {"2020-12-15T12:30:60Z", -1, 0, 0},
      {"", -1, 0, 0},
      {"2020x12-15T12:30:45Z", -1, 0, 0},
      {"2020-12x15T12:30:45Z", -1, 0, 0},
      {"2020-12-15X12:30:45Z", -1, 0, 0},
      {"2020-12-15T12x30:45Z", -1, 0, 0},
      {"2020-12-15T12:30x45Z", -1, 0, 0},
      {"a020-12-15T12:30:45Z", -1, 0, 0},
      {"2020-a2-15T12:30:45Z", -1, 0, 0},
      {"2020-12-a5T12:30:45Z", -1, 0, 0},
      {"2020-12-15Ta2:30:45Z", -1, 0, 0},
      {"2020-12-15T12:a0:45Z", -1, 0, 0},
      {"2020-12-15T12:30:a5Z", -1, 0, 0},
  };
  iso8601_t o;
  int rc = iso8601_split(t[_i].in, &o);
  ck_assert_int_eq(rc, t[_i].rc);
  if (rc == 0) {
    ck_assert_int_eq(o.offset_kind, t[_i].kind);
    ck_assert_int_eq(o.off_min, t[_i].off);
  }
}
END_TEST

START_TEST(iso8601_edge_cases_and_formatting) {
  static const char *const bad[] = {
    "2024-05-01T10:20:30.", "2024-05-01T10:20:30.Z", "2024-05-01T10:20:30+24:00", "2024-05-01T10:20:30+01:60", "2024-05-01T10:20:30+0100", "2024-05-01T10:20:30+01:0x",
    "2024-05-01T10:20:30Zx", "2024-05-01T10:20:30z", "2024-13-01T10:20:30Z", "2024-00-01T10:20:30Z", "2024-02-30T10:20:30Z", "2023-02-29T10:20:30Z", "1900-02-29T10:20:30Z",
    "2024-05-32T10:20:30Z", "2024-05-00T10:20:30Z", "2024-05-01T24:20:30Z", "2024-05-01T10:60:30Z", "2024-05-01T10:20:60Z", "2024-05-01X10:20:30Z", "2024/05/01T10:20:30Z",
    "20a4-05-01T10:20:30Z", "2024-05-01T10:20", "", "2024-05-01T10-20-30Z",
  };
  static const char *const good[] = {"2024-02-29T10:20:30Z", "2000-02-29T00:00:00", "2024-05-01T10:20:30.123456Z", "2024-05-01T10:20:30.5+05:30", "2024-05-01T10:20:30-23:59", "2024-12-31T23:59:59Z"};
  iso8601_t t;
  char out[32];
  char small[8];
  int y;
  int mo;
  int d;
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) ck_assert_msg(iso8601_split(bad[i], &t) == -1, "accepted %s", bad[i]);
  for (size_t i = 0; i < sizeof good / sizeof good[0]; i++) ck_assert_msg(iso8601_split(good[i], &t) == 0, "rejected %s", good[i]);
  ck_assert_int_eq(iso8601_split("2024-05-01T10:20:30-23:59", &t), 0);
  ck_assert_int_eq(t.off_min, -(23 * 60 + 59));
  ck_assert_int_eq(t.offset_kind, ISO8601_OFF_NUMERIC);
  ck_assert_int_eq(iso8601_split("2024-05-01T10:20:30Z", &t), 0);
  ck_assert_int_eq(t.offset_kind, ISO8601_OFF_Z);
  ck_assert_int_eq(iso8601_split("2024-05-01T10:20:30", &t), 0);
  ck_assert_int_eq(t.offset_kind, ISO8601_OFF_NONE);
  ck_assert_int_eq(iso8601_split("2024-05-01T10:20:30+01:30", &t), 0);
  ck_assert_int_eq(iso8601_mjd_minutes(&t), date_to_mjd(2024, 5, 1) * 1440L + 10 * 60 + 20 - 90);
  ck_assert_int_eq(iso8601_format(out, sizeof out, 0, 3725), 19);
  ck_assert_str_eq(out, "1970-01-01T01:02:05");
  ck_assert_int_eq(iso8601_format(small, sizeof small, 0, 0), -1);
  ck_assert_int_eq(iso8601_format(out, sizeof out, -1, 86399), 19);
  ck_assert_str_eq(out, "1969-12-31T23:59:59");
  mjd_to_date(date_to_mjd(1600, 2, 29), &y, &mo, &d);
  ck_assert_int_eq(y * 10000 + mo * 100 + d, 16000229);
  mjd_to_date(date_to_mjd(1, 1, 1), &y, &mo, &d);
  ck_assert_int_eq(y * 10000 + mo * 100 + d, 10101);
  mjd_to_date(date_to_mjd(-5, 12, 31), &y, &mo, &d);
  ck_assert_int_eq(y, -5);
}
END_TEST

START_TEST(mjd_known_dates) {
  ck_assert_int_eq(date_to_mjd(1993, 10, 13), 49273);
  ck_assert_int_eq(date_to_mjd(2020, 12, 15), 59198);
  ck_assert_int_eq(date_to_mjd(2020, 1, 1), 58849);
  ck_assert_int_eq(date_to_mjd(2020, 2, 29), 58908);
}
END_TEST

START_TEST(now_ms_non_negative) {
  ck_assert_int_ge(now_ms(), 0);
}
END_TEST

static Suite *dateutil_suite(void) {
  Suite *s = suite_create("dateutil");
  TCase *tc = tcase_create("core");
  tcase_add_loop_test(tc, iso8601_forms, 0, 43);
  tcase_add_test(tc, iso8601_edge_cases_and_formatting);
  tcase_add_test(tc, mjd_known_dates);
  tcase_add_test(tc, now_ms_non_negative);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(dateutil_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
