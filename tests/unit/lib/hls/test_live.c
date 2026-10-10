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

#include "lib/demux/crc32.h"
#include "lib/hls/aes128cbc.h"
#include "lib/hls/live.h"

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

static void passthrough_cb(void *ctx, hls_live_t *h, const unsigned char *data, size_t len) {
  (void)ctx;
  hls_live_emit(h, data, len);
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

  /* 5 segments listed (seq 200-204). only 3, within join-back window (202-204), have live ports */
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
  h = hls_live_new(&pl_url, "test-agent", 0, 0, "test", NULL, passthrough_cb, NULL);
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
  h = hls_live_new(&pl_url, "test-agent", 0, 0, "test", NULL, passthrough_cb, NULL);
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

static void run_reuse_case(const hls_insp_t *si) {
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
  h = hls_live_new(&pl_url, "test-agent", 0, 0, "test", si, passthrough_cb, NULL);
  ck_assert_ptr_nonnull(h);
  ck_assert_int_eq(drive_until_contains(h, buf, sizeof buf, &buf_len, "REUSED", 200), 1);
  hls_live_free(h);
  pthread_join(th, NULL);
}

START_TEST(hls_live_reuses_connection_for_playlist_and_segment) { run_reuse_case(NULL); }
END_TEST

START_TEST(hls_live_inspects_segments_when_on) {
  tsinspect_t *insp = NULL;
  hls_insp_t si = {&insp, METRICS_INSPECT_TS_BASIC, NULL, 0};

  run_reuse_case(&si);
  ck_assert_ptr_nonnull(insp);
  ck_assert_uint_eq(tsinspect_counters(insp)->packets, 4);
  tsinspect_free(insp);
}
END_TEST

START_TEST(hls_live_creates_no_inspector_when_off) {
  tsinspect_t *insp = NULL;
  hls_insp_t si = {&insp, METRICS_INSPECT_TS_OFF, NULL, 0};

  run_reuse_case(&si);
  ck_assert_ptr_null(insp);
}
END_TEST

START_TEST(hls_live_uses_fmp4_false_before_first_playlist_fetch) {
  http_url_t pl_url;
  hls_live_t *h;

  ck_assert_int_eq(http_url_parse("http://127.0.0.1:1/live.m3u8", &pl_url), 0);
  h = hls_live_new(&pl_url, "test-agent", 0, 0, "test", NULL, passthrough_cb, NULL);
  ck_assert_ptr_nonnull(h);
  ck_assert_int_eq(hls_live_uses_fmp4(h), 0);
  hls_live_free(h);
}
END_TEST

START_TEST(hls_aes128cbc_matches_nist_vector) {
  static const unsigned char key[16] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};
  static const unsigned char iv[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  unsigned char data[32] = {0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46, 0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d,
    0x50, 0x86, 0xcb, 0x9b, 0x50, 0x72, 0x19, 0xee, 0x95, 0xdb, 0x11, 0x3a, 0x91, 0x76, 0x78, 0xb2};
  static const unsigned char want[32] = {0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96, 0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a,
    0xae, 0x2d, 0x8a, 0x57, 0x1e, 0x03, 0xac, 0x9c, 0x9e, 0xb7, 0x6f, 0xac, 0x45, 0xaf, 0x8e, 0x51};

  ck_assert_int_eq(aes128cbc_decrypt(key, iv, data, sizeof data), 0);
  ck_assert_mem_eq(data, want, sizeof want);
}
END_TEST

START_TEST(hls_aes128cbc_rejects_bad_length) {
  unsigned char key[16] = {0};
  unsigned char iv[16] = {0};
  unsigned char data[20] = {0};

  ck_assert_int_eq(aes128cbc_decrypt(key, iv, data, 20), -1);
  ck_assert_int_eq(aes128cbc_decrypt(key, iv, data, 0), -1);
}
END_TEST

#define RIG_MAX 6
#define PL_HEAD "#EXTM3U\n#EXT-X-TARGETDURATION:1\n#EXT-X-MEDIA-SEQUENCE:%u\n%s"
#define PL_SEG "#EXTINF:1.0,\ns.ts\n"
#define KEY_TAG "#EXT-X-KEY:METHOD=AES-128,URI=\"k.bin\"\n"
#define ENC_PLAIN "ENCRYPTEDSEGMENT"

typedef struct {
  int fd;
  unsigned port;
  pthread_t th;
  scripted_server_t srv;
  char buf[RIG_MAX][1536];
  const char *resp[RIG_MAX];
  size_t lens[RIG_MAX];
  int n;
  hls_live_t *h;
} rig_t;

typedef struct {
  int calls;
  int result;
  size_t len;
  unsigned char data[32];
} init_rec_t;

static const unsigned char enc_key[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f};

static const unsigned char enc_cipher_iv1[32] = {0x0d, 0xa1, 0xe3, 0xd4, 0xb9, 0x3c, 0x5c, 0x63, 0xf2, 0x43, 0x04, 0x19, 0x76, 0xd2, 0x17, 0x68,
  0x00, 0x74, 0xb2, 0x50, 0x64, 0xbe, 0xe7, 0xfe, 0x4d, 0xb0, 0x3f, 0x38, 0xa4, 0x52, 0xfd, 0xda};

static const unsigned char enc_cipher_seq7[32] = {0xf4, 0x0e, 0x5e, 0x44, 0x5f, 0xdf, 0xfd, 0xaa, 0xa5, 0xd2, 0x2b, 0xd6, 0x04, 0xdf, 0x6f, 0x6c,
  0x3b, 0x77, 0xa6, 0xb7, 0x79, 0x24, 0x85, 0xea, 0xc0, 0x93, 0x2a, 0x50, 0x5d, 0xcb, 0x32, 0xf6};

static void rig_init(rig_t *r) {
  memset(r, 0, sizeof *r);
  r->fd = make_listener(&r->port);
}

static void rig_add_with(rig_t *r, const char *conn, const void *body, size_t len) {
  size_t hl;

  ck_assert_int_lt(r->n, RIG_MAX);
  hl = (size_t)snprintf(r->buf[r->n], sizeof r->buf[0], "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n%s\r\n", len, conn);
  ck_assert_uint_le(hl + len, sizeof r->buf[0]);
  memcpy(r->buf[r->n] + hl, body, len);
  r->resp[r->n] = r->buf[r->n];
  r->lens[r->n] = hl + len;
  r->n++;
}

static void rig_add(rig_t *r, const void *body, size_t len) { rig_add_with(r, "Connection: close\r\n", body, len); }

static void rig_add_text(rig_t *r, const char *text) { rig_add(r, text, strlen(text)); }

static void rig_add_keepalive_text(rig_t *r, const char *text) { rig_add_with(r, "", text, strlen(text)); }

static void rig_add_raw(rig_t *r, const char *text) {
  ck_assert_int_lt(r->n, RIG_MAX);
  r->resp[r->n] = text;
  r->lens[r->n] = strlen(text);
  r->n++;
}

static void rig_add_playlist(rig_t *r, unsigned seq, const char *body) {
  char pl[768];

  snprintf(pl, sizeof pl, PL_HEAD, seq, body);
  rig_add_text(r, pl);
}

static void rig_add_segment(rig_t *r, const char *tag) {
  unsigned char raw[188 * 4];
  size_t len = build_segment(raw, tag);

  rig_add(r, raw, len);
}

static void rig_open(rig_t *r, const hls_insp_t *si) {
  char uri[64];
  http_url_t url;

  r->srv.listen_fd = r->fd;
  r->srv.responses = r->resp;
  r->srv.response_lens = r->lens;
  r->srv.n_responses = r->n;
  ck_assert_int_eq(pthread_create(&r->th, NULL, serve_scripted, &r->srv), 0);
  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/live.m3u8", r->port);
  ck_assert_int_eq(http_url_parse(uri, &url), 0);
  r->h = hls_live_new(&url, "test-agent", 0, 0, "test", si, passthrough_cb, NULL);
  ck_assert_ptr_nonnull(r->h);
}

static void rig_close(rig_t *r) {
  hls_live_free(r->h);
  pthread_join(r->th, NULL);
  close(r->fd);
}

static ssize_t pump_once(hls_live_t *h, unsigned char *tmp, size_t cap, net_err_reason_t *reason) {
  struct pollfd pfd;
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
  return hls_live_read(h, tmp, cap, reason);
}

static int pump_collect(hls_live_t *h, unsigned char *acc, size_t cap, size_t *len, size_t want, int max_iters) {
  for (int i = 0; i < max_iters && *len < want; i++) {
    unsigned char tmp[256];
    ssize_t n = pump_once(h, tmp, sizeof tmp, NULL);
    if (n < 0) return -1;
    if (*len + (size_t)n > cap) return -1;
    memcpy(acc + *len, tmp, (size_t)n);
    *len += (size_t)n;
  }
  return *len >= want ? 1 : 0;
}

static int pump_until_error(hls_live_t *h, int max_iters, net_err_reason_t *reason) {
  for (int i = 0; i < max_iters; i++) {
    unsigned char tmp[64];
    if (pump_once(h, tmp, sizeof tmp, reason) < 0) return 1;
  }
  return 0;
}

static int init_rec_cb(void *ctx, const hls_live_t *h, const unsigned char *data, size_t len) {
  init_rec_t *rec = ctx;

  (void)h;
  rec->calls++;
  rec->len = len < sizeof rec->data ? len : sizeof rec->data;
  memcpy(rec->data, data, rec->len);
  return rec->result;
}

START_TEST(hls_live_segment_kind_classifies) {
  static const unsigned char ts[] = {0x47, 0x00};
  static const unsigned char id3[] = {'I', 'D', '3', 0x04};
  static const unsigned char adts[] = {0xFF, 0xF1};
  static const unsigned char ac3[] = {0x0B, 0x77};
  static const unsigned char ftyp[] = {0, 0, 0, 0x18, 'f', 't', 'y', 'p'};
  static const unsigned char styp[] = {0, 0, 0, 0x18, 's', 't', 'y', 'p'};
  static const unsigned char moof[] = {0, 0, 0, 0x18, 'm', 'o', 'o', 'f'};
  static const unsigned char ff_bad[] = {0xFF, 0x00};
  static const unsigned char b_bad[] = {0x0B, 0x00};
  static const unsigned char one_byte[] = {0x0B};
  static const unsigned char zeros[16] = {0};
  unsigned char unsynced[1024] = {0};
  const struct {
    const unsigned char *data;
    size_t len;
    hls_segment_kind_t want;
  } rows[] = {
    {ts, sizeof ts, HLS_SEG_TS},
    {id3, 3, HLS_SEG_PACKED_AUDIO},
    {adts, sizeof adts, HLS_SEG_PACKED_AUDIO},
    {ac3, sizeof ac3, HLS_SEG_PACKED_AUDIO},
    {ftyp, sizeof ftyp, HLS_SEG_FMP4},
    {styp, sizeof styp, HLS_SEG_FMP4},
    {moof, sizeof moof, HLS_SEG_FMP4},
    {unsynced, sizeof unsynced, HLS_SEG_TS},
    {ff_bad, sizeof ff_bad, HLS_SEG_UNKNOWN},
    {b_bad, sizeof b_bad, HLS_SEG_UNKNOWN},
    {one_byte, sizeof one_byte, HLS_SEG_UNKNOWN},
    {ftyp, 7, HLS_SEG_UNKNOWN},
    {zeros, sizeof zeros, HLS_SEG_UNKNOWN},
    {zeros, 0, HLS_SEG_UNKNOWN},
  };

  unsynced[5] = 0x47;
  unsynced[5 + 188] = 0x47;
  unsynced[5 + 2 * 188] = 0x47;
  for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++)
    ck_assert_msg(hls_live_segment_kind(rows[i].data, rows[i].len) == rows[i].want, "row %zu", i);
}
END_TEST

