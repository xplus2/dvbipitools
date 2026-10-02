/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../../tls_fixture.h"
#include "lib/net/tls.h"
#include "lib/net/tls_server.h"

#define DRIVE_ROUNDS 2000
#define PAYLOAD_BYTES 200000

static char g_dir[] = "/tmp/dvbipi_tls_test_XXXXXX";
static char g_cert_a[600];
static char g_key_a[600];
static char g_cert_b[600];
static char g_key_b[600];

static void setup(void) {
  ck_assert_ptr_nonnull(mkdtemp(g_dir));
  signal(SIGPIPE, SIG_IGN);
  snprintf(g_cert_a, sizeof g_cert_a, "%s/a.crt", g_dir);
  snprintf(g_key_a, sizeof g_key_a, "%s/a.key", g_dir);
  snprintf(g_cert_b, sizeof g_cert_b, "%s/b.crt", g_dir);
  snprintf(g_key_b, sizeof g_key_b, "%s/b.key", g_dir);
  tls_fixture_write_cert(g_cert_a, g_key_a);
  tls_fixture_write_cert(g_cert_b, g_key_b);
}

static void teardown(void) {
  char cmd[600];

  snprintf(cmd, sizeof cmd, "rm -rf %s", g_dir);
  ck_assert_int_eq(system(cmd), 0);
}

static void set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);

  ck_assert_int_ge(flags, 0);
  ck_assert_int_eq(fcntl(fd, F_SETFL, flags | O_NONBLOCK), 0);
}

static void make_pair(int fds[2], int nonblocking) {
  ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  if (nonblocking) {
    set_nonblocking(fds[0]);
    set_nonblocking(fds[1]);
  }
}

static void copy_file(const char *src, const char *dst) {
  char buf[4096];
  FILE *in = fopen(src, "rb");
  FILE *out = fopen(dst, "wb");
  size_t n;

  ck_assert_ptr_nonnull(in);
  ck_assert_ptr_nonnull(out);
  while ((n = fread(buf, 1, sizeof buf, in)) > 0) ck_assert_uint_eq(fwrite(buf, 1, n, out), n);
  fclose(in);
  fclose(out);
}

static void write_text(const char *path, const char *text) {
  FILE *f = fopen(path, "wb");

  ck_assert_ptr_nonnull(f);
  fputs(text, f);
  fclose(f);
}

static int is_terminal(tls_handshake_status_t st) {
  return st == TLS_HANDSHAKE_DONE || st == TLS_HANDSHAKE_ERROR;
}

static void drive_handshake(tls_t *client, const tls_t *server, tls_handshake_status_t *cst, tls_handshake_status_t *sst) {
  *cst = TLS_HANDSHAKE_WANT_WRITE;
  *sst = TLS_HANDSHAKE_WANT_READ;
  for (int i = 0; i < DRIVE_ROUNDS && !(is_terminal(*cst) && is_terminal(*sst)); i++) {
    if (!is_terminal(*cst)) *cst = tls_handshake_step(client);
    if (!is_terminal(*sst)) *sst = tls_server_handshake_step(server);
    if (*cst == TLS_HANDSHAKE_ERROR && !is_terminal(*sst)) {
      *sst = tls_server_handshake_step(server);
      break;
    }
    usleep(200);
  }
}

typedef struct {
  tls_t *client;
  tls_t *server;
} link_t;

static void open_link(link_t *l, const tls_server_ctx_t *sc, const char *host, int insecure, int fds[2], tls_handshake_status_t *cst, tls_handshake_status_t *sst) {
  make_pair(fds, 1);
  l->server = tls_server_accept_start(sc, fds[1]);
  l->client = tls_connect_start(fds[0], host, insecure);
  ck_assert_ptr_nonnull(l->server);
  ck_assert_ptr_nonnull(l->client);
  drive_handshake(l->client, l->server, cst, sst);
}

static void wait_readable(const tls_t *t, int fd) {
  struct pollfd pfd = {fd, POLLIN, 0};

  if (tls_pending(t) == 0) poll(&pfd, 1, 50);
}

START_TEST(server_ctx_rejects_missing_garbage_and_mismatched_files) {
  char garbage[600];

  snprintf(garbage, sizeof garbage, "%s/garbage.crt", g_dir);
  write_text(garbage, "not a certificate\n");

  ck_assert_ptr_null(tls_server_ctx_new("/nonexistent/cert.pem", "/nonexistent/key.pem"));
  ck_assert_ptr_null(tls_server_ctx_new(garbage, g_key_a));
  ck_assert_ptr_null(tls_server_ctx_new(g_cert_a, garbage));
  ck_assert_ptr_null(tls_server_ctx_new(g_cert_a, g_key_b));
}
END_TEST

