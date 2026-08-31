/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dipixy/core/tlscert.h"

static void touch(const char *path) {
  FILE *f = fopen(path, "w");
  ck_assert_ptr_nonnull(f);
  fclose(f);
}

static void chdir_fresh_tmpdir(void) {
  char tmpl[] = "/tmp/dipixy_tlscert_test_XXXXXX";
  char *dir = mkdtemp(tmpl);
  ck_assert_ptr_nonnull(dir);
  ck_assert_int_eq(chdir(dir), 0);
}

START_TEST(explicit_paths_returned_verbatim_even_if_missing) {
  const char *cert_out = NULL;
  const char *key_out = NULL;
  int found = tlscert_find("/no/such/cert.pem", "/no/such/key.pem", &cert_out, &key_out);
  ck_assert_int_eq(found, 1);
  ck_assert_str_eq(cert_out, "/no/such/cert.pem");
  ck_assert_str_eq(key_out, "/no/such/key.pem");
}
END_TEST

START_TEST(partial_explicit_args_fall_back_to_scan) {
  const char *cert_out = NULL;
  const char *key_out = NULL;
  int found;

  chdir_fresh_tmpdir();
  touch("server.crt");
  touch("server.key");

  found = tlscert_find("/no/such/cert.pem", NULL, &cert_out, &key_out);
  ck_assert_int_eq(found, 1);
  ck_assert_str_eq(cert_out, "./server.crt");
  ck_assert_str_eq(key_out, "./server.key");
}
END_TEST

START_TEST(cwd_pair_found) {
  const char *cert_out = NULL;
  const char *key_out = NULL;
  int found;

  chdir_fresh_tmpdir();
  touch("server.crt");
  touch("server.key");

  found = tlscert_find(NULL, NULL, &cert_out, &key_out);
  ck_assert_int_eq(found, 1);
  ck_assert_str_eq(cert_out, "./server.crt");
  ck_assert_str_eq(key_out, "./server.key");
}
END_TEST

START_TEST(cwd_missing_key_is_not_a_match) {
  const char *cert_out = NULL;
  const char *key_out = NULL;
  int found;

  chdir_fresh_tmpdir();
  touch("server.crt");

  found = tlscert_find(NULL, NULL, &cert_out, &key_out);
  ck_assert_int_eq(found, 0);
}
END_TEST

START_TEST(nothing_found_anywhere) {
  const char *cert_out = NULL;
  const char *key_out = NULL;
  int found;

  chdir_fresh_tmpdir();

  found = tlscert_find(NULL, NULL, &cert_out, &key_out);
  ck_assert_int_eq(found, 0);
}
END_TEST

static Suite *tlscert_suite(void) {
  Suite *s = suite_create("dipixy_tlscert");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, explicit_paths_returned_verbatim_even_if_missing);
  tcase_add_test(tc, partial_explicit_args_fall_back_to_scan);
  tcase_add_test(tc, cwd_pair_found);
  tcase_add_test(tc, cwd_missing_key_is_not_a_match);
  tcase_add_test(tc, nothing_found_anywhere);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(tlscert_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
