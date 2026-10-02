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

#include "http_fixture.h"

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

#define BUILT_SEG_CAP (188 * 10)

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

typedef struct {
  int got_frame;
  int hard_error;
  source_frame_t frame;
} hls_run_t;

static void run_hls_source(const unsigned char *seg_raw, size_t seg_len, const source_insp_t *si, unsigned prefill_ms, int max_iters, hls_run_t *run) {
  unsigned pl_port;
  unsigned seg_port;
  int pl_fd = fixture_listener(&pl_port);
  int seg_fd = fixture_listener(&seg_port);
  pthread_t pl_th;
  pthread_t seg_th;
  char pl_body[512];
  char pl_uri[64];
  unsigned char seg_resp[BUILT_SEG_CAP + 256];
  size_t seg_resp_len;
  const char *pl_responses[2];
  size_t pl_lens[2];
  const char *seg_responses[1];
  size_t seg_lens[1];
  http_fixture_t pl_srv;
  http_fixture_t seg_srv;
  source_t *s;

  memset(run, 0, sizeof *run);
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
  ck_assert_int_eq(pthread_create(&pl_th, NULL, fixture_serve, &pl_srv), 0);

  seg_responses[0] = (const char *)seg_resp;
  seg_lens[0] = seg_resp_len;
  seg_srv.listen_fd = seg_fd;
  seg_srv.responses = seg_responses;
  seg_srv.response_lens = seg_lens;
  seg_srv.n_responses = 1;
  ck_assert_int_eq(pthread_create(&seg_th, NULL, fixture_serve, &seg_srv), 0);

  snprintf(pl_uri, sizeof pl_uri, "http://127.0.0.1:%u/live.m3u8", pl_port);
  s = source_open(pl_uri, 0, "test", 0, NULL, NULL, si, NULL);
  ck_assert_ptr_nonnull(s);
  if (prefill_ms) ck_assert_int_eq(source_set_prefill_ms(s, prefill_ms), 0);

  for (int i = 0; i < max_iters && !run->got_frame; i++) {
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
    r = source_next_frame(s, &run->frame, NULL);
    if (r < 0) {
      run->hard_error = 1;
      break;
    }
    if (r == 1) run->got_frame = 1;
  }

  source_close(s);
  pthread_join(pl_th, NULL);
  pthread_join(seg_th, NULL);
  close(pl_fd);
  close(seg_fd);
}

static void decode_hls_audio(const source_insp_t *si, unsigned prefill_ms) {
  unsigned char seg_raw[BUILT_SEG_CAP];
  size_t seg_len = build_hls_audio_segment(seg_raw);
  hls_run_t run;

  run_hls_source(seg_raw, seg_len, si, prefill_ms, 200, &run);
  ck_assert_int_eq(run.got_frame, 1);
  ck_assert_int_eq(run.frame.codec, SRC_MPEG_AUDIO);
  ck_assert_uint_eq(run.frame.stream_type, 0x03);
  ck_assert_uint_eq(run.frame.sample_rate, 44100u);
  ck_assert_uint_eq(run.frame.samples, 1152u);
  ck_assert_uint_eq(run.frame.len, 417u);
}

START_TEST(source_open_decodes_audio_from_hls_media_playlist) {
  decode_hls_audio(NULL, 0);
}
END_TEST

START_TEST(hls_source_reports_dejitter_depth_to_the_inspector) {
  tsinspect_t *insp = tsinspect_new(METRICS_INSPECT_TS_BASIC);
  source_insp_t si = {&insp, METRICS_INSPECT_TS_BASIC, NULL, 0};
  metrics_writer_t w;
  metrics_hdr_t hdr;
  metrics_reader_t r;
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  uint64_t v = 0;
  int found = 0;

  decode_hls_audio(&si, 10);
  tsinspect_tick(insp, 1.0e9);
  memset(&hdr, 0, sizeof hdr);
  hdr.proto_version = METRICS_PROTO_VERSION;
  hdr.component = METRICS_COMPONENT_RADIOHEAD;
  hdr.metrics_id[0] = 'x';
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  tsinspect_put_metrics(insp, &w, "input0", 1.0, 1000.0);
  ck_assert_int_eq(metrics_reader_init(&r, w.buf, w.len, &hdr), 0);
  while (metrics_reader_next(&r, &id, label, sizeof label, &v) == 1)
    if (id == METRICS_ID_TS_INPUT_BUFFER_MILLISECONDS) found = 1;
  ck_assert_int_eq(found, 1);
  ck_assert_uint_le(v, 60u);
  tsinspect_free(insp);
}
END_TEST

typedef struct {
  const char *name;
  size_t (*damage)(unsigned char *seg, size_t len);
  int want_frame;
} damaged_segment_case_t;

static size_t damage_leading_garbage(unsigned char *seg, size_t len) {
  memmove(seg + 100, seg, len);
  memset(seg, 0x00, 100);
  return len + 100;
}

static size_t damage_truncated_tail(unsigned char *seg, size_t len) {
  (void)seg;
  return len - 188 + 50;
}

static size_t damage_lost_sync_byte(unsigned char *seg, size_t len) {
  seg[188 * 3] = 0x00;
  return len;
}

static size_t damage_without_psi(unsigned char *seg, size_t len) {
  memmove(seg, seg + 188 * 2, len - 188 * 2);
  return len - 188 * 2;
}

static size_t damage_all_garbage(unsigned char *seg, size_t len) {
  memset(seg, 0xA5, len);
  return len;
}

static const damaged_segment_case_t damaged_segment_cases[] = {
    {"garbage before the first packet", damage_leading_garbage, 1},
    {"segment cut inside the packet that completes the last frame", damage_truncated_tail, 0},
    {"one packet loses its sync byte", damage_lost_sync_byte, 0},
    {"segment without PAT and PMT", damage_without_psi, 0},
    {"segment of pure garbage", damage_all_garbage, 0},
};

START_TEST(damaged_hls_segment_is_survived_without_hard_error) {
  const damaged_segment_case_t *c = &damaged_segment_cases[_i];
  unsigned char seg[BUILT_SEG_CAP + 128];
  size_t len = build_hls_audio_segment(seg);
  hls_run_t run;

  len = c->damage(seg, len);
  run_hls_source(seg, len, NULL, 0, c->want_frame ? 200 : 8, &run);
  ck_assert_msg(run.hard_error == 0, "%s: hard error", c->name);
  ck_assert_msg(run.got_frame == c->want_frame, "%s: got_frame %d", c->name, run.got_frame);
}
END_TEST

static Suite *source_hls_suite(void) {
  Suite *s = suite_create("source_hls");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, source_open_decodes_audio_from_hls_media_playlist);
  tcase_add_test(tc, hls_source_reports_dejitter_depth_to_the_inspector);
  tcase_add_loop_test(tc, damaged_hls_segment_is_survived_without_hard_error, 0, (int)(sizeof damaged_segment_cases / sizeof damaged_segment_cases[0]));
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
