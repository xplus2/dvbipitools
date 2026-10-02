/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdatomic.h>

#include "h2_client.h"
#include "dipixy/dash/lldash.h"
#include "dipixy/http2/http2_int.h"
#include "dipixy/reactor/internal.h"
#include "dipixy/segment/mp4push.h"
#include "dipixy/ts/ts_push.h"

#define FAKE_SUBS 8
#define FAKE_RING_BYTES 1024
#define KIND_COUNT 3
#define FAKE_TID 3
#define FAKE_WS_HANDLE 17

typedef struct {
  uint8_t data[FAKE_RING_BYTES];
  size_t len;
  size_t off;
  int errored;
  int finalized;
  int closed;
  void *h2c;
  void *h2_slot;
  int tid;
  int ws_handle;
} fake_sub_t;

static fake_sub_t fake_dash[FAKE_SUBS];
static fake_sub_t fake_mp4[FAKE_SUBS];
static int wake_calls;
static int32_t wake_last_sid;
static int ts_closed[FAKE_SUBS];

_Thread_local int t_reactor_tid = FAKE_TID;
ts_sub_t *g_ts_subs;
int g_ts_subs_n;

void h2_wake_stream(conn_t *c, int32_t sid) {
  if (!c || !sid) return;
  wake_calls++;
  wake_last_sid = sid;
  nghttp2_session_resume_data(((h2_conn_t *)c->h2)->ng, sid);
}

void ts_push_set_reactor_tid(int idx, int tid) {
  g_ts_subs[idx].reactor_tid = tid;
}

void ts_push_unsubscribe_by_idx(int idx) {
  ts_closed[idx]++;
}

static size_t fake_read(fake_sub_t *f, uint8_t *buf, size_t maxlen) {
  size_t n = f->len - f->off;

  if (n > maxlen) n = maxlen;
  memcpy(buf, f->data + f->off, n);
  f->off += n;
  return n;
}

static void fake_write(fake_sub_t *f, const uint8_t *data, size_t len) {
  ck_assert_uint_le(f->len + len, sizeof f->data);
  memcpy(f->data + f->len, data, len);
  f->len += len;
}

size_t dash_lldash_ring_read(int slot, uint8_t *buf, size_t maxlen) { return fake_read(&fake_dash[slot], buf, maxlen); }
int dash_lldash_ring_errored(int slot) { return fake_dash[slot].errored; }
int dash_lldash_sub_finalized(int slot) { return fake_dash[slot].finalized; }
void dash_lldash_sub_close(int slot) { fake_dash[slot].closed++; }
void *dash_lldash_sub_h2c(int slot) { return fake_dash[slot].h2c; }
void *dash_lldash_sub_h2_slot(int slot) { return fake_dash[slot].h2_slot; }

void dash_lldash_h2_bind(int slot, void *h2c, void *h2_slot, int reactor_tid, int ws_handle) {
  fake_dash[slot].h2c = h2c;
  fake_dash[slot].h2_slot = h2_slot;
  fake_dash[slot].tid = reactor_tid;
  fake_dash[slot].ws_handle = ws_handle;
}

size_t mp4push_ring_read(int slot, uint8_t *buf, size_t maxlen) { return fake_read(&fake_mp4[slot], buf, maxlen); }
int mp4push_ring_errored(int slot) { return fake_mp4[slot].errored; }
void mp4push_sub_close(int slot) { fake_mp4[slot].closed++; }
void *mp4push_sub_h2c(int slot) { return fake_mp4[slot].h2c; }
void *mp4push_sub_h2_slot(int slot) { return fake_mp4[slot].h2_slot; }

void mp4push_h2_bind(int slot, void *h2c, void *h2_slot, int reactor_tid, int ws_handle) {
  fake_mp4[slot].h2c = h2c;
  fake_mp4[slot].h2_slot = h2_slot;
  fake_mp4[slot].tid = reactor_tid;
  fake_mp4[slot].ws_handle = ws_handle;
}

typedef struct {
  h2c_t cl;
  nghttp2_session *srv;
  h2_conn_t conn;
  conn_t fake_conn;
  int32_t req_sid[H2C_STREAMS];
  int req_n;
} mem_t;

