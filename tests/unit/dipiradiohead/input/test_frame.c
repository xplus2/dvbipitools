/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dipiradiohead/framer/aac_latm.h"
#include "dipiradiohead/input/source.h"

#include "http_fixture.h"

#define BODY_CAP 32768
#define ADTS_FRAME_LEN 40
#define ADTS_HEADER_LEN 7
#define MP3_FRAME_LEN 417

static int latm_new_fails;

struct aac_latm {
  int unused;
};

aac_latm_t *aac_latm_new(void) { return latm_new_fails ? NULL : calloc(1, sizeof(struct aac_latm)); }

void aac_latm_free(aac_latm_t *c) { free(c); }

int aac_latm_is_sync(const unsigned char *p, size_t avail) { return avail >= 2 && p[0] == 0x56 && (p[1] & 0xE0) == 0xE0; }

int aac_latm_probe(aac_latm_t *c, const unsigned char *p, size_t avail, aac_latm_info_t *info) {
  (void)c;
  (void)p;
  (void)avail;
  (void)info;
  return -1;
}

typedef struct {
  int listen_fd;
  pthread_t th;
  http_fixture_t fx;
  unsigned char resp[BODY_CAP];
  size_t resp_len;
  source_t *src;
} rig_t;

static void noop_meta_cb(void *ctx, const char *artist, const char *title) {
  (void)ctx;
  (void)artist;
  (void)title;
}

static void rig_open(rig_t *r, const unsigned char *body, size_t len) {
  static const char head[] = "HTTP/1.1 200 OK\r\nContent-Type: audio/mpeg\r\nConnection: close\r\n\r\n";
  unsigned port;
  char uri[64];

  ck_assert_uint_le(sizeof head - 1 + len, sizeof r->resp);
  memcpy(r->resp, head, sizeof head - 1);
  memcpy(r->resp + sizeof head - 1, body, len);
  r->resp_len = sizeof head - 1 + len;
  r->listen_fd = fixture_listener(&port);
  fixture_single(&r->fx, r->listen_fd, (const char *)r->resp, r->resp_len);
  ck_assert_int_eq(pthread_create(&r->th, NULL, fixture_serve, &r->fx), 0);
  snprintf(uri, sizeof uri, "http://127.0.0.1:%u/stream", port);
  r->src = source_open(uri, 0, "test", 0, noop_meta_cb, NULL, NULL, NULL);
  ck_assert_ptr_nonnull(r->src);
}

static void rig_close(rig_t *r) {
  source_close(r->src);
  pthread_join(r->th, NULL);
  close(r->listen_fd);
}

static int next_frame(rig_t *r, source_frame_t *f, net_err_reason_t *reason) {
  for (int i = 0; i < 100; i++) {
    struct pollfd pfd;
    int rc = source_next_frame(r->src, f, reason);

    if (rc != 0) return rc;
    pfd.fd = source_fd(r->src);
    pfd.events = POLLIN;
    pfd.revents = 0;
    poll(&pfd, 1, 50);
  }
  return 0;
}

static size_t put_adts(unsigned char *out, unsigned char marker) {
  memset(out, marker, ADTS_FRAME_LEN);
  out[0] = 0xFF;
  out[1] = 0xF1;
  out[2] = 0x50;
  out[3] = (unsigned char)(0x80 | ((ADTS_FRAME_LEN >> 11) & 0x03));
  out[4] = (unsigned char)((ADTS_FRAME_LEN >> 3) & 0xFF);
  out[5] = (unsigned char)(((ADTS_FRAME_LEN & 0x07) << 5) | 0x1F);
  out[6] = 0xFC;
  return ADTS_FRAME_LEN;
}

static size_t put_mp3(unsigned char *out, unsigned char marker) {
  memset(out, marker, MP3_FRAME_LEN);
  out[0] = 0xFF;
  out[1] = 0xFB;
  out[2] = 0x90;
  out[3] = 0x00;
  return MP3_FRAME_LEN;
}

