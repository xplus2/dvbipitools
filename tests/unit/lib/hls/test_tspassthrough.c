/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
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

static size_t http_response(const unsigned char *body, size_t n, unsigned char *out) {
  size_t hl = (size_t)sprintf((char *)out, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", n);
  memcpy(out + hl, body, n);
  return hl + n;
}

static void adts_frame(unsigned char *f, size_t flen) {
  f[0] = 0xFF;
  f[1] = 0xF1;
  f[2] = 0x4C; /* AAC-LC, 48 kHz */
  f[3] = (unsigned char)(0x80 | ((flen >> 11) & 3));
  f[4] = (unsigned char)((flen >> 3) & 0xFF);
  f[5] = (unsigned char)(((flen & 7) << 5) | 0x1F);
  f[6] = 0xFC;
  memset(f + 7, 0x55, flen - 7);
}

static size_t id3_with_ts(unsigned char *t, uint64_t ts) {
  static const char owner[] = "com.apple.streaming.transportStreamTimestamp";
  size_t fsz = sizeof owner + 8;
  size_t total = 10 + 10 + fsz;
  size_t body = total - 10;
  memset(t, 0, total);
  memcpy(t, "ID3\x04\x00\x00", 6);
  t[6] = (unsigned char)((body >> 21) & 0x7F);
  t[7] = (unsigned char)((body >> 14) & 0x7F);
  t[8] = (unsigned char)((body >> 7) & 0x7F);
  t[9] = (unsigned char)(body & 0x7F);
  memcpy(t + 10, "PRIV", 4);
  t[14] = (unsigned char)((fsz >> 21) & 0x7F);
  t[15] = (unsigned char)((fsz >> 14) & 0x7F);
  t[16] = (unsigned char)((fsz >> 7) & 0x7F);
  t[17] = (unsigned char)(fsz & 0x7F);
  memcpy(t + 20, owner, sizeof owner);
  for (int i = 0; i < 8; i++) t[20 + sizeof owner + i] = (unsigned char)(ts >> (56 - 8 * i));
  return total;
}

static uint64_t pes_pts(const unsigned char *pkt) {
  unsigned off = 4 + ((pkt[3] & 0x20) ? 1u + pkt[4] : 0);
  const unsigned char *p = pkt + off + 9;
  return ((uint64_t)(p[0] & 0x0E) << 29) | ((uint64_t)p[1] << 22) | ((uint64_t)(p[2] & 0xFE) << 14) | ((uint64_t)p[3] << 7) | (p[4] >> 1);
}

START_TEST(hls_ts_passthrough_converts_packed_aac) {
  unsigned pl_port, seg_port;
  int pl_fd = make_listener(&pl_port);
  int seg_fd = make_listener(&seg_port);
  pthread_t pl_th, seg_th;
  char pl_body[512];
  char pl_uri[64];
  unsigned char seg_raw[256];
  unsigned char seg_resp[512];
  unsigned char pl_resp[512];
  size_t seg_resp_len, pl_resp_len, off;
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

  off = id3_with_ts(seg_raw, 900000);
  for (int i = 0; i < 3; i++) {
    adts_frame(seg_raw + off, 20);
    off += 20;
  }
  seg_resp_len = http_response(seg_raw, off, seg_resp);
  snprintf(pl_body, sizeof pl_body, "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-MEDIA-SEQUENCE:1\n#EXTINF:6.0,\nhttp://127.0.0.1:%u/seg1.aac\n", seg_port);
  pl_resp_len = http_response((unsigned char *)pl_body, strlen(pl_body), pl_resp);

  pl_responses[0] = (const char *)pl_resp;
  pl_lens[0] = pl_resp_len;
  pl_srv = (scripted_server_t){pl_fd, pl_responses, pl_lens, 1};
  ck_assert_int_eq(pthread_create(&pl_th, NULL, serve_scripted, &pl_srv), 0);
  seg_responses[0] = (const char *)seg_resp;
  seg_lens[0] = seg_resp_len;
  seg_srv = (scripted_server_t){seg_fd, seg_responses, seg_lens, 1};
  ck_assert_int_eq(pthread_create(&seg_th, NULL, serve_scripted, &seg_srv), 0);

  snprintf(pl_uri, sizeof pl_uri, "http://127.0.0.1:%u/live.m3u8", pl_port);
  ck_assert_int_eq(http_url_parse(pl_uri, &pl_url), 0);
  memset(&pt, 0, sizeof pt);
  h = hls_live_new(&pl_url, "test-agent", 0, 0, "test", NULL, hls_ts_passthrough_feed, &pt);
  ck_assert_ptr_nonnull(h);
  ck_assert_int_eq(drive_until_len(h, acc, sizeof acc, &acc_len, 5 * 188, 200), 1);
  ck_assert_uint_eq(acc_len, 5 * 188);
  for (int i = 0; i < 5; i++) ck_assert_uint_eq(acc[i * 188], 0x47);
  ck_assert_uint_eq(((acc[1] & 0x1F) << 8) | acc[2], 0x0000);
  ck_assert_uint_eq(((acc[189] & 0x1F) << 8) | acc[190], 0x0100);
  for (int i = 2; i < 5; i++) ck_assert_uint_eq(((acc[i * 188 + 1] & 0x1F) << 8) | acc[i * 188 + 2], 0x0101);
  ck_assert_uint_eq(pes_pts(acc + 2 * 188), 900000);
  ck_assert_uint_eq(pes_pts(acc + 3 * 188), 900000 + 1920);
  ck_assert_uint_eq(pes_pts(acc + 4 * 188), 900000 + 2 * 1920);

  hls_live_free(h);
  pthread_join(pl_th, NULL);
  pthread_join(seg_th, NULL);
  close(pl_fd);
  close(seg_fd);
}
END_TEST

START_TEST(hls_live_rejoins_after_media_sequence_reset) {
  unsigned pl_port, seg_port;
  int pl_fd = make_listener(&pl_port);
  int seg_fd = make_listener(&seg_port);
  pthread_t pl_th, seg_th;
  char pl_body[2][512];
  char pl_uri[64];
  unsigned char seg_raw[2][188];
  unsigned char seg_resp[2][512];
  unsigned char pl_resp[2][512];
  size_t seg_lens[2], pl_lens[2];
  const char *pl_responses[2];
  const char *seg_responses[2];
  scripted_server_t pl_srv, seg_srv;
  http_url_t pl_url;
  hls_live_t *h;
  hls_ts_passthrough_t pt;
  unsigned char acc[4096];
  size_t acc_len = 0;
  const unsigned long seqs[2] = {5000, 0};

  for (int i = 0; i < 2; i++) {
    build_ts_packet(seg_raw[i], 0x100, (unsigned char)(0xA0 + i));
    seg_lens[i] = http_response(seg_raw[i], sizeof seg_raw[i], seg_resp[i]);
    seg_responses[i] = (const char *)seg_resp[i];
    snprintf(pl_body[i], sizeof pl_body[i], "#EXTM3U\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:%lu\n#EXTINF:2.0,\nhttp://127.0.0.1:%u/seg.ts\n", seqs[i], seg_port);
    pl_lens[i] = http_response((unsigned char *)pl_body[i], strlen(pl_body[i]), pl_resp[i]);
    pl_responses[i] = (const char *)pl_resp[i];
  }
  pl_srv = (scripted_server_t){pl_fd, pl_responses, pl_lens, 2};
  ck_assert_int_eq(pthread_create(&pl_th, NULL, serve_scripted, &pl_srv), 0);
  seg_srv = (scripted_server_t){seg_fd, seg_responses, seg_lens, 2};
  ck_assert_int_eq(pthread_create(&seg_th, NULL, serve_scripted, &seg_srv), 0);

  snprintf(pl_uri, sizeof pl_uri, "http://127.0.0.1:%u/live.m3u8", pl_port);
  ck_assert_int_eq(http_url_parse(pl_uri, &pl_url), 0);
  memset(&pt, 0, sizeof pt);
  h = hls_live_new(&pl_url, "test-agent", 0, 0, "test", NULL, hls_ts_passthrough_feed, &pt);
  ck_assert_ptr_nonnull(h);
  ck_assert_int_eq(drive_until_len(h, acc, sizeof acc, &acc_len, 2 * 188, 600), 1);
  ck_assert_uint_eq(acc_len, 2 * 188);
  ck_assert_uint_eq(acc[4], 0xA0);
  ck_assert_uint_eq(acc[188 + 4], 0xA1);

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
  tcase_add_test(tc, hls_ts_passthrough_converts_packed_aac);
  tcase_add_test(tc, hls_live_rejoins_after_media_sequence_reset);
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