START_TEST(close_and_free_accept_null) {
  tls_close(NULL);
  tls_server_ctx_free(NULL);
}
END_TEST

START_TEST(handshake_and_bulk_data_with_insecure_client) {
  tls_server_ctx_t *sc = tls_server_ctx_new(g_cert_a, g_key_a);
  link_t l;
  int fds[2];
  tls_handshake_status_t cst;
  tls_handshake_status_t sst;
  unsigned char *payload = malloc(PAYLOAD_BYTES);
  unsigned char *rx = malloc(PAYLOAD_BYTES);
  unsigned char small[16];
  size_t sent = 0;
  size_t got = 0;

  ck_assert_ptr_nonnull(sc);
  ck_assert_ptr_nonnull(payload);
  ck_assert_ptr_nonnull(rx);
  open_link(&l, sc, "localhost", 1, fds, &cst, &sst);
  ck_assert_int_eq(cst, TLS_HANDSHAKE_DONE);
  ck_assert_int_eq(sst, TLS_HANDSHAKE_DONE);

  ck_assert_int_eq(tls_read(l.server, small, sizeof small), 0);
  ck_assert_int_eq((int)tls_write(l.client, "hello", 5), 5);
  wait_readable(l.server, fds[1]);
  ck_assert_int_eq((int)tls_read(l.server, small, sizeof small), 5);
  ck_assert_mem_eq(small, "hello", 5);

  ck_assert_int_eq((int)tls_write(l.server, "0123456789", 10), 10);
  wait_readable(l.client, fds[0]);
  ck_assert_int_eq((int)tls_read(l.client, small, 1), 1);
  ck_assert_int_eq(tls_pending(l.client), 9);
  ck_assert_int_eq((int)tls_read(l.client, small + 1, 9), 9);
  ck_assert_mem_eq(small, "0123456789", 10);

  for (size_t i = 0; i < PAYLOAD_BYTES; i++) payload[i] = (unsigned char)(i * 29 + 3);
  for (int i = 0; i < DRIVE_ROUNDS * 4 && got < PAYLOAD_BYTES; i++) {
    if (sent < PAYLOAD_BYTES) {
      ssize_t w = tls_write(l.server, payload + sent, PAYLOAD_BYTES - sent);

      ck_assert_int_ge((int)w, 0);
      sent += (size_t)w;
    }
    ssize_t r = tls_read(l.client, rx + got, PAYLOAD_BYTES - got);

    ck_assert_int_ge((int)r, 0);
    got += (size_t)r;
  }
  ck_assert_uint_eq(got, (size_t)PAYLOAD_BYTES);
  ck_assert_mem_eq(rx, payload, PAYLOAD_BYTES);

  tls_close(l.server);
  for (int i = 0; i < 200; i++) {
    ssize_t r = tls_read(l.client, small, sizeof small);

    if (r < 0) break;
    usleep(1000);
    ck_assert_int_lt(i, 199);
  }
  tls_close(l.client);
  tls_server_ctx_free(sc);
  free(payload);
  free(rx);
}
END_TEST

typedef struct {
  const char *name;
  const char *host;
  int trust_b;
  int expect_done;
} verify_case_t;

static const verify_case_t verify_cases[] = {
    {"trusted certificate and matching dns name", "localhost", 0, 1},
    {"trusted certificate and matching ip", "127.0.0.1", 0, 1},
    {"trusted certificate and wrong name", "wrong.example", 0, 0},
    {"untrusted certificate", "localhost", 1, 0},
};

START_TEST(secure_client_verifies_trust_and_hostname) {
  const verify_case_t *c = &verify_cases[_i];
  tls_server_ctx_t *sc = tls_server_ctx_new(g_cert_a, g_key_a);
  link_t l;
  int fds[2];
  tls_handshake_status_t cst;
  tls_handshake_status_t sst;

  ck_assert_ptr_nonnull(sc);
  ck_assert_int_eq(setenv("SSL_CERT_FILE", c->trust_b ? g_cert_b : g_cert_a, 1), 0);
  open_link(&l, sc, c->host, 0, fds, &cst, &sst);
  ck_assert_msg((cst == TLS_HANDSHAKE_DONE) == c->expect_done, "%s: client status %d", c->name, (int)cst);
  if (!c->expect_done) ck_assert_msg(cst == TLS_HANDSHAKE_ERROR, "%s: expected a handshake error", c->name);
  tls_close(l.client);
  tls_close(l.server);
  tls_server_ctx_free(sc);
}
END_TEST

