/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dipixy/reactor/conn.h"

static void nonblocking_socketpair(int fds[2]) {
  ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  ck_assert_int_eq(fcntl(fds[0], F_SETFL, O_NONBLOCK), 0);
  ck_assert_int_eq(fcntl(fds[1], F_SETFL, O_NONBLOCK), 0);
}

static int g_release_calls;
static void *g_release_last_arg;
static void count_release(void *arg) {
  g_release_calls++;
  g_release_last_arg = arg;
}

START_TEST(new_plain_conn_has_reading_state) {
  conn_t *c = conn_new(5, NULL);
  ck_assert_ptr_nonnull(c);
  ck_assert_int_eq(c->state, CONN_READING);
  ck_assert_int_eq(c->fd, 5);
  ck_assert_int_eq(c->slot, 0);
  ck_assert_int_eq(c->epfd, -1);
  ck_assert_int_eq(c->reactor_tid, -1);
  ck_assert_int_eq(c->zc_confirmed_id, -1);
  conn_free(c);
}
END_TEST

START_TEST(new_tls_conn_has_handshake_state) {
  int dummy_ssl = 1;
  conn_t *c = conn_new(5, &dummy_ssl);
  ck_assert_int_eq(c->state, CONN_TLS_HANDSHAKE);
  conn_free(c);
}
END_TEST

START_TEST(conn_free_null_is_a_noop) {
  conn_free(NULL);
}
END_TEST

START_TEST(queue_appends_to_out_buffer) {
  conn_t *c = conn_new(-1, NULL);
  ck_assert_int_eq(conn_queue(c, "hello", 5), 0);
  ck_assert_uint_eq(c->out.len, 5);
  ck_assert_uint_eq(c->out.off, 0);
  ck_assert_mem_eq(c->out.buf, "hello", 5);
  ck_assert_int_eq(conn_queue(c, "!", 1), 0);
  ck_assert_uint_eq(c->out.len, 6);
  ck_assert_mem_eq(c->out.buf, "hello!", 6);
  conn_free(c);
}
END_TEST

START_TEST(queue_zero_length_is_a_noop) {
  conn_t *c = conn_new(-1, NULL);
  ck_assert_int_eq(conn_queue(c, "x", 0), 0);
  ck_assert_uint_eq(c->out.len, 0);
  conn_free(c);
}
END_TEST

START_TEST(queue_zc_falls_back_to_copy_when_tls) {
  int dummy_ssl = 1;
  conn_t *c = conn_new(-1, &dummy_ssl);
  g_release_calls = 0;
  ck_assert_int_eq(conn_queue_zc(c, "hi", 2, count_release, c), 0);
  ck_assert_int_eq(c->zc.active, 0);
  ck_assert_uint_eq(c->out.len, 2);
  ck_assert_int_eq(g_release_calls, 1); /* release runs immediately on fallback */
  conn_free(c);
}
END_TEST

START_TEST(queue_zc_takes_zc_path_when_plain) {
  conn_t *c = conn_new(-1, NULL);
  static const char payload[] = "zcdata";
  g_release_calls = 0;
  ck_assert_int_eq(conn_queue_zc(c, payload, sizeof payload - 1, count_release, c), 0);
  ck_assert_int_eq(c->zc.active, 1);
  ck_assert_uint_eq(c->zc.len, sizeof payload - 1);
  ck_assert_int_eq(g_release_calls, 0); /* deferred until conn_flush drains it */
  c->zc.release(c->zc.release_arg);     /* simulate what conn_flush would do, avoid leak in this test */
  conn_free(c);
}
END_TEST

START_TEST(queue_zc_zero_length_releases_immediately) {
  conn_t *c = conn_new(-1, NULL);
  g_release_calls = 0;
  ck_assert_int_eq(conn_queue_zc(c, "x", 0, count_release, c), 0);
  ck_assert_int_eq(c->zc.active, 0);
  ck_assert_int_eq(g_release_calls, 1);
  conn_free(c);
}
END_TEST

