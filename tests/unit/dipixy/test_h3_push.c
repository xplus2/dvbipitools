/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdatomic.h>

#include "dipixy/http3/http3_int.h"

#define FAKE_SUBS 8
#define FAKE_RING_BYTES 256
#define KIND_COUNT 3
#define FAKE_TID 5
#define FAKE_WS_HANDLE 21
#define FAKE_FD 9

typedef struct {
  uint8_t data[FAKE_RING_BYTES];
  size_t len;
  size_t off;
  int errored;
  int finalized;
  h3_conn_t *c;
  int64_t sid;
  int tid;
  int ws_handle;
} fake_sub_t;

static fake_sub_t fake_dash[FAKE_SUBS];
static fake_sub_t fake_mp4[FAKE_SUBS];
static int flush_calls;
static const h3_conn_t *flush_last;

_Thread_local int t_reactor_tid = FAKE_TID;
_Thread_local int t_h3_udp4 = -1;
_Thread_local int t_h3_udp6 = -1;
ts_sub_t *g_ts_subs;
int g_ts_subs_n;

void ts_push_set_reactor_tid(int idx, int tid) {
  g_ts_subs[idx].reactor_tid = tid;
}

void flush_tx(h3_conn_t *c, int udp_fd) {
  (void)udp_fd;
  flush_calls++;
  flush_last = c;
}

static const uint8_t *fake_peek(const fake_sub_t *f, size_t *len) {
  if (f->off >= f->len) return NULL;
  *len = f->len - f->off;
  return f->data + f->off;
}

static void fake_advance(fake_sub_t *f, size_t n) {
  f->off += n;
}

static void fake_write(fake_sub_t *f, const uint8_t *data, size_t len) {
  ck_assert_uint_le(f->len + len, sizeof f->data);
  memcpy(f->data + f->len, data, len);
  f->len += len;
}

const uint8_t *dash_lldash_ring_peek(int slot, size_t *len) { return fake_peek(&fake_dash[slot], len); }
void dash_lldash_ring_advance(int slot, size_t n) { fake_advance(&fake_dash[slot], n); }
int dash_lldash_ring_errored(int slot) { return fake_dash[slot].errored; }
int dash_lldash_sub_finalized(int slot) { return fake_dash[slot].finalized; }
void *dash_lldash_sub_h3c(int slot) { return fake_dash[slot].c; }
int64_t dash_lldash_sub_h3_sid(int slot) { return fake_dash[slot].sid; }

void dash_lldash_h3_bind(int slot, void *h3c, int64_t sid, int tid, int ws_handle) {
  fake_dash[slot].c = h3c;
  fake_dash[slot].sid = sid;
  fake_dash[slot].tid = tid;
  fake_dash[slot].ws_handle = ws_handle;
}

const uint8_t *mp4push_ring_peek(int slot, size_t *len) { return fake_peek(&fake_mp4[slot], len); }
void mp4push_ring_advance(int slot, size_t n) { fake_advance(&fake_mp4[slot], n); }
int mp4push_ring_errored(int slot) { return fake_mp4[slot].errored; }
void *mp4push_sub_h3c(int slot) { return fake_mp4[slot].c; }
int64_t mp4push_sub_h3_sid(int slot) { return fake_mp4[slot].sid; }

void mp4push_h3_bind(int slot, void *h3c, int64_t sid, int tid, int ws_handle) {
  fake_mp4[slot].c = h3c;
  fake_mp4[slot].sid = sid;
  fake_mp4[slot].tid = tid;
  fake_mp4[slot].ws_handle = ws_handle;
}

typedef struct {
  const char *name;
  nghttp3_ssize (*read_cb)(nghttp3_conn *, int64_t, nghttp3_vec *, size_t, uint32_t *, void *, void *);
  int (*dispatch)(h3_conn_t *c, h3_req_t *r, int sub);
  void (*wake)(int sub);
  void (*feed)(int sub, const uint8_t *data, size_t len);
  void (*set_sub)(h3_req_t *r, int sub);
  int (*get_sub)(const h3_req_t *r);
  void (*bound)(int sub, h3_conn_t **c, int64_t *sid, int *tid, int *ws);
  void (*bind_for_wake)(int sub, h3_conn_t *c, int64_t sid);
  int eof_when_errored;
  int eof_when_finalized;
} push_kind_t;

