/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "h3_rig.h"

#define FRAME_MAX 12000
#define SETTLE_MS 150
#define BIG_PATH 9000
#define FT_DATA 0x00
#define FT_HEADERS 0x01
#define FT_GREASE 0x21

typedef struct {
  uint8_t data[FRAME_MAX];
  size_t len;
} buf_t;

static void put(buf_t *b, const void *p, size_t n) {
  ck_assert_uint_le(b->len + n, sizeof b->data);
  memcpy(b->data + b->len, p, n);
  b->len += n;
}

static void put_byte(buf_t *b, uint8_t v) {
  put(b, &v, 1);
}

static void put_varint(buf_t *b, uint64_t v) {
  if (v < 64) {
    put_byte(b, (uint8_t)v);
  } else if (v < 16384) {
    put_byte(b, (uint8_t)(0x40 | (v >> 8)));
    put_byte(b, (uint8_t)v);
  } else if (v < (1u << 30)) {
    put_byte(b, (uint8_t)(0x80 | (v >> 24)));
    put_byte(b, (uint8_t)(v >> 16));
    put_byte(b, (uint8_t)(v >> 8));
    put_byte(b, (uint8_t)v);
  } else {
    put_byte(b, (uint8_t)(0xC0 | (v >> 56)));
    for (int shift = 48; shift >= 0; shift -= 8) put_byte(b, (uint8_t)(v >> shift));
  }
}

static void put_prefixed(buf_t *b, uint8_t first, unsigned prefix_bits, size_t value) {
  size_t max = ((size_t)1 << prefix_bits) - 1;

  if (value < max) {
    put_byte(b, (uint8_t)(first | value));
    return;
  }
  put_byte(b, (uint8_t)(first | max));
  value -= max;
  while (value >= 128) {
    put_byte(b, (uint8_t)((value & 0x7F) | 0x80));
    value >>= 7;
  }
  put_byte(b, (uint8_t)value);
}

static void put_field(buf_t *b, const char *name, const char *value, size_t value_len) {
  put_prefixed(b, 0x20, 3, strlen(name));
  put(b, name, strlen(name));
  put_prefixed(b, 0x00, 7, value_len);
  put(b, value, value_len);
}

static void put_text_field(buf_t *b, const char *name, const char *value) {
  put_field(b, name, value, strlen(value));
}

static void put_headers_frame(buf_t *b, const buf_t *section) {
  put_varint(b, FT_HEADERS);
  put_varint(b, section->len + 2);
  put_byte(b, 0);
  put_byte(b, 0);
  put(b, section->data, section->len);
}

static void valid_fields(buf_t *sec, const char *path) {
  put_text_field(sec, ":method", "GET");
  put_text_field(sec, ":scheme", "https");
  put_text_field(sec, ":authority", "localhost");
  put_text_field(sec, ":path", path);
}

static buf_t *heap_buf(void) {
  buf_t *b = calloc(1, sizeof *b);

  ck_assert_ptr_nonnull(b);
  return b;
}

typedef void (*build_fn)(buf_t *out);

static void build_truncated_headers(buf_t *out) {
  put_varint(out, FT_HEADERS);
  put_varint(out, 50);
  put_byte(out, 0);
  put_byte(out, 0);
  put_byte(out, 0xD1);
}

static void build_huge_frame_length(buf_t *out) {
  put_varint(out, FT_HEADERS);
  put_varint(out, 0x3FFFFFFFFFFFFFFFULL);
}

static void build_huge_frame_type(buf_t *out) {
  put_varint(out, 0x3FFFFFFFFFFFFFFFULL);
  put_varint(out, 4);
  put(out, "abcd", 4);
}

static void build_data_first(buf_t *out) {
  put_varint(out, FT_DATA);
  put_varint(out, 1);
  put_byte(out, 'x');
}

static void build_duplicate_path(buf_t *out) {
  buf_t *sec = heap_buf();

  valid_fields(sec, "/first");
  put_text_field(sec, ":path", "/second");
  put_headers_frame(out, sec);
  free(sec);
}

static void build_missing_path(buf_t *out) {
  buf_t *sec = heap_buf();

  put_text_field(sec, ":method", "GET");
  put_text_field(sec, ":scheme", "https");
  put_text_field(sec, ":authority", "localhost");
  put_headers_frame(out, sec);
  free(sec);
}

