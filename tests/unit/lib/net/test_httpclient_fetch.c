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
  char *capture;
  size_t capture_cap;
  size_t capture_len;
  const char *responses[2];
  size_t response_lens[2];
} server_arg_t;

static void *serve_once(void *arg) {
  server_arg_t *a = arg;
  int cfd = accept(a->listen_fd, NULL, NULL);
  struct timeval tv = {2, 0};
  char buf[4096];
  size_t got = 0;

  if (cfd < 0) return NULL;
  setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  for (;;) {
    ssize_t n = recv(cfd, buf + got, sizeof buf - got, 0);
    if (n <= 0) break;
    got += (size_t)n;
    if (got >= 4 && memcmp(buf + got - 4, "\r\n\r\n", 4) == 0) break;
  }
  if (a->capture) {
    size_t n = got < a->capture_cap - 1 ? got : a->capture_cap - 1;
    memcpy(a->capture, buf, n);
    a->capture[n] = '\0';
    a->capture_len = n;
  }
  send(cfd, a->response, a->response_len, 0);
  close(cfd);
  return NULL;
}

static void *serve_two_on_one_conn(void *arg) {
  server_arg_t *a = arg;
  int cfd = accept(a->listen_fd, NULL, NULL);
  struct timeval tv = {2, 0};
  close(a->listen_fd);
  if (cfd < 0) return NULL;
  setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  for (int resp_i = 0; resp_i < 2; resp_i++) {
    char buf[4096];
    size_t got = 0;
    for (;;) {
      ssize_t n = recv(cfd, buf + got, sizeof buf - got, 0);
      if (n <= 0) break;
      got += (size_t)n;
      if (got >= 4 && memcmp(buf + got - 4, "\r\n\r\n", 4) == 0) break;
    }
    send(cfd, a->responses[resp_i], a->response_lens[resp_i], 0);
  }
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

static http_fetch_state_t drive(http_fetch_t *f, int max_iters) {
  http_fetch_state_t st = HTTP_FETCH_PENDING;

  for (int i = 0; i < max_iters && st == HTTP_FETCH_PENDING; i++) {
    struct pollfd pfd;
    pfd.fd = http_fetch_poll_fd(f);
    pfd.events = http_fetch_poll_events(f);
    pfd.revents = 0;
    poll(&pfd, 1, 100);
    st = http_fetch_step(f, NULL);
  }
  return st;
}

START_TEST(http_fetch_completes_into_buffer) {
  unsigned port;
  int listen_fd = make_listener(&port);
  pthread_t th;
  server_arg_t sarg = {0};
  const char *resp = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nBODYBYTES";
  char uri[64];
  http_url_t url;
  http_fetch_t *f;
  unsigned char buf[64];
  size_t len;
  int status, truncated;

  sarg.listen_fd = listen_fd;
  sarg.response = resp;
  sarg.response_len = strlen(resp);
  ck_assert_int_eq(pthread_create(&th, NULL, serve_once, &sarg), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/manifest.m3u8", port);
  ck_assert_int_eq(http_url_parse(uri, &url), 0);
  f = http_fetch_start(&url, "test-agent", 0, NULL, NULL, buf, sizeof buf, NULL, NULL);
  ck_assert_ptr_nonnull(f);
  ck_assert_int_eq(drive(f, 200), HTTP_FETCH_DONE);

  http_fetch_take(f, &len, &status, NULL, 0, &truncated, NULL);
  ck_assert_int_eq(status, 200);
  ck_assert_int_eq(truncated, 0);
  ck_assert_uint_eq(len, strlen("BODYBYTES"));
  ck_assert_int_eq(memcmp(buf, "BODYBYTES", len), 0);

  pthread_join(th, NULL);
  close(listen_fd);
}
END_TEST

START_TEST(http_fetch_reports_truncation_at_cap) {
  unsigned port;
  int listen_fd = make_listener(&port);
  pthread_t th;
  server_arg_t sarg = {0};
  const char *resp = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nMORE-THAN-EIGHT-BYTES";
  char uri[64];
  http_url_t url;
  http_fetch_t *f;
  unsigned char buf[8];
  size_t len;
  int truncated;

  sarg.listen_fd = listen_fd;
  sarg.response = resp;
  sarg.response_len = strlen(resp);
  ck_assert_int_eq(pthread_create(&th, NULL, serve_once, &sarg), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/seg.ts", port);
  ck_assert_int_eq(http_url_parse(uri, &url), 0);
  f = http_fetch_start(&url, "test-agent", 0, NULL, NULL, buf, sizeof buf, NULL, NULL);
  ck_assert_ptr_nonnull(f);
  ck_assert_int_eq(drive(f, 200), HTTP_FETCH_DONE);

  http_fetch_take(f, &len, NULL, NULL, 0, &truncated, NULL);
  ck_assert_uint_eq(len, sizeof buf);
  ck_assert_int_eq(truncated, 1);

  pthread_join(th, NULL);
  close(listen_fd);
}
END_TEST

START_TEST(http_fetch_sends_etag_and_handles_304) {
  unsigned port;
  int listen_fd = make_listener(&port);
  pthread_t th;
  char capture[1024];
  server_arg_t sarg = {0};
  const char *resp = "HTTP/1.1 304 Not Modified\r\nConnection: close\r\n\r\n";
  char uri[64];
  http_url_t url;
  http_fetch_t *f;
  unsigned char buf[64];
  size_t len;
  int status;

  sarg.listen_fd = listen_fd;
  sarg.response = resp;
  sarg.response_len = strlen(resp);
  sarg.capture = capture;
  sarg.capture_cap = sizeof capture;
  ck_assert_int_eq(pthread_create(&th, NULL, serve_once, &sarg), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/live.m3u8", port);
  ck_assert_int_eq(http_url_parse(uri, &url), 0);
  f = http_fetch_start(&url, "test-agent", 0, NULL, "\"abc123\"", buf, sizeof buf, NULL, NULL);
  ck_assert_ptr_nonnull(f);
  ck_assert_int_eq(drive(f, 200), HTTP_FETCH_DONE);

  http_fetch_take(f, &len, &status, NULL, 0, NULL, NULL);
  ck_assert_int_eq(status, 304);
  ck_assert_uint_eq(len, 0);

  pthread_join(th, NULL);
  ck_assert_ptr_nonnull(strstr(capture, "If-None-Match: \"abc123\""));
  close(listen_fd);
}
END_TEST

START_TEST(http_fetch_captures_response_etag) {
  unsigned port;
  int listen_fd = make_listener(&port);
  pthread_t th;
  server_arg_t sarg = {0};
  const char *resp = "HTTP/1.1 200 OK\r\nETag: \"xyz\"\r\nConnection: close\r\n\r\nBODY";
  char uri[64];
  http_url_t url;
  http_fetch_t *f;
  unsigned char buf[64];
  char etag[32];

  sarg.listen_fd = listen_fd;
  sarg.response = resp;
  sarg.response_len = strlen(resp);
  ck_assert_int_eq(pthread_create(&th, NULL, serve_once, &sarg), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/live.m3u8", port);
  ck_assert_int_eq(http_url_parse(uri, &url), 0);
  f = http_fetch_start(&url, "test-agent", 0, NULL, NULL, buf, sizeof buf, NULL, NULL);
  ck_assert_ptr_nonnull(f);
  ck_assert_int_eq(drive(f, 200), HTTP_FETCH_DONE);

  http_fetch_take(f, NULL, NULL, etag, sizeof etag, NULL, NULL);
  ck_assert_str_eq(etag, "\"xyz\"");

  pthread_join(th, NULL);
  close(listen_fd);
}
END_TEST

START_TEST(http_fetch_reuses_connection_across_two_requests) {
  unsigned port;
  int listen_fd = make_listener(&port);
  pthread_t th;
  server_arg_t sarg = {0};
  const char *resp1 = "HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nFIRST";
  const char *resp2 = "HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\nSECOND";
  http_url_t url;
  http_fetch_t *f;
  http_t *reuse = NULL;
  unsigned char buf1[64], buf2[64];
  size_t len1, len2;
  char uri[64];

  sarg.listen_fd = listen_fd;
  sarg.responses[0] = resp1;
  sarg.response_lens[0] = strlen(resp1);
  sarg.responses[1] = resp2;
  sarg.response_lens[1] = strlen(resp2);
  ck_assert_int_eq(pthread_create(&th, NULL, serve_two_on_one_conn, &sarg), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/first", port);
  ck_assert_int_eq(http_url_parse(uri, &url), 0);
  f = http_fetch_start(&url, "test-agent", 0, NULL, NULL, buf1, sizeof buf1, NULL, NULL);
  ck_assert_ptr_nonnull(f);
  ck_assert_int_eq(drive(f, 200), HTTP_FETCH_DONE);
  http_fetch_take(f, &len1, NULL, NULL, 0, NULL, &reuse);
  ck_assert_ptr_nonnull(reuse);
  ck_assert_int_eq(memcmp(buf1, "FIRST", len1), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/second", port);
  ck_assert_int_eq(http_url_parse(uri, &url), 0);
  f = http_fetch_start(&url, "test-agent", 0, NULL, NULL, buf2, sizeof buf2, reuse, NULL);
  ck_assert_ptr_nonnull(f);
  ck_assert_int_eq(drive(f, 200), HTTP_FETCH_DONE);
  http_fetch_take(f, &len2, NULL, NULL, 0, NULL, NULL);
  ck_assert_int_eq(memcmp(buf2, "SECOND", len2), 0);

  pthread_join(th, NULL);
}
END_TEST

START_TEST(http_fetch_falls_back_when_reuse_host_mismatches) {
  unsigned port_a, port_b;
  int listen_a = make_listener(&port_a);
  int listen_b = make_listener(&port_b);
  pthread_t th_a, th_b;
  server_arg_t sarg_a = {0}, sarg_b = {0};
  const char *resp_a = "HTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nA";
  const char *resp_b = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nB";
  http_url_t url_a, url_b;
  http_fetch_t *f;
  http_t *reuse = NULL;
  unsigned char buf_a[16], buf_b[16];
  size_t len_a, len_b;
  char uri[64];

  sarg_a.listen_fd = listen_a;
  sarg_a.response = resp_a;
  sarg_a.response_len = strlen(resp_a);
  ck_assert_int_eq(pthread_create(&th_a, NULL, serve_once, &sarg_a), 0);
  sarg_b.listen_fd = listen_b;
  sarg_b.response = resp_b;
  sarg_b.response_len = strlen(resp_b);
  ck_assert_int_eq(pthread_create(&th_b, NULL, serve_once, &sarg_b), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/a", port_a);
  ck_assert_int_eq(http_url_parse(uri, &url_a), 0);
  f = http_fetch_start(&url_a, "test-agent", 0, NULL, NULL, buf_a, sizeof buf_a, NULL, NULL);
  ck_assert_ptr_nonnull(f);
  ck_assert_int_eq(drive(f, 200), HTTP_FETCH_DONE);
  http_fetch_take(f, &len_a, NULL, NULL, 0, NULL, &reuse);
  ck_assert_ptr_nonnull(reuse);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/b", port_b);
  ck_assert_int_eq(http_url_parse(uri, &url_b), 0);
  f = http_fetch_start(&url_b, "test-agent", 0, NULL, NULL, buf_b, sizeof buf_b, reuse, NULL);
  ck_assert_ptr_nonnull(f);
  ck_assert_int_eq(drive(f, 200), HTTP_FETCH_DONE);
  http_fetch_take(f, &len_b, NULL, NULL, 0, NULL, NULL);
  ck_assert_int_eq(memcmp(buf_b, "B", len_b), 0);

  pthread_join(th_a, NULL);
  pthread_join(th_b, NULL);
}
END_TEST

START_TEST(http_fetch_reports_error_on_refused_connection) {
  http_url_t url;
  http_fetch_t *f;
  unsigned char buf[16];

  ck_assert_int_eq(http_url_parse("http://127.0.0.1:1/nothing", &url), 0);
  f = http_fetch_start(&url, "test-agent", 0, NULL, NULL, buf, sizeof buf, NULL, NULL);
  ck_assert_ptr_nonnull(f);
  ck_assert_int_eq(drive(f, 200), HTTP_FETCH_ERROR);
  http_fetch_free(f);
}
END_TEST

static Suite *httpclient_fetch_suite(void) {
  Suite *s = suite_create("httpclient_fetch");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, http_fetch_completes_into_buffer);
  tcase_add_test(tc, http_fetch_reports_truncation_at_cap);
  tcase_add_test(tc, http_fetch_sends_etag_and_handles_304);
  tcase_add_test(tc, http_fetch_captures_response_etag);
  tcase_add_test(tc, http_fetch_reports_error_on_refused_connection);
  tcase_add_test(tc, http_fetch_reuses_connection_across_two_requests);
  tcase_add_test(tc, http_fetch_falls_back_when_reuse_host_mismatches);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(httpclient_fetch_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