START_TEST(hls_live_emit_drops_when_buffer_full) {
  static unsigned char big[300000];
  http_url_t url;
  hls_live_t *h;

  ck_assert_int_eq(http_url_parse("http://127.0.0.1:1/live.m3u8", &url), 0);
  h = hls_live_new(&url, NULL, 0, 0, NULL, NULL, passthrough_cb, NULL);
  ck_assert_ptr_nonnull(h);
  hls_live_emit(h, big, sizeof big);
  hls_live_emit(h, big, 16);
  ck_assert_int_eq(hls_live_has_buffered(h), 1);
  ck_assert_int_eq(hls_live_poll_fd(h), -1);
  ck_assert_int_eq(hls_live_poll_events(h), 0);
  hls_live_free(h);
}
END_TEST

static void run_idle_case(rig_t *r) {
  unsigned char tmp[64];
  net_err_reason_t reason = NET_ERR_COUNT;

  rig_open(r, NULL);
  ck_assert_int_eq(hls_live_read(r->h, tmp, sizeof tmp, &reason), 0);
  ck_assert_int_ge(hls_live_poll_fd(r->h), 0);
  ck_assert_int_ne(hls_live_poll_events(r->h), 0);
  ck_assert_int_eq(hls_live_has_buffered(r->h), 0);
  ck_assert_int_eq(pump_until_error(r->h, 15, &reason), 0);
  ck_assert_int_eq(hls_live_poll_fd(r->h), -1);
  ck_assert_int_eq(hls_live_has_buffered(r->h), 0);
  rig_close(r);
}

