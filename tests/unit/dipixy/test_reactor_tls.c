/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/bio.h>
#include <openssl/ssl.h>

#include "../tls_fixture.h"
#include "dipixy/reactor/reactor_tls_int.h"

#define HANDSHAKE_ROUNDS 20
#define BIO_PAIR_BUF 17000

typedef struct {
  char dir[64];
  char cert_a[96];
  char key_a[96];
  char cert_b[96];
  char key_b[96];
  char garbage[96];
} creds_t;

static void creds_make(creds_t *c) {
  FILE *f;

  snprintf(c->dir, sizeof c->dir, "/tmp/dipixy_rtls_XXXXXX");
  ck_assert_ptr_nonnull(mkdtemp(c->dir));
  snprintf(c->cert_a, sizeof c->cert_a, "%s/a.crt", c->dir);
  snprintf(c->key_a, sizeof c->key_a, "%s/a.key", c->dir);
  snprintf(c->cert_b, sizeof c->cert_b, "%s/b.crt", c->dir);
  snprintf(c->key_b, sizeof c->key_b, "%s/b.key", c->dir);
  snprintf(c->garbage, sizeof c->garbage, "%s/garbage.pem", c->dir);
  tls_fixture_write_cert(c->cert_a, c->key_a);
  tls_fixture_write_cert(c->cert_b, c->key_b);
  f = fopen(c->garbage, "wb");
  ck_assert_ptr_nonnull(f);
  fputs("not a certificate\n", f);
  fclose(f);
}

static void creds_drop(const creds_t *c) {
  char cmd[96];

  snprintf(cmd, sizeof cmd, "rm -rf %s", c->dir);
  ck_assert_int_eq(system(cmd), 0);
}

START_TEST(context_builds_only_from_a_matching_certificate_and_key) {
  creds_t c;
  SSL_CTX *ctx;

  creds_make(&c);
  ctx = build_ssl_ctx(c.cert_a, c.key_a);
  ck_assert_ptr_nonnull(ctx);
  SSL_CTX_free(ctx);
  ck_assert_ptr_null(build_ssl_ctx("/nonexistent/cert.pem", c.key_a));
  ck_assert_ptr_null(build_ssl_ctx(c.cert_a, "/nonexistent/key.pem"));
  ck_assert_ptr_null(build_ssl_ctx(c.garbage, c.key_a));
  ck_assert_ptr_null(build_ssl_ctx(c.cert_a, c.garbage));
  ck_assert_ptr_null(build_ssl_ctx(c.cert_a, c.key_b));
  ck_assert_ptr_null(build_ssl_ctx(c.cert_b, c.key_a));
  creds_drop(&c);
}
END_TEST

START_TEST(context_carries_the_hardening_options) {
  creds_t c;
  SSL_CTX *ctx;
  uint64_t opts;

  creds_make(&c);
  ctx = build_ssl_ctx(c.cert_a, c.key_a);
  ck_assert_ptr_nonnull(ctx);
  ck_assert_int_eq((int)SSL_CTX_get_min_proto_version(ctx), TLS1_2_VERSION);
  opts = SSL_CTX_get_options(ctx);
  ck_assert_int_ne((opts & SSL_OP_NO_COMPRESSION) != 0, 0);
  ck_assert_int_ne((opts & SSL_OP_NO_RENEGOTIATION) != 0, 0);
  ck_assert_int_ne((opts & SSL_OP_CIPHER_SERVER_PREFERENCE) != 0, 0);
  ck_assert_int_ne((int)(SSL_CTX_get_session_cache_mode(ctx) & SSL_SESS_CACHE_SERVER), 0);
  SSL_CTX_free(ctx);
  creds_drop(&c);
}
END_TEST

#ifdef HAVE_HTTP2
typedef struct {
  const char *name;
  const unsigned char *offer;
  unsigned offer_len;
  int http2_enabled;
  const char *expect;
} alpn_case_t;

