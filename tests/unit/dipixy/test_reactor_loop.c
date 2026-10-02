/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dipixy/reactor/conn.h"
#include "dipixy/dash/lldash.h"
#include "dipixy/reactor/internal.h"
#include "dipixy/segment/mp4push.h"
#include "dipixy/ts/ts_push.h"

#define EVENTS_MAX 16
#define DRIVE_ROUNDS 50
#define REPLY_MAX 512

typedef struct {
  int epfd;
  reactor_listeners_t rl;
  config_t cfg;
} loop_t;

static void loop_open(loop_t *l) {
  memset(l, 0, sizeof *l);
  l->epfd = epoll_create1(0);
  ck_assert_int_ge(l->epfd, 0);
  t_reactor_epfd = l->epfd;
  conn_table_init(1024);
  ts_push_init(0, 8);
  dash_lldash_init(8);
  mp4push_init(8);
  reactor_set_context(&l->cfg, NULL, NULL);
}

static void loop_close(const loop_t *l) {
  close(l->epfd);
}

static void drive(loop_t *l, int rounds) {
  for (int i = 0; i < rounds; i++) {
    struct epoll_event evs[EVENTS_MAX];
    int n = epoll_wait(l->epfd, evs, EVENTS_MAX, 5);

    for (int k = 0; k < n; k++) reactor_handle_event(l->epfd, &l->rl, 0, &evs[k]);
  }
}

static void add_listener(loop_t *l, int fd, reactor_listener_kind kind) {
  reactor_listener *lp = &l->rl.L[l->rl.nL++];
  struct epoll_event ev = {.events = EPOLLIN, .data.ptr = lp};

  lp->fd = fd;
  lp->is_tls = 0;
  lp->kind = kind;
  ck_assert_int_eq(epoll_ctl(l->epfd, EPOLL_CTL_ADD, fd, &ev), 0);
}

static conn_t *add_conn(const loop_t *l, int fd, conn_state state, int publish) {
  conn_t *c = conn_new(fd, NULL);
  struct epoll_event ev = {.events = EPOLLIN, .data.ptr = c};

  ck_assert_ptr_nonnull(c);
  c->state = state;
  c->epfd = l->epfd;
  if (publish) conn_publish(c);
  ck_assert_int_eq(epoll_ctl(l->epfd, EPOLL_CTL_ADD, fd, &ev), 0);
  return c;
}

static void pair(int sv[2]) {
  ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv), 0);
}

static int fd_is_open(int fd) {
  return fcntl(fd, F_GETFD) != -1;
}

static void feed_event(loop_t *l, void *ptr, uint32_t events) {
  struct epoll_event ev = {.events = events, .data.ptr = ptr};

  reactor_handle_event(l->epfd, &l->rl, 0, &ev);
}

START_TEST(wakeup_eventfds_are_drained_by_their_listener_kind) {
  static const reactor_listener_kind kinds[] = {RL_TSPUSH_EFD, RL_DASHCHUNK_EFD, RL_MP4PUSH_EFD};
  loop_t l;
  int efd = eventfd(0, EFD_NONBLOCK);
  uint64_t one = 1;
  uint64_t got = 0;

  ck_assert_int_ge(efd, 0);
  loop_open(&l);
  add_listener(&l, efd, kinds[_i]);
  ck_assert_int_eq((int)write(efd, &one, sizeof one), (int)sizeof one);
  feed_event(&l, &l.rl.L[0], EPOLLIN);
  ck_assert_int_eq((int)read(efd, &got, sizeof got), -1);
  ck_assert_int_eq(errno, EAGAIN);
  close(efd);
  loop_close(&l);
}
END_TEST

START_TEST(accepted_connection_is_read_and_answered_through_the_loop) {
  loop_t l;
  int lfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  struct sockaddr_in a = {0};
  socklen_t alen = sizeof a;
  int cfd;
  char reply[REPLY_MAX] = {0};
  static const char req[] = "GET /no/such/route HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
  ssize_t n = 0;

  ck_assert_int_ge(lfd, 0);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(lfd, (struct sockaddr *)&a, sizeof a), 0);
  ck_assert_int_eq(listen(lfd, 4), 0);
  ck_assert_int_eq(getsockname(lfd, (struct sockaddr *)&a, &alen), 0);
  loop_open(&l);
  add_listener(&l, lfd, RL_ACCEPT);
  cfd = socket(AF_INET, SOCK_STREAM, 0);
  ck_assert_int_eq(connect(cfd, (struct sockaddr *)&a, sizeof a), 0);
  ck_assert_int_eq((int)write(cfd, req, sizeof req - 1), (int)sizeof req - 1);
  for (int i = 0; i < DRIVE_ROUNDS && n <= 0; i++) {
    struct pollfd pfd = {.fd = cfd, .events = POLLIN};

    drive(&l, 1);
    if (poll(&pfd, 1, 1) > 0) n = read(cfd, reply, sizeof reply - 1);
  }
  ck_assert_int_gt((int)n, 0);
  ck_assert_ptr_nonnull(strstr(reply, "404"));
  close(cfd);
  drive(&l, 3);
  close(lfd);
  loop_close(&l);
}
END_TEST