START_TEST(hls_live_stays_idle_on_not_modified) {
  rig_t r;

  rig_init(&r);
  rig_add_raw(&r, "HTTP/1.1 304 Not Modified\r\nConnection: close\r\n\r\n");
  run_idle_case(&r);
}
END_TEST

START_TEST(hls_live_stays_idle_on_empty_playlist) {
  rig_t r;

  rig_init(&r);
  rig_add(&r, "", 0);
  run_idle_case(&r);
}
END_TEST

START_TEST(hls_live_playlist_without_header_is_format_error) {
  rig_t r;
  net_err_reason_t reason = NET_ERR_COUNT;

  rig_init(&r);
  rig_add_text(&r, "not a playlist\n");
  rig_open(&r, NULL);
  ck_assert_int_eq(pump_until_error(r.h, 50, &reason), 1);
  ck_assert_int_eq(reason, NET_ERR_FORMAT);
  rig_close(&r);
}
END_TEST

static void run_resequence_case(unsigned second_seq) {
  rig_t r;
  unsigned char acc[2048];
  size_t len = 0;

  rig_init(&r);
  rig_add_playlist(&r, 100, PL_SEG);
  rig_add_segment(&r, "FIRSTSEG");
  rig_add_playlist(&r, second_seq, PL_SEG);
  rig_add_segment(&r, "SECONDSEG");
  rig_open(&r, NULL);
  ck_assert_int_eq(drive_until_contains(r.h, acc, sizeof acc, &len, "FIRSTSEG", 200), 1);
  ck_assert_int_eq(drive_until_contains(r.h, acc, sizeof acc, &len, "SECONDSEG", 200), 1);
  rig_close(&r);
}

