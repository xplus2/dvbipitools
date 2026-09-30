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

#include "lib/net/tssource.h"

typedef struct {
  int listen_fd;
  const char *const *responses;
  const size_t *response_lens;
  int n_responses;
} scripted_server_t;

static void *serve_scripted(void *arg) {
  const scripted_server_t *a = arg;
  for (int i = 0; i < a->n_responses; i++) {
    int cfd = accept(a->listen_fd, NULL, NULL);
    struct timeval tv = {2, 0};
    char buf[4096];
    size_t got = 0;
    if (cfd < 0) return NULL;
    setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    for (;;) {
      ssize_t n = recv(cfd, buf + got, sizeof buf - got, 0);
      if (n <= 0) break;
      got += (size_t)n;
      if (got >= 4 && memcmp(buf + got - 4, "\r\n\r\n", 4) == 0) break;
    }
    send(cfd, a->responses[i], a->response_lens[i], 0);
    close(cfd);
  }
  return NULL;
}

static int make_listener(unsigned *port_out) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;
  ck_assert_int_ge(fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(fd, 4), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  *port_out = ntohs(addr.sin_port);
  return fd;
}

static tssrc_open_state_t drive_open(tssrc_open_t *o, int max_iters) {
  tssrc_open_state_t st = TSSRC_OPEN_PENDING;
  for (int i = 0; i < max_iters && st == TSSRC_OPEN_PENDING; i++) {
    struct pollfd pfd;
    int fd = tssrc_open_async_poll_fd(o);
    if (fd < 0) {
      st = tssrc_open_async_step(o, NULL);
      continue;
    }
    pfd.fd = fd;
    pfd.events = tssrc_open_async_poll_events(o);
    pfd.revents = 0;
    poll(&pfd, 1, 100);
    st = tssrc_open_async_step(o, NULL);
  }
  return st;
}

static size_t drive_read(tssrc_t *s, unsigned char *buf, size_t cap, int max_iters) {
  size_t got = 0;
  for (int i = 0; i < max_iters && got < cap; i++) {
    struct pollfd pfd;
    ssize_t n;
    int fd = tssrc_fd(s);
    if (fd >= 0) {
      pfd.fd = fd;
      pfd.events = POLLIN;
      pfd.revents = 0;
      poll(&pfd, 1, 50);
    }
    n = tssrc_read(s, buf + got, cap - got, NULL);
    if (n > 0) got += (size_t)n;
    else if (n < 0) break;
  }
  return got;
}

START_TEST(tssrc_http_dispatches_hls_ts_segmented_media_playlist) {
  unsigned pl_port, seg_port;
  int pl_fd = make_listener(&pl_port);
  int seg_fd = make_listener(&seg_port);
  pthread_t pl_th, seg_th;
  unsigned char ts_seg[3 * 188];
  char pl_body[512], pl_resp[768], seg_resp[1024];
  size_t pl_resp_len, seg_resp_len;
  const char *pl_responses[2];
  size_t pl_lens[2];
  const char *seg_responses[1];
  size_t seg_lens[1];
  scripted_server_t pl_srv, seg_srv;
  tssrc_cfg_t cfg;
  tssrc_open_t *o;
  tssrc_t *s;
  unsigned char out[1024];
  size_t got;
  char uri[64];

  memset(ts_seg, 0xAB, sizeof ts_seg);
  ts_seg[0] = 0x47;
  ts_seg[188] = 0x47;
  ts_seg[376] = 0x47;

  snprintf(pl_body, sizeof pl_body,
    "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-MEDIA-SEQUENCE:0\n"
    "#EXTINF:6.0,\nhttp://127.0.0.1:%u/seg1.ts\n#EXT-X-ENDLIST\n",
    seg_port);
  pl_resp_len = (size_t)snprintf(pl_resp, sizeof pl_resp, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n%s", strlen(pl_body), pl_body);
  pl_responses[0] = pl_resp;
  pl_responses[1] = pl_resp;
  pl_lens[0] = pl_resp_len;
  pl_lens[1] = pl_resp_len;
  pl_srv.listen_fd = pl_fd;
  pl_srv.responses = pl_responses;
  pl_srv.response_lens = pl_lens;
  pl_srv.n_responses = 2;
  ck_assert_int_eq(pthread_create(&pl_th, NULL, serve_scripted, &pl_srv), 0);

  {
    size_t head_len = (size_t)snprintf(seg_resp, sizeof seg_resp, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: %zu\r\n\r\n", sizeof ts_seg);
    memcpy(seg_resp + head_len, ts_seg, sizeof ts_seg);
    seg_resp_len = head_len + sizeof ts_seg;
  }
  seg_responses[0] = seg_resp;
  seg_lens[0] = seg_resp_len;
  seg_srv.listen_fd = seg_fd;
  seg_srv.responses = seg_responses;
  seg_srv.response_lens = seg_lens;
  seg_srv.n_responses = 1;
  ck_assert_int_eq(pthread_create(&seg_th, NULL, serve_scripted, &seg_srv), 0);

  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_HTTP;
  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/playlist.m3u8", pl_port);
  ck_assert_int_eq(http_url_parse(uri, &cfg.http), 0);

  o = tssrc_open_async_start(&cfg, NULL);
  ck_assert_ptr_nonnull(o);
  ck_assert_int_eq(drive_open(o, 300), TSSRC_OPEN_DONE);
  s = tssrc_open_async_take(o);
  ck_assert_ptr_nonnull(s);

  got = drive_read(s, out, sizeof ts_seg, 300);
  ck_assert_uint_eq(got, sizeof ts_seg);
  ck_assert_int_eq(memcmp(out, ts_seg, sizeof ts_seg), 0);

  tssrc_close(s);
  pthread_join(pl_th, NULL);
  pthread_join(seg_th, NULL);
  close(pl_fd);
  close(seg_fd);
}
END_TEST

static Suite *tssource_http_dispatch_suite(void) {
  Suite *s = suite_create("tssource_http_dispatch");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, tssrc_http_dispatches_hls_ts_segmented_media_playlist);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(tssource_http_dispatch_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
