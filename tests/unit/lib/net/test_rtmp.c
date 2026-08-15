/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/mux/amf.h"
#include "lib/net/rtmp/chunk.h"
#include "lib/net/rtmp/handshake.h"
#include "lib/net/rtmp/rtmp.h"

#define RTMP_TYPE_INVOKE 20

typedef struct {
  unsigned char buf[16384];
  size_t len;
  int ready;
  char error[128];
} capture_t;

static void write_cb(void *ctx, const unsigned char *data, size_t len) {
  capture_t *c = ctx;
  if (c->len + len <= sizeof c->buf) {
    memcpy(c->buf + c->len, data, len);
    c->len += len;
  }
}

static void ready_cb(void *ctx) { ((capture_t *)ctx)->ready = 1; }

static void error_cb(void *ctx, const char *msg) {
  capture_t *c = ctx;
  size_t n = strlen(msg);
  if (n >= sizeof c->error)
    n = sizeof c->error - 1;
  memcpy(c->error, msg, n);
  c->error[n] = '\0';
}

static size_t build_chunk(unsigned char *out, uint32_t cid, unsigned char type, uint32_t stream_id, const unsigned char *payload, size_t len) {
  rtmp_chunk_header_t h;
  size_t n;
  h.fmt = RTMP_CHUNK_FMT_0;
  h.cid = cid;
  h.timestamp = 0;
  h.length = (uint32_t)len;
  h.type = type;
  h.stream_id = stream_id;
  n = rtmp_chunk_basic_header_write(out, h.fmt, h.cid);
  n += rtmp_chunk_message_header_write(out + n, &h);
  memcpy(out + n, payload, len);
  return n + len;
}

static size_t build_result_connect(unsigned char *out) {
  ebuf_t b;
  size_t n;
  memset(&b, 0, sizeof b);
  amf_string(&b, "_result");
  amf_number(&b, 1);
  amf_object_start(&b);
  amf_object_key(&b, "fmsVer");
  amf_string(&b, "FMS/3,0,1,123");
  amf_object_key(&b, "capabilities");
  amf_number(&b, 31);
  amf_object_end(&b);
  amf_object_start(&b);
  amf_object_key(&b, "level");
  amf_string(&b, "status");
  amf_object_key(&b, "code");
  amf_string(&b, "NetConnection.Connect.Success");
  amf_object_end(&b);
  n = build_chunk(out, 3, RTMP_TYPE_INVOKE, 0, b.p, b.len);
  ebuf_free(&b);
  return n;
}

static size_t build_result_create_stream(unsigned char *out, double stream_id) {
  ebuf_t b;
  size_t n;
  memset(&b, 0, sizeof b);
  amf_string(&b, "_result");
  amf_number(&b, 2);
  amf_null(&b);
  amf_number(&b, stream_id);
  n = build_chunk(out, 3, RTMP_TYPE_INVOKE, 0, b.p, b.len);
  ebuf_free(&b);
  return n;
}

static size_t build_error(unsigned char *out, double transaction) {
  ebuf_t b;
  size_t n;
  memset(&b, 0, sizeof b);
  amf_string(&b, "_error");
  amf_number(&b, transaction);
  amf_null(&b);
  n = build_chunk(out, 3, RTMP_TYPE_INVOKE, 0, b.p, b.len);
  ebuf_free(&b);
  return n;
}

static rtmp_t *make_client(capture_t *cap) {
  rtmp_cfg_t cfg;
  memset(cap, 0, sizeof *cap);
  memset(&cfg, 0, sizeof cfg);
  cfg.app = "live";
  cfg.tcurl = "rtmp://host/live";
  cfg.stream_name = "key123";
  cfg.write_cb = write_cb;
  cfg.ready_cb = ready_cb;
  cfg.error_cb = error_cb;
  cfg.cb_ctx = cap;
  return rtmp_new(&cfg);
}

/* drives handshake: C0/C1 out, feeds back S0/S1/S2, expects connect on wire */
static void run_handshake(rtmp_t *r, capture_t *cap) {
  unsigned char s0s1s2[1 + 2 * RTMP_HANDSHAKE_SIZE];
  size_t i;

  rtmp_start(r);
  ck_assert_uint_eq(cap->len, 1 + RTMP_HANDSHAKE_SIZE);
  ck_assert_uint_eq(cap->buf[0], RTMP_VERSION);

  s0s1s2[0] = RTMP_VERSION;
  for (i = 0; i < 2 * RTMP_HANDSHAKE_SIZE; i++)
    s0s1s2[1 + i] = (unsigned char)(i * 7); /* arbitrary, simple handshake never validates it */
  cap->len = 0;
  ck_assert_int_eq(rtmp_feed(r, s0s1s2, sizeof s0s1s2), 0);

  /* C2 (1536B, echoes S1 with patched timestamp) then connect on wire */
  ck_assert(cap->len > RTMP_HANDSHAKE_SIZE);
  ck_assert(0 == memcmp(cap->buf + 8, s0s1s2 + 1 + 8, RTMP_HANDSHAKE_SIZE - 8)); /* S1 body past 8B timestamp/zero */
  ck_assert_ptr_nonnull(memmem(cap->buf, cap->len, "connect", 7));
  ck_assert_ptr_nonnull(memmem(cap->buf, cap->len, "live", 4));
}

