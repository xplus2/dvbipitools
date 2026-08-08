/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dipimetrics/httpserver.h"
#include "dipimetrics/store.h"

static int connect_to(int listen_fd) {
  struct sockaddr_in sa;
  socklen_t sl = sizeof sa;
  int cfd;

  ck_assert_int_eq(getsockname(listen_fd, (struct sockaddr *)&sa, &sl), 0);
  cfd = socket(AF_INET, SOCK_STREAM, 0);
  ck_assert_int_ge(cfd, 0);
  ck_assert_int_eq(connect(cfd, (struct sockaddr *)&sa, sl), 0);
  return cfd;
}

static size_t recv_all(int fd, char *buf, size_t cap) {
  size_t total = 0;
  for (;;) {
    ssize_t n = recv(fd, buf + total, cap - 1 - total, 0);
    if (n <= 0)
      break;
    total += (size_t)n;
    if (total >= cap - 1)
      break;
  }
  buf[total] = '\0';
  return total;
}

START_TEST(get_metrics_returns_200_and_openmetrics_body) {
  store_t st;
  int lfd, cfd;
  char buf[8192];

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  ck_assert_int_ge(lfd, 0);

  cfd = connect_to(lfd);
  ck_assert_int_eq((int)send(cfd, "GET /metrics HTTP/1.1\r\nHost: x\r\n\r\n", 35, 0), 35);

  http_accept_and_serve(lfd, &st, 10.0, 0);

  recv_all(cfd, buf, sizeof buf);
  ck_assert(strstr(buf, "HTTP/1.1 200 OK") == buf);
  ck_assert(strstr(buf, "Content-Type: application/openmetrics-text") != NULL);
  ck_assert(strstr(buf, "# EOF") != NULL);

  close(cfd);
  close(lfd);
}
END_TEST

START_TEST(unknown_path_returns_404) {
  store_t st;
  int lfd, cfd;
  char buf[8192];
  const char req[] = "GET /nope HTTP/1.1\r\nHost: x\r\n\r\n";

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  cfd = connect_to(lfd);
  ck_assert_int_eq((int)send(cfd, req, sizeof req - 1, 0), (int)sizeof req - 1);

  http_accept_and_serve(lfd, &st, 10.0, 0);

  recv_all(cfd, buf, sizeof buf);
  ck_assert(strstr(buf, "HTTP/1.1 404 Not Found") == buf);

  close(cfd);
  close(lfd);
}
END_TEST

START_TEST(post_to_metrics_also_returns_404) {
  store_t st;
  int lfd, cfd;
  char buf[8192];
  const char req[] = "POST /metrics HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n";

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  cfd = connect_to(lfd);
  ck_assert_int_eq((int)send(cfd, req, sizeof req - 1, 0), (int)sizeof req - 1);

  http_accept_and_serve(lfd, &st, 10.0, 0);

  recv_all(cfd, buf, sizeof buf);
  ck_assert(strstr(buf, "HTTP/1.1 404 Not Found") == buf);

  close(cfd);
  close(lfd);
}
END_TEST

START_TEST(query_string_is_stripped_before_matching) {
  store_t st;
  int lfd, cfd;
  char buf[8192];
  const char req[] = "GET /metrics?foo=bar HTTP/1.1\r\nHost: x\r\n\r\n";

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  cfd = connect_to(lfd);
  ck_assert_int_eq((int)send(cfd, req, sizeof req - 1, 0), (int)sizeof req - 1);

  http_accept_and_serve(lfd, &st, 10.0, 0);

  recv_all(cfd, buf, sizeof buf);
  ck_assert(strstr(buf, "HTTP/1.1 200 OK") == buf);

  close(cfd);
  close(lfd);
}
END_TEST

START_TEST(sequential_scrapes_each_get_a_correct_independent_response) {
  store_t st;
  int lfd, c1, c2;
  char buf1[8192], buf2[8192];

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);

  c1 = connect_to(lfd);
  ck_assert_int_gt((int)send(c1, "GET /metrics HTTP/1.1\r\n\r\n", 26, 0), 0);
  http_accept_and_serve(lfd, &st, 10.0, 0);
  recv_all(c1, buf1, sizeof buf1);

  c2 = connect_to(lfd);
  ck_assert_int_gt((int)send(c2, "GET /nope HTTP/1.1\r\n\r\n", 23, 0), 0);
  http_accept_and_serve(lfd, &st, 10.0, 0);
  recv_all(c2, buf2, sizeof buf2);

  ck_assert(strstr(buf1, "200 OK") != NULL);
  ck_assert(strstr(buf2, "404 Not Found") != NULL);

  close(c1);
  close(c2);
  close(lfd);
}
END_TEST

static Suite *httpserver_suite(void) {
  Suite *s = suite_create("dipimetrics_httpserver");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, get_metrics_returns_200_and_openmetrics_body);
  tcase_add_test(tc, unknown_path_returns_404);
  tcase_add_test(tc, post_to_metrics_also_returns_404);
  tcase_add_test(tc, query_string_is_stripped_before_matching);
  tcase_add_test(tc, sequential_scrapes_each_get_a_correct_independent_response);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(httpserver_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