START_TEST(hls_live_rejoins_after_media_sequence_reset) { run_resequence_case(50); }
END_TEST

START_TEST(hls_live_skips_ahead_over_segment_gap) { run_resequence_case(105); }
END_TEST

static void run_decrypt_case(const char *key_attrs, const unsigned char *cipher, int n_segments) {
  rig_t r;
  unsigned char acc[256];
  size_t len = 0;
  size_t plain = strlen(ENC_PLAIN);
  char body[256];

  snprintf(body, sizeof body, "%s%s%s", key_attrs, PL_SEG, n_segments > 1 ? PL_SEG : "");
  rig_init(&r);
  rig_add_playlist(&r, 7, body);
  rig_add(&r, enc_key, sizeof enc_key);
  for (int i = 0; i < n_segments; i++) rig_add(&r, cipher, 32);
  rig_open(&r, NULL);
  ck_assert_int_eq(pump_collect(r.h, acc, sizeof acc, &len, plain * (size_t)n_segments, 300), 1);
  ck_assert_uint_eq(len, plain * (size_t)n_segments);
  for (int i = 0; i < n_segments; i++) ck_assert_mem_eq(acc + (size_t)i * plain, ENC_PLAIN, plain);
  rig_close(&r);
}

START_TEST(hls_live_decrypts_with_explicit_iv_and_reuses_key) {
  run_decrypt_case("#EXT-X-KEY:METHOD=AES-128,URI=\"k.bin\",IV=0x00000000000000000000000000000001\n", enc_cipher_iv1, 2);
}
END_TEST

