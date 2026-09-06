/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <netinet/in.h>
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

static Suite *netconnect_suite(void) {
  Suite *s = suite_create("netconnect");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, netconnect_tcp_succeeds_against_local_listener);
  tcase_add_test(tc, netconnect_tcp_bounded_time_on_unreachable);
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
