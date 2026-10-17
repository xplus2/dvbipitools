/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../run_helper.h"
#include "dipixy/http3/http3_udp.h"

#define PAYLOAD 1500

static int rx_fd;
static int tx_fd;
static struct sockaddr_storage dst;
static socklen_t dstlen;

static void setup(void) {
  struct sockaddr_in a;
  socklen_t len = sizeof a;
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
  ck_assert_int_eq(h3_udp_init(PAYLOAD), 0);
  rx_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  tx_fd = socket(AF_INET, SOCK_DGRAM, 0);
  ck_assert_int_ge(rx_fd, 0);
  ck_assert_int_ge(tx_fd, 0);
  ck_assert_int_eq(bind(rx_fd, (struct sockaddr *)&a, sizeof a), 0);
  ck_assert_int_eq(getsockname(rx_fd, (struct sockaddr *)&a, &len), 0);
  memcpy(&dst, &a, sizeof a);
  dstlen = sizeof a;
}

static void teardown(void) {
  close(rx_fd);
  close(tx_fd);
  h3_udp_free();
}

static int recv_all(h3_rx_t *out, int want) {
  int got = 0;
  for (int tries = 0; tries < 50 && got < want; tries++) {
    fd_set fds;
    struct timeval tv = {0, 100000};
    FD_ZERO(&fds);
    FD_SET(rx_fd, &fds);
    if (select(rx_fd + 1, &fds, NULL, NULL, &tv) <= 0) continue;
    int n = h3_udp_recv(rx_fd, out + got, want - got);
    ck_assert_int_ge(n, 0);
    got += n;
  }
  return got;
}

static void check_burst(void) {
  uint8_t buf[5 * 1000 + 400];
  h3_rx_t rx[H3_RX_BATCH];
  for (size_t i = 0; i < sizeof buf; i++) buf[i] = (uint8_t)(i / 100);
  h3_udp_send(tx_fd, &dst, dstlen, NULL, buf, sizeof buf, 1000, 0);
  ck_assert_int_eq(recv_all(rx, 6), 6);
  for (int i = 0; i < 6; i++) {
    size_t want = i < 5 ? 1000 : 400;
    ck_assert_uint_eq(rx[i].len, want);
    ck_assert_int_eq(memcmp(rx[i].data, buf + (size_t)i * 1000, want), 0);
  }
}

START_TEST(fallback_send_splits_into_datagrams) {
  h3_udp_set_gso(0);
  check_burst();
}
END_TEST

START_TEST(gso_send_splits_into_datagrams) {
  if (!h3_udp_gso_probe(tx_fd)) return;
  check_burst();
}
END_TEST

START_TEST(single_datagram_needs_no_gso) {
  uint8_t buf[300];
  h3_rx_t rx[H3_RX_BATCH];
  memset(buf, 7, sizeof buf);
  h3_udp_send(tx_fd, &dst, dstlen, NULL, buf, sizeof buf, sizeof buf, 0);
  ck_assert_int_eq(recv_all(rx, 1), 1);
  ck_assert_uint_eq(rx[0].len, sizeof buf);
}
END_TEST

START_TEST(recv_reports_sender) {
  uint8_t buf[50] = {1};
  h3_rx_t rx[H3_RX_BATCH];
  struct sockaddr_in me;
  socklen_t len = sizeof me;
  h3_udp_send(tx_fd, &dst, dstlen, NULL, buf, sizeof buf, sizeof buf, 0);
  ck_assert_int_eq(recv_all(rx, 1), 1);
  ck_assert_int_eq(getsockname(tx_fd, (struct sockaddr *)&me, &len), 0);
  ck_assert_int_eq(rx[0].peer.ss_family, AF_INET);
  ck_assert_int_eq(((struct sockaddr_in *)&rx[0].peer)->sin_port, me.sin_port);
}
END_TEST

START_TEST(recv_on_empty_socket_returns_zero) {
  h3_rx_t rx[H3_RX_BATCH];
  ck_assert_int_eq(h3_udp_recv(rx_fd, rx, H3_RX_BATCH), 0);
}
END_TEST

START_TEST(recv_marks_oversized_datagram) {
  uint8_t big[PAYLOAD + 200];
  h3_rx_t rx[H3_RX_BATCH];
  memset(big, 1, sizeof big);
  h3_udp_send(tx_fd, &dst, dstlen, NULL, big, sizeof big, sizeof big, 0);
  ck_assert_int_eq(recv_all(rx, 1), 1);
  ck_assert_uint_eq(rx[0].len, 0);
}
END_TEST

