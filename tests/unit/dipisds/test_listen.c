/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipisds/listen.h"

static dvbstp_header_t make_header(unsigned payload_id, unsigned segment_id, unsigned version) {
  dvbstp_header_t h;
  memset(&h, 0, sizeof h);
  h.payload_id = payload_id;
  h.segment_id = segment_id;
  h.segment_version = version;
  return h;
}

START_TEST(already_seen_is_false_on_first_sighting) {
  seen_t seen[LISTEN_SEEN_MAX];
  int count = 0;
  dvbstp_header_t h = make_header(1, 2, 3);
  ck_assert_int_eq(already_seen(seen, &count, &h), 0);
  ck_assert_int_eq(count, 1);
}
END_TEST

START_TEST(already_seen_is_true_on_repeat) {
  seen_t seen[LISTEN_SEEN_MAX];
  int count = 0;
  dvbstp_header_t h = make_header(1, 2, 3);
  ck_assert_int_eq(already_seen(seen, &count, &h), 0);
  ck_assert_int_eq(already_seen(seen, &count, &h), 1);
  ck_assert_int_eq(count, 1);
}
END_TEST

START_TEST(already_seen_distinguishes_by_all_three_fields) {
  seen_t seen[LISTEN_SEEN_MAX];
  int count = 0;
  dvbstp_header_t a = make_header(1, 2, 3);
  dvbstp_header_t b = make_header(1, 2, 4); /* different version */
  dvbstp_header_t c = make_header(1, 9, 3); /* different segment_id */
  dvbstp_header_t d = make_header(9, 2, 3); /* different payload_id */
  ck_assert_int_eq(already_seen(seen, &count, &a), 0);
  ck_assert_int_eq(already_seen(seen, &count, &b), 0);
  ck_assert_int_eq(already_seen(seen, &count, &c), 0);
  ck_assert_int_eq(already_seen(seen, &count, &d), 0);
  ck_assert_int_eq(count, 4);
}
END_TEST

START_TEST(already_seen_stops_recording_past_the_cap) {
  seen_t seen[LISTEN_SEEN_MAX];
  int count = 0;
  int i;
  for (i = 0; i < LISTEN_SEEN_MAX + 5; i++) {
    dvbstp_header_t h = make_header((unsigned)i, 0, 0);
    ck_assert_int_eq(already_seen(seen, &count, &h), 0);
  }
  ck_assert_int_eq(count, LISTEN_SEEN_MAX);
}
END_TEST

static Suite *listen_suite(void) {
  Suite *s = suite_create("dipisds_listen");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, already_seen_is_false_on_first_sighting);
  tcase_add_test(tc, already_seen_is_true_on_repeat);
  tcase_add_test(tc, already_seen_distinguishes_by_all_three_fields);
  tcase_add_test(tc, already_seen_stops_recording_past_the_cap);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(listen_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