static void build_uppercase_name(buf_t *out) {
  buf_t *sec = heap_buf();

  valid_fields(sec, "/x");
  put_field(sec, "Bad-Name", "v", 1);
  put_headers_frame(out, sec);
  free(sec);
}

static void build_blocked_dynamic_reference(buf_t *out) {
  put_varint(out, FT_HEADERS);
  put_varint(out, 4);
  put_byte(out, 1);
  put_byte(out, 0);
  put_byte(out, 0x80);
  put_byte(out, 0x80);
}

static void build_oversized_path(buf_t *out) {
  buf_t *sec = heap_buf();
  char *path = malloc(BIG_PATH + 1);

  ck_assert_ptr_nonnull(path);
  path[0] = '/';
  memset(path + 1, 'a', BIG_PATH - 1);
  path[BIG_PATH] = '\0';
  valid_fields(sec, path);
  put_headers_frame(out, sec);
  free(path);
  free(sec);
}

static void build_grease_then_request(buf_t *out) {
  buf_t *sec = heap_buf();

  put_varint(out, FT_GREASE);
  put_varint(out, 2);
  put(out, "zz", 2);
  valid_fields(sec, "/no/such/route");
  put_headers_frame(out, sec);
  free(sec);
}

static void build_plain_request(buf_t *out) {
  buf_t *sec = heap_buf();

  valid_fields(sec, "/no/such/route");
  put_headers_frame(out, sec);
  free(sec);
}

typedef struct {
  const char *name;
  build_fn build;
  int fin;
  int expect_404;
} case_t;

static const case_t cases[] = {
    {"truncated headers frame", build_truncated_headers, 1, 0},
    {"frame length beyond any limit", build_huge_frame_length, 0, 0},
    {"frame type beyond any limit", build_huge_frame_type, 0, 0},
    {"data before headers", build_data_first, 0, 0},
    {"duplicate path pseudo header", build_duplicate_path, 1, 0},
    {"missing path pseudo header", build_missing_path, 1, 0},
    {"upper case header name", build_uppercase_name, 1, 0},
    {"reference into an empty dynamic table", build_blocked_dynamic_reference, 1, 0},
    {"path longer than the server buffer", build_oversized_path, 1, 1},
    {"unknown frame before a valid request", build_grease_then_request, 1, 1},
    {"well formed request (control)", build_plain_request, 1, 1},
};

static int cond_never(h3rig_t *h, void *arg) {
  (void)h;
  (void)arg;
  return 0;
}

static void assert_server_still_serves(h3rig_t *srv) {
  h3rig_t second;
  int64_t sid;

  memset(&second, 0, sizeof second);
  second.srv_fd = srv->srv_fd;
  second.srv_addr = srv->srv_addr;
  second.cli_fd = -1;
  h3r_client_start(&second);
  ck_assert_int_eq(h3r_pump_until(&second, h3r_cond_handshake, NULL), 1);
  ck_assert_int_eq(h3r_pump_until(&second, h3r_cond_server_ready_multi, NULL), 1);
  sid = h3r_request(&second, "GET", "/no/such/route", NULL, 0);
  ck_assert_int_eq(h3r_wait_response(&second, sid), 1);
  ck_assert_int_eq(h3r_resp_for(&second, sid)->status, 404);
  h3r_client_free(&second);
}

START_TEST(malformed_request_streams_never_break_the_server) {
  const case_t *tc = &cases[_i];
  h3rig_t h;
  buf_t *wire = heap_buf();
  int64_t sid;

  h3r_open(&h);
  tc->build(wire);
  ck_assert_int_eq(ngtcp2_conn_open_bidi_stream(h.qc, &sid, NULL), 0);
  h3r_send_raw(&h, sid, wire->data, wire->len, tc->fin);
  if (tc->expect_404) {
    ck_assert_int_eq(h3r_pump_until(&h, h3r_cond_raw_status, &sid), 1);
    ck_assert_int_eq(h3r_raw_status(&h, sid), 404);
  } else {
    (void)h3r_pump_timed(&h, cond_never, NULL, SETTLE_MS);
    ck_assert_int_le(h3r_raw_status(&h, sid), 0);
  }
  assert_server_still_serves(&h);
  free(wire);
  h3r_close(&h);
}
END_TEST

static Suite *req_suite(void) {
  Suite *s = suite_create("dipixy_h3_req");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_loop_test(tc, malformed_request_streams_never_break_the_server, 0, (int)(sizeof cases / sizeof cases[0]));
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(req_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
