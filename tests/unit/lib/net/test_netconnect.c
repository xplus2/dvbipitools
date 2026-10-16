/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
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

static int wait_pending(int fd, const netconnect_pending_t *pending) {
  struct pollfd pfd = {fd, netconnect_tcp_events(pending), 0};

  return poll(&pfd, 1, 5000);
}

static int first_candidate_is_ipv6(const char *host, unsigned port, int *count) {
  struct addrinfo hints;
  struct addrinfo *res;
  char portstr[8];
  int v6;

  snprintf(portstr, sizeof portstr, "%u", port);
  memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  ck_assert_int_eq(getaddrinfo(host, portstr, &hints, &res), 0);
  *count = 0;
  for (const struct addrinfo *ai = res; ai; ai = ai->ai_next) (*count)++;
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

START_TEST(netconnect_tcp_rejects_out_of_range_port) {
  net_err_reason_t reason = NET_ERR_COUNT;
  netconnect_pending_t *pending = NULL;

  ck_assert_int_eq(netconnect_tcp("127.0.0.1", 65536, 300, &reason), -1);
  ck_assert_int_eq(reason, NET_ERR_CONNECT);
  ck_assert_int_eq(errno, EINVAL);
  reason = NET_ERR_COUNT;
  ck_assert_int_eq(netconnect_tcp_start("127.0.0.1", 100000, &pending, &reason), -1);
  ck_assert_int_eq(reason, NET_ERR_CONNECT);
  ck_assert_ptr_null(pending);
}
END_TEST

START_TEST(netaddr_fill_builds_v4_and_v6_addresses) {
  struct sockaddr_storage ss;
  socklen_t len = 0;
  ck_assert_int_eq(netaddr_fill(AF_INET, "192.0.2.7", 5004, &ss, &len), 0);
  ck_assert_uint_eq(len, sizeof(struct sockaddr_in));
  ck_assert_int_eq(((struct sockaddr_in *)&ss)->sin_port, htons(5004));
  ck_assert_int_eq(netaddr_fill(AF_INET6, "2001:db8::7", 5004, &ss, &len), 0);
  ck_assert_uint_eq(len, sizeof(struct sockaddr_in6));
  ck_assert_int_eq(((struct sockaddr_in6 *)&ss)->sin6_port, htons(5004));
  ck_assert_int_eq(netaddr_fill(AF_INET, "not-an-address", 1, &ss, &len), -1);
  ck_assert_int_eq(netaddr_fill(AF_INET6, "192.0.2.7", 1, &ss, &len), -1);
}
END_TEST

START_TEST(dscp_is_applied_to_v4_and_v6_sockets) {
  int fd4 = socket(AF_INET, SOCK_DGRAM, 0);
  int fd6 = socket(AF_INET6, SOCK_DGRAM, 0);
  ck_assert_int_ge(fd4, 0);
  ck_assert_int_eq(net_set_dscp(fd4, AF_INET, NET_DSCP_VIDEO_HIGH), 0);
  close(fd4);
  if (fd6 >= 0) {
    ck_assert_int_eq(net_set_dscp(fd6, AF_INET6, NET_DSCP_VIDEO_HIGH), 0);
    close(fd6);
  }
}
END_TEST

START_TEST(sendto_tos_marks_each_packet_without_socket_state) {
  struct sockaddr_in to;
  socklen_t tolen = sizeof to;
  int rx = socket(AF_INET, SOCK_DGRAM, 0);
  int tx = socket(AF_INET, SOCK_DGRAM, 0);
  int on = 1;
  int want[2] = {NET_DSCP_VIDEO_HIGH, NET_DSCP_SIGNALLING};

  ck_assert_int_ge(rx, 0);
  ck_assert_int_ge(tx, 0);
  memset(&to, 0, sizeof to);
  to.sin_family = AF_INET;
  to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(rx, (struct sockaddr *)&to, sizeof to), 0);
  ck_assert_int_eq(getsockname(rx, (struct sockaddr *)&to, &tolen), 0);
  ck_assert_int_eq(setsockopt(rx, IPPROTO_IP, IP_RECVTOS, &on, sizeof on), 0);
  for (int i = 0; i < 2; i++) {
    char data[4];
    char ctl[64];
    struct iovec iov = {data, sizeof data};
    struct msghdr msg;
    struct cmsghdr *cm;
    int got = -1;

    ck_assert_int_eq(net_sendto_tos(tx, "abcd", 4, (struct sockaddr *)&to, sizeof to, want[i]), 4);
    memset(&msg, 0, sizeof msg);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = ctl;
    msg.msg_controllen = sizeof ctl;
    ck_assert_int_eq(recvmsg(rx, &msg, 0), 4);
    for (cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm))
      if (cm->cmsg_level == IPPROTO_IP && cm->cmsg_type == IP_TOS) got = *(unsigned char *)CMSG_DATA(cm);
    ck_assert_int_eq(got, want[i]);
  }
  close(rx);
  close(tx);
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