START_TEST(id3_tag_larger_than_the_buffer_is_skipped) {
  static unsigned char body[BODY_CAP];
  rig_t *r = calloc(1, sizeof *r);
  source_frame_t f;
  unsigned size = 20000;
  size_t n = 10 + size;

  if (!r) abort();
  memcpy(body, "ID3\x04\x00\x00", 6);
  body[6] = (unsigned char)((size >> 21) & 0x7F);
  body[7] = (unsigned char)((size >> 14) & 0x7F);
  body[8] = (unsigned char)((size >> 7) & 0x7F);
  body[9] = (unsigned char)(size & 0x7F);
  for (int i = 0; i < 4; i++) n += put_mp3(body + n, (unsigned char)(0x30 + i));
  rig_open(r, body, n);
  for (int i = 0; i < 3; i++) {
    ck_assert_int_eq(next_frame(r, &f, NULL), 1);
    ck_assert_int_eq(f.codec, SRC_MPEG_AUDIO);
    ck_assert_uint_eq(f.data[4], (unsigned)(0x30 + i));
  }
  rig_close(r);
  free(r);
}
END_TEST

START_TEST(stream_without_any_codec_sync_is_a_hard_error) {
  static unsigned char body[20000];
  rig_t *r = calloc(1, sizeof *r);
  source_frame_t f;
  net_err_reason_t reason = NET_ERR_OTHER;

  if (!r) abort();
  rig_open(r, body, sizeof body);
  ck_assert_int_eq(next_frame(r, &f, &reason), -1);
  ck_assert_int_eq(reason, NET_ERR_FORMAT);
  rig_close(r);
  free(r);
}
END_TEST

typedef struct {
  const char *name;
  size_t lead_len;
  unsigned char lead[16];
  int lead_is_frame;
} resync_case_t;

static const resync_case_t resync_cases[] = {
    {"garbage after a complete first frame", 5, {0x00, 0x00, 0x00, 0x00, 0x00}, 1},
    {"sync word with an invalid header", 12, {0xFF, 0xF1, 0x3C, 0x80, 0x05, 0x1F, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00}, 0},
    {"sync word with multiple raw blocks", 12, {0xFF, 0xF1, 0x50, 0x80, 0x05, 0x1F, 0xFD, 0x00, 0x00, 0x00, 0x00, 0x00}, 0},
    {"sync word with a header shorter than its own length", 12, {0xFF, 0xF1, 0x50, 0x80, 0x00, 0x1F, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00}, 0},
};

START_TEST(damaged_stream_resyncs_to_the_next_good_frame) {
  const resync_case_t *c = &resync_cases[_i];
  static unsigned char body[512];
  rig_t *r = calloc(1, sizeof *r);
  source_frame_t f;
  size_t n = 0;

  if (!r) abort();
  if (c->lead_is_frame) n += put_adts(body + n, 0x11);
  memcpy(body + n, c->lead, c->lead_len);
  n += c->lead_len;
  n += put_adts(body + n, 0x22);
  n += put_adts(body + n, 0x33);
  n += put_adts(body + n, 0x44);
  rig_open(r, body, n);

  ck_assert_msg(next_frame(r, &f, NULL) == 1, "%s: no first frame", c->name);
  ck_assert_msg(f.codec == SRC_AAC_ADTS, "%s: codec %d", c->name, f.codec);
  ck_assert_msg(f.len == ADTS_FRAME_LEN, "%s: length %zu", c->name, f.len);
  ck_assert_msg(f.data[ADTS_HEADER_LEN] == 0x22, "%s: first frame marker 0x%02x", c->name, f.data[ADTS_HEADER_LEN]);
  ck_assert_msg(next_frame(r, &f, NULL) == 1, "%s: no second frame", c->name);
  ck_assert_msg(f.data[ADTS_HEADER_LEN] == 0x33, "%s: second frame marker 0x%02x", c->name, f.data[ADTS_HEADER_LEN]);
  rig_close(r);
  free(r);
}
END_TEST