START_TEST(recv_batches_many_datagrams) {
  uint8_t buf[40];
  h3_rx_t rx[H3_RX_BATCH];
  memset(buf, 3, sizeof buf);
  for (int i = 0; i < 20; i++) h3_udp_send(tx_fd, &dst, dstlen, NULL, buf, sizeof buf, sizeof buf, 0);
  ck_assert_int_eq(recv_all(rx, 20), 20);
}
END_TEST

static int wild_sock(struct sockaddr_in *bound) {
  struct sockaddr_in a;
  socklen_t len = sizeof a;
  int fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  ck_assert_int_ge(fd, 0);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&a, sizeof a), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)bound, &len), 0);
  return fd;
}

START_TEST(recv_reports_local_destination) {
  struct sockaddr_in w, to;
  struct sockaddr_storage d;
  uint8_t buf[20] = {1};
  h3_rx_t rx[H3_RX_BATCH];
  int wfd = wild_sock(&w);
  int saved = rx_fd;
  ck_assert_int_eq(h3_udp_pktinfo(wfd, AF_INET), 0);
  to = w;
  inet_pton(AF_INET, "127.0.0.2", &to.sin_addr);
  memcpy(&d, &to, sizeof to);
  h3_udp_send(tx_fd, &d, sizeof to, NULL, buf, sizeof buf, sizeof buf, 0);
  rx_fd = wfd;
  ck_assert_int_eq(recv_all(rx, 1), 1);
  rx_fd = saved;
  close(wfd);
  ck_assert_uint_eq(rx[0].locallen, sizeof(struct sockaddr_in));
  ck_assert_int_eq(rx[0].local.ss_family, AF_INET);
  ck_assert_uint_eq(((struct sockaddr_in *)&rx[0].local)->sin_addr.s_addr, to.sin_addr.s_addr);
}
END_TEST

START_TEST(recv_without_pktinfo_has_no_local) {
  uint8_t buf[20] = {1};
  h3_rx_t rx[H3_RX_BATCH];
  h3_udp_send(tx_fd, &dst, dstlen, NULL, buf, sizeof buf, sizeof buf, 0);
  ck_assert_int_eq(recv_all(rx, 1), 1);
  ck_assert_uint_eq(rx[0].locallen, 0);
}
END_TEST

START_TEST(send_uses_requested_source) {
  struct sockaddr_in w;
  struct sockaddr_in src;
  struct sockaddr_storage s;
  uint8_t buf[2500];
  h3_rx_t rx[H3_RX_BATCH];
  int wfd = wild_sock(&w);
  memset(buf, 5, sizeof buf);
  src = w;
  inet_pton(AF_INET, "127.0.0.2", &src.sin_addr);
  memcpy(&s, &src, sizeof src);
  h3_udp_send(wfd, &dst, dstlen, &s, buf, sizeof buf, 1000, 0);
  close(wfd);
  ck_assert_int_eq(recv_all(rx, 3), 3);
  for (int i = 0; i < 3; i++) {
    ck_assert_int_eq(rx[i].peer.ss_family, AF_INET);
    ck_assert_uint_eq(((struct sockaddr_in *)&rx[i].peer)->sin_addr.s_addr, src.sin_addr.s_addr);
  }
}
END_TEST

START_TEST(ecn_marks_roundtrip) {
  uint8_t buf[100];
  h3_rx_t rx[H3_RX_BATCH];
  memset(buf, 7, sizeof buf);
  ck_assert_int_eq(h3_udp_pktinfo(rx_fd, AF_INET), 0);
  h3_udp_send(tx_fd, &dst, dstlen, NULL, buf, sizeof buf, sizeof buf, 2);
  ck_assert_int_eq(recv_all(rx, 1), 1);
  ck_assert_uint_eq(rx[0].ecn, 2);
  h3_udp_send(tx_fd, &dst, dstlen, NULL, buf, sizeof buf, sizeof buf, 0);
  ck_assert_int_eq(recv_all(rx, 1), 1);
  ck_assert_uint_eq(rx[0].ecn, 0);
}
END_TEST

