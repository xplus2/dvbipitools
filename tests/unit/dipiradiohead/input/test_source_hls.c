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

#include "dipiradiohead/input/source.h"
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

static size_t build_pmt_1mpegaudio(unsigned char *out, unsigned pid) {
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
  body[n++] = 0x03;
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

static size_t wrap_pes_multi(unsigned char *out, unsigned pid, const unsigned char *es, size_t es_len) {
  static const unsigned char pes_hdr[9] = {0x00, 0x00, 0x01, 0xC0, 0x00, 0x00, 0x80, 0x00, 0x00};
  size_t off = 0;
  size_t es_off = 0;
  int first = 1;

  while (first || es_off < es_len) {
    unsigned char *pkt = out + off;
    size_t cap;
    size_t chunk;
    pkt[0] = 0x47;
    pkt[1] = (unsigned char)((first ? 0x40 : 0x00) | ((pid >> 8) & 0x1F));
    pkt[2] = (unsigned char)pid;
    pkt[3] = 0x10;
    if (first) {
      memcpy(pkt + 4, pes_hdr, sizeof pes_hdr);
      cap = 188 - 4 - sizeof pes_hdr;
      chunk = es_len - es_off < cap ? es_len - es_off : cap;
      memcpy(pkt + 4 + sizeof pes_hdr, es + es_off, chunk);
      for (size_t i = 4 + sizeof pes_hdr + chunk; i < 188; i++) pkt[i] = 0x00;
      first = 0;
    } else {
      cap = 188 - 4;
      chunk = es_len - es_off < cap ? es_len - es_off : cap;
      memcpy(pkt + 4, es + es_off, chunk);
      for (size_t i = 4 + chunk; i < 188; i++) pkt[i] = 0x00;
    }
    es_off += chunk;
    off += 188;
  }
  return off;
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

static size_t build_hls_audio_segment(unsigned char *out) {
  unsigned char section[32];
  unsigned char es[417 * 2];
  size_t slen;
  size_t off = 0;

  memset(es, 0, sizeof es);
  es[0] = 0xFF;
  es[1] = 0xFA;
  es[2] = 0x90;
  es[3] = 0x00;
  memcpy(es + 417, es, 4);

  slen = build_pat(section, 0x100);
  wrap_psi_packet(out + off, 0x0000, section, slen);
  off += 188;
  slen = build_pmt_1mpegaudio(section, 0x101);
  wrap_psi_packet(out + off, 0x100, section, slen);
  off += 188;
  off += wrap_pes_multi(out + off, 0x101, es, sizeof es);
  wrap_pes_packet(out + off, 0x101, (const unsigned char *)"F", 1);
  off += 188;
  return off;
}

START_TEST(source_open_decodes_audio_from_hls_media_playlist) {
  unsigned pl_port;
  unsigned seg_port;
  int pl_fd = make_listener(&pl_port);
  int seg_fd = make_listener(&seg_port);
  pthread_t pl_th;
  pthread_t seg_th;
  char pl_body[512];
  char pl_uri[64];
  unsigned char seg_raw[188 * 10];
  size_t seg_len;
  unsigned char seg_resp[188 * 10 + 256];
  size_t seg_resp_len;
  const char *pl_responses[2];
  size_t pl_lens[2];
  const char *seg_responses[1];
  size_t seg_lens[1];
  scripted_server_t pl_srv;
  scripted_server_t seg_srv;
  source_t *s;
  source_frame_t frame;
  int got_frame = 0;

  seg_len = build_hls_audio_segment(seg_raw);
  seg_resp_len =
      (size_t)snprintf((char *)seg_resp, sizeof seg_resp, "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", seg_len);
  memcpy(seg_resp + seg_resp_len, seg_raw, seg_len);
  seg_resp_len += seg_len;

  snprintf(pl_body, sizeof pl_body,
    "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.apple.mpegurl\r\nConnection: close\r\n\r\n"
    "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-MEDIA-SEQUENCE:1\n"
    "#EXTINF:6.0,\nhttp://127.0.0.1:%u/seg1.ts\n",
    seg_port);

  /* served twice: sniff GET + hls_live_t's own poll fetch */
  pl_responses[0] = pl_body;
  pl_responses[1] = pl_body;
  pl_lens[0] = strlen(pl_body);
  pl_lens[1] = strlen(pl_body);
  pl_srv.listen_fd = pl_fd;
  pl_srv.responses = pl_responses;
  pl_srv.response_lens = pl_lens;
  pl_srv.n_responses = 2;
  ck_assert_int_eq(pthread_create(&pl_th, NULL, serve_scripted, &pl_srv), 0);

  seg_responses[0] = (const char *)seg_resp;
  seg_lens[0] = seg_resp_len;
  seg_srv.listen_fd = seg_fd;
  seg_srv.responses = seg_responses;
  seg_srv.response_lens = seg_lens;
  seg_srv.n_responses = 1;
  ck_assert_int_eq(pthread_create(&seg_th, NULL, serve_scripted, &seg_srv), 0);

  snprintf(pl_uri, sizeof pl_uri, "http://127.0.0.1:%u/live.m3u8", pl_port);
  s = source_open(pl_uri, 0, "test", 0, NULL, NULL, NULL, NULL);
  ck_assert_ptr_nonnull(s);

  for (int i = 0; i < 200 && !got_frame; i++) {
    struct pollfd pfd;
    int fd = source_fd(s);
    int r;
    if (fd >= 0) {
      pfd.fd = fd;
      pfd.events = POLLIN;
      pfd.revents = 0;
      poll(&pfd, 1, 100);
    } else {
      struct timespec ts = {0, 20000000};
      nanosleep(&ts, NULL);
    }
    r = source_next_frame(s, &frame, NULL);
    if (r < 0) break;
    if (r == 1) got_frame = 1;
  }

  ck_assert_int_eq(got_frame, 1);
  ck_assert_int_eq(frame.codec, SRC_MPEG_AUDIO);
  ck_assert_uint_eq(frame.stream_type, 0x03);
  ck_assert_uint_eq(frame.sample_rate, 44100u);
  ck_assert_uint_eq(frame.samples, 1152u);
  ck_assert_uint_eq(frame.len, 417u);

  source_close(s);
  pthread_join(pl_th, NULL);
  pthread_join(seg_th, NULL);
  close(pl_fd);
  close(seg_fd);
}
END_TEST

static Suite *source_hls_suite(void) {
  Suite *s = suite_create("source_hls");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, source_open_decodes_audio_from_hls_media_playlist);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(source_hls_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