static int ts_dispatch(h3_conn_t *c, h3_req_t *r, int sub) { return h3_tspush_dispatch(c, r, sub); }
static int dash_dispatch(h3_conn_t *c, h3_req_t *r, int sub) { return h3_dashchunk_dispatch(c, r, sub, FAKE_WS_HANDLE); }
static int mp4_dispatch(h3_conn_t *c, h3_req_t *r, int sub) { return h3_mp4push_dispatch(c, r, sub, FAKE_WS_HANDLE); }

static void ts_feed(int sub, const uint8_t *data, size_t len) {
  ck_assert_int_eq(byte_ring_write(&g_ts_subs[sub].h3_ring, data, len), 1);
}

static void dash_feed(int sub, const uint8_t *data, size_t len) { fake_write(&fake_dash[sub], data, len); }
static void mp4_feed(int sub, const uint8_t *data, size_t len) { fake_write(&fake_mp4[sub], data, len); }

static void ts_set_sub(h3_req_t *r, int sub) { r->tspush_sub_idx = sub; }
static void dash_set_sub(h3_req_t *r, int sub) { r->dashchunk_sub_idx = sub; }
static void mp4_set_sub(h3_req_t *r, int sub) { r->mp4push_sub_idx = sub; }
static int ts_get_sub(const h3_req_t *r) { return r->tspush_sub_idx; }
static int dash_get_sub(const h3_req_t *r) { return r->dashchunk_sub_idx; }
static int mp4_get_sub(const h3_req_t *r) { return r->mp4push_sub_idx; }

static void ts_bound(int sub, h3_conn_t **c, int64_t *sid, int *tid, int *ws) {
  *c = g_ts_subs[sub].h3c;
  *sid = g_ts_subs[sub].h3_sid;
  *tid = g_ts_subs[sub].reactor_tid;
  *ws = FAKE_WS_HANDLE;
}

static void dash_bound(int sub, h3_conn_t **c, int64_t *sid, int *tid, int *ws) {
  *c = fake_dash[sub].c;
  *sid = fake_dash[sub].sid;
  *tid = fake_dash[sub].tid;
  *ws = fake_dash[sub].ws_handle;
}

static void mp4_bound(int sub, h3_conn_t **c, int64_t *sid, int *tid, int *ws) {
  *c = fake_mp4[sub].c;
  *sid = fake_mp4[sub].sid;
  *tid = fake_mp4[sub].tid;
  *ws = fake_mp4[sub].ws_handle;
}

static void ts_bind_for_wake(int sub, h3_conn_t *c, int64_t sid) {
  g_ts_subs[sub].h3c = c;
  g_ts_subs[sub].h3_sid = sid;
}

static void dash_bind_for_wake(int sub, h3_conn_t *c, int64_t sid) {
  fake_dash[sub].c = c;
  fake_dash[sub].sid = sid;
}

static void mp4_bind_for_wake(int sub, h3_conn_t *c, int64_t sid) {
  fake_mp4[sub].c = c;
  fake_mp4[sub].sid = sid;
}

static const push_kind_t kinds[KIND_COUNT] = {
    {"tspush", h3_tspush_read_cb, ts_dispatch, h3_tspush_wake, ts_feed, ts_set_sub, ts_get_sub, ts_bound, ts_bind_for_wake, 0, 0},
    {"dashchunk", h3_dashchunk_read_cb, dash_dispatch, h3_dashchunk_wake, dash_feed, dash_set_sub, dash_get_sub, dash_bound, dash_bind_for_wake, 1, 1},
    {"mp4push", h3_mp4push_read_cb, mp4_dispatch, h3_mp4push_wake, mp4_feed, mp4_set_sub, mp4_get_sub, mp4_bound, mp4_bind_for_wake, 1, 0},
};

static void world_open(h3_conn_t *c) {
  nghttp3_callbacks cbs = {0};
  nghttp3_settings st;

  memset(fake_dash, 0, sizeof fake_dash);
  memset(fake_mp4, 0, sizeof fake_mp4);
  flush_calls = 0;
  flush_last = NULL;
  t_h3_udp4 = -1;
  t_h3_udp6 = -1;
  g_ts_subs_n = FAKE_SUBS;
  g_ts_subs = calloc(FAKE_SUBS, sizeof *g_ts_subs);
  ck_assert_ptr_nonnull(g_ts_subs);
  for (int i = 0; i < FAKE_SUBS; i++) {
    byte_ring_reset(&g_ts_subs[i].h3_ring, FAKE_RING_BYTES);
    atomic_store(&g_ts_subs[i].alive, TS_SUB_ALIVE);
  }
  memset(c, 0, sizeof *c);
  nghttp3_settings_default(&st);
  ck_assert_int_eq(nghttp3_conn_server_new(&c->h3conn, &cbs, &st, nghttp3_mem_default(), c), 0);
  ck_assert_int_eq(nghttp3_conn_bind_control_stream(c->h3conn, 3), 0);
  ck_assert_int_eq(nghttp3_conn_bind_qpack_streams(c->h3conn, 7, 11), 0);
  c->local_addr.ss_family = AF_INET;
}

