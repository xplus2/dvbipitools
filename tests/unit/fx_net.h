/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_TESTS_UNIT_FX_NET_H
#define DVBIPITOOLS_TESTS_UNIT_FX_NET_H

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <check.h>
#include <netinet/in.h>
#include <net/if.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static inline unsigned free_udp_port(void) {
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  unsigned port;

  if (fd < 0) abort();
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  port = ntohs(addr.sin_port);
  close(fd);
  return port;
}

static inline int make_listener(unsigned *port_out) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;
  if (fd < 0) abort();
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(fd, 4), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  *port_out = ntohs(addr.sin_port);
  return fd;
}

static inline int bind_listener(unsigned *port_out) {
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;
  int fd = socket(AF_INET, SOCK_STREAM, 0);

  ck_assert_int_ge(fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(fd, 1), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  *port_out = ntohs(addr.sin_port);
  return fd;
}

static inline int bind_udp_loopback(unsigned port) {
  struct sockaddr_in addr;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);

  ck_assert_int_ge(fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((unsigned short)port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static inline int udp_bound(struct sockaddr_in *addr_out) {
  socklen_t len = sizeof *addr_out;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);

  memset(addr_out, 0, sizeof *addr_out);
  addr_out->sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &addr_out->sin_addr);
  if (fd < 0) abort();
  ck_assert_int_eq(bind(fd, (struct sockaddr *)addr_out, sizeof *addr_out), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)addr_out, &len), 0);
  return fd;
}

static inline unsigned free_even_udp_port_pair(void) {
  for (int tries = 0; tries < 100; tries++) {
    struct sockaddr_in addr;
    socklen_t alen = sizeof addr;
    int a = bind_udp_loopback(0);
    unsigned port;
    int b;

    ck_assert_int_ge(a, 0);
    ck_assert_int_eq(getsockname(a, (struct sockaddr *)&addr, &alen), 0);
    port = ntohs(addr.sin_port) & ~1u;
    close(a);
    a = bind_udp_loopback(port);
    b = bind_udp_loopback(port + 1);
    if (a >= 0) close(a);
    if (b >= 0) close(b);
    if (a >= 0 && b >= 0) return port;
  }
  ck_abort_msg("no free even/odd udp port pair");
  return 0;
}

static inline int open_sender(const char *group, unsigned port, struct sockaddr_in *dst) {
  int sock = socket(AF_INET, SOCK_DGRAM, 0);
  struct ip_mreqn mif;

  memset(&mif, 0, sizeof mif);
  mif.imr_ifindex = (int)if_nametoindex("lo");
  setsockopt(sock, IPPROTO_IP, IP_MULTICAST_IF, &mif, sizeof mif);
  memset(dst, 0, sizeof *dst);
  dst->sin_family = AF_INET;
  dst->sin_port = htons((unsigned short)port);
  inet_pton(AF_INET, group, &dst->sin_addr);
  return sock;
}

static inline int is_group(const struct sockaddr *sa) {
  const struct sockaddr_in *in = (const struct sockaddr_in *)sa;
  return sa && sa->sa_family == AF_INET && (ntohl(in->sin_addr.s_addr) >> 28) == 0xE;
}

static inline double mono(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static inline double now_seconds(void) {
  struct timespec ts;

  ck_assert_int_eq(clock_gettime(CLOCK_MONOTONIC, &ts), 0);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static inline double monotonic_seconds(void) {
  struct timespec ts;
  ck_assert_int_eq(clock_gettime(CLOCK_MONOTONIC, &ts), 0);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static inline void assert_closed_by_server(int fd) {
  double deadline = monotonic_seconds() + 2.0;
  for (;;) {
    struct pollfd pfd = {fd, POLLIN, 0};
    unsigned char buf[256];
    ssize_t rn;
    int remain_ms = (int)((deadline - monotonic_seconds()) * 1000.0);
    int pr;
    if (remain_ms <= 0)
      ck_abort_msg("fd %d: server never closed the connection", fd);
    pr = poll(&pfd, 1, remain_ms);
    ck_assert_msg(pr > 0, "fd %d: expected server-side close to be observable, poll returned %d", fd, pr);
    rn = read(fd, buf, sizeof buf);
    if (rn == 0)
      return;
    if (rn < 0 && errno == ECONNRESET)
      return;
    ck_assert_msg(rn > 0, "fd %d: unexpected read error, rn=%zd errno=%d", fd, rn, errno);
  }
}

#endif
