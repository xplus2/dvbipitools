/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "lib/net/multicast.h"

static int rx_open(int family, unsigned *port_out) {
  struct sockaddr_storage ss;
  struct timeval tv = {2, 0};
  socklen_t len;
  int fd = socket(family, SOCK_DGRAM, 0);

  if (fd < 0)
    return -1;
  memset(&ss, 0, sizeof ss);
  if (family == AF_INET) {
    struct sockaddr_in *a = (struct sockaddr_in *)&ss;
    a->sin_family = AF_INET;
    inet_pton(AF_INET, "127.0.0.1", &a->sin_addr);
    len = sizeof *a;
  } else {
    struct sockaddr_in6 *a = (struct sockaddr_in6 *)&ss;
    a->sin6_family = AF_INET6;
    inet_pton(AF_INET6, "::1", &a->sin6_addr);
    len = sizeof *a;
  }
  if (bind(fd, (struct sockaddr *)&ss, len) < 0 || getsockname(fd, (struct sockaddr *)&ss, &len) < 0) {
    close(fd);
    return -1;
  }
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  *port_out = ntohs(family == AF_INET ? ((struct sockaddr_in *)&ss)->sin_port : ((struct sockaddr_in6 *)&ss)->sin6_port);
  return fd;
}

static void sendv_round_trip(int family, const char *dest) {
  unsigned char hdr[12];
  unsigned char payload[100];
  unsigned char got[256];
  unsigned port;
  int rx = rx_open(family, &port);
  mcast_t *m;
  ssize_t n;

  if (rx < 0)
    return;
  memset(hdr, 0xA5, sizeof hdr);
  memset(payload, 0x3C, sizeof payload);
  m = mcast_open_send(family, dest, port, NULL, 0);
  ck_assert_ptr_nonnull(m);
  ck_assert_int_eq(mcast_sendv(m, hdr, sizeof hdr, payload, sizeof payload), (ssize_t)(sizeof hdr + sizeof payload));
  n = recv(rx, got, sizeof got, 0);
  ck_assert_int_eq(n, (int)(sizeof hdr + sizeof payload));
  ck_assert_mem_eq(got, hdr, sizeof hdr);
  ck_assert_mem_eq(got + sizeof hdr, payload, sizeof payload);
  mcast_close(m);
  close(rx);
}

START_TEST(sendv_v4_gathers_header_and_payload) {
  sendv_round_trip(AF_INET, "127.0.0.1");
}
END_TEST

START_TEST(sendv_v6_gathers_header_and_payload) {
  sendv_round_trip(AF_INET6, "::1");
}
END_TEST

START_TEST(sendv_with_empty_header_sends_payload_only) {
  unsigned char payload[7] = {1, 2, 3, 4, 5, 6, 7};
  unsigned char got[32];
  unsigned port;
  int rx = rx_open(AF_INET, &port);
  mcast_t *m;

  ck_assert_int_ge(rx, 0);
  m = mcast_open_send(AF_INET, "127.0.0.1", port, NULL, 0);
  ck_assert_ptr_nonnull(m);
  ck_assert_int_eq(mcast_sendv(m, NULL, 0, payload, sizeof payload), (ssize_t)sizeof payload);
  ck_assert_int_eq(recv(rx, got, sizeof got, 0), (int)sizeof payload);
  ck_assert_mem_eq(got, payload, sizeof payload);
  mcast_close(m);
  close(rx);
}
END_TEST

START_TEST(send_delivers_one_datagram) {
  unsigned char payload[40];
  unsigned char got[64];
  unsigned port;
  int rx = rx_open(AF_INET, &port);
  mcast_t *m;

  ck_assert_int_ge(rx, 0);
  memset(payload, 0x77, sizeof payload);
  m = mcast_open_send(AF_INET, "127.0.0.1", port, NULL, 0);
  ck_assert_ptr_nonnull(m);
  ck_assert_int_eq(mcast_send(m, payload, sizeof payload), (ssize_t)sizeof payload);
  ck_assert_int_eq(recv(rx, got, sizeof got, 0), (int)sizeof payload);
  ck_assert_mem_eq(got, payload, sizeof payload);
  mcast_close(m);
  close(rx);
}
END_TEST

START_TEST(send_and_sendv_fail_on_port_zero) {
  unsigned char b[4] = {0};
  mcast_t *m4 = mcast_open_send(AF_INET, "127.0.0.1", 0, NULL, 0);

  ck_assert_ptr_nonnull(m4);
  ck_assert_int_eq(mcast_send(m4, b, sizeof b), -1);
  ck_assert_int_eq(mcast_sendv(m4, b, sizeof b, b, sizeof b), -1);
  mcast_close(m4);
}
END_TEST