START_TEST(in_reserve_grows_and_compacts) {
  conn_t *c = conn_new(-1, NULL);
  ck_assert_int_eq(conn_in_reserve(c, 100), 0);
  ck_assert(c->in.cap >= 100);

  /* simulate a partially-consumed request buffer: off=3, len=6 -> "def" remains */
  memcpy(c->in.buf, "abcdef", 6);
  c->in.len = 6;
  c->in.off = 3;
  ck_assert_int_eq(conn_in_reserve(c, 50), 0);
  ck_assert_uint_eq(c->in.off, 0);
  ck_assert_uint_eq(c->in.len, 3);
  ck_assert_mem_eq(c->in.buf, "def", 3);
  ck_assert(c->in.cap >= 53);

  conn_free(c);
}
END_TEST

START_TEST(epoll_mod_arms_requested_events) {
  int epfd = epoll_create1(0);
  int fds[2];
  struct epoll_event ev, out_ev;
  conn_t *c;

  nonblocking_socketpair(fds);
  c = conn_new(fds[0], NULL);

  memset(&ev, 0, sizeof ev);
  ev.events = EPOLLIN;
  ev.data.ptr = c;
  ck_assert_int_eq(epoll_ctl(epfd, EPOLL_CTL_ADD, c->fd, &ev), 0);

  ck_assert_int_eq(conn_epoll_mod(c, epfd, 1), 0);
  ck_assert_int_eq(epoll_wait(epfd, &out_ev, 1, 100), 1);
  ck_assert(out_ev.events & EPOLLOUT);
  ck_assert_ptr_eq(out_ev.data.ptr, c);

  c->read_done = 1;
  ck_assert_int_eq(conn_epoll_mod(c, epfd, 0), 0);
  ck_assert_int_eq(epoll_wait(epfd, &out_ev, 1, 50), 0); /* neither EPOLLIN nor EPOLLOUT armed */

  close(fds[0]);
  close(fds[1]);
  close(epfd);
  conn_free(c);
}
END_TEST

START_TEST(epoll_mod_on_unadded_fd_fails) {
  int epfd = epoll_create1(0);
  int fds[2];
  conn_t *c;

  nonblocking_socketpair(fds);
  c = conn_new(fds[0], NULL);
  ck_assert_int_eq(conn_epoll_mod(c, epfd, 1), -1); /* never EPOLL_CTL_ADD'd: ENOENT */

  close(fds[0]);
  close(fds[1]);
  close(epfd);
  conn_free(c);
}
END_TEST

START_TEST(flush_small_payload_completes_immediately) {
  int fds[2];
  conn_t *c;
  char rbuf[16];
  ssize_t n;

  nonblocking_socketpair(fds);
  c = conn_new(fds[0], NULL);
  conn_queue(c, "ping", 4);

  ck_assert_int_eq(conn_flush(c, -1), CONN_FLUSH_DONE);
  ck_assert_uint_eq(c->out.len, 0);
  ck_assert_uint_eq(c->out.off, 0);

  n = recv(fds[1], rbuf, sizeof rbuf, 0);
  ck_assert_int_eq(n, 4);
  ck_assert_mem_eq(rbuf, "ping", 4);

  close(fds[0]);
  close(fds[1]);
  conn_free(c);
}
END_TEST

