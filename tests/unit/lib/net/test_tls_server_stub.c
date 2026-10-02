/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>

#include "lib/net/tls_server.h"

START_TEST(context_creation_always_fails) {
  ck_assert_ptr_null(tls_server_ctx_new("/some/cert.pem", "/some/key.pem"));
  ck_assert_ptr_null(tls_server_ctx_new(NULL, NULL));
}
END_TEST

START_TEST(reload_reports_failure_and_free_accepts_null) {
  ck_assert_int_eq(tls_server_ctx_reload(NULL), -1);
  tls_server_ctx_free(NULL);
}
END_TEST

START_TEST(accept_start_yields_no_connection) {
  ck_assert_ptr_null(tls_server_accept_start(NULL, -1));
  ck_assert_ptr_null(tls_server_accept_start(NULL, 0));
}
END_TEST

START_TEST(handshake_step_always_reports_error) {
  ck_assert_int_eq(tls_server_handshake_step(NULL), TLS_HANDSHAKE_ERROR);
}
END_TEST

static Suite *tls_server_stub_suite(void) {
  Suite *s = suite_create("tls_server_stub");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, context_creation_always_fails);
  tcase_add_test(tc, reload_reports_failure_and_free_accepts_null);
  tcase_add_test(tc, accept_start_yields_no_connection);
  tcase_add_test(tc, handshake_step_always_reports_error);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(tls_server_stub_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