typedef struct {
  const char *name;
  conn_state state;
  int published;
} state_case_t;

static const state_case_t hup_states[] = {
    {"reading", CONN_READING, 0},
    {"writing", CONN_WRITING, 0},
    {"tspush", CONN_TSPUSH, 1},
    {"websocket", CONN_WS, 1},
    {"dash chunk", CONN_DASHCHUNK, 1},
    {"mp4 push", CONN_MP4PUSH, 1},
};

START_TEST(hangup_without_input_closes_the_connection_for_every_state) {
  const state_case_t *sc = &hup_states[_i];
  loop_t l;
  int sv[2];
  conn_t *c;
  int fd;

  loop_open(&l);
  pair(sv);
  c = add_conn(&l, sv[0], sc->state, sc->published);
  fd = sv[0];
  feed_event(&l, c, EPOLLHUP);
  ck_assert_int_eq(fd_is_open(fd), 0);
  if (sc->published) ck_assert_ptr_null(conn_for_fd(fd));
  close(sv[1]);
  loop_close(&l);
}
END_TEST

START_TEST(hangup_with_pending_input_still_reads_first) {
  loop_t l;
  int sv[2];
  conn_t *c;

  loop_open(&l);
  pair(sv);
  c = add_conn(&l, sv[0], CONN_READING, 1);
  feed_event(&l, c, EPOLLHUP | EPOLLIN);
  ck_assert_ptr_nonnull(conn_for_fd(sv[0]));
  ck_assert_int_eq(fd_is_open(sv[0]), 1);
  conn_unpublish(c);
  close(sv[0]);
  close(sv[1]);
  conn_free(c);
  loop_close(&l);
}
END_TEST

START_TEST(parked_blocking_reload_connections_ignore_socket_events) {
  loop_t l;
  int sv[2];
  conn_t *c;

  loop_open(&l);
  pair(sv);
  c = add_conn(&l, sv[0], CONN_DISPATCH, 1);
  feed_event(&l, c, EPOLLIN);
  feed_event(&l, c, EPOLLOUT);
  ck_assert_int_eq(c->state, CONN_DISPATCH);
  ck_assert_ptr_nonnull(conn_for_fd(sv[0]));
  conn_unpublish(c);
  close(sv[0]);
  close(sv[1]);
  conn_free(c);
  loop_close(&l);
}
END_TEST

START_TEST(writing_connections_flush_their_queue_and_close_when_asked) {
  loop_t l;
  int sv[2];
  conn_t *c;
  char buf[16] = {0};

  loop_open(&l);
  pair(sv);
  c = add_conn(&l, sv[0], CONN_WRITING, 0);
  ck_assert_int_eq(conn_queue(c, "hello", 5), 0);
  c->close_after_flush = 1;
  feed_event(&l, c, EPOLLOUT);
  ck_assert_int_eq((int)read(sv[1], buf, sizeof buf), 5);
  ck_assert_str_eq(buf, "hello");
  ck_assert_int_eq(fd_is_open(sv[0]), 0);
  close(sv[1]);
  loop_close(&l);
}
END_TEST

START_TEST(error_events_without_a_socket_error_fall_through_to_the_state_handler) {
  loop_t l;
  int sv[2];
  conn_t *c;

  loop_open(&l);
  pair(sv);
  c = add_conn(&l, sv[0], CONN_READING, 1);
  feed_event(&l, c, EPOLLERR);
  ck_assert_ptr_nonnull(conn_for_fd(sv[0]));
  ck_assert_int_eq(fd_is_open(sv[0]), 1);
  conn_unpublish(c);
  close(sv[0]);
  close(sv[1]);
  conn_free(c);
  loop_close(&l);
}
END_TEST

static Suite *loop_suite(void) {
  Suite *s = suite_create("dipixy_reactor_loop");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 20);
  tcase_add_loop_test(tc, wakeup_eventfds_are_drained_by_their_listener_kind, 0, 3);
  tcase_add_test(tc, accepted_connection_is_read_and_answered_through_the_loop);
  tcase_add_loop_test(tc, hangup_without_input_closes_the_connection_for_every_state, 0, (int)(sizeof hup_states / sizeof hup_states[0]));
  tcase_add_test(tc, hangup_with_pending_input_still_reads_first);
  tcase_add_test(tc, parked_blocking_reload_connections_ignore_socket_events);
  tcase_add_test(tc, writing_connections_flush_their_queue_and_close_when_asked);
  tcase_add_test(tc, error_events_without_a_socket_error_fall_through_to_the_state_handler);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(loop_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