START_TEST(flush_full_send_buffer_arms_epollout_then_drains) {
  int epfd = epoll_create1(0);
  int fds[2];
  conn_t *c;
  struct epoll_event ev;
  char *big;
  size_t biglen = 4 * 1024 * 1024;
  int sndbuf = 4096;
  int rc;

  nonblocking_socketpair(fds);
  ck_assert_int_eq(setsockopt(fds[0], SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof sndbuf), 0);

  c = conn_new(fds[0], NULL);
  memset(&ev, 0, sizeof ev);
  ev.events = EPOLLIN;
  ev.data.ptr = c;
  ck_assert_int_eq(epoll_ctl(epfd, EPOLL_CTL_ADD, c->fd, &ev), 0);

  big = malloc(biglen);
  memset(big, 'a', biglen);
  conn_queue(c, big, biglen);
  free(big);

  rc = conn_flush(c, epfd);
  ck_assert_int_eq(rc, CONN_FLUSH_MORE);
  ck_assert_int_eq(c->want_write, 1);

  /* drain the peer so the kernel buffer empties, then finish flushing */
  {
    char drain[65536];
    ssize_t got;
    int drained_any = 0;
    for (;;) {
      got = recv(fds[1], drain, sizeof drain, MSG_DONTWAIT);
      if (got <= 0)
        break;
      drained_any += (int)got;
    }
    ck_assert(drained_any > 0);
  }

  while ((rc = conn_flush(c, epfd)) == CONN_FLUSH_MORE) {
    char drain[65536];
    ssize_t got;
    while ((got = recv(fds[1], drain, sizeof drain, MSG_DONTWAIT)) > 0) {
    }
  }
  ck_assert_int_eq(rc, CONN_FLUSH_DONE);
  ck_assert_int_eq(c->want_write, 0);

  close(fds[0]);
  close(fds[1]);
  close(epfd);
  conn_free(c);
}
END_TEST

START_TEST(table_lifecycle_publish_unpublish) {
  conn_t *c;
  ck_assert_int_eq(conn_table_init(16), 0);
  ck_assert_ptr_null(conn_for_fd(3));

  c = conn_new(3, NULL);
  conn_publish(c);
  ck_assert_ptr_eq(conn_for_fd(3), c);

  conn_unpublish(c);
  ck_assert_ptr_null(conn_for_fd(3));

  conn_free(c);
}
END_TEST

START_TEST(table_ignores_out_of_range_fd) {
  conn_t *c;
  ck_assert_int_eq(conn_table_init(4), 0);
  c = conn_new(99, NULL); /* out of [0,4) range */
  conn_publish(c);        /* must not crash, silently ignored */
  ck_assert_ptr_null(conn_for_fd(99));
  conn_free(c);
}
END_TEST

START_TEST(table_uninitialized_for_fd_is_null) {
  ck_assert_ptr_null(conn_for_fd(0));
}
END_TEST

START_TEST(request_close_without_epfd_just_sets_flags) {
  conn_t *c = conn_new(-1, NULL);
  conn_request_close(c);
  ck_assert_int_eq(c->close_after_flush, 1);
  ck_assert_int_eq(c->requested_close, 1);
  ck_assert_int_eq(c->want_write, 0); /* epfd < 0: no epoll touched */
  conn_free(c);
}
END_TEST

START_TEST(request_close_with_epfd_arms_epollout) {
  int epfd = epoll_create1(0);
  int fds[2];
  conn_t *c;
  struct epoll_event ev, out_ev;

  nonblocking_socketpair(fds);
  c = conn_new(fds[0], NULL);
  c->epfd = epfd;
  memset(&ev, 0, sizeof ev);
  ev.events = EPOLLIN;
  ev.data.ptr = c;
  ck_assert_int_eq(epoll_ctl(epfd, EPOLL_CTL_ADD, c->fd, &ev), 0);

  conn_request_close(c);
  ck_assert_int_eq(c->want_write, 1);
  ck_assert_int_eq(epoll_wait(epfd, &out_ev, 1, 100), 1);
  ck_assert(out_ev.events & EPOLLOUT);

  close(fds[0]);
  close(fds[1]);
  close(epfd);
  conn_free(c);
}
END_TEST

START_TEST(request_close_twice_skips_second_epoll_ctl) {
  int epfd = epoll_create1(0);
  int fds[2];
  conn_t *c;
  struct epoll_event ev;
  nonblocking_socketpair(fds);
  c = conn_new(fds[0], NULL);
  c->epfd = epfd;
  memset(&ev, 0, sizeof ev);
  ev.events = EPOLLIN;
  ev.data.ptr = c;
  ck_assert_int_eq(epoll_ctl(epfd, EPOLL_CTL_ADD, c->fd, &ev), 0);
  conn_request_close(c);
  ck_assert_int_eq(c->want_write, 1);
  epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
  conn_request_close(c); /* already armed: must not touch epoll_ctl */
  ck_assert_int_eq(c->want_write, 1);
  ck_assert_int_eq(c->close_after_flush, 1);

  close(fds[0]);
  close(fds[1]);
  close(epfd);
  conn_free(c);
}
END_TEST