static int srv_on_frame(nghttp2_session *s, const nghttp2_frame *f, void *ud) {
  mem_t *m = ud;

  (void)s;
  if (f->hd.type == NGHTTP2_HEADERS && (f->hd.flags & NGHTTP2_FLAG_END_HEADERS) && m->req_n < H2C_STREAMS) m->req_sid[m->req_n++] = f->hd.stream_id;
  return 0;
}

static int srv_on_close(nghttp2_session *s, int32_t sid, uint32_t code, void *ud) {
  mem_t *m = ud;

  (void)s;
  (void)code;
  h2_tspush_on_stream_close(&m->conn, sid);
  h2_dashchunk_on_stream_close(&m->conn, sid);
  h2_mp4push_on_stream_close(&m->conn, sid);
  return 0;
}

static void mem_open(mem_t *m) {
  nghttp2_session_callbacks *cbs;

  memset(m, 0, sizeof *m);
  memset(fake_dash, 0, sizeof fake_dash);
  memset(fake_mp4, 0, sizeof fake_mp4);
  memset(ts_closed, 0, sizeof ts_closed);
  wake_calls = 0;
  wake_last_sid = 0;
  g_ts_subs_n = FAKE_SUBS;
  free(g_ts_subs);
  g_ts_subs = calloc(FAKE_SUBS, sizeof *g_ts_subs);
  ck_assert_ptr_nonnull(g_ts_subs);
  for (int i = 0; i < FAKE_SUBS; i++) byte_ring_reset(&g_ts_subs[i].h2_ring, FAKE_RING_BYTES);
  h2c_init(&m->cl);
  ck_assert_int_eq(nghttp2_session_callbacks_new(&cbs), 0);
  nghttp2_session_callbacks_set_on_frame_recv_callback(cbs, srv_on_frame);
  nghttp2_session_callbacks_set_on_stream_close_callback(cbs, srv_on_close);
  ck_assert_int_eq(nghttp2_session_server_new(&m->srv, cbs, m), 0);
  nghttp2_session_callbacks_del(cbs);
  ck_assert_int_eq(nghttp2_submit_settings(m->srv, NGHTTP2_FLAG_NONE, NULL, 0), 0);
  m->conn.ng = m->srv;
  m->conn.c = &m->fake_conn;
  m->fake_conn.h2 = &m->conn;
}

static void mem_close(mem_t *m) {
  nghttp2_session_del(m->srv);
  h2c_free(&m->cl);
  for (int i = 0; i < FAKE_SUBS; i++) byte_ring_free(&g_ts_subs[i].h2_ring);
  free(g_ts_subs);
  g_ts_subs = NULL;
}

static void mem_pump(mem_t *m) {
  uint8_t buf[H2C_IO_BUF];
  const uint8_t *out;
  ssize_t n;

  for (int round = 0; round < 6; round++) {
    size_t cn = h2c_take(&m->cl, buf, sizeof buf);

    if (cn) ck_assert_int_ge((int)nghttp2_session_mem_recv(m->srv, buf, cn), 0);
    while ((n = nghttp2_session_mem_send(m->srv, &out)) > 0) h2c_feed(&m->cl, out, (size_t)n);
  }
}

static int32_t mem_request(mem_t *m) {
  int32_t sid = h2c_request(&m->cl, "GET", "/push", NULL, 0);

  mem_pump(m);
  return sid;
}

typedef struct {
  const char *name;
  const char *content_type;
  int max;
  int (*dispatch)(mem_t *m, int32_t sid, int sub);
  void (*on_close)(h2_conn_t *conn, int32_t sid);
  h2_push_slot_t *(*slots)(h2_conn_t *conn);
  void (*wake)(int sub);
  void (*feed)(int sub, const uint8_t *data, size_t len);
  int (*closed)(int sub);
  void (*bound)(int sub, void **h2c, void **slot, int *tid, int *ws);
} push_kind_t;

