/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/ssl.h>

#include "../tls_fixture.h"
#include "dipixy/reactor/conn.h"
#include "dipixy/reactor/reactor_tls_int.h"

#define FD_TABLE 256
#define HANDSHAKE_ROUNDS 50

typedef struct {
  char dir[64];
  char cert[96];
  char key[96];
} creds_t;

static void creds_make(creds_t *c) {
  snprintf(c->dir, sizeof c->dir, "/tmp/dipixy_rtlslc_XXXXXX");
  ck_assert_ptr_nonnull(mkdtemp(c->dir));
  snprintf(c->cert, sizeof c->cert, "%s/a.crt", c->dir);
  snprintf(c->key, sizeof c->key, "%s/a.key", c->dir);
  tls_fixture_write_cert(c->cert, c->key);
}

static void creds_drop(const creds_t *c) {
  unlink(c->cert);
  unlink(c->key);
  rmdir(c->dir);
}

static void set_nonblocking(int fd) {
  ck_assert_int_ne(fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK), -1);
}

static SSL *client_handshake(int server_fd, int client_fd, SSL_CTX *cctx) {
  SSL *ssl = SSL_new(cctx);
  int server_done = 0;
  int client_done = 0;

  ck_assert_ptr_nonnull(ssl);
  SSL_set_fd(ssl, client_fd);
  SSL_set_connect_state(ssl);
  for (int i = 0; i < HANDSHAKE_ROUNDS && !(server_done && client_done); i++) {
    if (!client_done) client_done = SSL_do_handshake(ssl) == 1;
    if (!server_done) server_done = tls_handshake(server_fd) == 1;
  }
  ck_assert_int_eq(client_done && server_done, 1);
  return ssl;
}

START_TEST(init_with_an_unusable_certificate_leaves_tls_off) {
  ck_assert_int_eq(tls_init("/nonexistent/cert.pem", "/nonexistent/key.pem", FD_TABLE), 0);
  ck_assert_int_eq(tls_is_running(), 0);
  ck_assert_int_eq(tls_create_listen_sock(0, "127.0.0.1"), -1);
  ck_assert_int_eq(tls_create_listen_sock6(0, "::1"), -1);
  ck_assert_int_eq(tls_accept(5), -1);
  ck_assert_int_eq(reload_tls(), -1);
}
END_TEST

START_TEST(init_rejects_an_oversized_descriptor_table) {
  creds_t c;

  creds_make(&c);
  ck_assert_int_eq(tls_init(c.cert, c.key, CONN_TABLE_MAX + 1), -1);
  ck_assert_int_eq(tls_is_running(), 0);
  creds_drop(&c);
}
END_TEST

START_TEST(listen_sockets_follow_the_tls_lifecycle) {
  creds_t c;
  int fd;

  creds_make(&c);
  ck_assert_int_eq(tls_init(c.cert, c.key, FD_TABLE), 0);
  ck_assert_int_eq(tls_is_running(), 1);
  fd = tls_create_listen_sock(0, "127.0.0.1");
  ck_assert_int_ge(fd, 0);
  close(fd);
  if (geteuid() != 0) ck_assert_int_eq(tls_create_listen_sock(1, "127.0.0.1"), -1);
  fd = socket(AF_INET6, SOCK_STREAM, 0);
  if (fd >= 0) {
    int v6;

    close(fd);
    v6 = tls_create_listen_sock6(0, "::1");
    if (v6 >= 0) close(v6);
    v6 = tls_create_listen_sock6(0, NULL);
    if (v6 >= 0) close(v6);
    v6 = tls_create_listen_sock6(0, "::");
    if (v6 >= 0) close(v6);
    if (geteuid() != 0) ck_assert_int_eq(tls_create_listen_sock6(1, "::1"), -1);
  }
  tls_cleanup();
  ck_assert_int_eq(tls_is_running(), 0);
  ck_assert_int_eq(tls_create_listen_sock(0, "127.0.0.1"), -1);
  creds_drop(&c);
}
END_TEST

START_TEST(reload_swaps_the_context_and_keeps_the_old_one_on_failure) {
  creds_t c;
  FILE *f;

  creds_make(&c);
  ck_assert_int_eq(tls_init(c.cert, c.key, FD_TABLE), 0);
  for (int i = 0; i < 10; i++) ck_assert_int_eq(reload_tls(), 0);
  tls_ctx_gc_sweep();
  f = fopen(c.cert, "wb");
  ck_assert_ptr_nonnull(f);
  fputs("not a certificate\n", f);
  fclose(f);
  ck_assert_int_eq(reload_tls(), -1);
  ck_assert_int_eq(tls_is_running(), 1);
  tls_cleanup();
  creds_drop(&c);
}
END_TEST

