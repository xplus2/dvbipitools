/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
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

static http_t *get_response(const char *resp, unsigned *port_out) {
  int listen_fd = make_listener(port_out);
  pthread_t th;
  static server_arg_t sarg;
  char uri[64];
  http_url_t url;
  http_t *h;

  sarg.listen_fd = listen_fd;
  sarg.response = resp;
  sarg.response_len = strlen(resp);
  ck_assert_int_eq(pthread_create(&th, NULL, serve_once, &sarg), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/x", *port_out);
  ck_assert_int_eq(http_url_parse(uri, &url), 0);
  h = http_get(&url, "test-agent", 0, NULL, NULL);
  ck_assert_ptr_nonnull(h);

  pthread_join(th, NULL);
  close(listen_fd);
  return h;
}

START_TEST(http_header_looks_up_case_insensitively) {
  unsigned port;
  http_t *h = get_response("HTTP/1.1 200 OK\r\nETag: \"abc123\"\r\nX-Custom: value\r\nContent-Length: 4\r\nConnection: close\r\n\r\nBODY", &port);

  ck_assert_str_eq(http_header(h, "etag"), "\"abc123\"");
  ck_assert_str_eq(http_header(h, "x-custom"), "value");
  ck_assert_str_eq(http_header(h, "content-length"), "4");
  ck_assert_ptr_null(http_header(h, "missing-header"));

  http_close(h);
}
END_TEST

START_TEST(http_can_reuse_true_for_304_keep_alive) {
  unsigned port;
  http_t *h = get_response("HTTP/1.1 304 Not Modified\r\nConnection: keep-alive\r\n\r\n", &port);

  ck_assert_int_eq(http_status(h), 304);
  ck_assert_int_eq(http_can_reuse(h), 1);

  http_close(h);
}
END_TEST

START_TEST(http_can_reuse_true_for_304_no_connection_header) {
  unsigned port;
  http_t *h = get_response("HTTP/1.1 304 Not Modified\r\n\r\n", &port);

  ck_assert_int_eq(http_can_reuse(h), 1);

  http_close(h);
}
END_TEST

START_TEST(http_can_reuse_false_for_connection_close) {
  unsigned port;
  http_t *h = get_response("HTTP/1.1 304 Not Modified\r\nConnection: close\r\n\r\n", &port);

  ck_assert_int_eq(http_can_reuse(h), 0);

  http_close(h);
}
END_TEST

START_TEST(http_can_reuse_false_for_connection_close_mixed_case) {
  unsigned port;
  http_t *h = get_response("HTTP/1.1 304 Not Modified\r\nConnection: Close\r\n\r\n", &port);

  ck_assert_int_eq(http_can_reuse(h), 0);

  http_close(h);
}
END_TEST

START_TEST(http_can_reuse_false_for_close_token_in_list) {
  unsigned port;
  http_t *h = get_response("HTTP/1.1 304 Not Modified\r\nConnection: Upgrade, Close\r\n\r\n", &port);

  ck_assert_int_eq(http_can_reuse(h), 0);

  http_close(h);
}
END_TEST

static Suite *httpclient_headers_suite(void) {
  Suite *s = suite_create("httpclient_headers");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, http_header_looks_up_case_insensitively);
  tcase_add_test(tc, http_can_reuse_true_for_304_keep_alive);
  tcase_add_test(tc, http_can_reuse_true_for_304_no_connection_header);
  tcase_add_test(tc, http_can_reuse_false_for_connection_close);
  tcase_add_test(tc, http_can_reuse_false_for_connection_close_mixed_case);
  tcase_add_test(tc, http_can_reuse_false_for_close_token_in_list);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(httpclient_headers_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