START_TEST(open_send_applies_ttl_and_tos) {
  mcast_t *m = mcast_open_send(AF_INET, "127.0.0.1", 9, NULL, 5);
  int ttl = 0;
  int tos = 0;
  socklen_t len = sizeof ttl;

  ck_assert_ptr_nonnull(m);
  ck_assert_int_ge(mcast_fd(m), 0);
  ck_assert_int_eq(getsockopt(mcast_fd(m), IPPROTO_IP, IP_MULTICAST_TTL, &ttl, &len), 0);
  ck_assert_int_eq(ttl, 5);
  ck_assert_int_eq(mcast_set_tos(m, 0xB8), 0);
  len = sizeof tos;
  ck_assert_int_eq(getsockopt(mcast_fd(m), IPPROTO_IP, IP_TOS, &tos, &len), 0);
  ck_assert_int_eq(tos, 0xB8);
  mcast_close(m);
}
END_TEST

START_TEST(open_send_v6_applies_hop_limit) {
  mcast_t *m = mcast_open_send(AF_INET6, "::1", 9, NULL, 7);
  int hops = 0;
  socklen_t len = sizeof hops;

  ck_assert_ptr_nonnull(m);
  ck_assert_int_eq(getsockopt(mcast_fd(m), IPPROTO_IPV6, IPV6_MULTICAST_HOPS, &hops, &len), 0);
  ck_assert_int_eq(hops, 7);
  mcast_close(m);
}
END_TEST

START_TEST(open_send_rejects_bad_interface_and_group) {
  ck_assert_ptr_null(mcast_open_send(AF_INET, "127.0.0.1", 9, "nonexistent-if0", 0));
  ck_assert_ptr_null(mcast_open_send(AF_INET, "not-an-address", 9, NULL, 0));
  ck_assert_ptr_null(mcast_open_send(AF_INET6, "not-an-address", 9, NULL, 0));
  ck_assert_ptr_null(mcast_open_send(AF_INET6, "::1", 9, "nonexistent-if0", 0));
}
END_TEST

START_TEST(open_send_on_loopback_interface_sets_multicast_if) {
  mcast_t *m4 = mcast_open_send(AF_INET, "127.0.0.1", 9, "lo", 0);
  mcast_t *m6 = mcast_open_send(AF_INET6, "::1", 9, "lo", 0);

  ck_assert_ptr_nonnull(m4);
  ck_assert_ptr_nonnull(m6);
  mcast_close(m4);
  mcast_close(m6);
}
END_TEST

START_TEST(close_null_is_safe) {
  mcast_close(NULL);
}
END_TEST

static unsigned free_udp_port(void) {
  struct sockaddr_in a;
  socklen_t len = sizeof a;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  unsigned port;

  ck_assert_int_ge(fd, 0);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&a, sizeof a), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&a, &len), 0);
  port = ntohs(a.sin_port);
  close(fd);
  return port;
}

static void unique_group(char *out, size_t cap) {
  pid_t pid = getpid();

  snprintf(out, cap, "239.78.%u.%u", ((unsigned)pid >> 8) & 0xFF, 1 + ((unsigned)pid & 0xFF) % 250);
}

START_TEST(open_rejects_unknown_interface) {
  char group[32];

  unique_group(group, sizeof group);
  ck_assert_ptr_null(mcast_open(AF_INET, group, free_udp_port(), "nonexistent-if0", 100));
  ck_assert_ptr_null(mcast_open(AF_INET6, "ff02::1234", free_udp_port(), "nonexistent-if0", 100));
}
END_TEST

START_TEST(open_rejects_bad_and_non_multicast_groups) {
  ck_assert_ptr_null(mcast_open(AF_INET, "not-an-address", free_udp_port(), NULL, 100));
  ck_assert_ptr_null(mcast_open(AF_INET6, "not-an-address", free_udp_port(), NULL, 100));
  ck_assert_ptr_null(mcast_open(AF_INET, "127.0.0.1", free_udp_port(), NULL, 100));
}
END_TEST

