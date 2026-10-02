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

#include "http_fixture.h"

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
  int listen_fd = fixture_listener(&port);
  pthread_t th;
  http_fixture_t fx;
  const char *resp = "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nConnection: close\r\n\r\nnot-really-audio-but-thats-ok-here";
  char uri[64];
  source_open_t *o;
  source_t *s;

  fixture_single(&fx, listen_fd, resp, strlen(resp));
  ck_assert_int_eq(pthread_create(&th, NULL, fixture_serve, &fx), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/stream", port);
  o = source_open_async_start(uri, 0, "test", 0, noop_meta_cb, NULL, NULL, NULL);
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
  int listen_a = fixture_listener(&port_a);
  int listen_b = fixture_listener(&port_b);
  pthread_t th_a, th_b;
  http_fixture_t fx_a, fx_b;
  char resp_a[256];
  const char *resp_b = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nsecond-server-body";
  char uri[64];
  source_open_t *o;
  source_t *s;

  /* m3u playlist body: any non-'#' line starting with http:// is followed, no header needed */
  snprintf(resp_a, sizeof resp_a, "HTTP/1.1 200 OK\r\nContent-Type: audio/x-mpegurl\r\nConnection: close\r\n\r\nhttp://127.0.0.1:%u/next\n", port_b);
  fixture_single(&fx_a, listen_a, resp_a, strlen(resp_a));
  fixture_single(&fx_b, listen_b, resp_b, strlen(resp_b));
  ck_assert_int_eq(pthread_create(&th_a, NULL, fixture_serve, &fx_a), 0);
  ck_assert_int_eq(pthread_create(&th_b, NULL, fixture_serve, &fx_b), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/playlist.m3u", port_a);
  o = source_open_async_start(uri, 0, "test", 0, noop_meta_cb, NULL, NULL, NULL);
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
  source_open_t *o = source_open_async_start("http://127.0.0.1:1/nothing", 0, "test", 0, noop_meta_cb, NULL, NULL, NULL); /* port 1: nothing listens here */
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive(o, 200), SOURCE_OPEN_ERROR);
  source_open_async_free(o);
}
END_TEST

#define MP3_FRAME_LEN FIXTURE_MP3_FRAME_LEN

typedef struct {
  int listen_fd;
} staged_arg_t;

static void *serve_staged_mp3(void *arg) {
  staged_arg_t *a = arg;
  int cfd = accept(a->listen_fd, NULL, NULL);
  static unsigned char body[20 * MP3_FRAME_LEN];
  const char *hdr = "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nConnection: close\r\n\r\n";

  if (cfd < 0) return NULL;
  fixture_read_request(cfd);
  fixture_mp3_frames(body, 20);
  fixture_send_all(cfd, hdr, strlen(hdr));
  fixture_send_all(cfd, body, 6 * MP3_FRAME_LEN);
  usleep(300000);
  fixture_send_all(cfd, body + 6 * MP3_FRAME_LEN, 14 * MP3_FRAME_LEN);
  usleep(300000);
  close(cfd);
  return NULL;
}

START_TEST(source_prefill_holds_frames_until_queued_then_flags_resume) {
  unsigned port;
  int listen_fd = fixture_listener(&port);
  pthread_t th;
  staged_arg_t sarg;
  char uri[64];
  source_open_t *o;
  source_t *s;
  source_frame_t f;
  int got = 0;

  sarg.listen_fd = listen_fd;
  ck_assert_int_eq(pthread_create(&th, NULL, serve_staged_mp3, &sarg), 0);
  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/stream", port);
  o = source_open_async_start(uri, 0, "test", 0, noop_meta_cb, NULL, NULL, NULL);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive(o, 200), SOURCE_OPEN_DONE);
  s = source_open_async_take(o);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(source_set_prefill_ms(s, 400), 0);

  usleep(100000);
  for (int i = 0; i < 5; i++) ck_assert_int_eq(source_next_frame(s, &f, NULL), 0);
  ck_assert_int_eq(source_take_resumed(s), 0);

  for (int i = 0; i < 40 && !got; i++) {
    struct pollfd pfd;
    pfd.fd = source_fd(s);
    pfd.events = POLLIN;
    pfd.revents = 0;
    poll(&pfd, 1, 100);
    got = source_next_frame(s, &f, NULL);
  }
  ck_assert_int_eq(got, 1);
  ck_assert_uint_eq(f.len, MP3_FRAME_LEN);
  ck_assert_int_eq(source_take_resumed(s), 1);
  ck_assert_int_eq(source_take_resumed(s), 0);
  ck_assert_int_eq(source_next_frame(s, &f, NULL), 1);
  ck_assert_int_eq(source_take_resumed(s), 0);

  source_close(s);
  pthread_join(th, NULL);
  close(listen_fd);
}
END_TEST

