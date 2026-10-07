/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../../tls_fixture.h"
#include "lib/net/httpclient/httpclient.h"
#include "lib/net/tls_server.h"

#define SERVER_ROUNDS 300
#define POLL_MS 20

typedef struct {
  int listen_fd;
  tls_server_ctx_t *sc;
  const char *response;
} tls_server_arg_t;

static char g_dir[] = "/tmp/dvbipi_httptls_XXXXXX";
static char g_cert[600];
static char g_key[600];

static void setup(void) {
  ck_assert_ptr_nonnull(mkdtemp(g_dir));
  signal(SIGPIPE, SIG_IGN);
  snprintf(g_cert, sizeof g_cert, "%s/a.crt", g_dir);
  snprintf(g_key, sizeof g_key, "%s/a.key", g_dir);
  tls_fixture_write_cert(g_cert, g_key);
}

static void teardown(void) {
  unlink(g_cert);
  unlink(g_key);
  rmdir(g_dir);
}

static int make_listener(unsigned *port_out) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(fd, 1), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  *port_out = ntohs(addr.sin_port);
  return fd;
}

static int server_handshake(const tls_t *t, int fd) {
  for (int i = 0; i < SERVER_ROUNDS; i++) {
    struct pollfd pfd = {fd, POLLIN, 0};
    tls_handshake_status_t st = tls_server_handshake_step(t);

    if (st == TLS_HANDSHAKE_DONE) return 0;
    if (st == TLS_HANDSHAKE_ERROR) return -1;
    poll(&pfd, 1, POLL_MS);
  }
  return -1;
}

static void server_serve_request(tls_t *t, int fd, const char *response) {
  char buf[4096];
  size_t got = 0;
  size_t sent = 0;
  size_t len = strlen(response);

  for (int i = 0; i < SERVER_ROUNDS; i++) {
    struct pollfd pfd = {fd, POLLIN, 0};
    ssize_t n = tls_read(t, buf + got, sizeof buf - got);

    if (n < 0) return;
    got += (size_t)n;
    if (got >= 4 && memmem(buf, got, "\r\n\r\n", 4)) break;
    if (n == 0) poll(&pfd, 1, POLL_MS);
  }
  for (int i = 0; i < SERVER_ROUNDS && sent < len; i++) {
    struct pollfd pfd = {fd, POLLOUT, 0};
    ssize_t n = tls_write(t, response + sent, len - sent);

    if (n < 0) return;
    sent += (size_t)n;
    if (n == 0) poll(&pfd, 1, POLL_MS);
  }
}

static void *tls_serve_once(void *arg) {
  const tls_server_arg_t *a = arg;
  int cfd = accept(a->listen_fd, NULL, NULL);
  tls_t *t;

  if (cfd < 0) return NULL;
  ck_assert_int_eq(fcntl(cfd, F_SETFL, fcntl(cfd, F_GETFL, 0) | O_NONBLOCK), 0);
  t = tls_server_accept_start(a->sc, cfd);
  if (!t) {
    close(cfd);
    return NULL;
  }
  if (0 == server_handshake(t, cfd)) server_serve_request(t, cfd, a->response);
  tls_close(t);
  return NULL;
}

static http_async_state_t drive(http_async_t *a, net_err_reason_t *reason) {
  http_async_state_t st = HTTP_ASYNC_PENDING;

  for (int i = 0; i < 300 && st == HTTP_ASYNC_PENDING; i++) {
    struct pollfd pfd;

    pfd.fd = http_async_poll_fd(a);
    pfd.events = http_async_poll_events(a);
    pfd.revents = 0;
    poll(&pfd, 1, 100);
    st = http_async_step(a, reason);
  }
  return st;
}

static http_async_t *start_https(unsigned port, int insecure) {
  char uri[64];
  http_url_t url;

  snprintf(uri, sizeof uri, "https://127.0.0.1:%u/stream", port);
  ck_assert_int_eq(http_url_parse(uri, &url), 0);
  return http_async_start(&url, "test-agent", insecure, NULL, NULL);
}

START_TEST(https_async_connects_and_reads_body) {
  unsigned port;
  int listen_fd = make_listener(&port);
  tls_server_ctx_t *sc = tls_server_ctx_new(g_cert, g_key);
  tls_server_arg_t sarg = {listen_fd, sc, "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nConnection: close\r\nContent-Length: 9\r\n\r\nBODYBYTES"};
  pthread_t th;
  http_async_t *a;
  http_t *h;
  char buf[32];
  size_t got = 0;
  net_err_reason_t reason = NET_ERR_COUNT;

  ck_assert_ptr_nonnull(sc);
  ck_assert_int_eq(pthread_create(&th, NULL, tls_serve_once, &sarg), 0);
  a = start_https(port, 1);
  ck_assert_ptr_nonnull(a);
  ck_assert_int_eq(drive(a, &reason), HTTP_ASYNC_DONE);
  h = http_async_take(a);
  ck_assert_int_eq(http_status(h), 200);
  for (int i = 0; i < 200 && got < 9; i++) {
    ssize_t n = http_read(h, buf + got, sizeof buf - 1 - got, NULL);

    ck_assert_int_ge(n, 0);
    got += (size_t)n;
    if (n == 0) usleep(5000);
  }
  buf[got] = '\0';
  ck_assert_str_eq(buf, "BODYBYTES");
  http_close(h);
  pthread_join(th, NULL);
  tls_server_ctx_free(sc);
  close(listen_fd);
}
END_TEST

START_TEST(https_async_untrusted_certificate_reports_tls_error) {
  unsigned port;
  int listen_fd = make_listener(&port);
  tls_server_ctx_t *sc = tls_server_ctx_new(g_cert, g_key);
  tls_server_arg_t sarg = {listen_fd, sc, "HTTP/1.1 200 OK\r\n\r\n"};
  pthread_t th;
  http_async_t *a;
  net_err_reason_t reason = NET_ERR_COUNT;

  ck_assert_ptr_nonnull(sc);
  ck_assert_int_eq(pthread_create(&th, NULL, tls_serve_once, &sarg), 0);
  a = start_https(port, 0);
  ck_assert_ptr_nonnull(a);
  ck_assert_int_eq(drive(a, &reason), HTTP_ASYNC_ERROR);
  ck_assert_int_eq(reason, NET_ERR_TLS);
  http_async_free(a);
  pthread_join(th, NULL);
  tls_server_ctx_free(sc);
  close(listen_fd);
}
END_TEST

static Suite *httpclient_async_tls_suite(void) {
  Suite *s = suite_create("httpclient_async_tls");
  TCase *tc = tcase_create("tls");

  tcase_add_checked_fixture(tc, setup, teardown);
  tcase_add_test(tc, https_async_connects_and_reads_body);
  tcase_add_test(tc, https_async_untrusted_certificate_reports_tls_error);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(httpclient_async_tls_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
