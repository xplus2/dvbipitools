/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "dipiradiohead/input/hls/live.h"
#include "lib/demux/crc32.h"

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

static void *serve_one_conn_scripted(void *arg) {
  const scripted_server_t *a = arg;
  int cfd = accept(a->listen_fd, NULL, NULL);
  struct timeval tv = {2, 0};
  close(a->listen_fd);
  if (cfd < 0)
    return NULL;
  setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  for (int i = 0; i < a->n_responses; i++) {
    char buf[4096];
    size_t got = 0;
    for (;;) {
      ssize_t n = recv(cfd, buf + got, sizeof buf - got, 0);
      if (n <= 0)
        break;
      got += (size_t)n;
      if (got >= 4 && memcmp(buf + got - 4, "\r\n\r\n", 4) == 0)
        break;
    }
    send(cfd, a->responses[i], a->response_lens[i], 0);
  }
  close(cfd);
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

static size_t build_pat(unsigned char *out, unsigned pmt_pid) {
  unsigned char body[16];
  size_t n = 0;
  size_t hdr;
  size_t crc_at;
  uint32_t crc;
  body[n++] = 0x00;
  body[n++] = 0x01;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = 0x01;
  body[n++] = (unsigned char)(0xE0 | ((pmt_pid >> 8) & 0x1F));
  body[n++] = (unsigned char)pmt_pid;
  hdr = n + 4;
  out[0] = 0x00;
  out[1] = (unsigned char)(0xB0 | ((hdr >> 8) & 0x0F));
  out[2] = (unsigned char)hdr;
  memcpy(out + 3, body, n);
  crc_at = 3 + n;
  crc = crc32_mpeg(out, crc_at);
  out[crc_at + 0] = (unsigned char)(crc >> 24);
  out[crc_at + 1] = (unsigned char)(crc >> 16);
  out[crc_at + 2] = (unsigned char)(crc >> 8);
  out[crc_at + 3] = (unsigned char)crc;
  return crc_at + 4;
}

static size_t build_pmt_1audio(unsigned char *out, unsigned pid) {
  unsigned char body[16];
  size_t n = 0;
  size_t hdr;
  size_t crc_at;
  uint32_t crc;
  body[n++] = 0x00;
  body[n++] = 0x01;
  body[n++] = 0xC1;
  body[n++] = 0x00;
  body[n++] = 0x00;
  body[n++] = (unsigned char)(0xE0 | ((pid >> 8) & 0x1F));
  body[n++] = (unsigned char)pid;
  body[n++] = 0xF0;
  body[n++] = 0x00;
  body[n++] = 0x0F;
  body[n++] = (unsigned char)(0xE0 | ((pid >> 8) & 0x1F));
  body[n++] = (unsigned char)pid;
  body[n++] = 0xF0;
  body[n++] = 0x00;
  hdr = n + 4;
  out[0] = 0x02;
  out[1] = (unsigned char)(0xB0 | ((hdr >> 8) & 0x0F));
  out[2] = (unsigned char)hdr;
  memcpy(out + 3, body, n);
  crc_at = 3 + n;
  crc = crc32_mpeg(out, crc_at);
  out[crc_at + 0] = (unsigned char)(crc >> 24);
  out[crc_at + 1] = (unsigned char)(crc >> 16);
  out[crc_at + 2] = (unsigned char)(crc >> 8);
  out[crc_at + 3] = (unsigned char)crc;
  return crc_at + 4;
}

static void wrap_psi_packet(unsigned char pkt[188], unsigned pid, const unsigned char *section, size_t slen) {
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  pkt[4] = 0x00;
  memcpy(pkt + 5, section, slen);
  for (size_t i = 5 + slen; i < 188; i++) pkt[i] = 0xFF;
}

static void wrap_pes_packet(unsigned char pkt[188], unsigned pid, const unsigned char *payload, size_t plen) {
  unsigned char hdr[9] = {0x00, 0x00, 0x01, 0xC0, 0x00, 0x00, 0x80, 0x00, 0x00};
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(0x40 | ((pid >> 8) & 0x1F));
  pkt[2] = (unsigned char)pid;
  pkt[3] = 0x10;
  memcpy(pkt + 4, hdr, sizeof hdr);
  memcpy(pkt + 4 + sizeof hdr, payload, plen);
  for (size_t i = 4 + sizeof hdr + plen; i < 188; i++) pkt[i] = 0xFF;
}

/* 2nd PES needed: flush waits for next PUSI */
static size_t build_segment(unsigned char *out, const char *tag) {
  unsigned char section[32];
  size_t slen;
  size_t off = 0;

  slen = build_pat(section, 0x100);
  wrap_psi_packet(out + off, 0x0000, section, slen);
  off += 188;
  slen = build_pmt_1audio(section, 0x101);
  wrap_psi_packet(out + off, 0x100, section, slen);
  off += 188;
  wrap_pes_packet(out + off, 0x101, (const unsigned char *)tag, strlen(tag));
  off += 188;
  wrap_pes_packet(out + off, 0x101, (const unsigned char *)"X", 1);
  off += 188;
  return off;
}

static int drive_until_contains(hls_live_t *h, unsigned char *acc, size_t acc_cap, size_t *acc_len, const char *needle, int max_iters) {
  size_t needle_len = strlen(needle);
  for (int i = 0; i < max_iters; i++) {
    unsigned char tmp[256];
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
    for (size_t j = 0; j + needle_len <= *acc_len; j++) if (!memcmp(acc + j, needle, needle_len)) return 1;
  }
  return 0;
}

START_TEST(hls_live_joins_n_segments_back_and_skips_older_segments) {
  unsigned pl_port;
  unsigned seg_ports[3];
  int pl_fd = make_listener(&pl_port);
  int seg_fds[3];
  pthread_t pl_th;
  pthread_t seg_ths[3];
  char pl_body[768];
  char pl_uri[64];
  char seg_body_raw[3][188 * 4];
  size_t seg_len[3];
  char seg_resp[3][188 * 4 + 256];
  size_t seg_resp_len[3];
  const char *pl_responses[1];
  size_t pl_lens[1];
  const char *seg_responses[3][1];
  size_t seg_lens[3][1];
  scripted_server_t pl_srv;
  scripted_server_t seg_srv[3];
  http_url_t pl_url;
  hls_live_t *h;
  unsigned char buf[4096];
  size_t buf_len = 0;
  const char *tags[3] = {"SEG202", "SEG203", "SEG204"};

  for (int i = 0; i < 3; i++) {
    seg_fds[i] = make_listener(&seg_ports[i]);
    seg_len[i] = build_segment((unsigned char *)seg_body_raw[i], tags[i]);
    seg_resp_len[i] =
        (size_t)snprintf(seg_resp[i], sizeof seg_resp[i], "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", seg_len[i]);
    memcpy(seg_resp[i] + seg_resp_len[i], seg_body_raw[i], seg_len[i]);
    seg_resp_len[i] += seg_len[i];
  }

  /* 5 segments listed (seq 200-204) - only the 3 within the join-back window (202-204) have live ports */
  snprintf(pl_body, sizeof pl_body,
    "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"
    "#EXTM3U\n#EXT-X-TARGETDURATION:1\n#EXT-X-MEDIA-SEQUENCE:200\n"
    "#EXTINF:1.0,\nhttp://127.0.0.1:1/seq200-never-fetched.ts\n"
    "#EXTINF:1.0,\nhttp://127.0.0.1:1/seq201-never-fetched.ts\n"
    "#EXTINF:1.0,\nhttp://127.0.0.1:%u/seq202.ts\n"
    "#EXTINF:1.0,\nhttp://127.0.0.1:%u/seq203.ts\n"
    "#EXTINF:1.0,\nhttp://127.0.0.1:%u/seq204.ts\n",
    seg_ports[0], seg_ports[1], seg_ports[2]);

  pl_responses[0] = pl_body;
  pl_lens[0] = strlen(pl_body);
  pl_srv.listen_fd = pl_fd;
  pl_srv.responses = pl_responses;
  pl_srv.response_lens = pl_lens;
  pl_srv.n_responses = 1;
  ck_assert_int_eq(pthread_create(&pl_th, NULL, serve_scripted, &pl_srv), 0);
  for (int i = 0; i < 3; i++) {
    seg_responses[i][0] = seg_resp[i];
    seg_lens[i][0] = seg_resp_len[i];
    seg_srv[i].listen_fd = seg_fds[i];
    seg_srv[i].responses = seg_responses[i];
    seg_srv[i].response_lens = seg_lens[i];
    seg_srv[i].n_responses = 1;
    ck_assert_int_eq(pthread_create(&seg_ths[i], NULL, serve_scripted, &seg_srv[i]), 0);
  }
  snprintf(pl_uri, sizeof pl_uri, "http://127.0.0.1:%u/live.m3u8", pl_port);
  ck_assert_int_eq(http_url_parse(pl_uri, &pl_url), 0);
  h = hls_live_new(&pl_url, "test-agent", 0, 0, "test");
  ck_assert_ptr_nonnull(h);
  ck_assert_int_eq(drive_until_contains(h, buf, sizeof buf, &buf_len, "SEG202", 200), 1);
  ck_assert_int_eq(drive_until_contains(h, buf, sizeof buf, &buf_len, "SEG203", 200), 1);
  ck_assert_int_eq(drive_until_contains(h, buf, sizeof buf, &buf_len, "SEG204", 200), 1);
  hls_live_free(h);
  pthread_join(pl_th, NULL);
  for (int i = 0; i < 3; i++) pthread_join(seg_ths[i], NULL);
  close(pl_fd);
  for (int i = 0; i < 3; i++) close(seg_fds[i]);
}
END_TEST

START_TEST(hls_live_fetches_only_newly_appended_segment) {
  unsigned pl_port;
  unsigned segA_port;
  unsigned segB_port;
  int pl_fd = make_listener(&pl_port);
  int segA_fd = make_listener(&segA_port);
  int segB_fd = make_listener(&segB_port);
  pthread_t pl_th;
  pthread_t segA_th;
  pthread_t segB_th;
  char pl_body1[512];
  char pl_body2[512];
  char pl_uri[64];
  unsigned char segA_raw[188 * 4];
  unsigned char segB_raw[188 * 4];
  size_t segA_len;
  size_t segB_len;
  char segA_resp[188 * 4 + 256];
  char segB_resp[188 * 4 + 256];
  size_t segA_resp_len;
  size_t segB_resp_len;
  const char *pl_responses[2];
  size_t pl_lens[2];
  const char *segA_responses[1];
  const char *segB_responses[1];
  size_t segA_lens[1];
  size_t segB_lens[1];
  scripted_server_t pl_srv;
  scripted_server_t segA_srv;
  scripted_server_t segB_srv;
  http_url_t pl_url;
  hls_live_t *h;
  unsigned char buf[4096];
  size_t buf_len = 0;

  segA_len = build_segment(segA_raw, "SEGA");
  segA_resp_len = (size_t)snprintf(segA_resp, sizeof segA_resp, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", segA_len);
  memcpy(segA_resp + segA_resp_len, segA_raw, segA_len);
  segA_resp_len += segA_len;

  segB_len = build_segment(segB_raw, "SEGB");
  segB_resp_len = (size_t)snprintf(segB_resp, sizeof segB_resp, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", segB_len);
  memcpy(segB_resp + segB_resp_len, segB_raw, segB_len);
  segB_resp_len += segB_len;

  /* poll 1: one segment (seq 300) */
  snprintf(pl_body1, sizeof pl_body1,
    "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"
    "#EXTM3U\n#EXT-X-TARGETDURATION:1\n#EXT-X-MEDIA-SEQUENCE:300\n"
    "#EXTINF:1.0,\nhttp://127.0.0.1:%u/segA.ts\n",
    segA_port);
  /* poll 2: segA still listed at dead port (NO refetch) + new segB */
  snprintf(pl_body2, sizeof pl_body2,
    "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"
    "#EXTM3U\n#EXT-X-TARGETDURATION:1\n#EXT-X-MEDIA-SEQUENCE:300\n"
    "#EXTINF:1.0,\nhttp://127.0.0.1:1/segA-must-not-be-refetched.ts\n"
    "#EXTINF:1.0,\nhttp://127.0.0.1:%u/segB.ts\n",
    segB_port);

  pl_responses[0] = pl_body1;
  pl_lens[0] = strlen(pl_body1);
  pl_responses[1] = pl_body2;
  pl_lens[1] = strlen(pl_body2);
  pl_srv.listen_fd = pl_fd;
  pl_srv.responses = pl_responses;
  pl_srv.response_lens = pl_lens;
  pl_srv.n_responses = 2;
  ck_assert_int_eq(pthread_create(&pl_th, NULL, serve_scripted, &pl_srv), 0);

  segA_responses[0] = segA_resp;
  segA_lens[0] = segA_resp_len;
  segA_srv.listen_fd = segA_fd;
  segA_srv.responses = segA_responses;
  segA_srv.response_lens = segA_lens;
  segA_srv.n_responses = 1;
  ck_assert_int_eq(pthread_create(&segA_th, NULL, serve_scripted, &segA_srv), 0);

  segB_responses[0] = segB_resp;
  segB_lens[0] = segB_resp_len;
  segB_srv.listen_fd = segB_fd;
  segB_srv.responses = segB_responses;
  segB_srv.response_lens = segB_lens;
  segB_srv.n_responses = 1;
  ck_assert_int_eq(pthread_create(&segB_th, NULL, serve_scripted, &segB_srv), 0);

  snprintf(pl_uri, sizeof pl_uri, "http://127.0.0.1:%u/live.m3u8", pl_port);
  ck_assert_int_eq(http_url_parse(pl_uri, &pl_url), 0);
  h = hls_live_new(&pl_url, "test-agent", 0, 0, "test");
  ck_assert_ptr_nonnull(h);
  ck_assert_int_eq(drive_until_contains(h, buf, sizeof buf, &buf_len, "SEGA", 200), 1);
  ck_assert_int_eq(drive_until_contains(h, buf, sizeof buf, &buf_len, "SEGB", 200), 1);
  hls_live_free(h);
  pthread_join(pl_th, NULL);
  pthread_join(segA_th, NULL);
  pthread_join(segB_th, NULL);
  close(pl_fd);
  close(segA_fd);
  close(segB_fd);
}
END_TEST

START_TEST(hls_live_reuses_connection_for_playlist_and_segment) {
  unsigned port;
  int listen_fd = make_listener(&port);
  pthread_t th;
  char pl_body[512];
  char seg_resp[188 * 4 + 256];
  char pl_uri[64];
  unsigned char seg_raw[188 * 4];
  size_t seg_len;
  size_t seg_resp_len;
  size_t pl_resp_len;
  char pl_resp[768];
  const char *responses[2];
  size_t response_lens[2];
  scripted_server_t srv;
  http_url_t pl_url;
  hls_live_t *h;
  unsigned char buf[4096];
  size_t buf_len = 0;

  seg_len = build_segment(seg_raw, "REUSED");
  seg_resp_len = (size_t)snprintf(seg_resp, sizeof seg_resp, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n", seg_len);
  memcpy(seg_resp + seg_resp_len, seg_raw, seg_len);
  seg_resp_len += seg_len;

  snprintf(pl_body, sizeof pl_body,
           "#EXTM3U\n#EXT-X-TARGETDURATION:1\n#EXT-X-MEDIA-SEQUENCE:500\n"
           "#EXTINF:1.0,\nseg.ts\n");
  pl_resp_len = (size_t)snprintf(pl_resp, sizeof pl_resp, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n%s", strlen(pl_body), pl_body);

  responses[0] = pl_resp;
  response_lens[0] = pl_resp_len;
  responses[1] = seg_resp;
  response_lens[1] = seg_resp_len;
  srv.listen_fd = listen_fd;
  srv.responses = responses;
  srv.response_lens = response_lens;
  srv.n_responses = 2;
  ck_assert_int_eq(pthread_create(&th, NULL, serve_one_conn_scripted, &srv), 0);
  snprintf(pl_uri, sizeof pl_uri, "http://127.0.0.1:%u/live.m3u8", port);
  ck_assert_int_eq(http_url_parse(pl_uri, &pl_url), 0);
  h = hls_live_new(&pl_url, "test-agent", 0, 0, "test");
  ck_assert_ptr_nonnull(h);
  ck_assert_int_eq(drive_until_contains(h, buf, sizeof buf, &buf_len, "REUSED", 200), 1);
  hls_live_free(h);
  pthread_join(th, NULL);
}
END_TEST

static Suite *hls_live_suite(void) {
  Suite *s = suite_create("hls_live");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, hls_live_joins_n_segments_back_and_skips_older_segments);
  tcase_add_test(tc, hls_live_fetches_only_newly_appended_segment);
  tcase_add_test(tc, hls_live_reuses_connection_for_playlist_and_segment);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(hls_live_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