START_TEST(send_buffered_queues_both_chunks) {
  conn_t *c = conn_new(-1, NULL);
  ck_assert_int_eq(conn_send_buffered(c, "ab", 2, "cde", 3), 0);
  ck_assert_uint_eq(c->out.len, 5);
  ck_assert_mem_eq(c->out.buf, "abcde", 5);
  ck_assert_int_eq(c->dead, 0);
  conn_free(c);
}
END_TEST

START_TEST(send_buffered_over_cap_marks_dead_without_queuing) {
  conn_t *c = conn_new(-1, NULL);
  char *huge = malloc(5u * 1024 * 1024);
  memset(huge, 'x', 5u * 1024 * 1024);
  ck_assert_int_eq(conn_send_buffered(c, huge, 5u * 1024 * 1024, NULL, 0), -1);
  ck_assert_int_eq(c->dead, 1);
  ck_assert_uint_eq(c->out.len, 0);
  free(huge);
  conn_free(c);
}
END_TEST

START_TEST(send_buffered_on_dead_conn_short_circuits) {
  conn_t *c = conn_new(-1, NULL);
  c->dead = 1;
  ck_assert_int_eq(conn_send_buffered(c, "x", 1, NULL, 0), -1);
  ck_assert_uint_eq(c->out.len, 0); /* never touched */
  conn_free(c);
}
END_TEST

START_TEST(claim_teardown_wins_once) {
  conn_t *c = conn_new(-1, NULL);
  ck_assert_int_eq(conn_claim_teardown(c), 1);
  ck_assert_int_eq(c->dead, 1);
  ck_assert_int_eq(conn_claim_teardown(c), 0); /* already claimed */
  conn_free(c);
}
END_TEST

static Suite *conn_suite(void) {
  Suite *s = suite_create("dipixy_conn");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, new_plain_conn_has_reading_state);
  tcase_add_test(tc, new_tls_conn_has_handshake_state);
  tcase_add_test(tc, conn_free_null_is_a_noop);
  tcase_add_test(tc, queue_appends_to_out_buffer);
  tcase_add_test(tc, queue_zero_length_is_a_noop);
  tcase_add_test(tc, queue_zc_falls_back_to_copy_when_tls);
  tcase_add_test(tc, queue_zc_takes_zc_path_when_plain);
  tcase_add_test(tc, queue_zc_zero_length_releases_immediately);
  tcase_add_test(tc, in_reserve_grows_and_compacts);
  tcase_add_test(tc, epoll_mod_arms_requested_events);
  tcase_add_test(tc, epoll_mod_on_unadded_fd_fails);
  tcase_add_test(tc, flush_small_payload_completes_immediately);
  tcase_add_test(tc, flush_full_send_buffer_arms_epollout_then_drains);
  tcase_add_test(tc, table_lifecycle_publish_unpublish);
  tcase_add_test(tc, table_ignores_out_of_range_fd);
  tcase_add_test(tc, table_uninitialized_for_fd_is_null);
  tcase_add_test(tc, request_close_without_epfd_just_sets_flags);
  tcase_add_test(tc, request_close_with_epfd_arms_epollout);
  tcase_add_test(tc, request_close_twice_skips_second_epoll_ctl);
  tcase_add_test(tc, send_buffered_queues_both_chunks);
  tcase_add_test(tc, send_buffered_over_cap_marks_dead_without_queuing);
  tcase_add_test(tc, send_buffered_on_dead_conn_short_circuits);
  tcase_add_test(tc, claim_teardown_wins_once);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(conn_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
