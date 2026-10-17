/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/net/rist/ristin.h"
#include "lib/net/rist/ristlog.h"
#include "lib/net/rist/ristout.h"

START_TEST(output_stub_fails_cleanly) {
  ristout_cfg_t cfg;
  unsigned char buf[188] = {0};
  memset(&cfg, 0, sizeof cfg);
  ck_assert_ptr_null(ristout_open(&cfg));
  ck_assert_int_eq(ristout_write(NULL, buf, sizeof buf), -1);
  ristout_idle_push(NULL, 1.0);
  ristout_close(NULL);
}
END_TEST

START_TEST(input_stub_fails_cleanly) {
  ristin_cfg_t cfg;
  memset(&cfg, 0, sizeof cfg);
  ck_assert_ptr_null(ristin_open(&cfg));
  ck_assert_int_eq(ristin_fd(NULL), -1);
  ristin_close(NULL);
}
END_TEST

START_TEST(log_stub_has_no_settings) {
  ck_assert_ptr_null(ristlog_get(0));
  ck_assert_ptr_null(ristlog_get(1));
}
END_TEST

static Suite *rist_stub_suite(void) {
  Suite *s = suite_create("rist_stubs");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, output_stub_fails_cleanly);
  tcase_add_test(tc, input_stub_fails_cleanly);
  tcase_add_test(tc, log_stub_has_no_settings);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(rist_stub_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