static void world_close(h3_conn_t *c) {
  nghttp3_conn_del(c->h3conn);
  for (int i = 0; i < FAKE_SUBS; i++) byte_ring_free(&g_ts_subs[i].h3_ring);
  free(g_ts_subs);
  g_ts_subs = NULL;
}

static void make_req(h3_req_t *r, int64_t sid) {
  memset(r, 0, sizeof *r);
  r->stream_id = sid;
  r->tspush_sub_idx = -1;
  r->dashchunk_sub_idx = -1;
  r->mp4push_sub_idx = -1;
}

START_TEST(read_callback_ends_the_stream_without_a_request_or_subscription) {
  const push_kind_t *k = &kinds[_i];
  h3_conn_t c;
  h3_req_t r;
  nghttp3_vec vec[1];
  uint32_t flags = 0;

  world_open(&c);
  ck_assert_int_eq((int)k->read_cb(NULL, 0, vec, 1, &flags, &c, NULL), 0);
  ck_assert_uint_eq(flags, (uint32_t)NGHTTP3_DATA_FLAG_EOF);
  flags = 0;
  make_req(&r, 4);
  ck_assert_int_eq((int)k->read_cb(NULL, 4, vec, 1, &flags, &c, &r), 0);
  ck_assert_uint_eq(flags, (uint32_t)NGHTTP3_DATA_FLAG_EOF);
  world_close(&c);
}
END_TEST

START_TEST(read_callback_blocks_on_an_empty_ring) {
  const push_kind_t *k = &kinds[_i];
  h3_conn_t c;
  h3_req_t r;
  nghttp3_vec vec[1];
  uint32_t flags = 0;

  world_open(&c);
  make_req(&r, 4);
  k->set_sub(&r, 2);
  ck_assert_int_eq((int)k->read_cb(NULL, 4, vec, 1, &flags, &c, &r), NGHTTP3_ERR_WOULDBLOCK);
  world_close(&c);
}
END_TEST

START_TEST(read_callback_hands_out_ring_bytes_and_consumes_them) {
  const push_kind_t *k = &kinds[_i];
  static const uint8_t payload[] = "abcdefghij";
  h3_conn_t c;
  h3_req_t r;
  nghttp3_vec vec[1];
  uint32_t flags = 99;

  world_open(&c);
  make_req(&r, 4);
  k->set_sub(&r, 2);
  k->feed(2, payload, sizeof payload - 1);
  ck_assert_int_eq((int)k->read_cb(NULL, 4, vec, 1, &flags, &c, &r), 1);
  ck_assert_uint_eq(flags, (uint32_t)NGHTTP3_DATA_FLAG_NONE);
  ck_assert_uint_eq(vec[0].len, sizeof payload - 1);
  ck_assert_mem_eq(vec[0].base, payload, sizeof payload - 1);
  ck_assert_int_eq((int)k->read_cb(NULL, 4, vec, 1, &flags, &c, &r), NGHTTP3_ERR_WOULDBLOCK);
  world_close(&c);
}
END_TEST

START_TEST(read_callback_ends_after_an_overflowed_ring_where_supported) {
  const push_kind_t *k = &kinds[_i];
  h3_conn_t c;
  h3_req_t r;
  nghttp3_vec vec[1];
  uint32_t flags = 0;

  world_open(&c);
  make_req(&r, 4);
  k->set_sub(&r, 3);
  if (k->eof_when_errored) {
    if (_i == 1) fake_dash[3].errored = 1;
    else fake_mp4[3].errored = 1;
    ck_assert_int_eq((int)k->read_cb(NULL, 4, vec, 1, &flags, &c, &r), 0);
    ck_assert_uint_eq(flags, (uint32_t)NGHTTP3_DATA_FLAG_EOF);
  } else {
    atomic_store(&g_ts_subs[3].alive, TS_SUB_CLOSING);
    ck_assert_int_eq((int)k->read_cb(NULL, 4, vec, 1, &flags, &c, &r), 0);
    ck_assert_uint_eq(flags, (uint32_t)NGHTTP3_DATA_FLAG_EOF);
  }
  world_close(&c);
}
END_TEST