START_TEST(rtmp_full_publish_sequence_reaches_ready) {
  capture_t cap;
  rtmp_t *r = make_client(&cap);
  unsigned char msg[512];
  size_t n;

  ck_assert_ptr_nonnull(r);
  run_handshake(r, &cap);

  cap.len = 0;
  n = build_result_connect(msg);
  ck_assert_int_eq(rtmp_feed(r, msg, n), 0);
  ck_assert_ptr_nonnull(memmem(cap.buf, cap.len, "releaseStream", 13));
  ck_assert_ptr_nonnull(memmem(cap.buf, cap.len, "FCPublish", 9));
  ck_assert_ptr_nonnull(memmem(cap.buf, cap.len, "createStream", 12));
  ck_assert_int_eq(cap.ready, 0);

  cap.len = 0;
  n = build_result_create_stream(msg, 5.0);
  ck_assert_int_eq(rtmp_feed(r, msg, n), 0);
  ck_assert_int_eq(cap.ready, 1);
  ck_assert_ptr_nonnull(memmem(cap.buf, cap.len, "publish", 7));
  ck_assert_ptr_nonnull(memmem(cap.buf, cap.len, "key123", 6));

  rtmp_free(r);
}
END_TEST

START_TEST(rtmp_send_before_ready_fails) {
  capture_t cap;
  rtmp_t *r = make_client(&cap);
  unsigned char payload[4] = {1, 2, 3, 4};

  ck_assert_ptr_nonnull(r);
  ck_assert_int_eq(rtmp_send_video(r, 0, payload, sizeof payload), -1);
  ck_assert_int_eq(rtmp_send_audio(r, 0, payload, sizeof payload), -1);
  ck_assert_int_eq(rtmp_send_data(r, payload, sizeof payload), -1);

  rtmp_free(r);
}
END_TEST

START_TEST(rtmp_send_after_ready_uses_created_stream_id) {
  capture_t cap;
  rtmp_t *r = make_client(&cap);
  unsigned char msg[512];
  unsigned char video[3] = {0x17, 0x00, 0x01};
  size_t n;

  ck_assert_ptr_nonnull(r);
  run_handshake(r, &cap);
  n = build_result_connect(msg);
  rtmp_feed(r, msg, n);
  n = build_result_create_stream(msg, 7.0);
  rtmp_feed(r, msg, n);
  ck_assert_int_eq(cap.ready, 1);

  cap.len = 0;
  ck_assert_int_eq(rtmp_send_video(r, 1234, video, sizeof video), 0);
  ck_assert(cap.len >= 3);
  /* fmt 0: 1B basic header (cid 5) + 3B timestamp + 3B length + 1B type + 4B stream_id LE */
  ck_assert_uint_eq(cap.buf[0], (0 << 6) | 5);
  ck_assert_uint_eq(cap.buf[8], 7); /* stream_id byte 0, LE */
  ck_assert_uint_eq(cap.buf[9], 0);
  ck_assert_ptr_nonnull(memmem(cap.buf, cap.len, video, sizeof video));

  rtmp_free(r);
}
END_TEST

START_TEST(rtmp_error_reply_fires_error_cb) {
  capture_t cap;
  rtmp_t *r = make_client(&cap);
  unsigned char msg[512];
  size_t n;

  ck_assert_ptr_nonnull(r);
  run_handshake(r, &cap);

  n = build_error(msg, 1);
  ck_assert_int_eq(rtmp_feed(r, msg, n), 0);
  ck_assert_str_eq(cap.error, "_error");
  ck_assert_int_eq(cap.ready, 0);

  rtmp_free(r);
}
END_TEST

static Suite *rtmp_suite(void) {
  Suite *s = suite_create("rtmp");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, rtmp_full_publish_sequence_reaches_ready);
  tcase_add_test(tc, rtmp_send_before_ready_fails);
  tcase_add_test(tc, rtmp_send_after_ready_uses_created_stream_id);
  tcase_add_test(tc, rtmp_error_reply_fires_error_cb);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(rtmp_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
