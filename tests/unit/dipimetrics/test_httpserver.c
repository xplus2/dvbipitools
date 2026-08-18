/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "dipimetrics/httpserver.h"
#include "dipimetrics/store.h"

static double mono(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

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

/* drives hs via real poll(), same shape as main.c's loop, until cfd's peer (server)
   has closed: whatever it sent is then fully queued for a non-blocking recv_all() */
static void drive_until_closed(http_server_t *hs, store_t *st, int cfd) {
  double deadline = mono() + 2.0;
  while (mono() < deadline) {
    struct pollfd pfds[1 + HTTP_MAX_CONNS];
    struct pollfd cpfd;
    int n = 0;

    http_server_poll_fds(hs, pfds, (int)(sizeof pfds / sizeof *pfds), &n);
    poll(pfds, (nfds_t)n, 20);
    http_server_service(hs, pfds, n, st, mono(), 0);

    cpfd.fd = cfd;
    cpfd.events = POLLIN;
    cpfd.revents = 0;
    poll(&cpfd, 1, 0);
    if (cpfd.revents & (POLLIN | POLLHUP)) /* POLLHUP alone doesn't fire while unread data remains */
      return;
  }
  ck_abort_msg("server never closed the connection");
}

START_TEST(get_metrics_returns_200_and_openmetrics_body) {
  store_t st;
  int lfd, cfd;
  http_server_t *hs;
  char buf[8192];

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  ck_assert_int_ge(lfd, 0);
  hs = http_server_new(lfd);
  ck_assert_ptr_nonnull(hs);

  cfd = connect_to(lfd);
  ck_assert_int_eq((int)send(cfd, "GET /metrics HTTP/1.1\r\nHost: x\r\n\r\n", 35, 0), 35);

  drive_until_closed(hs, &st, cfd);
  recv_all(cfd, buf, sizeof buf);
  ck_assert(strstr(buf, "HTTP/1.1 200 OK") == buf);
  ck_assert(strstr(buf, "Content-Type: application/openmetrics-text") != NULL);
  ck_assert(strstr(buf, "# EOF") != NULL);

  close(cfd);
  http_server_free(hs);
  close(lfd);
}
END_TEST

START_TEST(unknown_path_returns_404) {
  store_t st;
  int lfd, cfd;
  http_server_t *hs;
  char buf[8192];
  const char req[] = "GET /nope HTTP/1.1\r\nHost: x\r\n\r\n";

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  hs = http_server_new(lfd);
  cfd = connect_to(lfd);
  ck_assert_int_eq((int)send(cfd, req, sizeof req - 1, 0), (int)sizeof req - 1);

  drive_until_closed(hs, &st, cfd);
  recv_all(cfd, buf, sizeof buf);
  ck_assert(strstr(buf, "HTTP/1.1 404 Not Found") == buf);

  close(cfd);
  http_server_free(hs);
  close(lfd);
}
END_TEST

START_TEST(post_to_metrics_also_returns_404) {
  store_t st;
  int lfd, cfd;
  http_server_t *hs;
  char buf[8192];
  const char req[] = "POST /metrics HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n";

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  hs = http_server_new(lfd);
  cfd = connect_to(lfd);
  ck_assert_int_eq((int)send(cfd, req, sizeof req - 1, 0), (int)sizeof req - 1);

  drive_until_closed(hs, &st, cfd);
  recv_all(cfd, buf, sizeof buf);
  ck_assert(strstr(buf, "HTTP/1.1 404 Not Found") == buf);

  close(cfd);
  http_server_free(hs);
  close(lfd);
}
END_TEST

START_TEST(query_string_is_stripped_before_matching) {
  store_t st;
  int lfd, cfd;
  http_server_t *hs;
  char buf[8192];
  const char req[] = "GET /metrics?foo=bar HTTP/1.1\r\nHost: x\r\n\r\n";

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  hs = http_server_new(lfd);
  cfd = connect_to(lfd);
  ck_assert_int_eq((int)send(cfd, req, sizeof req - 1, 0), (int)sizeof req - 1);

  drive_until_closed(hs, &st, cfd);
  recv_all(cfd, buf, sizeof buf);
  ck_assert(strstr(buf, "HTTP/1.1 200 OK") == buf);

  close(cfd);
  http_server_free(hs);
  close(lfd);
}
END_TEST

START_TEST(sequential_scrapes_each_get_a_correct_independent_response) {
  store_t st;
  int lfd, c1, c2;
  http_server_t *hs;
  char buf1[8192], buf2[8192];

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  hs = http_server_new(lfd);

  c1 = connect_to(lfd);
  ck_assert_int_gt((int)send(c1, "GET /metrics HTTP/1.1\r\n\r\n", 26, 0), 0);
  drive_until_closed(hs, &st, c1);
  recv_all(c1, buf1, sizeof buf1);

  c2 = connect_to(lfd);
  ck_assert_int_gt((int)send(c2, "GET /nope HTTP/1.1\r\n\r\n", 23, 0), 0);
  drive_until_closed(hs, &st, c2);
  recv_all(c2, buf2, sizeof buf2);

  ck_assert(strstr(buf1, "200 OK") != NULL);
  ck_assert(strstr(buf2, "404 Not Found") != NULL);

  close(c1);
  close(c2);
  http_server_free(hs);
  close(lfd);
}
END_TEST

START_TEST(request_counters_reflect_status_and_include_current_request) {
  store_t st;
  int lfd, c1, c2, c3;
  http_server_t *hs;
  char buf1[8192], buf2[8192], buf3[8192];

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  hs = http_server_new(lfd);

  c1 = connect_to(lfd);
  ck_assert_int_gt((int)send(c1, "GET /metrics HTTP/1.1\r\n\r\n", 26, 0), 0);
  drive_until_closed(hs, &st, c1);
  recv_all(c1, buf1, sizeof buf1);
  ck_assert(strstr(buf1, "dvbipi_metrics_http_requests_total{status=\"200\"} 1") != NULL);
  ck_assert(strstr(buf1, "dvbipi_metrics_http_requests_total{status=\"404\"} 0") != NULL);

  c2 = connect_to(lfd);
  ck_assert_int_gt((int)send(c2, "GET /nope HTTP/1.1\r\n\r\n", 23, 0), 0);
  drive_until_closed(hs, &st, c2);
  recv_all(c2, buf2, sizeof buf2);
  ck_assert_uint_eq(st.stats.http_requests_200, 1u);
  ck_assert_uint_eq(st.stats.http_requests_404, 1u);

  c3 = connect_to(lfd);
  ck_assert_int_gt((int)send(c3, "GET /metrics HTTP/1.1\r\n\r\n", 26, 0), 0);
  drive_until_closed(hs, &st, c3);
  recv_all(c3, buf3, sizeof buf3);
  ck_assert(strstr(buf3, "dvbipi_metrics_http_requests_total{status=\"200\"} 2") != NULL);
  ck_assert(strstr(buf3, "dvbipi_metrics_http_requests_total{status=\"404\"} 1") != NULL);

  close(c1);
  close(c2);
  close(c3);
  http_server_free(hs);
  close(lfd);
}
END_TEST

START_TEST(idle_connection_past_deadline_is_reaped) {
  store_t st;
  int lfd, cfd;
  http_server_t *hs;
  char buf[8];

  store_init(&st);
  lfd = http_listen(AF_INET, "127.0.0.1", 0);
  hs = http_server_new(lfd);

  cfd = connect_to(lfd);
  ck_assert_int_gt((int)send(cfd, "GET ", 4, 0), 0); /* never completes request line */

  {
    double deadline = mono() + 8.0; /* HTTP_IDLE_TIMEOUT_S (5s) plus slack */
    int closed = 0;
    while (mono() < deadline) {
      struct pollfd pfds[1 + HTTP_MAX_CONNS];
      struct pollfd cpfd;
      int n = 0;

      http_server_poll_fds(hs, pfds, (int)(sizeof pfds / sizeof *pfds), &n);
      poll(pfds, (nfds_t)n, 200);
      http_server_service(hs, pfds, n, &st, mono(), 0);

      cpfd.fd = cfd;
      cpfd.events = POLLIN;
      cpfd.revents = 0;
      poll(&cpfd, 1, 0);
      if (cpfd.revents & (POLLIN | POLLHUP)) {
        closed = 1;
        break;
      }
    }
    ck_assert_msg(closed, "idle connection was never reaped");
  }
  ck_assert_int_eq((int)recv(cfd, buf, sizeof buf, 0), 0); /* server closed, nothing sent */

  close(cfd);
  http_server_free(hs);
  close(lfd);
}
END_TEST

static Suite *httpserver_suite(void) {
  Suite *s = suite_create("dipimetrics_httpserver");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 15);
  tcase_add_test(tc, get_metrics_returns_200_and_openmetrics_body);
  tcase_add_test(tc, unknown_path_returns_404);
  tcase_add_test(tc, post_to_metrics_also_returns_404);
  tcase_add_test(tc, query_string_is_stripped_before_matching);
  tcase_add_test(tc, sequential_scrapes_each_get_a_correct_independent_response);
  tcase_add_test(tc, request_counters_reflect_status_and_include_current_request);
  tcase_add_test(tc, idle_connection_past_deadline_is_reaped);
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