START_TEST(clean_stream_delivers_every_frame_in_order) {
  static unsigned char body[512];
  rig_t *r = calloc(1, sizeof *r);
  source_frame_t f;
  size_t n = 0;

  if (!r) abort();
  n += put_adts(body + n, 0x11);
  n += put_adts(body + n, 0x22);
  n += put_adts(body + n, 0x33);
  rig_open(r, body, n);
  ck_assert_int_eq(next_frame(r, &f, NULL), 1);
  ck_assert_uint_eq(f.data[ADTS_HEADER_LEN], 0x11u);
  ck_assert_uint_eq(f.stream_type, 0x0Fu);
  ck_assert_uint_eq(f.sample_rate, 44100u);
  ck_assert_int_eq(next_frame(r, &f, NULL), 1);
  ck_assert_uint_eq(f.data[ADTS_HEADER_LEN], 0x22u);
  rig_close(r);
  free(r);
}
END_TEST

START_TEST(latm_state_allocation_failure_is_a_hard_error) {
  static unsigned char body[128];
  rig_t *r = calloc(1, sizeof *r);
  source_frame_t f;
  net_err_reason_t reason = NET_ERR_FORMAT;

  if (!r) abort();
  body[0] = 0x56;
  body[1] = 0xE0;
  latm_new_fails = 1;
  rig_open(r, body, sizeof body);
  ck_assert_int_eq(next_frame(r, &f, &reason), -1);
  ck_assert_int_eq(reason, NET_ERR_OTHER);
  latm_new_fails = 0;
  rig_close(r);
  free(r);
}
END_TEST

START_TEST(stray_sync_bytes_do_not_lock_the_wrong_codec) {
  static unsigned char body[4096];
  rig_t *r = calloc(1, sizeof *r);
  source_frame_t f;
  size_t n = 0;

  if (!r) abort();
  memset(body, 0x5A, 200);
  body[100] = 0x56;
  body[101] = 0xE5;
  n = 200;
  for (int i = 0; i < 4; i++) n += put_mp3(body + n, (unsigned char)(0x10 + i));
  rig_open(r, body, n);
  ck_assert_int_eq(next_frame(r, &f, NULL), 1);
  ck_assert_int_eq(f.codec, SRC_MPEG_AUDIO);
  ck_assert_uint_eq(f.data[4], 0x10u);
  rig_close(r);
  free(r);
}
END_TEST

START_TEST(codec_is_redetected_after_repeated_bad_frames) {
  static unsigned char body[BODY_CAP];
  rig_t *r = calloc(1, sizeof *r);
  source_frame_t f;
  size_t n = 0;

  if (!r) abort();
  for (int i = 0; i < 3; i++) n += put_adts(body + n, 0x11);
  n += 20000;
  for (int i = 0; i < 4; i++) n += put_mp3(body + n, (unsigned char)(0x20 + i));
  rig_open(r, body, n);
  ck_assert_int_eq(next_frame(r, &f, NULL), 1);
  ck_assert_int_eq(f.codec, SRC_AAC_ADTS);
  ck_assert_int_eq(next_frame(r, &f, NULL), 1);
  ck_assert_int_eq(f.codec, SRC_AAC_ADTS);
  ck_assert_int_eq(next_frame(r, &f, NULL), 1);
  ck_assert_int_eq(f.codec, SRC_MPEG_AUDIO);
  ck_assert_uint_eq(f.data[4], 0x20u);
  rig_close(r);
  free(r);
}
END_TEST

static Suite *frame_suite(void) {
  Suite *s = suite_create("frame");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, id3_tag_larger_than_the_buffer_is_skipped);
  tcase_add_test(tc, stream_without_any_codec_sync_is_a_hard_error);
  tcase_add_loop_test(tc, damaged_stream_resyncs_to_the_next_good_frame, 0, (int)(sizeof resync_cases / sizeof resync_cases[0]));
  tcase_add_test(tc, clean_stream_delivers_every_frame_in_order);
  tcase_add_test(tc, latm_state_allocation_failure_is_a_hard_error);
  tcase_add_test(tc, stray_sync_bytes_do_not_lock_the_wrong_codec);
  tcase_add_test(tc, codec_is_redetected_after_repeated_bad_frames);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(frame_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