static int v6_pair(int *rx6, int *tx6, struct sockaddr_in6 *dst6) {
  struct sockaddr_in6 a;
  socklen_t len = sizeof a;
  *rx6 = socket(AF_INET6, SOCK_DGRAM | SOCK_NONBLOCK, 0);
  if (*rx6 < 0) return 0;
  memset(&a, 0, sizeof a);
  a.sin6_family = AF_INET6;
  inet_pton(AF_INET6, "::1", &a.sin6_addr);
  if (bind(*rx6, (struct sockaddr *)&a, sizeof a) != 0) {
    close(*rx6);
    return 0;
  }
  getsockname(*rx6, (struct sockaddr *)dst6, &len);
  *tx6 = socket(AF_INET6, SOCK_DGRAM, 0);
  return *tx6 >= 0;
}

START_TEST(ipv6_ecn_pktinfo_and_source_roundtrip) {
  int rx6;
  int tx6;
  struct sockaddr_in6 d6;
  struct sockaddr_storage d;
  struct sockaddr_storage srcs;
  struct sockaddr_in6 src6;
  uint8_t buf[100];
  h3_rx_t rx[H3_RX_BATCH];
  int n = 0;
  if (!v6_pair(&rx6, &tx6, &d6)) return;
  memset(buf, 9, sizeof buf);
  memcpy(&d, &d6, sizeof d6);
  ck_assert_int_eq(h3_udp_pktinfo(rx6, AF_INET6), 0);
  memset(&src6, 0, sizeof src6);
  src6.sin6_family = AF_INET6;
  inet_pton(AF_INET6, "::1", &src6.sin6_addr);
  memset(&srcs, 0, sizeof srcs);
  memcpy(&srcs, &src6, sizeof src6);
  h3_udp_send(tx6, &d, sizeof d6, &srcs, buf, sizeof buf, sizeof buf, 1);
  for (int i = 0; i < 50 && n < 1; i++) {
    n = h3_udp_recv(rx6, rx, H3_RX_BATCH);
    if (n == 0) usleep(20000);
  }
  ck_assert_int_eq(n, 1);
  ck_assert_uint_eq(rx[0].ecn, 1);
  ck_assert_uint_eq(rx[0].locallen, sizeof(struct sockaddr_in6));
  ck_assert_int_eq(rx[0].local.ss_family, AF_INET6);
  close(rx6);
  close(tx6);
}
END_TEST

START_TEST(unusable_sources_are_ignored) {
  struct sockaddr_in any;
  struct sockaddr_in6 any6;
  struct sockaddr_in6 six;
  struct sockaddr_storage s;
  uint8_t buf[60];
  h3_rx_t rx[H3_RX_BATCH];
  memset(buf, 4, sizeof buf);
  memset(&any, 0, sizeof any);
  any.sin_family = AF_INET;
  memset(&s, 0, sizeof s);
  memcpy(&s, &any, sizeof any);
  h3_udp_send(tx_fd, &dst, dstlen, &s, buf, sizeof buf, sizeof buf, 0);
  ck_assert_int_eq(recv_all(rx, 1), 1);
  memset(&six, 0, sizeof six);
  six.sin6_family = AF_INET6;
  inet_pton(AF_INET6, "::1", &six.sin6_addr);
  memset(&s, 0, sizeof s);
  memcpy(&s, &six, sizeof six);
  h3_udp_send(tx_fd, &dst, dstlen, &s, buf, sizeof buf, sizeof buf, 0);
  ck_assert_int_eq(recv_all(rx, 1), 1);
  memset(&any6, 0, sizeof any6);
  any6.sin6_family = AF_INET6;
  memset(&s, 0, sizeof s);
  memcpy(&s, &any6, sizeof any6);
  h3_udp_send(tx_fd, &dst, dstlen, &s, buf, sizeof buf, sizeof buf, 0);
  ck_assert_int_eq(recv_all(rx, 1), 1);
  memset(&s, 0, sizeof s);
  s.ss_family = AF_UNIX;
  h3_udp_send(tx_fd, &dst, dstlen, &s, buf, sizeof buf, sizeof buf, 0);
  ck_assert_int_eq(recv_all(rx, 1), 1);
}
END_TEST

START_TEST(empty_send_is_a_no_op_and_zero_segment_sends_whole) {
  uint8_t buf[200];
  h3_rx_t rx[H3_RX_BATCH];
  memset(buf, 6, sizeof buf);
  h3_udp_send(tx_fd, &dst, dstlen, NULL, buf, 0, 100, 0);
  ck_assert_int_eq(h3_udp_recv(rx_fd, rx, H3_RX_BATCH), 0);
  h3_udp_set_gso(0);
  h3_udp_send(tx_fd, &dst, dstlen, NULL, buf, sizeof buf, 0, 0);
  ck_assert_int_eq(recv_all(rx, 1), 1);
  ck_assert_uint_eq(rx[0].len, sizeof buf);
}
END_TEST

