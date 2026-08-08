/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "dipiradiohead/input/source.h"

typedef struct {
  int listen_fd;
  const char *response;
  size_t response_len;
} server_arg_t;

static void *serve_once(void *arg) {
  server_arg_t *a = arg;
  int cfd = accept(a->listen_fd, NULL, NULL);
  struct timeval tv = {2, 0};
  char buf[4096];
  size_t got = 0;

  if (cfd < 0)
    return NULL;
  setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  for (;;) {
    ssize_t n = recv(cfd, buf + got, sizeof buf - got, 0);
    if (n <= 0)
      break;
    got += (size_t)n;
    if (got >= 4 && memcmp(buf + got - 4, "\r\n\r\n", 4) == 0)
      break;
  }
  send(cfd, a->response, a->response_len, 0);
  close(cfd);
  return NULL;
}

static int make_listener(unsigned *port_out) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(fd, 1), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  *port_out = ntohs(addr.sin_port);
  return fd;
}

static source_open_state_t drive(source_open_t *o, int max_iters) {
  source_open_state_t st = SOURCE_OPEN_PENDING;
  int i;

  for (i = 0; i < max_iters && st == SOURCE_OPEN_PENDING; i++) {
    struct pollfd pfd;
    pfd.fd = source_open_async_poll_fd(o);
    pfd.events = source_open_async_poll_events(o);
    pfd.revents = 0;
    poll(&pfd, 1, 100);
    st = source_open_async_step(o, NULL);
  }
  return st;
}

static void noop_meta_cb(void *ctx, const char *artist, const char *title) {
  (void)ctx;
  (void)artist;
  (void)title;
}

START_TEST(source_open_async_completes_for_plain_body) {
  unsigned port;
  int listen_fd = make_listener(&port);
  pthread_t th;
  server_arg_t sarg;
  const char *resp = "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nConnection: close\r\n\r\nnot-really-audio-but-thats-ok-here";
  char uri[64];
  source_open_t *o;
  source_t *s;

  sarg.listen_fd = listen_fd;
  sarg.response = resp;
  sarg.response_len = strlen(resp);
  ck_assert_int_eq(pthread_create(&th, NULL, serve_once, &sarg), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/stream", port);
  o = source_open_async_start(uri, 0, noop_meta_cb, NULL, NULL);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive(o, 200), SOURCE_OPEN_DONE);

  s = source_open_async_take(o);
  ck_assert_ptr_nonnull(s);

  source_close(s);
  pthread_join(th, NULL);
  close(listen_fd);
}
END_TEST

START_TEST(source_open_async_follows_playlist_redirect) {
  unsigned port_a, port_b;
  int listen_a = make_listener(&port_a);
  int listen_b = make_listener(&port_b);
  pthread_t th_a, th_b;
  server_arg_t sarg_a, sarg_b;
  char resp_a[256];
  const char *resp_b = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nsecond-server-body";
  char uri[64];
  source_open_t *o;
  source_t *s;

  /* m3u playlist body: any non-'#' line starting with http:// is followed, no header needed */
  snprintf(resp_a, sizeof resp_a, "HTTP/1.1 200 OK\r\nContent-Type: audio/x-mpegurl\r\nConnection: close\r\n\r\nhttp://127.0.0.1:%u/next\n", port_b);
  sarg_a.listen_fd = listen_a;
  sarg_a.response = resp_a;
  sarg_a.response_len = strlen(resp_a);
  sarg_b.listen_fd = listen_b;
  sarg_b.response = resp_b;
  sarg_b.response_len = strlen(resp_b);
  ck_assert_int_eq(pthread_create(&th_a, NULL, serve_once, &sarg_a), 0);
  ck_assert_int_eq(pthread_create(&th_b, NULL, serve_once, &sarg_b), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/playlist.m3u", port_a);
  o = source_open_async_start(uri, 0, noop_meta_cb, NULL, NULL);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive(o, 200), SOURCE_OPEN_DONE);

  s = source_open_async_take(o);
  ck_assert_ptr_nonnull(s);

  source_close(s);
  pthread_join(th_a, NULL);
  pthread_join(th_b, NULL);
  close(listen_a);
  close(listen_b);
}
END_TEST

START_TEST(source_open_async_reports_error_on_refused_connection) {
  source_open_t *o = source_open_async_start("http://127.0.0.1:1/nothing", 0, noop_meta_cb, NULL, NULL); /* port 1: nothing listens here */
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive(o, 200), SOURCE_OPEN_ERROR);
  source_open_async_free(o);
}
END_TEST

static Suite *source_async_suite(void) {
  Suite *s = suite_create("source_async");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, source_open_async_completes_for_plain_body);
  tcase_add_test(tc, source_open_async_follows_playlist_redirect);
  tcase_add_test(tc, source_open_async_reports_error_on_refused_connection);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(source_async_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
