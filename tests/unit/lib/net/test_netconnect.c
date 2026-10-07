/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "lib/net/netconnect.h"

static double mono(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

START_TEST(netconnect_tcp_succeeds_against_local_listener) {
  int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;
  int fd;

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(listen_fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(listen_fd, 1), 0);
  ck_assert_int_eq(getsockname(listen_fd, (struct sockaddr *)&addr, &alen), 0);

  fd = netconnect_tcp("127.0.0.1", ntohs(addr.sin_port), 2000, NULL);
  ck_assert_int_ge(fd, 0);

  close(fd);
  close(listen_fd);
}
END_TEST

/* can't force a real hang deterministically; asserts the timeout still bounds the call */
START_TEST(netconnect_tcp_bounded_time_on_unreachable) {
  double start = mono();
  int fd = netconnect_tcp("127.0.0.1", 1, 300, NULL); /* port 1: nothing listens here */
  double elapsed = mono() - start;

  if (fd >= 0)
    close(fd);
  ck_assert_double_le(elapsed, 5.0);
}
END_TEST

START_TEST(dscp_parse_symbolic_names) {
  int tos;
  ck_assert_int_eq(net_dscp_parse("video-high", &tos), 0);
  ck_assert_int_eq(tos, NET_DSCP_VIDEO_HIGH);
  ck_assert_int_eq(net_dscp_parse("video-low", &tos), 0);
  ck_assert_int_eq(tos, NET_DSCP_VIDEO_LOW);
  ck_assert_int_eq(net_dscp_parse("voice", &tos), 0);
  ck_assert_int_eq(tos, NET_DSCP_VOICE_BEARER);
  ck_assert_int_eq(net_dscp_parse("signalling", &tos), 0);
  ck_assert_int_eq(tos, NET_DSCP_SIGNALLING);
  ck_assert_int_eq(net_dscp_parse("best-effort", &tos), 0);
  ck_assert_int_eq(tos, NET_DSCP_BEST_EFFORT);
}
END_TEST

START_TEST(dscp_parse_raw_numeric) {
  int tos;
  ck_assert_int_eq(net_dscp_parse("0", &tos), 0);
  ck_assert_int_eq(tos, 0);
  ck_assert_int_eq(net_dscp_parse("63", &tos), 0);
  ck_assert_int_eq(tos, 63 << 2);
  ck_assert_int_eq(net_dscp_parse("34", &tos), 0);
  ck_assert_int_eq(tos, 34 << 2);
}
END_TEST

START_TEST(dscp_parse_rejects_invalid) {
  int tos;
  ck_assert_int_eq(net_dscp_parse("64", &tos), -1);
  ck_assert_int_eq(net_dscp_parse("-1", &tos), -1);
  ck_assert_int_eq(net_dscp_parse("video-med", &tos), -1);
  ck_assert_int_eq(net_dscp_parse("", &tos), -1);
}
END_TEST

static int free_loopback_tcp_port(void) {
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  int port;

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  port = ntohs(addr.sin_port);
  close(fd);
  return port;
}

static int listen_loopback(unsigned *port_out, int backlog) {
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;
  int fd = socket(AF_INET, SOCK_STREAM, 0);

  ck_assert_int_ge(fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(fd, backlog), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  *port_out = ntohs(addr.sin_port);
  return fd;
}

static int wait_writable(int fd) {
  struct pollfd pfd = {fd, POLLOUT, 0};

  return poll(&pfd, 1, 2000);
}

static int first_candidate_is_ipv6(const char *host, unsigned port, int *count) {
  struct addrinfo hints;
  struct addrinfo *res;
  struct addrinfo *ai;
  char portstr[8];
  int v6;

  snprintf(portstr, sizeof portstr, "%u", port);
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  ck_assert_int_eq(getaddrinfo(host, portstr, &hints, &res), 0);
  *count = 0;
  for (ai = res; ai; ai = ai->ai_next) (*count)++;
  v6 = res->ai_family == AF_INET6;
  freeaddrinfo(res);
  return v6;
}

START_TEST(netconnect_tcp_refused_reports_connect_error) {
  net_err_reason_t reason = NET_ERR_COUNT;

  ck_assert_int_eq(netconnect_tcp("127.0.0.1", (unsigned)free_loopback_tcp_port(), 2000, &reason), -1);
  ck_assert_int_eq(reason, NET_ERR_CONNECT);
}
END_TEST

START_TEST(netconnect_tcp_start_finish_refused_single_candidate) {
  netconnect_pending_t *pending = NULL;
  net_err_reason_t reason = NET_ERR_COUNT;
  int fd = netconnect_tcp_start("127.0.0.1", (unsigned)free_loopback_tcp_port(), &pending, &reason);

  ck_assert_int_ge(fd, 0);
  ck_assert_ptr_nonnull(pending);
  ck_assert_int_eq(wait_writable(fd), 1);
  ck_assert_int_eq(netconnect_tcp_finish(&pending, &fd, &reason), -1);
  ck_assert_ptr_null(pending);
  ck_assert_int_eq(fd, -1);
}
END_TEST

START_TEST(netconnect_tcp_times_out_against_full_backlog) {
  unsigned port;
  int lfd = listen_loopback(&port, 0);
  int fillers[8];
  net_err_reason_t reason = NET_ERR_COUNT;
  double start;
  double elapsed;

  for (int i = 0; i < 8; i++) {
    struct sockaddr_in addr;

    fillers[i] = socket(AF_INET, SOCK_STREAM, 0);
    ck_assert_int_ge(fillers[i], 0);
    ck_assert_int_eq(fcntl(fillers[i], F_SETFL, fcntl(fillers[i], F_GETFL, 0) | O_NONBLOCK), 0);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    connect(fillers[i], (struct sockaddr *)&addr, sizeof addr);
  }
  start = mono();
  ck_assert_int_eq(netconnect_tcp("127.0.0.1", port, 300, &reason), -1);
  elapsed = mono() - start;
  ck_assert_int_eq(reason, NET_ERR_TIMEOUT);
  ck_assert_double_ge(elapsed, 0.25);
  ck_assert_double_lt(elapsed, 2.0);
  for (int i = 0; i < 8; i++) close(fillers[i]);
  close(lfd);
}
END_TEST

START_TEST(netconnect_tcp_finish_falls_through_to_next_candidate) {
  unsigned port;
  int lfd = listen_loopback(&port, 1);
  netconnect_pending_t *pending = NULL;
  net_err_reason_t reason = NET_ERR_COUNT;
  int count;
  int v6_first = first_candidate_is_ipv6("localhost", port, &count);
  int fd = netconnect_tcp_start("localhost", port, &pending, &reason);
  int fallbacks = 0;
  int r;

  ck_assert_int_ge(fd, 0);
  for (;;) {
    ck_assert_int_eq(wait_writable(fd), 1);
    r = netconnect_tcp_finish(&pending, &fd, &reason);
    if (r != 0) break;
    fallbacks++;
    ck_assert_int_ge(fd, 0);
  }
  ck_assert_int_eq(r, 1);
  ck_assert_ptr_null(pending);
  if (count > 1 && v6_first) ck_assert_int_ge(fallbacks, 1);
  close(fd);
  close(lfd);
}
END_TEST

START_TEST(netconnect_tcp_finish_reports_error_after_all_candidates_fail) {
  netconnect_pending_t *pending = NULL;
  net_err_reason_t reason = NET_ERR_COUNT;
  int fd = netconnect_tcp_start("localhost", (unsigned)free_loopback_tcp_port(), &pending, &reason);
  int r;

  ck_assert_int_ge(fd, 0);
  for (;;) {
    ck_assert_int_eq(wait_writable(fd), 1);
    r = netconnect_tcp_finish(&pending, &fd, &reason);
    if (r != 0) break;
  }
  ck_assert_int_eq(r, -1);
  ck_assert_ptr_null(pending);
  ck_assert_int_eq(fd, -1);
}
END_TEST

static Suite *netconnect_suite(void) {
  Suite *s = suite_create("netconnect");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, netconnect_tcp_succeeds_against_local_listener);
  tcase_add_test(tc, netconnect_tcp_bounded_time_on_unreachable);
  tcase_add_test(tc, netconnect_tcp_refused_reports_connect_error);
  tcase_add_test(tc, netconnect_tcp_start_finish_refused_single_candidate);
  tcase_add_test(tc, netconnect_tcp_times_out_against_full_backlog);
  tcase_add_test(tc, netconnect_tcp_finish_falls_through_to_next_candidate);
  tcase_add_test(tc, netconnect_tcp_finish_reports_error_after_all_candidates_fail);
  tcase_add_test(tc, dscp_parse_symbolic_names);
  tcase_add_test(tc, dscp_parse_raw_numeric);
  tcase_add_test(tc, dscp_parse_rejects_invalid);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(netconnect_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