START_TEST(reload_swaps_certificate_and_keeps_old_one_on_failure) {
  tls_server_ctx_t *sc = tls_server_ctx_new(g_cert_a, g_key_a);
  link_t l;
  int fds[2];
  tls_handshake_status_t cst;
  tls_handshake_status_t sst;

  ck_assert_ptr_nonnull(sc);
  ck_assert_int_eq(setenv("SSL_CERT_FILE", g_cert_b, 1), 0);

  open_link(&l, sc, "localhost", 0, fds, &cst, &sst);
  ck_assert_int_eq(cst, TLS_HANDSHAKE_ERROR);
  tls_close(l.client);
  tls_close(l.server);

  copy_file(g_cert_b, g_cert_a);
  copy_file(g_key_b, g_key_a);
  ck_assert_int_eq(tls_server_ctx_reload(sc), 0);
  open_link(&l, sc, "localhost", 0, fds, &cst, &sst);
  ck_assert_int_eq(cst, TLS_HANDSHAKE_DONE);
  ck_assert_int_eq(sst, TLS_HANDSHAKE_DONE);
  tls_close(l.client);
  tls_close(l.server);

  write_text(g_cert_a, "broken\n");
  ck_assert_int_eq(tls_server_ctx_reload(sc), -1);
  open_link(&l, sc, "localhost", 0, fds, &cst, &sst);
  ck_assert_int_eq(cst, TLS_HANDSHAKE_DONE);
  tls_close(l.client);
  tls_close(l.server);
  tls_server_ctx_free(sc);
}
END_TEST

typedef struct {
  tls_server_ctx_t *sc;
  int fd;
  int ok;
} echo_arg_t;

static void *echo_server_main(void *arg) {
  echo_arg_t *a = arg;
  tls_t *t = tls_server_accept_start(a->sc, a->fd);
  char buf[16];
  ssize_t n;

  a->ok = 0;
  if (!t) return NULL;
  if (tls_server_handshake_step(t) != TLS_HANDSHAKE_DONE) {
    tls_close(t);
    return NULL;
  }
  n = tls_read(t, buf, sizeof buf);
  if (n > 0 && tls_write(t, buf, (size_t)n) == n) a->ok = 1;
  tls_close(t);
  return NULL;
}

START_TEST(blocking_connect_echoes_through_server_thread) {
  tls_server_ctx_t *sc = tls_server_ctx_new(g_cert_a, g_key_a);
  echo_arg_t arg;
  pthread_t th;
  int fds[2];
  tls_t *client;
  char buf[16];

  ck_assert_ptr_nonnull(sc);
  make_pair(fds, 0);
  arg.sc = sc;
  arg.fd = fds[1];
  arg.ok = 0;
  ck_assert_int_eq(pthread_create(&th, NULL, echo_server_main, &arg), 0);

  client = tls_connect(fds[0], "localhost", 1);
  ck_assert_ptr_nonnull(client);
  ck_assert_int_eq((int)tls_write(client, "ping", 4), 4);
  ck_assert_int_eq((int)tls_read(client, buf, sizeof buf), 4);
  ck_assert_mem_eq(buf, "ping", 4);
  tls_close(client);
  pthread_join(th, NULL);
  ck_assert_int_eq(arg.ok, 1);
  tls_server_ctx_free(sc);
}
END_TEST

START_TEST(blocking_connect_fails_when_peer_closes_first_and_caller_keeps_fd) {
  int fds[2];

  make_pair(fds, 0);
  close(fds[1]);
  ck_assert_ptr_null(tls_connect(fds[0], "localhost", 1));
  ck_assert_int_eq(close(fds[0]), 0);
}
END_TEST

static Suite *tls_suite(void) {
  Suite *s = suite_create("tls");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_checked_fixture(tc, setup, teardown);
  tcase_add_test(tc, server_ctx_rejects_missing_garbage_and_mismatched_files);
  tcase_add_test(tc, close_and_free_accept_null);
  tcase_add_test(tc, handshake_and_bulk_data_with_insecure_client);
  tcase_add_loop_test(tc, secure_client_verifies_trust_and_hostname, 0, (int)(sizeof verify_cases / sizeof verify_cases[0]));
  tcase_add_test(tc, reload_swaps_certificate_and_keeps_old_one_on_failure);
  tcase_add_test(tc, blocking_connect_echoes_through_server_thread);
  tcase_add_test(tc, blocking_connect_fails_when_peer_closes_first_and_caller_keeps_fd);
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