START_TEST(open_fails_when_group_port_is_taken_exclusively) {
  struct sockaddr_in a;
  char group[32];
  unsigned port = free_udp_port();
  int fd = socket(AF_INET, SOCK_DGRAM, 0);

  ck_assert_int_ge(fd, 0);
  unique_group(group, sizeof group);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons((unsigned short)port);
  ck_assert_int_eq(inet_pton(AF_INET, group, &a.sin_addr), 1);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&a, sizeof a), 0);
  ck_assert_ptr_null(mcast_open(AF_INET, group, port, NULL, 100));
  ck_assert_ptr_null(mcast_open_ssm(AF_INET, group, port, "192.0.2.1", NULL, 100));
  close(fd);
}
END_TEST

START_TEST(open_ssm_rejects_bad_arguments_and_failed_join) {
  char group[32];

  unique_group(group, sizeof group);
  ck_assert_ptr_null(mcast_open_ssm(AF_INET, group, free_udp_port(), "192.0.2.1", "nonexistent-if0", 100));
  ck_assert_ptr_null(mcast_open_ssm(AF_INET, "not-an-address", free_udp_port(), "192.0.2.1", NULL, 100));
  ck_assert_ptr_null(mcast_open_ssm(AF_INET, group, free_udp_port(), "not-an-address", NULL, 100));
  ck_assert_ptr_null(mcast_open_ssm(AF_INET, "127.0.0.1", free_udp_port(), "192.0.2.1", NULL, 100));
  ck_assert_ptr_null(mcast_open_ssm(AF_INET6, "ff02::1234", free_udp_port(), "192.0.2.1", NULL, 100));
}
END_TEST

START_TEST(rx_timestamps_are_zero_until_enabled_then_wall_clock) {
  struct sockaddr_in dst;
  struct timespec now;
  unsigned char buf[16];
  char group[32];
  unsigned port = free_udp_port();
  mcast_t *m;
  int tx = socket(AF_INET, SOCK_DGRAM, 0);
  uint64_t now_ns;
  uint64_t rx_ns;

  ck_assert_int_ge(tx, 0);
  unique_group(group, sizeof group);
  m = mcast_open(AF_INET, group, port, NULL, 1000);
  ck_assert_ptr_nonnull(m);
  memset(&dst, 0, sizeof dst);
  dst.sin_family = AF_INET;
  dst.sin_port = htons((unsigned short)port);
  ck_assert_int_eq(inet_pton(AF_INET, group, &dst.sin_addr), 1);

  ck_assert_int_eq((int)sendto(tx, "x", 1, 0, (struct sockaddr *)&dst, sizeof dst), 1);
  ck_assert_int_eq((int)mcast_recv(m, buf, sizeof buf, NULL), 1);
  ck_assert_uint_eq(mcast_last_rx_ns(m), 0u);

  ck_assert_int_eq(mcast_enable_rx_timestamps(m), 0);
  ck_assert_int_eq((int)sendto(tx, "y", 1, 0, (struct sockaddr *)&dst, sizeof dst), 1);
  ck_assert_int_eq((int)mcast_recv(m, buf, sizeof buf, NULL), 1);
  rx_ns = mcast_last_rx_ns(m);
  clock_gettime(CLOCK_REALTIME, &now);
  now_ns = (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
  ck_assert_uint_gt(rx_ns, 0u);
  ck_assert_uint_le(rx_ns, now_ns);
  ck_assert_uint_lt(now_ns - rx_ns, 5000000000ULL);
  close(tx);
  mcast_close(m);
}
END_TEST

static Suite *multicast_suite(void) {
  Suite *s = suite_create("multicast");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, sendv_v4_gathers_header_and_payload);
  tcase_add_test(tc, sendv_v6_gathers_header_and_payload);
  tcase_add_test(tc, sendv_with_empty_header_sends_payload_only);
  tcase_add_test(tc, send_delivers_one_datagram);
  tcase_add_test(tc, send_and_sendv_fail_on_port_zero);
  tcase_add_test(tc, open_send_applies_ttl_and_tos);
  tcase_add_test(tc, open_send_v6_applies_hop_limit);
  tcase_add_test(tc, open_send_rejects_bad_interface_and_group);
  tcase_add_test(tc, open_send_on_loopback_interface_sets_multicast_if);
  tcase_add_test(tc, open_rejects_unknown_interface);
  tcase_add_test(tc, open_rejects_bad_and_non_multicast_groups);
  tcase_add_test(tc, open_fails_when_group_port_is_taken_exclusively);
  tcase_add_test(tc, open_ssm_rejects_bad_arguments_and_failed_join);
  tcase_add_test(tc, rx_timestamps_are_zero_until_enabled_then_wall_clock);
  tcase_add_test(tc, close_null_is_safe);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(multicast_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
