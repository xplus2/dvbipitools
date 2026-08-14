/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "dipifccret/listen.h"
#include "lib/signal.h"

#define TEST_PORT 19245

typedef struct {
  pthread_mutex_t mu;
  int count;
  size_t last_len;
  unsigned char last_pkt[64];
  struct sockaddr_storage last_from;
  socklen_t last_fromlen;
  size_t last_slot;
} recorder_t;

static void recorder_init(recorder_t *r) {
  memset(r, 0, sizeof *r);
  pthread_mutex_init(&r->mu, NULL);
}

static void recv_cb(const unsigned char *pkt, size_t len, int fd, const struct sockaddr *from, socklen_t fromlen, void *user) {
  recorder_t *r = user;
  (void)fd;
  pthread_mutex_lock(&r->mu);
  r->count++;
  r->last_len = len < sizeof r->last_pkt ? len : sizeof r->last_pkt;
  memcpy(r->last_pkt, pkt, r->last_len);
  memcpy(&r->last_from, from, fromlen);
  r->last_fromlen = fromlen;
  pthread_mutex_unlock(&r->mu);
}

static int recorder_wait_count(recorder_t *r, int want, double timeout_s) {
  struct timespec deadline;
  clock_gettime(CLOCK_MONOTONIC, &deadline);
  deadline.tv_sec += (time_t)timeout_s;
  for (;;) {
    struct timespec now;
    int cur;
    pthread_mutex_lock(&r->mu);
    cur = r->count;
    pthread_mutex_unlock(&r->mu);
    if (cur >= want)
      return 1;
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec > deadline.tv_sec || (now.tv_sec == deadline.tv_sec && now.tv_nsec > deadline.tv_nsec))
      return 0;
    usleep(2000);
  }
}

static void recv_multi_cb(const unsigned char *pkt, size_t len, size_t slot, int fd, const struct sockaddr *from, socklen_t fromlen, void *user) {
  recorder_t *r = user;
  (void)fd;
  pthread_mutex_lock(&r->mu);
  r->count++;
  r->last_len = len < sizeof r->last_pkt ? len : sizeof r->last_pkt;
  memcpy(r->last_pkt, pkt, r->last_len);
  memcpy(&r->last_from, from, fromlen);
  r->last_fromlen = fromlen;
  r->last_slot = slot;
  pthread_mutex_unlock(&r->mu);
}

static int send_udp(unsigned port, const unsigned char *data, size_t len) {
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  struct sockaddr_in dst;
  ssize_t n;
  memset(&dst, 0, sizeof dst);
  dst.sin_family = AF_INET;
  dst.sin_port = htons((unsigned short)port);
  inet_pton(AF_INET, "127.0.0.1", &dst.sin_addr);
  n = sendto(fd, data, len, 0, (const struct sockaddr *)&dst, sizeof dst);
  close(fd);
  return n == (ssize_t)len;
}

START_TEST(listen_pool_delivers_datagram_to_callback) {
  recorder_t rec;
  listen_pool_t *p;
  const unsigned char msg[] = "hello listen pool";

  recorder_init(&rec);
  p = listen_pool_start(AF_INET, "127.0.0.1", TEST_PORT, 2, recv_cb, &rec);
  ck_assert_ptr_nonnull(p);

  ck_assert_int_eq(send_udp(TEST_PORT, msg, sizeof msg - 1), 1);
  ck_assert_int_eq(recorder_wait_count(&rec, 1, 2.0), 1);
  ck_assert_uint_eq(rec.last_len, sizeof msg - 1);
  ck_assert_mem_eq(rec.last_pkt, msg, sizeof msg - 1);
  ck_assert_int_eq(rec.last_from.ss_family, AF_INET);

  signals_install();
  raise(SIGTERM); /* listen_pool_stop() only returns once signal_stop_requested() is true */
  listen_pool_stop(p);
}
END_TEST

