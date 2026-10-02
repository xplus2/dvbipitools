/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "h3_rig.h"

START_TEST(unknown_path_is_answered_not_found_over_a_real_quic_connection) {
  h3rig_t h;
  int64_t sid;

  h3r_open(&h);
  sid = h3r_request(&h, "GET", "/no/such/route", NULL, 0);
  ck_assert_int_eq(h3r_wait_response(&h, sid), 1);
  ck_assert_int_eq(h3r_resp_for(&h, sid)->status, 404);
  h3r_close(&h);
}
END_TEST

static Suite *h3_rig_suite(void) {
  Suite *s = suite_create("dipixy_h3_rig");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, unknown_path_is_answered_not_found_over_a_real_quic_connection);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(h3_rig_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