static int ts_dispatch(mem_t *m, int32_t sid, int sub) {
  return h2_tspush_dispatch(&m->conn, &m->fake_conn, sid, sub);
}

static int dash_dispatch(mem_t *m, int32_t sid, int sub) {
  return h2_dashchunk_dispatch(&m->conn, &m->fake_conn, sid, sub, FAKE_WS_HANDLE);
}

static int mp4_dispatch(mem_t *m, int32_t sid, int sub) {
  return h2_mp4push_dispatch(&m->conn, &m->fake_conn, sid, sub, FAKE_WS_HANDLE);
}

static h2_push_slot_t *ts_slots(h2_conn_t *conn) { return conn->tspush; }
static h2_push_slot_t *dash_slots(h2_conn_t *conn) { return conn->dashchunk; }
static h2_push_slot_t *mp4_slots(h2_conn_t *conn) { return conn->mp4push; }

static void ts_feed(int sub, const uint8_t *data, size_t len) {
  ck_assert_int_eq(byte_ring_write(&g_ts_subs[sub].h2_ring, data, len), 1);
}

static void dash_feed(int sub, const uint8_t *data, size_t len) { fake_write(&fake_dash[sub], data, len); }
static void mp4_feed(int sub, const uint8_t *data, size_t len) { fake_write(&fake_mp4[sub], data, len); }

static int ts_closed_count(int sub) { return ts_closed[sub]; }
static int dash_closed_count(int sub) { return fake_dash[sub].closed; }
static int mp4_closed_count(int sub) { return fake_mp4[sub].closed; }

static void ts_bound(int sub, void **h2c, void **slot, int *tid, int *ws) {
  *h2c = g_ts_subs[sub].h2c;
  *slot = g_ts_subs[sub].h2_slot;
  *tid = g_ts_subs[sub].reactor_tid;
  *ws = FAKE_WS_HANDLE;
}

static void dash_bound(int sub, void **h2c, void **slot, int *tid, int *ws) {
  *h2c = fake_dash[sub].h2c;
  *slot = fake_dash[sub].h2_slot;
  *tid = fake_dash[sub].tid;
  *ws = fake_dash[sub].ws_handle;
}

static void mp4_bound(int sub, void **h2c, void **slot, int *tid, int *ws) {
  *h2c = fake_mp4[sub].h2c;
  *slot = fake_mp4[sub].h2_slot;
  *tid = fake_mp4[sub].tid;
  *ws = fake_mp4[sub].ws_handle;
}

static const push_kind_t kinds[KIND_COUNT] = {
    {"tspush", "video/mp2t", H2_TSPUSH_MAX, ts_dispatch, h2_tspush_on_stream_close, ts_slots, h2_tspush_wake, ts_feed, ts_closed_count, ts_bound},
    {"dashchunk", "video/mp4", H2_DASHCHUNK_MAX, dash_dispatch, h2_dashchunk_on_stream_close, dash_slots, h2_dashchunk_wake, dash_feed, dash_closed_count, dash_bound},
    {"mp4push", "video/mp4", H2_MP4PUSH_MAX, mp4_dispatch, h2_mp4push_on_stream_close, mp4_slots, h2_mp4push_wake, mp4_feed, mp4_closed_count, mp4_bound},
};

static void slots_reset_markers(h2_push_slot_t *slots, int max) {
  for (int i = 0; i < max; i++) slots[i].sub_idx = -1;
}

START_TEST(dispatch_answers_with_the_kind_content_type) {
  const push_kind_t *k = &kinds[_i];
  mem_t m;
  int32_t sid;
  client_resp_t *st;
  int found = 0;

  mem_open(&m);
  slots_reset_markers(k->slots(&m.conn), k->max);
  sid = mem_request(&m);
  ck_assert_int_eq(k->dispatch(&m, sid, 2), 1);
  mem_pump(&m);
  st = h2c_stream_for(&m.cl, sid);
  ck_assert_int_eq(st->status, 200);
  for (int i = 0; i < st->hdr_count; i++) {
    if (!strcmp(st->hdr_name[i], "content-type")) {
      ck_assert_str_eq(st->hdr_value[i], k->content_type);
      found = 1;
    }
  }
  ck_assert_int_eq(found, 1);
  ck_assert_int_eq(st->body_len, 0);
  ck_assert_int_eq(st->ended, 0);
  ck_assert_int_eq(k->slots(&m.conn)[0].sid, sid);
  ck_assert_int_eq(k->slots(&m.conn)[0].sub_idx, 2);
  mem_close(&m);
}
END_TEST