START_TEST(descriptor_queries_are_safe_for_unknown_descriptors) {
  creds_t c;
  char cn[32] = "x";

  creds_make(&c);
  ck_assert_int_eq(tls_init(c.cert, c.key, FD_TABLE), 0);
  ck_assert_int_eq(tls_is_tls(-1), 0);
  ck_assert_int_eq(tls_is_tls(FD_TABLE + 1), 0);
  ck_assert_int_eq(tls_is_tls(7), 0);
  ck_assert_int_eq(tls_client_cert_verified(-1), 0);
  ck_assert_int_eq(tls_client_cert_verified(FD_TABLE + 1), 0);
  ck_assert_int_eq(tls_client_cert_verified(7), 0);
  tls_get_client_cert_cn(7, cn, sizeof cn);
  ck_assert_str_eq(cn, "");
  tls_get_client_cert_cn(-1, cn, sizeof cn);
  tls_get_client_cert_cn(FD_TABLE + 1, cn, sizeof cn);
  tls_get_client_cert_cn(7, NULL, 0);
  ck_assert_int_eq(tls_has_pending(-1), 0);
  ck_assert_int_eq(tls_has_pending(7), 0);
  ck_assert_int_eq(tls_accept(-1), -1);
  ck_assert_int_eq(tls_accept(FD_TABLE + 1), -1);
  ck_assert_int_eq(tls_handshake(-1), -1);
  ck_assert_int_eq(tls_handshake(7), -1);
  tls_cleanup();
  creds_drop(&c);
}
END_TEST

START_TEST(a_session_over_a_socket_pair_handshakes_and_closes) {
  creds_t c;
  int sv[2];
  SSL_CTX *cctx = SSL_CTX_new(TLS_client_method());
  SSL *client;
  char buf[16];
  char cn[32] = "x";

  creds_make(&c);
  ck_assert_int_eq(tls_init(c.cert, c.key, FD_TABLE), 0);
  tls_gc_init(2);
  ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  set_nonblocking(sv[0]);
  set_nonblocking(sv[1]);
  SSL_CTX_set_verify(cctx, SSL_VERIFY_NONE, NULL);
  ck_assert_int_eq(tls_accept(sv[0]), 0);
  ck_assert_int_eq(tls_is_tls(sv[0]), 1);
  client = client_handshake(sv[0], sv[1], cctx);

  ck_assert_int_eq(SSL_write(client, "ping", 4), 4);
  ck_assert_int_eq((int)tls_net_recv(sv[0], buf, sizeof buf), 4);
  ck_assert_mem_eq(buf, "ping", 4);
  ck_assert_int_eq(tls_has_pending(sv[0]), 0);
  ck_assert_int_eq((int)tls_net_send(sv[0], "pong", 4), 4);
  ck_assert_int_eq(SSL_read(client, buf, sizeof buf), 4);
  ck_assert_mem_eq(buf, "pong", 4);
  ck_assert_int_eq(tls_client_cert_verified(sv[0]), 0);
  tls_get_client_cert_cn(sv[0], cn, sizeof cn);
  ck_assert_str_eq(cn, "");

  tls_close_fd(sv[0]);
  ck_assert_int_eq(tls_is_tls(sv[0]), 0);
  tls_gc_sweep();
  tls_cleanup();
  SSL_free(client);
  SSL_CTX_free(cctx);
  close(sv[1]);
  creds_drop(&c);
}
END_TEST

START_TEST(close_without_a_gc_queue_frees_immediately) {
  creds_t c;
  int sv[2];

  creds_make(&c);
  ck_assert_int_eq(tls_init(c.cert, c.key, FD_TABLE), 0);
  ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  ck_assert_int_eq(tls_accept(sv[0]), 0);
  tls_close_fd(sv[0]);
  ck_assert_int_eq(tls_is_tls(sv[0]), 0);
  close(sv[1]);
  tls_cleanup();
  creds_drop(&c);
}
END_TEST

START_TEST(close_of_plain_and_foreign_descriptors) {
  creds_t c;
  int sv[2];

  creds_make(&c);
  ck_assert_int_eq(tls_init(c.cert, c.key, FD_TABLE), 0);
  ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  tls_close_fd(sv[0]);
  tls_close_fd(-1);
  tls_close_fd(FD_TABLE + 1);
  t_reactor_fd = sv[1];
  t_close_deferred = 0;
  tls_close_fd(sv[1]);
  ck_assert_int_eq(t_close_deferred, 1);
  t_reactor_fd = -1;
  close(sv[1]);
  tls_cleanup();
  creds_drop(&c);
}
END_TEST

static Suite *tls_lifecycle_suite(void) {
  Suite *s = suite_create("dipixy_reactor_tls_lifecycle");
  TCase *tc = tcase_create("core");

  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, init_with_an_unusable_certificate_leaves_tls_off);
  tcase_add_test(tc, init_rejects_an_oversized_descriptor_table);
  tcase_add_test(tc, listen_sockets_follow_the_tls_lifecycle);
  tcase_add_test(tc, reload_swaps_the_context_and_keeps_the_old_one_on_failure);
  tcase_add_test(tc, descriptor_queries_are_safe_for_unknown_descriptors);
  tcase_add_test(tc, a_session_over_a_socket_pair_handshakes_and_closes);
  tcase_add_test(tc, close_without_a_gc_queue_frees_immediately);
  tcase_add_test(tc, close_of_plain_and_foreign_descriptors);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(tls_lifecycle_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