START_TEST(finalized_dash_segments_end_the_stream_once_drained) {
  h3_conn_t c;
  h3_req_t r;
  nghttp3_vec vec[1];
  uint32_t flags = 0;
  static const uint8_t payload[] = "tail";

  world_open(&c);
  make_req(&r, 4);
  r.dashchunk_sub_idx = 1;
  fake_dash[1].finalized = 1;
  fake_write(&fake_dash[1], payload, sizeof payload - 1);
  ck_assert_int_eq((int)h3_dashchunk_read_cb(NULL, 4, vec, 1, &flags, &c, &r), 1);
  ck_assert_int_eq((int)h3_dashchunk_read_cb(NULL, 4, vec, 1, &flags, &c, &r), 0);
  ck_assert_uint_eq(flags, (uint32_t)NGHTTP3_DATA_FLAG_EOF);
  world_close(&c);
}
END_TEST

START_TEST(dispatch_binds_the_subscription_to_stream_and_thread) {
  const push_kind_t *k = &kinds[_i];
  h3_conn_t c;
  h3_req_t r;
  h3_conn_t *bc = NULL;
  int64_t sid = -1;
  int tid = -1;
  int ws = -1;

  world_open(&c);
  make_req(&r, 8);
  ck_assert_int_eq(k->dispatch(&c, &r, 4), 1);
  ck_assert_int_eq(k->get_sub(&r), 4);
  k->bound(4, &bc, &sid, &tid, &ws);
  ck_assert_ptr_eq(bc, &c);
  ck_assert_int_eq((int)sid, 8);
  ck_assert_int_eq(tid, FAKE_TID);
  ck_assert_int_eq(ws, FAKE_WS_HANDLE);
  if (_i == 0) ck_assert_int_eq(atomic_load(&g_ts_subs[4].ready), 1);
  world_close(&c);
}
END_TEST

START_TEST(wake_resumes_only_bound_live_connections) {
  const push_kind_t *k = &kinds[_i];
  h3_conn_t c;

  world_open(&c);
  k->wake(6);
  ck_assert_int_eq(flush_calls, 0);
  k->bind_for_wake(6, &c, 8);
  k->wake(6);
  ck_assert_int_eq(flush_calls, 0);
  t_h3_udp4 = FAKE_FD;
  k->wake(6);
  ck_assert_int_eq(flush_calls, 1);
  ck_assert_ptr_eq(flush_last, &c);
  if (_i == 0) {
    c.done = 1;
    k->wake(6);
    ck_assert_int_eq(flush_calls, 1);
  }
  world_close(&c);
}
END_TEST

START_TEST(tspush_wake_ignores_out_of_range_indexes) {
  h3_conn_t c;

  world_open(&c);
  t_h3_udp4 = FAKE_FD;
  h3_tspush_wake(-1);
  h3_tspush_wake(FAKE_SUBS);
  ck_assert_int_eq(flush_calls, 0);
  world_close(&c);
}
END_TEST

static Suite *push_suite(void) {
  Suite *s = suite_create("dipixy_h3_push");
  TCase *tc = tcase_create("core");
  tcase_add_loop_test(tc, read_callback_ends_the_stream_without_a_request_or_subscription, 0, KIND_COUNT);
  tcase_add_loop_test(tc, read_callback_blocks_on_an_empty_ring, 0, KIND_COUNT);
  tcase_add_loop_test(tc, read_callback_hands_out_ring_bytes_and_consumes_them, 0, KIND_COUNT);
  tcase_add_loop_test(tc, read_callback_ends_after_an_overflowed_ring_where_supported, 0, KIND_COUNT);
  tcase_add_test(tc, finalized_dash_segments_end_the_stream_once_drained);
  tcase_add_loop_test(tc, dispatch_binds_the_subscription_to_stream_and_thread, 0, KIND_COUNT);
  tcase_add_loop_test(tc, wake_resumes_only_bound_live_connections, 0, KIND_COUNT);
  tcase_add_test(tc, tspush_wake_ignores_out_of_range_indexes);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(push_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