START_TEST(hls_live_decrypts_with_sequence_iv) { run_decrypt_case(KEY_TAG, enc_cipher_seq7, 1); }
END_TEST

START_TEST(hls_live_rejects_key_of_wrong_length) {
  rig_t r;
  net_err_reason_t reason = NET_ERR_COUNT;

  rig_init(&r);
  rig_add_playlist(&r, 7, KEY_TAG PL_SEG);
  rig_add(&r, enc_key, 8);
  rig_open(&r, NULL);
  ck_assert_int_eq(pump_until_error(r.h, 100, &reason), 1);
  ck_assert_int_eq(reason, NET_ERR_FORMAT);
  rig_close(&r);
}
END_TEST

START_TEST(hls_live_skips_segment_that_fails_decrypt) {
  rig_t r;
  unsigned char acc[64];
  size_t len = 0;

  rig_init(&r);
  rig_add_playlist(&r, 7, KEY_TAG PL_SEG);
  rig_add(&r, enc_key, sizeof enc_key);
  rig_add(&r, enc_cipher_seq7, 20);
  rig_open(&r, NULL);
  ck_assert_int_eq(pump_collect(r.h, acc, sizeof acc, &len, 1, 25), 0);
  ck_assert_uint_eq(len, 0);
  rig_close(&r);
}
END_TEST

static void run_init_case(int cb_result, const char *init_body, size_t init_len, int expect_error) {
  rig_t r;
  init_rec_t rec = {0, cb_result, 0, {0}};
  unsigned char acc[256];
  size_t len = 0;
  tsinspect_t *insp = NULL;
  hls_insp_t si = {&insp, METRICS_INSPECT_TS_BASIC, NULL, 0};
  net_err_reason_t reason = NET_ERR_COUNT;

  rig_init(&r);
  rig_add_playlist(&r, 3, "#EXT-X-MAP:URI=\"init.mp4\"\n" PL_SEG);
  rig_add(&r, init_body, init_len);
  if (!expect_error) rig_add_text(&r, "SEGDATA");
  rig_open(&r, &si);
  hls_live_set_init_cb(r.h, init_rec_cb, &rec);
  if (expect_error) {
    ck_assert_int_eq(pump_until_error(r.h, 100, &reason), 1);
    ck_assert_int_eq(reason, NET_ERR_FORMAT);
  } else {
    ck_assert_int_eq(drive_until_contains(r.h, acc, sizeof acc, &len, "SEGDATA", 200), 1);
    ck_assert_int_eq(rec.calls, 1);
    ck_assert_uint_eq(rec.len, init_len);
    ck_assert_mem_eq(rec.data, init_body, init_len);
    ck_assert_int_eq(hls_live_uses_fmp4(r.h), 1);
    ck_assert_ptr_null(insp);
  }
  rig_close(&r);
}

START_TEST(hls_live_fetches_init_segment_before_media) { run_init_case(1, "INITDATA", 8, 0); }
END_TEST

START_TEST(hls_live_rejects_init_segment_when_callback_fails) { run_init_case(0, "INITDATA", 8, 1); }
END_TEST

START_TEST(hls_live_rejects_empty_init_segment) { run_init_case(1, "", 0, 1); }
END_TEST

static void run_bad_url_case(const char *tags, int with_init_cb) {
  rig_t r;
  init_rec_t rec = {0, 1, 0, {0}};
  net_err_reason_t reason = NET_ERR_COUNT;
  char pl[768];

  snprintf(pl, sizeof pl, PL_HEAD, 3u, tags);
  rig_init(&r);
  rig_add_keepalive_text(&r, pl);
  rig_open(&r, NULL);
  if (with_init_cb) hls_live_set_init_cb(r.h, init_rec_cb, &rec);
  ck_assert_int_eq(pump_until_error(r.h, 100, &reason), 1);
  ck_assert_int_eq(reason, NET_ERR_OTHER);
  rig_close(&r);
}