START_TEST(dispatch_binds_the_subscription_to_connection_and_thread) {
  const push_kind_t *k = &kinds[_i];
  mem_t m;
  int32_t sid;
  void *h2c = NULL;
  void *slot = NULL;
  int tid = -1;
  int ws = -1;

  mem_open(&m);
  sid = mem_request(&m);
  ck_assert_int_eq(k->dispatch(&m, sid, 1), 1);
  k->bound(1, &h2c, &slot, &tid, &ws);
  ck_assert_ptr_eq(h2c, &m.fake_conn);
  ck_assert_ptr_eq(slot, &k->slots(&m.conn)[0]);
  ck_assert_int_eq(tid, FAKE_TID);
  ck_assert_int_eq(ws, FAKE_WS_HANDLE);
  mem_close(&m);
}
END_TEST

START_TEST(data_arrives_only_after_a_wake) {
  const push_kind_t *k = &kinds[_i];
  static const uint8_t payload[] = "0123456789abcdef";
  mem_t m;
  int32_t sid;
  client_resp_t *st;

  mem_open(&m);
  sid = mem_request(&m);
  ck_assert_int_eq(k->dispatch(&m, sid, 0), 1);
  mem_pump(&m);
  k->feed(0, payload, sizeof payload - 1);
  mem_pump(&m);
  st = h2c_stream_for(&m.cl, sid);
  ck_assert_int_eq(st->body_len, 0);

  k->wake(0);
  ck_assert_int_eq(wake_calls, 1);
  ck_assert_int_eq(wake_last_sid, sid);
  mem_pump(&m);
  ck_assert_uint_eq(st->body_len, sizeof payload - 1);
  ck_assert_mem_eq(st->body, payload, sizeof payload - 1);
  ck_assert_int_eq(st->ended, 0);
  mem_close(&m);
}
END_TEST

START_TEST(slot_table_refuses_when_full_and_leaves_state_alone) {
  const push_kind_t *k = &kinds[_i];
  mem_t m;
  int32_t sids[H2C_STREAMS];

  mem_open(&m);
  ck_assert_int_lt(k->max, H2C_STREAMS);
  for (int i = 0; i <= k->max; i++) sids[i] = mem_request(&m);
  for (int i = 0; i < k->max; i++) ck_assert_int_eq(k->dispatch(&m, sids[i], i), 1);
  ck_assert_int_eq(k->dispatch(&m, sids[k->max], k->max), 0);
  for (int i = 0; i < k->max; i++) {
    ck_assert_int_eq(k->slots(&m.conn)[i].sid, sids[i]);
    ck_assert_int_eq(k->slots(&m.conn)[i].sub_idx, i);
  }
  mem_close(&m);
}
END_TEST

START_TEST(client_reset_frees_the_slot_and_closes_the_subscription) {
  const push_kind_t *k = &kinds[_i];
  mem_t m;
  int32_t sid;

  mem_open(&m);
  sid = mem_request(&m);
  ck_assert_int_eq(k->dispatch(&m, sid, 4), 1);
  mem_pump(&m);
  ck_assert_int_eq(k->closed(4), 0);
  ck_assert_int_eq(nghttp2_submit_rst_stream(m.cl.cli, NGHTTP2_FLAG_NONE, sid, NGHTTP2_CANCEL), 0);
  mem_pump(&m);
  ck_assert_int_eq(k->slots(&m.conn)[0].sid, 0);
  ck_assert_int_eq(k->slots(&m.conn)[0].sub_idx, -1);
  ck_assert_int_eq(k->closed(4), 1);

  sid = mem_request(&m);
  ck_assert_int_eq(k->dispatch(&m, sid, 5), 1);
  ck_assert_int_eq(k->slots(&m.conn)[0].sub_idx, 5);
  mem_close(&m);
}
END_TEST