START_TEST(listen_pool_delivers_multiple_datagrams) {
  recorder_t rec;
  listen_pool_t *p;
  const unsigned char a[] = "one";
  const unsigned char b[] = "two-longer";

  recorder_init(&rec);
  p = listen_pool_start(AF_INET, "127.0.0.1", TEST_PORT + 1, 2, recv_cb, &rec);
  ck_assert_ptr_nonnull(p);

  ck_assert_int_eq(send_udp(TEST_PORT + 1, a, sizeof a - 1), 1);
  ck_assert_int_eq(send_udp(TEST_PORT + 1, b, sizeof b - 1), 1);
  ck_assert_int_eq(recorder_wait_count(&rec, 2, 2.0), 1);

  signals_install();
  raise(SIGTERM);
  listen_pool_stop(p);
}
END_TEST

START_TEST(listen_pool_start_fails_on_bad_address) {
  listen_pool_t *p = listen_pool_start(AF_INET, "not-an-address", TEST_PORT + 2, 1, NULL, NULL);
  ck_assert_ptr_null(p);
}
END_TEST

START_TEST(listen_multi_delivers_datagram_with_correct_slot) {
  recorder_t rec;
  listen_multi_t *p;
  const unsigned char msg[] = "hello listen multi";

  recorder_init(&rec);
  p = listen_multi_start(AF_INET, "127.0.0.1", TEST_PORT + 3, 4, recv_multi_cb, &rec);
  ck_assert_ptr_nonnull(p);

  ck_assert_int_eq(send_udp(TEST_PORT + 3 + 2, msg, sizeof msg - 1), 1); /* slot 2's dedicated port */
  ck_assert_int_eq(recorder_wait_count(&rec, 1, 2.0), 1);
  ck_assert_uint_eq(rec.last_len, sizeof msg - 1);
  ck_assert_mem_eq(rec.last_pkt, msg, sizeof msg - 1);
  ck_assert_uint_eq(rec.last_slot, 2);

  signals_install();
  raise(SIGTERM);
  listen_multi_stop(p);
}
END_TEST

START_TEST(listen_multi_distinguishes_slots) {
  recorder_t rec;
  listen_multi_t *p;
  const unsigned char a[] = "a";
  const unsigned char b[] = "b";

  recorder_init(&rec);
  p = listen_multi_start(AF_INET, "127.0.0.1", TEST_PORT + 10, 4, recv_multi_cb, &rec);
  ck_assert_ptr_nonnull(p);

  ck_assert_int_eq(send_udp(TEST_PORT + 10 + 0, a, sizeof a - 1), 1);
  ck_assert_int_eq(recorder_wait_count(&rec, 1, 2.0), 1);
  ck_assert_uint_eq(rec.last_slot, 0);

  ck_assert_int_eq(send_udp(TEST_PORT + 10 + 3, b, sizeof b - 1), 1);
  ck_assert_int_eq(recorder_wait_count(&rec, 2, 2.0), 1);
  ck_assert_uint_eq(rec.last_slot, 3);

  signals_install();
  raise(SIGTERM);
  listen_multi_stop(p);
}
END_TEST

START_TEST(listen_multi_start_fails_on_bad_address) {
  listen_multi_t *p = listen_multi_start(AF_INET, "not-an-address", TEST_PORT + 20, 4, NULL, NULL);
  ck_assert_ptr_null(p);
}
END_TEST

START_TEST(listen_multi_start_fails_on_zero_count) {
  listen_multi_t *p = listen_multi_start(AF_INET, "127.0.0.1", TEST_PORT + 21, 0, NULL, NULL);
  ck_assert_ptr_null(p);
}
END_TEST

static Suite *listen_suite(void) {
  Suite *s = suite_create("dipifccret_listen");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 10);
  tcase_add_test(tc, listen_pool_delivers_datagram_to_callback);
  tcase_add_test(tc, listen_pool_delivers_multiple_datagrams);
  tcase_add_test(tc, listen_pool_start_fails_on_bad_address);
  tcase_add_test(tc, listen_multi_delivers_datagram_with_correct_slot);
  tcase_add_test(tc, listen_multi_distinguishes_slots);
  tcase_add_test(tc, listen_multi_start_fails_on_bad_address);
  tcase_add_test(tc, listen_multi_start_fails_on_zero_count);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(listen_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