static const alpn_case_t alpn_cases[] = {
    {"h2 preferred", (const unsigned char *)"\x02h2\x08http/1.1", 12, 1, "h2"},
    {"h2 listed second", (const unsigned char *)"\x08http/1.1\x02h2", 12, 1, "h2"},
    {"only http/1.1", (const unsigned char *)"\x08http/1.1", 9, 1, "http/1.1"},
    {"h2 disabled", (const unsigned char *)"\x02h2\x08http/1.1", 12, 0, "http/1.1"},
    {"unknown protocols get no alpn", (const unsigned char *)"\x05spdy3", 6, 1, NULL},
    {"empty offer gets no alpn", (const unsigned char *)"", 0, 1, NULL},
};

START_TEST(alpn_selection_prefers_h2_and_falls_back_to_http11) {
  const alpn_case_t *ac = &alpn_cases[_i];
  const unsigned char *out = NULL;
  unsigned char outlen = 0;

  tls_set_http2_enabled(ac->http2_enabled);
  if (!ac->expect) {
    ck_assert_int_eq(alpn_select_cb(NULL, &out, &outlen, ac->offer, ac->offer_len, NULL), SSL_TLSEXT_ERR_NOACK);
    return;
  }
  ck_assert_int_eq(alpn_select_cb(NULL, &out, &outlen, ac->offer, ac->offer_len, NULL), SSL_TLSEXT_ERR_OK);
  ck_assert_uint_eq(outlen, strlen(ac->expect));
  ck_assert_mem_eq(out, ac->expect, outlen);
}
END_TEST

static int handshake_pair(SSL *server, SSL *client) {
  BIO *b1 = NULL;
  BIO *b2 = NULL;

  ck_assert_int_eq(BIO_new_bio_pair(&b1, BIO_PAIR_BUF, &b2, BIO_PAIR_BUF), 1);
  SSL_set_bio(server, b1, b1);
  SSL_set_bio(client, b2, b2);
  SSL_set_accept_state(server);
  SSL_set_connect_state(client);
  for (int i = 0; i < HANDSHAKE_ROUNDS; i++) {
    int c = SSL_do_handshake(client);
    int s = SSL_do_handshake(server);

    if (c == 1 && s == 1) return 1;
  }
  return 0;
}

START_TEST(negotiated_protocol_matches_the_client_offer_end_to_end) {
  const alpn_case_t *ac = &alpn_cases[_i];
  creds_t c;
  SSL_CTX *sctx;
  SSL_CTX *cctx = SSL_CTX_new(TLS_client_method());
  SSL *server;
  SSL *client;
  const unsigned char *got = NULL;
  unsigned got_len = 0;

  creds_make(&c);
  tls_set_http2_enabled(ac->http2_enabled);
  sctx = build_ssl_ctx(c.cert_a, c.key_a);
  ck_assert_ptr_nonnull(sctx);
  SSL_CTX_set_verify(cctx, SSL_VERIFY_NONE, NULL);
  server = SSL_new(sctx);
  client = SSL_new(cctx);
  if (ac->offer_len) ck_assert_int_eq(SSL_set_alpn_protos(client, ac->offer, ac->offer_len), 0);
  ck_assert_int_eq(handshake_pair(server, client), 1);
  SSL_get0_alpn_selected(client, &got, &got_len);
  if (ac->expect) {
    ck_assert_uint_eq(got_len, strlen(ac->expect));
    ck_assert_mem_eq(got, ac->expect, got_len);
  } else {
    ck_assert_uint_eq(got_len, 0u);
  }
  SSL_free(server);
  SSL_free(client);
  SSL_CTX_free(sctx);
  SSL_CTX_free(cctx);
  creds_drop(&c);
}
END_TEST
#endif

static Suite *tls_suite(void) {
  Suite *s = suite_create("dipixy_reactor_tls");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 20);
  tcase_add_test(tc, context_builds_only_from_a_matching_certificate_and_key);
  tcase_add_test(tc, context_carries_the_hardening_options);
#ifdef HAVE_HTTP2
  tcase_add_loop_test(tc, alpn_selection_prefers_h2_and_falls_back_to_http11, 0, (int)(sizeof alpn_cases / sizeof alpn_cases[0]));
  tcase_add_loop_test(tc, negotiated_protocol_matches_the_client_offer_end_to_end, 0, (int)(sizeof alpn_cases / sizeof alpn_cases[0]));
#endif
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(tls_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
