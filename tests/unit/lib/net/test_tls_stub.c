/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>

#include "lib/net/tls.h"

START_TEST(every_entry_point_fails_cleanly) {
  char buf[8];
  ck_assert_ptr_null(tls_connect(-1, "example.org", 0));
  ck_assert_ptr_null(tls_connect_start(-1, "example.org", 1));
  ck_assert_int_eq(tls_handshake_step(NULL), TLS_HANDSHAKE_ERROR);
  ck_assert_int_eq((int)tls_read(NULL, buf, sizeof buf), -1);
  ck_assert_int_eq((int)tls_write(NULL, buf, sizeof buf), -1);
  ck_assert_int_eq(tls_pending(NULL), 0);
  ck_assert_int_eq(tls_eof(NULL), 0);
  ck_assert_int_eq(tls_want_read(NULL), 0);
  tls_close(NULL);
}
END_TEST

static Suite *tls_stub_suite(void) {
  Suite *s = suite_create("tls_stub");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, every_entry_point_fails_cleanly);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(tls_stub_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
