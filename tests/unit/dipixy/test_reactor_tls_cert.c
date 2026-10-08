/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/ssl.h>
#include <openssl/x509.h>

#include "../tls_fixture.h"
#include "dipixy/reactor/reactor_tls_int.h"

typedef struct {
  char dir[64];
  char cert[96];
  char key[96];
  char garbage[96];
} creds_t;

static void creds_make(creds_t *c) {
  FILE *f;

  snprintf(c->dir, sizeof c->dir, "/tmp/dipixy_rtlscert_XXXXXX");
  ck_assert_ptr_nonnull(mkdtemp(c->dir));
  snprintf(c->cert, sizeof c->cert, "%s/a.crt", c->dir);
  snprintf(c->key, sizeof c->key, "%s/a.key", c->dir);
  snprintf(c->garbage, sizeof c->garbage, "%s/garbage.pem", c->dir);
  tls_fixture_write_cert(c->cert, c->key);
  f = fopen(c->garbage, "wb");
  ck_assert_ptr_nonnull(f);
  fputs("not a certificate\n", f);
  fclose(f);
}

static void creds_drop(const creds_t *c) {
  unlink(c->cert);
  unlink(c->key);
  unlink(c->garbage);
  rmdir(c->dir);
}

static SSL_CTX *swap_ctx(SSL_CTX *ctx) {
  return atomic_exchange(&g_ssl_ctx, ctx);
}

START_TEST(name_cn_is_extracted_and_truncated) {
  X509_NAME *name = X509_NAME_new();
  char buf[32];

  ck_assert_ptr_nonnull(name);
  ck_assert_int_eq(tls_name_cn(name, buf, sizeof buf), -1);
  ck_assert_int_eq(X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char *)"host.example", -1, -1, 0), 1);
  ck_assert_int_eq(tls_name_cn(name, buf, sizeof buf), 0);
  ck_assert_str_eq(buf, "host.example");
  ck_assert_int_eq(tls_name_cn(name, buf, 5), 0);
  ck_assert_str_eq(buf, "host");
  ck_assert_int_eq(tls_name_cn(name, NULL, sizeof buf), -1);
  ck_assert_int_eq(tls_name_cn(name, buf, 0), -1);
  X509_NAME_free(name);
}
END_TEST

START_TEST(info_from_file_describes_the_certificate) {
  creds_t c;
  char buf[256];

  creds_make(&c);
  tls_cert_info(buf, sizeof buf, c.cert, 1);
  ck_assert_ptr_nonnull(strstr(buf, "CN=localhost"));
  ck_assert_ptr_nonnull(strstr(buf, "valid "));
  creds_drop(&c);
}
END_TEST

START_TEST(info_from_file_reports_unusable_paths) {
  creds_t c;
  char buf[256];

  creds_make(&c);
  tls_cert_info(buf, sizeof buf, NULL, 1);
  ck_assert_str_eq(buf, "(no path configured)");
  tls_cert_info(buf, sizeof buf, "", 1);
  ck_assert_str_eq(buf, "(no path configured)");
  tls_cert_info(buf, sizeof buf, "/nonexistent/cert.pem", 1);
  ck_assert_ptr_nonnull(strstr(buf, "(cannot open /nonexistent/cert.pem)"));
  tls_cert_info(buf, sizeof buf, c.garbage, 1);
  ck_assert_ptr_nonnull(strstr(buf, "(failed to parse "));
  creds_drop(&c);
}
END_TEST

START_TEST(info_from_context_follows_the_loaded_certificate) {
  creds_t c;
  char buf[256];
  SSL_CTX *bare = SSL_CTX_new(TLS_server_method());
  SSL_CTX *full;
  SSL_CTX *saved;

  ck_assert_ptr_nonnull(bare);
  creds_make(&c);
  full = build_ssl_ctx(c.cert, c.key);
  ck_assert_ptr_nonnull(full);

  saved = swap_ctx(NULL);
  tls_cert_info(buf, sizeof buf, NULL, 0);
  ck_assert_str_eq(buf, "(TLS not initialised)");
  swap_ctx(bare);
  tls_cert_info(buf, sizeof buf, NULL, 0);
  ck_assert_str_eq(buf, "(no certificate loaded)");
  swap_ctx(full);
  tls_cert_info(buf, sizeof buf, NULL, 0);
  ck_assert_ptr_nonnull(strstr(buf, "CN=localhost"));
  swap_ctx(saved);

  SSL_CTX_free(bare);
  SSL_CTX_free(full);
  creds_drop(&c);
}
END_TEST

START_TEST(detail_from_file_carries_name_validity_and_aliases) {
  creds_t c;
  tls_cert_detail_t d;

  creds_make(&c);
  ck_assert_int_eq(tls_cert_detail(c.cert, 1, &d), 1);
  ck_assert_str_eq(d.cn, "localhost");
  ck_assert_uint_gt(strlen(d.valid_from), 0u);
  ck_assert_uint_gt(strlen(d.valid_to), 0u);
  ck_assert_int_eq(d.alias_count, 1);
  ck_assert_str_eq(d.aliases[0], "localhost");
  creds_drop(&c);
}
END_TEST

START_TEST(detail_from_file_fails_on_unusable_paths) {
  creds_t c;
  tls_cert_detail_t d;

  creds_make(&c);
  ck_assert_int_eq(tls_cert_detail(NULL, 1, &d), 0);
  ck_assert_int_eq(tls_cert_detail("", 1, &d), 0);
  ck_assert_int_eq(tls_cert_detail("/nonexistent/cert.pem", 1, &d), 0);
  ck_assert_int_eq(tls_cert_detail(c.garbage, 1, &d), 0);
  creds_drop(&c);
}
END_TEST

START_TEST(detail_from_context_follows_the_loaded_certificate) {
  creds_t c;
  tls_cert_detail_t d;
  SSL_CTX *bare = SSL_CTX_new(TLS_server_method());
  SSL_CTX *full;
  SSL_CTX *saved;

  ck_assert_ptr_nonnull(bare);
  creds_make(&c);
  full = build_ssl_ctx(c.cert, c.key);
  ck_assert_ptr_nonnull(full);

  saved = swap_ctx(NULL);
  ck_assert_int_eq(tls_cert_detail(NULL, 0, &d), 0);
  swap_ctx(bare);
  ck_assert_int_eq(tls_cert_detail(NULL, 0, &d), 0);
  swap_ctx(full);
  ck_assert_int_eq(tls_cert_detail(NULL, 0, &d), 1);
  ck_assert_str_eq(d.cn, "localhost");
  swap_ctx(saved);

  SSL_CTX_free(bare);
  SSL_CTX_free(full);
  creds_drop(&c);
}
END_TEST

static Suite *tls_cert_suite(void) {
  Suite *s = suite_create("dipixy_reactor_tls_cert");
  TCase *tc = tcase_create("core");

  tcase_add_test(tc, name_cn_is_extracted_and_truncated);
  tcase_add_test(tc, info_from_file_describes_the_certificate);
  tcase_add_test(tc, info_from_file_reports_unusable_paths);
  tcase_add_test(tc, info_from_context_follows_the_loaded_certificate);
  tcase_add_test(tc, detail_from_file_carries_name_validity_and_aliases);
  tcase_add_test(tc, detail_from_file_fails_on_unusable_paths);
  tcase_add_test(tc, detail_from_context_follows_the_loaded_certificate);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(tls_cert_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
