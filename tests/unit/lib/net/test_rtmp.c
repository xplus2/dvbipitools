/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <check.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/mux/amf.h"
#include "lib/net/rtmp/chunk.h"
#include "lib/net/rtmp/handshake.h"
#include "lib/net/rtmp/priv.h"
#include "lib/net/rtmp/rtmp.h"

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
  if (n >= sizeof c->error) n = sizeof c->error - 1;
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

/* description kept short: build_chunk() writes one contiguous chunk, no
   continuation headers, payload must stay under the 128B default chunk size */
static size_t build_error_needauth(unsigned char *out, double transaction) {
  ebuf_t b;
  size_t n;
  memset(&b, 0, sizeof b);
  amf_string(&b, "_error");
  amf_number(&b, transaction);
  amf_null(&b);
  amf_object_start(&b);
  amf_object_key(&b, "description");
  amf_string(&b, "?reason=needauth&user=bob&salt=abcd1234&opaque=deadbeef");
  amf_object_end(&b);
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

static rtmp_t *make_client_with_auth(capture_t *cap, const char *user, const char *password) {
  rtmp_cfg_t cfg;
  memset(cap, 0, sizeof *cap);
  memset(&cfg, 0, sizeof cfg);
  cfg.app = "live";
  cfg.tcurl = "rtmp://host/live";
  cfg.stream_name = "key123";
  cfg.user = user;
  cfg.password = password;
  cfg.write_cb = write_cb;
  cfg.ready_cb = ready_cb;
  cfg.error_cb = error_cb;
  cfg.cb_ctx = cap;
  return rtmp_new(&cfg);
}

/* drives handshake: C0/C1 out, feeds back S0/S1/S2, expects connect on wire */
static void run_handshake(rtmp_t *r, capture_t *cap) {
  unsigned char s0s1s2[1 + 2 * RTMP_HANDSHAKE_SIZE];

  rtmp_start(r);
  ck_assert_uint_eq(cap->len, 1 + RTMP_HANDSHAKE_SIZE);
  ck_assert_uint_eq(cap->buf[0], RTMP_VERSION);

  s0s1s2[0] = RTMP_VERSION;
  for (size_t i = 0; i < 2 * RTMP_HANDSHAKE_SIZE; i++)
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
  ck_assert_int_eq(rtmp_send_video(r, 0, payload, sizeof payload, NULL, 0), -1);
  ck_assert_int_eq(rtmp_send_audio(r, 0, payload, sizeof payload, NULL, 0), -1);
  ck_assert_int_eq(rtmp_send_data(r, payload, sizeof payload, NULL, 0), -1);

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
  ck_assert_int_eq(rtmp_send_video(r, 1234, video, sizeof video, NULL, 0), 0);
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

START_TEST(rtmp_connect_includes_authmod_when_user_set) {
  capture_t cap;
  rtmp_t *r = make_client_with_auth(&cap, "bob", "hunter2");

  ck_assert_ptr_nonnull(r);
  run_handshake(r, &cap); /* asserts "connect"/"live" already present on wire */
  ck_assert_ptr_nonnull(memmem(cap.buf, cap.len, "authmod=adobe&user=bob", 23));

  rtmp_free(r);
}
END_TEST

/* no OpenSSL backend in this test binary (auth_stub.c): needauth challenge
   can't be answered, client must fail cleanly rather than hang or crash */
START_TEST(rtmp_needauth_error_without_crypto_fails_gracefully) {
  capture_t cap;
  rtmp_t *r = make_client_with_auth(&cap, "bob", "hunter2");
  unsigned char msg[512];
  size_t n;

  ck_assert_ptr_nonnull(r);
  run_handshake(r, &cap);

  n = build_error_needauth(msg, 1);
  ck_assert_int_eq(rtmp_feed(r, msg, n), 0);
  ck_assert_str_eq(cap.error, "_error");
  ck_assert_int_eq(cap.ready, 0);

  rtmp_free(r);
}
END_TEST

#define HS_PATTERN(i) ((unsigned char)((i) * 13 + 5))

static const uint32_t c1_timestamps[] = {0, 1, 0x12345678u, 0xFFFFFFFFu};

START_TEST(handshake_c1_layout_and_determinism) {
  unsigned char a[RTMP_HANDSHAKE_SIZE];
  unsigned char b[RTMP_HANDSHAKE_SIZE];
  unsigned char other[RTMP_HANDSHAKE_SIZE];
  uint32_t ts = c1_timestamps[_i];
  int varied = 0;

  rtmp_handshake_c1(a, ts);
  rtmp_handshake_c1(b, ts);
  ck_assert_mem_eq(a, b, sizeof a);
  ck_assert_uint_eq(((uint32_t)a[0] << 24) | ((uint32_t)a[1] << 16) | ((uint32_t)a[2] << 8) | a[3], ts);
  for (size_t i = 4; i < 8; i++) ck_assert_uint_eq(a[i], 0);
  for (size_t i = 9; i < sizeof a; i++)
    if (a[i] != a[8]) varied = 1;
  ck_assert_int_eq(varied, 1);
  rtmp_handshake_c1(other, ts ^ 0x5A5A5A5Au);
  ck_assert_mem_ne(a + 8, other + 8, sizeof a - 8);
}
END_TEST

START_TEST(handshake_c2_echoes_s1_with_patched_timestamp) {
  unsigned char s1[RTMP_HANDSHAKE_SIZE];
  unsigned char c2[RTMP_HANDSHAKE_SIZE];

  for (size_t i = 0; i < sizeof s1; i++) s1[i] = HS_PATTERN(i);
  rtmp_handshake_c2(c2, s1, 0xAABBCCDDu);
  ck_assert_mem_eq(c2, s1, 4);
  ck_assert_uint_eq(c2[4], 0xAA);
  ck_assert_uint_eq(c2[5], 0xBB);
  ck_assert_uint_eq(c2[6], 0xCC);
  ck_assert_uint_eq(c2[7], 0xDD);
  ck_assert_mem_eq(c2 + 8, s1 + 8, sizeof s1 - 8);
}
END_TEST

static void fill_server_handshake(unsigned char *in) {
  in[0] = RTMP_VERSION;
  for (size_t i = 0; i < 2 * RTMP_HANDSHAKE_SIZE; i++) in[1 + i] = HS_PATTERN(i);
}

static const size_t handshake_splits[] = {1, 2, 100, RTMP_HANDSHAKE_SIZE, RTMP_HANDSHAKE_SIZE + 1, RTMP_HANDSHAKE_SIZE + 2, 2000, 2 * RTMP_HANDSHAKE_SIZE};

START_TEST(handshake_result_is_independent_of_feed_boundaries) {
  unsigned char in[1 + 2 * RTMP_HANDSHAKE_SIZE];
  capture_t ref_cap;
  capture_t cap;
  rtmp_t *ref = make_client(&ref_cap);
  rtmp_t *r = make_client(&cap);
  size_t split = handshake_splits[_i];

  fill_server_handshake(in);
  rtmp_start(ref);
  rtmp_start(r);
  ref_cap.len = 0;
  cap.len = 0;
  ck_assert_int_eq(rtmp_feed(ref, in, sizeof in), 0);
  ck_assert_int_eq(rtmp_feed(r, in, split), 0);
  ck_assert_int_eq(rtmp_feed(r, in + split, sizeof in - split), 0);

  ck_assert_uint_eq(cap.len, ref_cap.len);
  memset(ref_cap.buf + 4, 0, 4);
  memset(cap.buf + 4, 0, 4);
  ck_assert_mem_eq(cap.buf, ref_cap.buf, cap.len);
  rtmp_free(ref);
  rtmp_free(r);
}
END_TEST

START_TEST(handshake_incomplete_s1_and_s2_emit_nothing) {
  unsigned char in[1 + 2 * RTMP_HANDSHAKE_SIZE];
  capture_t cap;
  rtmp_t *r = make_client(&cap);
  size_t s1_end = 1 + RTMP_HANDSHAKE_SIZE;

  fill_server_handshake(in);
  rtmp_start(r);
  cap.len = 0;

  ck_assert_int_eq(rtmp_feed(r, in, s1_end - 1), 0);
  ck_assert_uint_eq(cap.len, 0u);
  ck_assert_int_eq(rtmp_feed(r, in + s1_end - 1, 1), 0);
  ck_assert_uint_eq(cap.len, (size_t)RTMP_HANDSHAKE_SIZE);

  cap.len = 0;
  ck_assert_int_eq(rtmp_feed(r, in + s1_end, RTMP_HANDSHAKE_SIZE - 1), 0);
  ck_assert_uint_eq(cap.len, 0u);
  ck_assert_int_eq(rtmp_feed(r, in + sizeof in - 1, 1), 0);
  ck_assert_ptr_nonnull(memmem(cap.buf, cap.len, "connect", 7));
  rtmp_free(r);
}
END_TEST

static rtmp_t *client_after_handshake(capture_t *cap) {
  rtmp_t *r = make_client(cap);

  ck_assert_ptr_nonnull(r);
  run_handshake(r, cap);
  return r;
}

static void feed_ok(rtmp_t *r, const unsigned char *data, size_t len) {
  ck_assert_int_eq(rtmp_feed(r, data, len), 0);
}

static void set_in_chunk_size(rtmp_t *r, uint32_t size) {
  unsigned char body[4] = {(unsigned char)(size >> 24), (unsigned char)(size >> 16), (unsigned char)(size >> 8), (unsigned char)size};
  unsigned char msg[64];

  size_t plen = build_chunk(msg, RTMP_CID_PROTOCOL, RTMP_TYPE_SET_CHUNK_SIZE, 0, body, sizeof body);
  feed_ok(r, msg, plen);
}

typedef struct {
  const char *name;
  unsigned char bytes[24];
  size_t len;
  int expect;
} chunk_case_t;

static const chunk_case_t chunk_cases[] = {
    {"fmt 3 on unseen chunk stream", {0xC3, 0x00}, 2, -1},
    {"fmt 2 on unseen chunk stream", {0x83, 0x00, 0x00, 0x00}, 4, -1},
    {"message length above limit", {0x03, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00}, 13, -1},
    {"partial message header", {0x03, 0x00, 0x00}, 3, 0},
    {"two byte basic header cut", {0x00}, 1, 0},
    {"zero length message then next byte", {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x63, 0x00, 0x00, 0x00, 0x00, 0xC3}, 13, 0},
    {"extended timestamp cut", {0x03, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x63, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02}, 14, 0},
};

START_TEST(chunk_parser_handles_malformed_input) {
  const chunk_case_t *c = &chunk_cases[_i];
  capture_t cap;
  rtmp_t *r = client_after_handshake(&cap);

  ck_assert_msg(rtmp_feed(r, c->bytes, c->len) == c->expect, "%s: single feed", c->name);
  rtmp_free(r);
}
END_TEST

START_TEST(chunk_parser_handles_malformed_input_bytewise) {
  const chunk_case_t *c = &chunk_cases[_i];
  capture_t cap;
  rtmp_t *r = client_after_handshake(&cap);
  int result = 0;

  for (size_t i = 0; i < c->len && result == 0; i++) result = rtmp_feed(r, c->bytes + i, 1);
  ck_assert_msg(result == c->expect, "%s: bytewise feed", c->name);
  rtmp_free(r);
}
END_TEST

START_TEST(chunk_parser_rejects_ninth_chunk_stream) {
  capture_t cap;
  rtmp_t *r = client_after_handshake(&cap);
  unsigned char msg[12] = {0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x63, 0x00, 0x00, 0x00, 0x00};

  for (unsigned cid = 10; cid < 10 + RTMP_N_CHAN; cid++) {
    msg[0] = (unsigned char)cid;
    feed_ok(r, msg, sizeof msg);
  }
  msg[0] = 10 + RTMP_N_CHAN;
  ck_assert_int_eq(rtmp_feed(r, msg, sizeof msg), -1);
  rtmp_free(r);
}
END_TEST

typedef struct {
  const char *name;
  unsigned char bytes[24];
  size_t len;
  uint32_t cid;
} cid_case_t;

static const cid_case_t cid_cases[] = {
    {"two byte basic header", {0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00}, 17, 100},
    {"three byte basic header", {0x01, 0xF4, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00}, 18, 564},
};

START_TEST(chunk_parser_decodes_extended_chunk_stream_ids) {
  const cid_case_t *c = &cid_cases[_i];
  capture_t cap;
  rtmp_t *r = client_after_handshake(&cap);
  int found = 0;

  feed_ok(r, c->bytes, c->len);
  ck_assert_uint_eq(r->in_chunk_size, 512u);
  for (unsigned i = 0; i < RTMP_N_CHAN; i++)
    if (r->in_chan[i].used && r->in_chan[i].cid == c->cid) found = 1;
  ck_assert_msg(found, "%s: chunk stream %u not tracked", c->name, c->cid);
  rtmp_free(r);
}
END_TEST

START_TEST(chunk_parser_applies_extended_timestamp) {
  static const unsigned char msg[] = {0x03, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x00, 0x00, 0x01, 0x00};
  capture_t cap;
  rtmp_t *r = client_after_handshake(&cap);

  feed_ok(r, msg, sizeof msg);
  ck_assert_uint_eq(r->in_chunk_size, 256u);
  ck_assert_uint_eq(r->in_chan[0].cid, 3u);
  ck_assert_uint_eq(r->in_chan[0].clock, 0x01020304u);
  rtmp_free(r);
}
END_TEST

START_TEST(chunk_extended_timestamp_write_is_big_endian) {
  unsigned char o[4] = {0};

  rtmp_chunk_extended_timestamp_write(o, 0x01020304u);
  ck_assert_uint_eq(o[0], 0x01);
  ck_assert_uint_eq(o[1], 0x02);
  ck_assert_uint_eq(o[2], 0x03);
  ck_assert_uint_eq(o[3], 0x04);
}
END_TEST

static const uint32_t ext_ts_values[] = {0u, 1u, 0xFFFFFFu, 0x1000000u, 0x7FFFFFFFu, 0xFFFFFFFFu};

START_TEST(chunk_extended_timestamp_roundtrip) {
  unsigned char o[4];

  rtmp_chunk_extended_timestamp_write(o, ext_ts_values[_i]);
  ck_assert_uint_eq(rtmp_chunk_extended_timestamp_read(o), ext_ts_values[_i]);
}
END_TEST

START_TEST(chunk_message_header_write_clamps_large_timestamp) {
  rtmp_chunk_header_t h = {0};
  unsigned char o[16];

  h.fmt = RTMP_CHUNK_FMT_0;
  h.timestamp = 0x1000000u;
  ck_assert_uint_eq(rtmp_chunk_message_header_write(o, &h), 11u);
  ck_assert_uint_eq(o[0], 0xFF);
  ck_assert_uint_eq(o[1], 0xFF);
  ck_assert_uint_eq(o[2], 0xFF);
}
END_TEST

typedef struct {
  uint32_t value;
  int valid;
} chunk_size_case_t;

static const chunk_size_case_t chunk_size_cases[] = {
    {0x00000000u, 0},
    {0x80000000u, 0},
    {0x00000001u, 1},
    {0x7FFFFFFFu, 1},
    {0xFFFFFFFFu, 1},
};

START_TEST(set_chunk_size_rejects_zero_and_survives_following_data) {
  const chunk_size_case_t *c = &chunk_size_cases[_i];
  capture_t cap;
  rtmp_t *r = client_after_handshake(&cap);
  unsigned char msg[64];

  set_in_chunk_size(r, c->value);
  if (c->valid) {
    ck_assert_uint_eq(r->in_chunk_size, c->value & 0x7FFFFFFFu);
    ck_assert_str_eq(cap.error, "");
  } else {
    ck_assert_uint_eq(r->in_chunk_size, 128u);
    ck_assert_str_ne(cap.error, "");
  }
  size_t plen = build_error(msg, 1);
  feed_ok(r, msg, plen);
  rtmp_free(r);
}
END_TEST

typedef struct {
  const char *name;
  unsigned char payload[48];
  size_t len;
} invoke_case_t;

static const invoke_case_t invoke_cases[] = {
    {"empty payload", {0}, 0},
    {"string marker only", {0x02}, 1},
    {"string length past end", {0x02, 0x00, 0x10, '_', 'r'}, 5},
    {"number marker for command", {0x00, 0x3F, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 9},
    {"command without transaction", {0x02, 0x00, 0x07, '_', 'r', 'e', 's', 'u', 'l', 't'}, 10},
    {"transaction truncated", {0x02, 0x00, 0x07, '_', 'r', 'e', 's', 'u', 'l', 't', 0x00, 0x3F, 0xF0}, 13},
    {"unknown command", {0x02, 0x00, 0x08, 'o', 'n', 'B', 'W', 'D', 'o', 'n', 'e', 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 20},
    {"result for wrong transaction", {0x02, 0x00, 0x07, '_', 'r', 'e', 's', 'u', 'l', 't', 0x00, 0x40, 0x58, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00}, 19},
};

static void expect_connect_still_pending(rtmp_t *r, capture_t *cap) {
  unsigned char msg[512];

  cap->len = 0;
  size_t plen = build_result_connect(msg);
  feed_ok(r, msg, plen);
  ck_assert_ptr_nonnull(memmem(cap->buf, cap->len, "createStream", 12));
  ck_assert_str_eq(cap->error, "");
}

START_TEST(invoke_handler_ignores_malformed_commands) {
  const invoke_case_t *c = &invoke_cases[_i];
  capture_t cap;
  rtmp_t *r = client_after_handshake(&cap);
  unsigned char msg[128];

  size_t plen = build_chunk(msg, RTMP_CID_INVOKE, RTMP_TYPE_INVOKE, 0, c->payload, c->len);
  ck_assert_msg(rtmp_feed(r, msg, plen) == 0, "%s: feed", c->name);
  expect_connect_still_pending(r, &cap);
  rtmp_free(r);
}
END_TEST

START_TEST(invoke_handler_ignores_command_name_longer_than_buffer) {
  capture_t cap;
  rtmp_t *r = client_after_handshake(&cap);
  unsigned char msg[256];
  ebuf_t b;
  char name[41];

  memset(name, 'a', sizeof name - 1);
  name[sizeof name - 1] = '\0';
  memset(&b, 0, sizeof b);
  amf_string(&b, name);
  amf_number(&b, 1);
  size_t plen = build_chunk(msg, RTMP_CID_INVOKE, RTMP_TYPE_INVOKE, 0, b.p, b.len);
  feed_ok(r, msg, plen);
  ebuf_free(&b);
  expect_connect_still_pending(r, &cap);
  rtmp_free(r);
}
END_TEST

typedef struct {
  const char *name;
  int has_id;
  double id;
  int ready;
} stream_case_t;

static const stream_case_t stream_cases[] = {
    {"no stream id value", 0, 0.0, 0},
    {"zero stream id", 1, 0.0, 0},
    {"negative stream id", 1, -1.0, 0},
    {"nan stream id", 1, NAN, 0},
    {"stream id beyond 32 bits", 1, 5e9, 0},
    {"smallest stream id", 1, 1.0, 1},
    {"largest stream id", 1, 4294967295.0, 1},
};

START_TEST(create_stream_result_requires_usable_stream_id) {
  const stream_case_t *c = &stream_cases[_i];
  capture_t cap;
  rtmp_t *r = client_after_handshake(&cap);
  unsigned char msg[512];
  ebuf_t b;

  size_t plen = build_result_connect(msg);
  feed_ok(r, msg, plen);
  memset(&b, 0, sizeof b);
  amf_string(&b, "_result");
  amf_number(&b, 2);
  amf_null(&b);
  if (c->has_id) amf_number(&b, c->id);
  plen = build_chunk(msg, RTMP_CID_INVOKE, RTMP_TYPE_INVOKE, 0, b.p, b.len);
  feed_ok(r, msg, plen);
  ebuf_free(&b);

  ck_assert_msg(cap.ready == c->ready, "%s: ready %d", c->name, cap.ready);
  if (c->ready) ck_assert_msg(r->stream_id == (uint32_t)c->id, "%s: stream id %u", c->name, r->stream_id);
  else ck_assert_msg(cap.error[0] != '\0', "%s: no error reported", c->name);
  rtmp_free(r);
}
END_TEST

#define NEEDAUTH_VARIANTS 6

START_TEST(needauth_error_with_malformed_description_fails_cleanly) {
  capture_t cap;
  rtmp_t *r = make_client_with_auth(&cap, "bob", "hunter2");
  unsigned char msg[1024];
  char desc[512];
  ebuf_t b;

  ck_assert_ptr_nonnull(r);
  run_handshake(r, &cap);
  set_in_chunk_size(r, 4096);

  memset(&b, 0, sizeof b);
  amf_string(&b, "_error");
  amf_number(&b, 1);
  amf_null(&b);
  if (_i > 0) {
    amf_object_start(&b);
    if (_i == 1) {
      amf_object_key(&b, "level");
      amf_string(&b, "error");
    } else {
      if (_i == 2) snprintf(desc, sizeof desc, "?reason=needauth&user=bob");
      else if (_i == 3) snprintf(desc, sizeof desc, "?reason=needauth&salt=abcd1234");
      else if (_i == 4) snprintf(desc, sizeof desc, "?reason=needauth&salt=%0200d&opaque=x", 7);
      else snprintf(desc, sizeof desc, "?reason=needauth&xsalt=abcd&xopaque=deadbeef");
      amf_object_key(&b, "description");
      amf_string(&b, desc);
    }
    amf_object_end(&b);
  }
  size_t plen = build_chunk(msg, RTMP_CID_INVOKE, RTMP_TYPE_INVOKE, 0, b.p, b.len);
  feed_ok(r, msg, plen);
  ebuf_free(&b);

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
  tcase_add_test(tc, rtmp_connect_includes_authmod_when_user_set);
  tcase_add_test(tc, rtmp_needauth_error_without_crypto_fails_gracefully);
  tcase_add_loop_test(tc, handshake_c1_layout_and_determinism, 0, (int)(sizeof c1_timestamps / sizeof c1_timestamps[0]));
  tcase_add_test(tc, handshake_c2_echoes_s1_with_patched_timestamp);
  tcase_add_loop_test(tc, handshake_result_is_independent_of_feed_boundaries, 0, (int)(sizeof handshake_splits / sizeof handshake_splits[0]));
  tcase_add_test(tc, handshake_incomplete_s1_and_s2_emit_nothing);
  tcase_add_loop_test(tc, chunk_parser_handles_malformed_input, 0, (int)(sizeof chunk_cases / sizeof chunk_cases[0]));
  tcase_add_loop_test(tc, chunk_parser_handles_malformed_input_bytewise, 0, (int)(sizeof chunk_cases / sizeof chunk_cases[0]));
  tcase_add_test(tc, chunk_parser_rejects_ninth_chunk_stream);
  tcase_add_loop_test(tc, chunk_parser_decodes_extended_chunk_stream_ids, 0, (int)(sizeof cid_cases / sizeof cid_cases[0]));
  tcase_add_test(tc, chunk_parser_applies_extended_timestamp);
  tcase_add_test(tc, chunk_extended_timestamp_write_is_big_endian);
  tcase_add_loop_test(tc, chunk_extended_timestamp_roundtrip, 0, (int)(sizeof ext_ts_values / sizeof ext_ts_values[0]));
  tcase_add_test(tc, chunk_message_header_write_clamps_large_timestamp);
  tcase_add_loop_test(tc, set_chunk_size_rejects_zero_and_survives_following_data, 0, (int)(sizeof chunk_size_cases / sizeof chunk_size_cases[0]));
  tcase_add_loop_test(tc, invoke_handler_ignores_malformed_commands, 0, (int)(sizeof invoke_cases / sizeof invoke_cases[0]));
  tcase_add_test(tc, invoke_handler_ignores_command_name_longer_than_buffer);
  tcase_add_loop_test(tc, create_stream_result_requires_usable_stream_id, 0, (int)(sizeof stream_cases / sizeof stream_cases[0]));
  tcase_add_loop_test(tc, needauth_error_with_malformed_description_fails_cleanly, 0, NEEDAUTH_VARIANTS);
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