START_TEST(netconnect_tcp_start_hostname_resolves_without_blocking) {
  netconnect_pending_t *pending = NULL;
  net_err_reason_t reason = NET_ERR_COUNT;
  int fd = netconnect_tcp_start("localhost", 1, &pending, &reason);

  ck_assert_int_ge(fd, 0);
  ck_assert_ptr_nonnull(pending);
  ck_assert_int_eq(netconnect_tcp_events(pending), POLLIN);
  ck_assert_int_eq(wait_pending(fd, pending), 1);
  ck_assert_int_eq(netconnect_tcp_finish(&pending, &fd, &reason), 0);
  ck_assert_int_eq(netconnect_tcp_events(pending), POLLOUT);
  netconnect_tcp_abort(pending);
  close(fd);
}
END_TEST

START_TEST(netconnect_tcp_start_literal_skips_resolver) {
  netconnect_pending_t *pending = NULL;
  int fd = netconnect_tcp_start("127.0.0.1", (unsigned)free_loopback_tcp_port(), &pending, NULL);

  ck_assert_int_ge(fd, 0);
  ck_assert_int_eq(netconnect_tcp_events(pending), POLLOUT);
  netconnect_tcp_abort(pending);
  close(fd);
}
END_TEST

START_TEST(netconnect_tcp_abort_while_resolving_is_clean) {
  for (int i = 0; i < 20; i++) {
    netconnect_pending_t *pending = NULL;
    int fd = netconnect_tcp_start("localhost", 1, &pending, NULL);

    ck_assert_int_ge(fd, 0);
    netconnect_tcp_abort(pending);
    close(fd);
  }
}
END_TEST

START_TEST(netconnect_tcp_start_slow_lookup_does_not_block) {
  netconnect_pending_t *pending = NULL;
  double t0 = mono();
  int fd = netconnect_tcp_start("nonexistent.invalid", 80, &pending, NULL);

  ck_assert_double_lt(mono() - t0, 0.5);
  ck_assert_int_ge(fd, 0);
  ck_assert_int_eq(netconnect_tcp_events(pending), POLLIN);
  netconnect_tcp_abort(pending);
  close(fd);
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
    ck_assert_int_eq(wait_pending(fd, pending), 1);
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
    ck_assert_int_eq(wait_pending(fd, pending), 1);
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
  tcase_add_test(tc, sendto_tos_marks_each_packet_without_socket_state);
  tcase_add_test(tc, netconnect_tcp_succeeds_against_local_listener);
  tcase_add_test(tc, netconnect_tcp_bounded_time_on_unreachable);
  tcase_add_test(tc, netconnect_tcp_refused_reports_connect_error);
  tcase_add_test(tc, netconnect_tcp_rejects_out_of_range_port);
  tcase_add_test(tc, netaddr_fill_builds_v4_and_v6_addresses);
  tcase_add_test(tc, dscp_is_applied_to_v4_and_v6_sockets);
  tcase_add_test(tc, netconnect_tcp_start_finish_refused_single_candidate);
  tcase_add_test(tc, netconnect_tcp_times_out_against_full_backlog);
  tcase_add_test(tc, netconnect_tcp_start_hostname_resolves_without_blocking);
  tcase_add_test(tc, netconnect_tcp_start_literal_skips_resolver);
  tcase_add_test(tc, netconnect_tcp_abort_while_resolving_is_clean);
  tcase_add_test(tc, netconnect_tcp_start_slow_lookup_does_not_block);
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
