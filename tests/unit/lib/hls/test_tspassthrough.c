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
#include <time.h>
#include <unistd.h>

#include "lib/hls/tspassthrough.h"

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

static void build_ts_packet(unsigned char pkt[188], unsigned pid, unsigned char fill) {
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((pid >> 8) & 0x1F);
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  memset(pkt + 4, fill, 184);
}

static int drive_until_len(hls_live_t *h, unsigned char *acc, size_t acc_cap, size_t *acc_len, size_t want, int max_iters) {
  for (int i = 0; i < max_iters && *acc_len < want; i++) {
    unsigned char tmp[512];
    struct pollfd pfd;
    ssize_t n;
    int fd = hls_live_poll_fd(h);
    if (fd >= 0) {
      pfd.fd = fd;
      pfd.events = hls_live_poll_events(h);
      pfd.revents = 0;
      poll(&pfd, 1, 100);
    } else {
      struct timespec ts = {0, 20000000};
      nanosleep(&ts, NULL);
    }
    n = hls_live_read(h, tmp, sizeof tmp, NULL);
    if (n < 0) return -1;
    if (n <= 0) continue;
    if (*acc_len + (size_t)n > acc_cap) return -1;
    memcpy(acc + *acc_len, tmp, (size_t)n);
    *acc_len += (size_t)n;
  }
  return *acc_len >= want ? 1 : 0;
}

START_TEST(hls_ts_passthrough_forwards_segment_bytes_unchanged) {
  unsigned pl_port, seg_port;
  int pl_fd = make_listener(&pl_port);
  int seg_fd = make_listener(&seg_port);
  pthread_t pl_th, seg_th;
  char pl_body[512];
  char pl_uri[64];
  unsigned char seg_raw[188 * 3];
  unsigned char seg_resp[188 * 3 + 256];
  size_t seg_resp_len;
  const char *pl_responses[1];
  size_t pl_lens[1];
  const char *seg_responses[1];
  size_t seg_lens[1];
  scripted_server_t pl_srv, seg_srv;
  http_url_t pl_url;
  hls_live_t *h;
  hls_ts_passthrough_t pt;
  unsigned char acc[4096];
  size_t acc_len = 0;

  build_ts_packet(seg_raw, 0x100, 0xAA);
  build_ts_packet(seg_raw + 188, 0x101, 0xBB);
  build_ts_packet(seg_raw + 376, 0x102, 0xCC);
  seg_resp_len = (size_t)snprintf((char *)seg_resp, sizeof seg_resp, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", sizeof seg_raw);
  memcpy(seg_resp + seg_resp_len, seg_raw, sizeof seg_raw);
  seg_resp_len += sizeof seg_raw;

  snprintf(pl_body, sizeof pl_body,
    "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"
    "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-MEDIA-SEQUENCE:1\n"
    "#EXTINF:6.0,\nhttp://127.0.0.1:%u/seg1.ts\n",
    seg_port);

  pl_responses[0] = pl_body;
  pl_lens[0] = strlen(pl_body);
  pl_srv.listen_fd = pl_fd;
  pl_srv.responses = pl_responses;
  pl_srv.response_lens = pl_lens;
  pl_srv.n_responses = 1;
  ck_assert_int_eq(pthread_create(&pl_th, NULL, serve_scripted, &pl_srv), 0);

  seg_responses[0] = (const char *)seg_resp;
  seg_lens[0] = seg_resp_len;
  seg_srv.listen_fd = seg_fd;
  seg_srv.responses = seg_responses;
  seg_srv.response_lens = seg_lens;
  seg_srv.n_responses = 1;
  ck_assert_int_eq(pthread_create(&seg_th, NULL, serve_scripted, &seg_srv), 0);

  snprintf(pl_uri, sizeof pl_uri, "http://127.0.0.1:%u/live.m3u8", pl_port);
  ck_assert_int_eq(http_url_parse(pl_uri, &pl_url), 0);
  memset(&pt, 0, sizeof pt);
  h = hls_live_new(&pl_url, "test-agent", 0, 0, "test", NULL, hls_ts_passthrough_feed, &pt);
  ck_assert_ptr_nonnull(h);
  ck_assert_int_eq(drive_until_len(h, acc, sizeof acc, &acc_len, sizeof seg_raw, 200), 1);
  ck_assert_uint_eq(acc_len, sizeof seg_raw);
  ck_assert_int_eq(memcmp(acc, seg_raw, sizeof seg_raw), 0);

  hls_live_free(h);
  pthread_join(pl_th, NULL);
  pthread_join(seg_th, NULL);
  close(pl_fd);
  close(seg_fd);
}
END_TEST

static Suite *hls_ts_passthrough_suite(void) {
  Suite *s = suite_create("hls_ts_passthrough");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, hls_ts_passthrough_forwards_segment_bytes_unchanged);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(hls_ts_passthrough_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
