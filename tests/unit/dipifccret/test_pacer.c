/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "dipifccret/run/run.h"
#include "lib/sys/signal.h"

#define TEST_PORT 19345
#define RTX_HDR_LEN 14

static channel_table_t *g_table;

static channel_t *lookup_ip(channel_table_t *t, const char *ip, unsigned port) {
  unsigned char addr[4];
  inet_pton(AF_INET, ip, addr);
  return channel_lookup(t, AF_INET, addr, sizeof addr, port);
}

static channel_t *make_channel_ready_to_burst(void) {
  channel_t *c;
  unsigned char pkt[188];
  g_table = channel_table_new(1, 0, 8);
  c = lookup_ip(g_table, "239.1.1.1", 5000);
  memset(pkt, 0xAB, sizeof pkt);
  pkt[0] = 0x47;
  atomic_store_explicit(&c->cache.have_rap, 1, memory_order_relaxed);
  atomic_store_explicit(&c->nominal_bps, 50000000.0, memory_order_relaxed);
  channel_store(g_table, c, 0x1234, 1, 0, 0, pkt, sizeof pkt);
  return c;
}

static void addr_of(struct sockaddr_in *sin, unsigned port) {
  memset(sin, 0, sizeof *sin);
  sin->sin_family = AF_INET;
  sin->sin_port = htons((unsigned short)port);
  inet_pton(AF_INET, "127.0.0.1", &sin->sin_addr);
}

static int open_bound_udp(unsigned port) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in sin;
  addr_of(&sin, port);
  if (fd < 0) return -1;
  if (bind(fd, (const struct sockaddr *)&sin, sizeof sin) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static ssize_t recv_wait(int fd, unsigned char *buf, size_t cap, double timeout_s) {
  struct timespec deadline;
  clock_gettime(CLOCK_MONOTONIC, &deadline);
  deadline.tv_sec += (time_t)timeout_s;
  for (;;) {
    ssize_t n = recv(fd, buf, cap, MSG_DONTWAIT);
    struct timespec now;
    if (n >= 0) return n;
    if (errno != EAGAIN && errno != EWOULDBLOCK) return -1;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec > deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec > deadline.tv_nsec))
      return -1;
    usleep(2000);
  }
}

START_TEST(pacer_delivers_cached_packet_to_client) {
  burst_table_t *t = burst_table_new(1);
  channel_t *c = make_channel_ready_to_burst();
  burst_t *b = burst_new(c, 1.0, 0, 96);
  struct sockaddr_in sin;
  int recv_fd, send_fd;
  pthread_t th;
  pacer_ctx_t pc;
  unsigned char buf[256];
  ssize_t n;

  recv_fd = open_bound_udp(TEST_PORT);
  ck_assert_int_ge(recv_fd, 0);
  send_fd = socket(AF_INET, SOCK_DGRAM, 0);
  ck_assert_int_ge(send_fd, 0);

  addr_of(&sin, TEST_PORT);
  ck_assert_ptr_nonnull(burst_table_claim(t, (const struct sockaddr *)&sin, sizeof sin, send_fd, b));

  pc = (pacer_ctx_t){.bursts = t, .duration_cap_ms = 5000};
  ck_assert_int_eq(pthread_create(&th, NULL, pacer_main, &pc), 0);

  n = recv_wait(recv_fd, buf, sizeof buf, 2.0);
  ck_assert_int_eq((int)n, RTX_HDR_LEN + 188);
  ck_assert_int_eq(buf[RTX_HDR_LEN], 0x47);
  ck_assert_int_eq(buf[RTX_HDR_LEN + 1], 0xAB);
  ck_assert_int_eq(buf[RTX_HDR_LEN + 187], 0xAB);

  signals_install();
  raise(SIGTERM);
  pthread_join(th, NULL);

  close(recv_fd);
  close(send_fd);
  burst_table_free(t);
  channel_table_free(g_table);
}
END_TEST

START_TEST(pacer_exits_promptly_on_stop_with_no_active_bursts) {
  burst_table_t *t = burst_table_new(1);
  pthread_t th;
  pacer_ctx_t pc;
  struct timespec t0;
  struct timespec t1;
  double elapsed_s;

  pc = (pacer_ctx_t){.bursts = t, .duration_cap_ms = 1000};
  ck_assert_int_eq(pthread_create(&th, NULL, pacer_main, &pc), 0);

  usleep(50000);

  signals_install();
  clock_gettime(CLOCK_MONOTONIC, &t0);
  raise(SIGTERM);
  pthread_join(th, NULL);
  clock_gettime(CLOCK_MONOTONIC, &t1);

  elapsed_s = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
  ck_assert_double_lt(elapsed_s, 0.5);

  burst_table_free(t);
}
END_TEST

static Suite *pacer_suite(void) {
  Suite *s = suite_create("dipifccret_pacer");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 10);
  tcase_add_test(tc, pacer_delivers_cached_packet_to_client);
  tcase_add_test(tc, pacer_exits_promptly_on_stop_with_no_active_bursts);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(pacer_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