START_TEST(gso_failure_falls_back_to_individual_datagrams) {
  uint8_t *big = malloc(140000);
  h3_rx_t rx[H3_RX_BATCH];
  ck_assert_ptr_nonnull(big);
  memset(big, 8, 140000);
  h3_udp_set_gso(1);
  h3_udp_send(tx_fd, &dst, dstlen, NULL, big, 140000, 60000, 0);
  ck_assert_int_ge(recv_all(rx, 3), 1);
  free(big);
}
END_TEST

START_TEST(many_segments_are_sent_in_batches) {
  uint8_t *buf = malloc(100 * 40);
  h3_rx_t rx[H3_RX_BATCH];
  int got = 0;
  ck_assert_ptr_nonnull(buf);
  memset(buf, 2, 100 * 40);
  h3_udp_set_gso(0);
  h3_udp_send(tx_fd, &dst, dstlen, NULL, buf, 100 * 40, 40, 0);
  for (int round = 0; round < 20 && got < 100; round++) {
    int n = h3_udp_recv(rx_fd, rx, H3_RX_BATCH);
    if (n > 0) got += n;
    else sleep_ms(10);
  }
  ck_assert_int_ge(got, 1);
  free(buf);
}
END_TEST

START_TEST(recv_on_a_closed_descriptor_is_an_error) {
  h3_rx_t rx[H3_RX_BATCH];
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  close(fd);
  ck_assert_int_eq(h3_udp_recv(fd, rx, H3_RX_BATCH), -1);
  ck_assert_int_eq(h3_udp_recv(rx_fd, rx, H3_RX_BATCH + 50), 0);
}
END_TEST

START_TEST(init_is_idempotent_and_free_resets) {
  ck_assert_int_eq(h3_udp_init(PAYLOAD), 0);
  ck_assert_uint_gt(h3_udp_tx_cap(), 0u);
  ck_assert_ptr_nonnull(h3_udp_tx_buf());
  h3_udp_free();
  ck_assert_uint_eq(h3_udp_tx_cap(), 0u);
  ck_assert_ptr_null(h3_udp_tx_buf());
  ck_assert_int_eq(h3_udp_init(PAYLOAD), 0);
}
END_TEST

START_TEST(gso_probe_is_sticky_across_calls) {
  int first = h3_udp_gso_probe(tx_fd);
  int second = h3_udp_gso_probe(tx_fd);
  ck_assert_int_eq(first, second);
  ck_assert_int_eq(h3_udp_gso_probe(-1), 0);
}
END_TEST

static Suite *h3_udp_suite(void) {
  Suite *s = suite_create("dipixy_h3_udp");
  TCase *tc = tcase_create("core");
  tcase_add_checked_fixture(tc, setup, teardown);
  tcase_add_test(tc, fallback_send_splits_into_datagrams);
  tcase_add_test(tc, gso_send_splits_into_datagrams);
  tcase_add_test(tc, single_datagram_needs_no_gso);
  tcase_add_test(tc, recv_reports_sender);
  tcase_add_test(tc, recv_on_empty_socket_returns_zero);
  tcase_add_test(tc, recv_marks_oversized_datagram);
  tcase_add_test(tc, recv_batches_many_datagrams);
  tcase_add_test(tc, recv_reports_local_destination);
  tcase_add_test(tc, recv_without_pktinfo_has_no_local);
  tcase_add_test(tc, send_uses_requested_source);
  tcase_add_test(tc, ecn_marks_roundtrip);
  tcase_add_test(tc, ipv6_ecn_pktinfo_and_source_roundtrip);
  tcase_add_test(tc, unusable_sources_are_ignored);
  tcase_add_test(tc, empty_send_is_a_no_op_and_zero_segment_sends_whole);
  tcase_add_test(tc, gso_failure_falls_back_to_individual_datagrams);
  tcase_add_test(tc, many_segments_are_sent_in_batches);
  tcase_add_test(tc, recv_on_a_closed_descriptor_is_an_error);
  tcase_add_test(tc, init_is_idempotent_and_free_resets);
  tcase_add_test(tc, gso_probe_is_sticky_across_calls);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(h3_udp_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