START_TEST(hls_live_fails_on_unparsable_segment_url) { run_bad_url_case("#EXTINF:1.0,\nhttp://127.0.0.1:99999/s.ts\n", 0); }
END_TEST

START_TEST(hls_live_fails_on_unparsable_key_url) {
  run_bad_url_case("#EXT-X-KEY:METHOD=AES-128,URI=\"http://127.0.0.1:99999/k.bin\"\n" PL_SEG, 0);
}
END_TEST

START_TEST(hls_live_fails_on_unparsable_init_url) {
  run_bad_url_case("#EXT-X-MAP:URI=\"http://127.0.0.1:99999/i.mp4\"\n" PL_SEG, 1);
}
END_TEST

START_TEST(hls_live_reports_segment_fetch_error) {
  rig_t r;
  net_err_reason_t reason = NET_ERR_COUNT;

  rig_init(&r);
  rig_add_playlist(&r, 3, PL_SEG);
  rig_add_raw(&r, "");
  rig_open(&r, NULL);
  ck_assert_int_eq(pump_until_error(r.h, 100, &reason), 1);
  ck_assert_int_ne(reason, NET_ERR_COUNT);
  rig_close(&r);
}
END_TEST

START_TEST(hls_live_repolls_with_etag_after_empty_playlist) {
  static const char body[] = "#EXTM3U\n#EXT-X-TARGETDURATION:1\n#EXT-X-MEDIA-SEQUENCE:5\n";
  rig_t r;
  net_err_reason_t reason = NET_ERR_COUNT;

  rig_init(&r);
  rig_add_with(&r, "ETag: \"v1\"\r\nConnection: close\r\n", body, sizeof body - 1);
  rig_add_raw(&r, "HTTP/1.1 304 Not Modified\r\nConnection: close\r\n\r\n");
  rig_open(&r, NULL);
  ck_assert_int_eq(pump_until_error(r.h, 100, &reason), 0);
  rig_close(&r);
}
END_TEST

static Suite *hls_live_suite(void) {
  Suite *s = suite_create("hls_live");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, hls_live_joins_n_segments_back_and_skips_older_segments);
  tcase_add_test(tc, hls_live_fetches_only_newly_appended_segment);
  tcase_add_test(tc, hls_live_reuses_connection_for_playlist_and_segment);
  tcase_add_test(tc, hls_live_inspects_segments_when_on);
  tcase_add_test(tc, hls_live_creates_no_inspector_when_off);
  tcase_add_test(tc, hls_live_uses_fmp4_false_before_first_playlist_fetch);
  tcase_add_test(tc, hls_aes128cbc_matches_nist_vector);
  tcase_add_test(tc, hls_aes128cbc_rejects_bad_length);
  tcase_add_test(tc, hls_live_segment_kind_classifies);
  tcase_add_test(tc, hls_live_emit_drops_when_buffer_full);
  tcase_add_test(tc, hls_live_stays_idle_on_not_modified);
  tcase_add_test(tc, hls_live_stays_idle_on_empty_playlist);
  tcase_add_test(tc, hls_live_playlist_without_header_is_format_error);
  tcase_add_test(tc, hls_live_rejoins_after_media_sequence_reset);
  tcase_add_test(tc, hls_live_skips_ahead_over_segment_gap);
  tcase_add_test(tc, hls_live_decrypts_with_explicit_iv_and_reuses_key);
  tcase_add_test(tc, hls_live_decrypts_with_sequence_iv);
  tcase_add_test(tc, hls_live_rejects_key_of_wrong_length);
  tcase_add_test(tc, hls_live_skips_segment_that_fails_decrypt);
  tcase_add_test(tc, hls_live_fetches_init_segment_before_media);
  tcase_add_test(tc, hls_live_rejects_init_segment_when_callback_fails);
  tcase_add_test(tc, hls_live_rejects_empty_init_segment);
  tcase_add_test(tc, hls_live_fails_on_unparsable_segment_url);
  tcase_add_test(tc, hls_live_fails_on_unparsable_key_url);
  tcase_add_test(tc, hls_live_fails_on_unparsable_init_url);
  tcase_add_test(tc, hls_live_reports_segment_fetch_error);
  tcase_add_test(tc, hls_live_repolls_with_etag_after_empty_playlist);
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