START_TEST(closing_an_unrelated_stream_changes_nothing) {
  const push_kind_t *k = &kinds[_i];
  mem_t m;
  int32_t sid;

  mem_open(&m);
  sid = mem_request(&m);
  ck_assert_int_eq(k->dispatch(&m, sid, 2), 1);
  k->on_close(&m.conn, sid + 2);
  ck_assert_int_eq(k->slots(&m.conn)[0].sid, sid);
  ck_assert_int_eq(k->closed(2), 0);
  mem_close(&m);
}
END_TEST

START_TEST(wake_of_an_unbound_subscription_is_a_noop) {
  const push_kind_t *k = &kinds[_i];
  mem_t m;

  mem_open(&m);
  k->wake(6);
  ck_assert_int_eq(wake_calls, 0);
  mem_close(&m);
}
END_TEST

START_TEST(tspush_wake_ignores_out_of_range_indexes) {
  mem_t m;

  mem_open(&m);
  h2_tspush_wake(-1);
  h2_tspush_wake(FAKE_SUBS);
  ck_assert_int_eq(wake_calls, 0);
  mem_close(&m);
}
END_TEST

START_TEST(overflowed_ring_resets_the_stream) {
  mem_t m;
  int32_t sid;
  client_resp_t *st;
  const push_kind_t *k = &kinds[_i ? 2 : 1];

  mem_open(&m);
  sid = mem_request(&m);
  if (_i) fake_mp4[0].errored = 1;
  else fake_dash[0].errored = 1;
  ck_assert_int_eq(k->dispatch(&m, sid, 0), 1);
  mem_pump(&m);
  st = h2c_stream_for(&m.cl, sid);
  ck_assert_int_eq(st->closed, 1);
  ck_assert_uint_eq(st->close_code, NGHTTP2_INTERNAL_ERROR);
  mem_close(&m);
}
END_TEST

START_TEST(finalized_dash_segment_ends_the_stream_once_drained) {
  static const uint8_t payload[] = "chunk";
  mem_t m;
  int32_t sid;
  client_resp_t *st;

  mem_open(&m);
  sid = mem_request(&m);
  ck_assert_int_eq(kinds[1].dispatch(&m, sid, 0), 1);
  kinds[1].feed(0, payload, sizeof payload - 1);
  fake_dash[0].finalized = 1;
  kinds[1].wake(0);
  mem_pump(&m);
  st = h2c_stream_for(&m.cl, sid);
  ck_assert_uint_eq(st->body_len, sizeof payload - 1);
  ck_assert_mem_eq(st->body, payload, sizeof payload - 1);
  ck_assert_int_eq(st->ended, 1);
  mem_close(&m);
}
END_TEST

static Suite *push_suite(void) {
  Suite *s = suite_create("dipixy_http2_push");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 20);
  tcase_add_loop_test(tc, dispatch_answers_with_the_kind_content_type, 0, KIND_COUNT);
  tcase_add_loop_test(tc, dispatch_binds_the_subscription_to_connection_and_thread, 0, KIND_COUNT);
  tcase_add_loop_test(tc, data_arrives_only_after_a_wake, 0, KIND_COUNT);
  tcase_add_loop_test(tc, slot_table_refuses_when_full_and_leaves_state_alone, 0, KIND_COUNT);
  tcase_add_loop_test(tc, client_reset_frees_the_slot_and_closes_the_subscription, 0, KIND_COUNT);
  tcase_add_loop_test(tc, closing_an_unrelated_stream_changes_nothing, 0, KIND_COUNT);
  tcase_add_loop_test(tc, wake_of_an_unbound_subscription_is_a_noop, 0, KIND_COUNT);
  tcase_add_test(tc, tspush_wake_ignores_out_of_range_indexes);
  tcase_add_loop_test(tc, overflowed_ring_resets_the_stream, 0, 2);
  tcase_add_test(tc, finalized_dash_segment_ends_the_stream_once_drained);
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