START_TEST(source_open_async_poll_accessors_name_a_live_fd_while_pending) {
  unsigned port;
  int listen_fd = fixture_listener(&port);
  pthread_t th;
  http_fixture_t fx;
  const char *resp = "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nConnection: close\r\n\r\nbody-bytes";
  char uri[64];
  source_open_t *o;
  source_open_state_t st = SOURCE_OPEN_PENDING;
  source_t *s;

  fixture_single(&fx, listen_fd, resp, strlen(resp));
  ck_assert_int_eq(pthread_create(&th, NULL, fixture_serve, &fx), 0);
  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/stream", port);
  o = source_open_async_start(uri, 0, "test", 0, noop_meta_cb, NULL, NULL, NULL);
  ck_assert_ptr_nonnull(o);
  for (int i = 0; i < 200 && st == SOURCE_OPEN_PENDING; i++) {
    struct pollfd pfd;

    ck_assert_int_ge(source_open_async_poll_fd(o), 0);
    ck_assert_int_ne(source_open_async_poll_events(o) & (POLLIN | POLLOUT), 0);
    pfd.fd = source_open_async_poll_fd(o);
    pfd.events = source_open_async_poll_events(o);
    pfd.revents = 0;
    poll(&pfd, 1, 100);
    st = source_open_async_step(o, NULL);
  }
  ck_assert_int_eq(st, SOURCE_OPEN_DONE);
  s = source_open_async_take(o);
  ck_assert_ptr_nonnull(s);
  source_close(s);
  pthread_join(th, NULL);
  close(listen_fd);
}
END_TEST

typedef struct {
  const char *name;
  int playlists;
  int final_body;
  source_open_state_t want;
} hop_case_t;

static const hop_case_t hop_cases[] = {
    {"last allowed redirect still reaches the audio", 4, 1, SOURCE_OPEN_DONE},
    {"redirect chain at the hop limit is rejected", 5, 0, SOURCE_OPEN_ERROR},
};

START_TEST(source_open_async_enforces_the_playlist_hop_limit) {
  const hop_case_t *c = &hop_cases[_i];
  unsigned port;
  int listen_fd = fixture_listener(&port);
  pthread_t th;
  http_fixture_t fx;
  char playlist[256];
  const char *responses[8];
  size_t lens[8];
  char uri[64];
  source_open_t *o;
  net_err_reason_t reason = NET_ERR_OTHER;
  source_open_state_t st;
  int n = 0;

  snprintf(playlist, sizeof playlist, "HTTP/1.1 200 OK\r\nContent-Type: audio/x-mpegurl\r\nConnection: close\r\n\r\nhttp://127.0.0.1:%u/next\n", port);
  for (; n < c->playlists; n++) {
    responses[n] = playlist;
    lens[n] = strlen(playlist);
  }
  if (c->final_body) {
    responses[n] = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nfinal-body";
    lens[n] = strlen(responses[n]);
    n++;
  }
  fx.listen_fd = listen_fd;
  fx.responses = responses;
  fx.response_lens = lens;
  fx.n_responses = n;
  ck_assert_int_eq(pthread_create(&th, NULL, fixture_serve, &fx), 0);

  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/first", port);
  o = source_open_async_start(uri, 0, "test", 0, noop_meta_cb, NULL, NULL, NULL);
  ck_assert_ptr_nonnull(o);
  st = SOURCE_OPEN_PENDING;
  for (int i = 0; i < 400 && st == SOURCE_OPEN_PENDING; i++) {
    struct pollfd pfd;

    pfd.fd = source_open_async_poll_fd(o);
    pfd.events = source_open_async_poll_events(o);
    pfd.revents = 0;
    poll(&pfd, 1, 100);
    st = source_open_async_step(o, &reason);
  }
  ck_assert_msg(st == c->want, "%s: state %d", c->name, st);
  if (st == SOURCE_OPEN_ERROR) ck_assert_msg(reason == NET_ERR_FORMAT, "%s: reason %d", c->name, reason);
  if (st == SOURCE_OPEN_DONE) source_close(source_open_async_take(o));
  else source_open_async_free(o);
  pthread_join(th, NULL);
  close(listen_fd);
}
END_TEST

static Suite *source_async_suite(void) {
  Suite *s = suite_create("source_async");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, source_open_async_completes_for_plain_body);
  tcase_add_test(tc, source_prefill_holds_frames_until_queued_then_flags_resume);
  tcase_add_test(tc, source_open_async_follows_playlist_redirect);
  tcase_add_test(tc, source_open_async_reports_error_on_refused_connection);
  tcase_add_test(tc, source_open_async_poll_accessors_name_a_live_fd_while_pending);
  tcase_add_loop_test(tc, source_open_async_enforces_the_playlist_hop_limit, 0, (int)(sizeof hop_cases / sizeof hop_cases[0]));
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
