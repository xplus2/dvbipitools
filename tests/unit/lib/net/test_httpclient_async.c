/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "lib/net/httpclient/httpclient.h"

typedef struct {
  int listen_fd;
  const char *response;
  size_t response_len;
} server_arg_t;

static void *serve_once(void *arg) {
  server_arg_t *a = arg;
  int cfd = accept(a->listen_fd, NULL, NULL);
  struct timeval tv = {2, 0};
  char buf[4096];
  size_t got = 0;

  if (cfd < 0)
    return NULL;
  setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  for (;;) {
    ssize_t n = recv(cfd, buf + got, sizeof buf - got, 0);
    if (n <= 0)
      break;
    got += (size_t)n;
    if (got >= 4 && memcmp(buf + got - 4, "\r\n\r\n", 4) == 0)
      break;
  }
  send(cfd, a->response, a->response_len, 0);
  close(cfd);
  return NULL;
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

static http_async_state_t drive(http_async_t *a, int max_iters) {
  http_async_state_t st = HTTP_ASYNC_PENDING;

  for (int i = 0; i < max_iters && st == HTTP_ASYNC_PENDING; i++) {
    struct pollfd pfd;
    pfd.fd = http_async_poll_fd(a);
    pfd.events = http_async_poll_events(a);
    pfd.revents = 0;
    poll(&pfd, 1, 100);
    st = http_async_step(a, NULL);
  }
  return st;
}

static size_t drain_body(http_t *h, char *buf, size_t cap, size_t want) {
  size_t got = 0;
  int tries = 0;

  while (got < want && tries < 100) {
    ssize_t n = http_read(h, buf + got, cap - got, NULL);
    if (n > 0)
      got += (size_t)n;
    else if (n < 0)
      break;
    else
      usleep(5000);
    tries++;
  }
  return got;
}

START_TEST(http_async_completes_against_local_server) {
  unsigned port;
  int listen_fd = make_listener(&port);
  pthread_t th;
  server_arg_t sarg;
  const char *resp = "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nConnection: close\r\n\r\nBODYBYTES";
  char uri[64];
  http_url_t url;
  http_async_t *a;
  http_t *h;
  char buf[64];
  size_t got;

  sarg.listen_fd = listen_fd;
  sarg.response = resp;
  sarg.response_len = strlen(resp);
  ck_assert_int_eq(pthread_create(&th, NULL, serve_once, &sarg), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/stream", port);
  ck_assert_int_eq(http_url_parse(uri, &url), 0);
  a = http_async_start(&url, "test-agent", 0, NULL, NULL);
  ck_assert_ptr_nonnull(a);
  ck_assert_int_eq(drive(a, 200), HTTP_ASYNC_DONE);

  h = http_async_take(a);
  ck_assert_int_eq(http_status(h), 200);
  got = drain_body(h, buf, sizeof buf - 1, strlen("BODYBYTES"));
  buf[got] = '\0';
  ck_assert_str_eq(buf, "BODYBYTES");

  http_close(h);
  pthread_join(th, NULL);
  close(listen_fd);
}
END_TEST

START_TEST(http_async_follows_redirect) {
  unsigned port_a, port_b;
  int listen_a = make_listener(&port_a);
  int listen_b = make_listener(&port_b);
  pthread_t th_a, th_b;
  server_arg_t sarg_a, sarg_b;
  char resp_a[256];
  const char *resp_b = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nOK";
  char uri[64];
  http_url_t url;
  http_async_t *a;
  http_t *h;
  char buf[16];
  size_t got;

  snprintf(resp_a, sizeof resp_a, "HTTP/1.1 302 Found\r\nLocation: http://127.0.0.1:%u/next\r\nConnection: close\r\n\r\n", port_b);
  sarg_a.listen_fd = listen_a;
  sarg_a.response = resp_a;
  sarg_a.response_len = strlen(resp_a);
  sarg_b.listen_fd = listen_b;
  sarg_b.response = resp_b;
  sarg_b.response_len = strlen(resp_b);
  ck_assert_int_eq(pthread_create(&th_a, NULL, serve_once, &sarg_a), 0);
  ck_assert_int_eq(pthread_create(&th_b, NULL, serve_once, &sarg_b), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/start", port_a);
  ck_assert_int_eq(http_url_parse(uri, &url), 0);
  a = http_async_start(&url, "test-agent", 0, NULL, NULL);
  ck_assert_ptr_nonnull(a);
  ck_assert_int_eq(drive(a, 200), HTTP_ASYNC_DONE);

  h = http_async_take(a);
  ck_assert_int_eq(http_status(h), 200);
  ck_assert_uint_eq(http_final_url(h)->port, port_b);
  got = drain_body(h, buf, sizeof buf - 1, strlen("OK"));
  buf[got] = '\0';
  ck_assert_str_eq(buf, "OK");

  http_close(h);
  pthread_join(th_a, NULL);
  pthread_join(th_b, NULL);
  close(listen_a);
  close(listen_b);
}
END_TEST

START_TEST(http_async_reports_error_on_refused_connection) {
  http_url_t url;
  http_async_t *a;

  ck_assert_int_eq(http_url_parse("http://127.0.0.1:1/nothing", &url), 0); /* port 1: nothing listens here */
  a = http_async_start(&url, "test-agent", 0, NULL, NULL);
  ck_assert_ptr_nonnull(a);
  ck_assert_int_eq(drive(a, 200), HTTP_ASYNC_ERROR);
  http_async_free(a);
}
END_TEST

START_TEST(http_async_decodes_chunked_body) {
  unsigned port;
  int listen_fd = make_listener(&port);
  pthread_t th;
  server_arg_t sarg;
  const char *resp = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
                      "5\r\nHELLO\r\n"
                      "1;ext=ignored\r\n,\r\n"
                      "6\r\n WORLD\r\n"
                      "0\r\n\r\n";
  char uri[64];
  http_url_t url;
  http_async_t *a;
  http_t *h;
  char buf[64];
  size_t got;

  sarg.listen_fd = listen_fd;
  sarg.response = resp;
  sarg.response_len = strlen(resp);
  ck_assert_int_eq(pthread_create(&th, NULL, serve_once, &sarg), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/stream", port);
  ck_assert_int_eq(http_url_parse(uri, &url), 0);
  a = http_async_start(&url, "test-agent", 0, NULL, NULL);
  ck_assert_ptr_nonnull(a);
  ck_assert_int_eq(drive(a, 200), HTTP_ASYNC_DONE);

  h = http_async_take(a);
  ck_assert_int_eq(http_status(h), 200);
  got = drain_body(h, buf, sizeof buf - 1, strlen("HELLO, WORLD"));
  buf[got] = '\0';
  ck_assert_str_eq(buf, "HELLO, WORLD");

  http_close(h);
  pthread_join(th, NULL);
  close(listen_fd);
}
END_TEST

START_TEST(http_get_decodes_chunked_body) {
  unsigned port;
  int listen_fd = make_listener(&port);
  pthread_t th;
  server_arg_t sarg;
  const char *resp = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
                      "4\r\nBODY\r\n"
                      "5\r\nBYTES\r\n"
                      "0\r\n\r\n";
  char uri[64];
  http_url_t url;
  http_t *h;
  char buf[64];
  size_t got;

  sarg.listen_fd = listen_fd;
  sarg.response = resp;
  sarg.response_len = strlen(resp);
  ck_assert_int_eq(pthread_create(&th, NULL, serve_once, &sarg), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/stream", port);
  ck_assert_int_eq(http_url_parse(uri, &url), 0);
  h = http_get(&url, "test-agent", 0, NULL, NULL);
  ck_assert_ptr_nonnull(h);
  ck_assert_int_eq(http_status(h), 200);

  got = drain_body(h, buf, sizeof buf - 1, strlen("BODYBYTES"));
  buf[got] = '\0';
  ck_assert_str_eq(buf, "BODYBYTES");

  http_close(h);
  pthread_join(th, NULL);
  close(listen_fd);
}
END_TEST

START_TEST(http_get_rejects_unsupported_transfer_encoding) {
  unsigned port;
  int listen_fd = make_listener(&port);
  pthread_t th;
  server_arg_t sarg;
  const char *resp = "HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\nConnection: close\r\n\r\ngarbage";
  char uri[64];
  http_url_t url;
  http_t *h;

  sarg.listen_fd = listen_fd;
  sarg.response = resp;
  sarg.response_len = strlen(resp);
  ck_assert_int_eq(pthread_create(&th, NULL, serve_once, &sarg), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/stream", port);
  ck_assert_int_eq(http_url_parse(uri, &url), 0);
  h = http_get(&url, "test-agent", 0, NULL, NULL);
  ck_assert_ptr_null(h);

  pthread_join(th, NULL);
  close(listen_fd);
}
END_TEST

static Suite *httpclient_async_suite(void) {
  Suite *s = suite_create("httpclient_async");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, http_async_completes_against_local_server);
  tcase_add_test(tc, http_async_follows_redirect);
  tcase_add_test(tc, http_async_reports_error_on_refused_connection);
  tcase_add_test(tc, http_async_decodes_chunked_body);
  tcase_add_test(tc, http_get_decodes_chunked_body);
  tcase_add_test(tc, http_get_rejects_unsupported_transfer_encoding);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(httpclient_async_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
