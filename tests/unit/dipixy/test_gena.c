/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipixy/args.h"
#include "dipixy/dlna/gena.h"

START_TEST(subscribe_new_returns_a_sid) {
  config_t cfg;
  char sid[64];
  memset(&cfg, 0, sizeof cfg);
  gena_subscribe_new(&cfg, "cd", "<http://127.0.0.1:1/notify>", sid, sizeof sid);
  ck_assert_int_eq(strncmp(sid, "uuid:", 5), 0);
}
END_TEST

START_TEST(subscribe_new_ignores_missing_callback) {
  config_t cfg;
  char sid[64];
  memset(&cfg, 0, sizeof cfg);
  gena_subscribe_new(&cfg, "cd", NULL, sid, sizeof sid);
  ck_assert_int_eq(strncmp(sid, "uuid:", 5), 0);
}
END_TEST

START_TEST(renew_echoes_given_sid) {
  char sid[64];
  gena_renew("uuid:some-existing-sid", sid, sizeof sid);
  ck_assert_str_eq(sid, "uuid:some-existing-sid");
}
END_TEST

START_TEST(renew_without_sid_returns_a_fresh_one) {
  char sid[64];
  gena_renew(NULL, sid, sizeof sid);
  ck_assert_int_eq(strncmp(sid, "uuid:", 5), 0);
}
END_TEST

START_TEST(unsubscribe_is_a_noop) {
  gena_unsubscribe("uuid:whatever");
  gena_unsubscribe(NULL);
}
END_TEST

START_TEST(system_update_id_stays_fixed) {
  ck_assert_uint_eq(gena_system_update_id(), 1);
  gena_notify_system_update();
  ck_assert_uint_eq(gena_system_update_id(), 1);
}
END_TEST

static Suite *gena_suite(void) {
  Suite *s = suite_create("dipixy_gena");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, subscribe_new_returns_a_sid);
  tcase_add_test(tc, subscribe_new_ignores_missing_callback);
  tcase_add_test(tc, renew_echoes_given_sid);
  tcase_add_test(tc, renew_without_sid_returns_a_fresh_one);
  tcase_add_test(tc, unsubscribe_is_a_noop);
  tcase_add_test(tc, system_update_id_stays_fixed);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(gena_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
