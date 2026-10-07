/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "h3_rig.h"
#include "resp_cases.h"
#include "dipixy/segment/segment.h"
#include "dipixy/ts/ts_push.h"
#include "dipixy/ws/ws_broadcast.h"
#include "dipixy/ws/ws_clients.h"
#include "dipixy/ws/ws_frame.h"
#include "../run_helper.h"

#define PARK_GROUP run_helper_group_n(30)
#define PARK_PORT run_helper_port(30)
#define PARK_PATH park_path()

static const char *park_path(void) {
  static char path[96];

  snprintf(path, sizeof path, "/udp/%s:%u/hls", PARK_GROUP, PARK_PORT);
  return path;
}
#define WS_PATH "/ui/ws/"
#define SETTLE_MS 100
#define COLD_WAITERS_MAX 64
#define LLHLS_WAITERS_MAX 8
#define TS_PKT 188

static int cond_never(h3rig_t *h, void *arg) {
  (void)h;
  (void)arg;
  return 0;
}

static h3_conn_t *server_conn(void) {
  ck_assert_int_eq(t_h3_active_cnt, 1);
  return t_h3_active[0];
}

static int64_t parked_request(h3rig_t *h, const char *path) {
  int64_t sid = h3r_request(h, "GET", path, NULL, 0);

  (void)h3r_pump_timed(h, cond_never, NULL, SETTLE_MS);
  ck_assert_ptr_nonnull(find_req(server_conn(), sid));
  return sid;
}

static void flush_server(const h3rig_t *h) {
  flush_tx(server_conn(), h->srv_fd);
}

static void submit_and_wait(h3rig_t *h, int64_t sid, int status, const char *ctype, const char *etag, size_t len, uint8_t *body, const char *origin) {
  h3_submit_resp(server_conn(), find_req(server_conn(), sid), status, ctype, etag, len, body, 0, origin);
  flush_server(h);
  ck_assert_int_eq(h3r_wait_response(h, sid), 1);
}

START_TEST(response_carries_headers_and_the_whole_body) {
  h3rig_t h;
  int64_t sid;
  client_resp_t *st;

  h3r_open(&h);
  sid = parked_request(&h, PARK_PATH);
  submit_and_wait(&h, sid, 200, "text/plain", "abc", MID_BODY, make_body(MID_BODY), NULL);
  st = h3r_resp_for(&h, sid);
  ck_assert_int_eq(st->status, 200);
  ck_assert_str_eq(client_resp_header_value(st, "content-type"), "text/plain");
  ck_assert_str_eq(client_resp_header_value(st, "content-length"), "1500");
  ck_assert_str_eq(client_resp_header_value(st, "etag"), "\"abc\"");
  ck_assert_str_eq(client_resp_header_value(st, "access-control-allow-origin"), "*");
  ck_assert_uint_eq(st->total_bytes, (size_t)MID_BODY);
  ck_assert_uint_eq(st->checksum, body_checksum(MID_BODY));
  h3r_close(&h);
}
END_TEST

START_TEST(large_body_is_streamed_in_full) {
  h3rig_t h;
  int64_t sid;
  client_resp_t *st;

  h3r_open(&h);
  sid = parked_request(&h, PARK_PATH);
  submit_and_wait(&h, sid, 200, "application/octet-stream", NULL, BIG_BODY, make_body(BIG_BODY), NULL);
  st = h3r_resp_for(&h, sid);
  ck_assert_uint_eq(st->total_bytes, (size_t)BIG_BODY);
  ck_assert_uint_eq(st->checksum, body_checksum(BIG_BODY));
  ck_assert_ptr_null(client_resp_header_value(st, "etag"));
  h3r_close(&h);
}
END_TEST

START_TEST(bodyless_response_ends_with_the_headers) {
  h3rig_t h;
  int64_t sid;
  client_resp_t *st;

  h3r_open(&h);
  sid = parked_request(&h, PARK_PATH);
  submit_and_wait(&h, sid, 304, NULL, "tag", 0, NULL, NULL);
  st = h3r_resp_for(&h, sid);
  ck_assert_int_eq(st->status, 304);
  ck_assert_ptr_null(client_resp_header_value(st, "content-type"));
  ck_assert_str_eq(client_resp_header_value(st, "content-length"), "0");
  ck_assert_str_eq(client_resp_header_value(st, "etag"), "\"tag\"");
  ck_assert_uint_eq(st->total_bytes, 0u);
  h3r_close(&h);
}
END_TEST

START_TEST(status_codes_map_to_the_supported_set) {
  const status_case_t *sc = &status_cases[_i];
  h3rig_t h;
  int64_t sid;

  h3r_open(&h);
  sid = parked_request(&h, PARK_PATH);
  submit_and_wait(&h, sid, sc->given, NULL, NULL, 0, NULL, NULL);
  ck_assert_int_eq(h3r_resp_for(&h, sid)->status, sc->expect);
  h3r_close(&h);
}
END_TEST

START_TEST(oversized_etag_is_clamped_to_the_header_buffer) {
  h3rig_t h;
  int64_t sid;
  char long_etag[200];
  const char *got;

  memset(long_etag, 'e', sizeof long_etag - 1);
  long_etag[sizeof long_etag - 1] = '\0';
  h3r_open(&h);
  sid = parked_request(&h, PARK_PATH);
  submit_and_wait(&h, sid, 200, NULL, long_etag, 0, NULL, NULL);
  got = client_resp_header_value(h3r_resp_for(&h, sid), "etag");
  ck_assert_ptr_nonnull(got);
  ck_assert_uint_eq(strlen(got), 56u);
  h3r_close(&h);
}
END_TEST

START_TEST(cors_allowlist_echoes_only_listed_origins) {
  const cors_case_t *cc = &cors_cases[_i];
  h3rig_t h;
  int64_t sid;
  client_resp_t *st;

  h3r_open(&h);
  h.cfg.cors_origins = cors_list;
  sid = parked_request(&h, PARK_PATH);
  submit_and_wait(&h, sid, 200, NULL, NULL, 0, NULL, cc->origin);
  st = h3r_resp_for(&h, sid);
  if (cc->expect_allow) ck_assert_str_eq(client_resp_header_value(st, "access-control-allow-origin"), cc->expect_allow);
  else ck_assert_ptr_null(client_resp_header_value(st, "access-control-allow-origin"));
  if (cc->expect_vary) ck_assert_str_eq(client_resp_header_value(st, "vary"), "Origin");
  else ck_assert_ptr_null(client_resp_header_value(st, "vary"));
  h3r_close(&h);
}
END_TEST

typedef struct {
  const char *route;
  int expect_status;
} cold_case_t;

static const cold_case_t cold_cases[] = {
    {"hls", 404},
    {"llhls", 404},
    {"dash", 404},
    {"mp4", 501},
};

START_TEST(cold_waiter_is_answered_when_its_deadline_passes) {
  const cold_case_t *cc = &cold_cases[_i];
  h3rig_t h;
  int64_t sid;
  char path[96];

  h3r_open(&h);
  snprintf(path, sizeof path, "/udp/%s:%u/%s", PARK_GROUP, PARK_PORT, cc->route);
  sid = parked_request(&h, path);
  ck_assert_int_eq(h3r_resp_for(&h, sid)->status, 0);
  h3_hls_cold_flush_waiters();
  ck_assert_int_eq(h3r_wait_response(&h, sid), 1);
  ck_assert_int_eq(h3r_resp_for(&h, sid)->status, cc->expect_status);
  h3_hls_cold_flush_waiters();
  h3r_close(&h);
}
END_TEST

START_TEST(cold_waiter_table_fills_and_releases_by_stream_and_connection) {
  h3rig_t h;
  h3_conn_t *c;
  capture_ctx_t *ctx;
  pid_filter_t filter = {0};
  lcevc_select_t lcevc = {0};

  h3r_open(&h);
  c = server_conn();
  ctx = capture_open(AF_INET, PARK_GROUP, PARK_PORT, NULL, 0, NULL, NULL, NULL, 0, 0);
  ck_assert_ptr_nonnull(ctx);
  {
    hls_cold_park_req_t req = {ctx, &filter, 0, &lcevc, "index.m3u8", HLS_COLD_HLS, SEG_CONTAINER_TS, 0, 0, 0, NULL, 60000, -1};

    for (int i = 0; i < COLD_WAITERS_MAX; i++) ck_assert_int_eq(h3_hls_cold_try_park(c, 1000 + 4 * i, &req), 1);
    ck_assert_int_eq(h3_hls_cold_try_park(c, 9999, &req), 0);
    h3_hls_cold_on_stream_close(c, 1000);
    ck_assert_int_eq(h3_hls_cold_try_park(c, 9999, &req), 1);
    ck_assert_int_eq(h3_hls_cold_try_park(c, 9995, &req), 0);
    h3_hls_cold_on_stream_close(c, 4242);
    ck_assert_int_eq(h3_hls_cold_try_park(c, 9995, &req), 0);
    h3_hls_cold_on_conn_close(c);
    for (int i = 0; i < COLD_WAITERS_MAX; i++) ck_assert_int_eq(h3_hls_cold_try_park(c, 1000 + 4 * i, &req), 1);
    h3_hls_cold_on_conn_close(c);
  }
  capture_close(ctx);
  h3r_close(&h);
}
END_TEST

START_TEST(llhls_waiter_is_answered_when_its_deadline_passes) {
  h3rig_t h;
  int64_t sid;
  capture_ctx_t *ctx;
  pid_filter_t filter = {0};
  lcevc_select_t lcevc = {0};

  h3r_open(&h);
  ctx = capture_open(AF_INET, PARK_GROUP, PARK_PORT, NULL, 0, NULL, NULL, NULL, 0, 0);
  ck_assert_ptr_nonnull(ctx);
  sid = parked_request(&h, PARK_PATH);
  {
    llhls_park_req_t req = {ctx, &filter, 0, &lcevc, "index_ll.m3u8", 0, 0, NULL, NULL, 5, 1, 0, -1};

    ck_assert_int_eq(h3_llhls_try_park(server_conn(), sid, &req), 1);
  }
  h3_llhls_flush_waiters();
  ck_assert_int_eq(h3r_wait_response(&h, sid), 1);
  ck_assert_int_eq(h3r_resp_for(&h, sid)->status, 404);
  capture_close(ctx);
  h3r_close(&h);
}
END_TEST

START_TEST(llhls_waiter_table_fills_and_releases_by_stream_and_connection) {
  h3rig_t h;
  h3_conn_t *c;
  capture_ctx_t *ctx;
  pid_filter_t filter = {0};
  lcevc_select_t lcevc = {0};

  h3r_open(&h);
  c = server_conn();
  ctx = capture_open(AF_INET, PARK_GROUP, PARK_PORT, NULL, 0, NULL, NULL, NULL, 0, 0);
  ck_assert_ptr_nonnull(ctx);
  {
    llhls_park_req_t req = {ctx, &filter, 0, &lcevc, "index_ll.m3u8", 0, 0, NULL, NULL, 5, 1, 60000, -1};

    for (int i = 0; i < LLHLS_WAITERS_MAX; i++) ck_assert_int_eq(h3_llhls_try_park(c, 1000 + 4 * i, &req), 1);
    ck_assert_int_eq(h3_llhls_try_park(c, 9999, &req), 0);
    h3_llhls_on_stream_close(c, 1000);
    ck_assert_int_eq(h3_llhls_try_park(c, 9999, &req), 1);
    h3_llhls_on_conn_close(c);
    for (int i = 0; i < LLHLS_WAITERS_MAX; i++) ck_assert_int_eq(h3_llhls_try_park(c, 1000 + 4 * i, &req), 1);
    h3_llhls_on_conn_close(c);
  }
  capture_close(ctx);
  h3r_close(&h);
}
END_TEST

static int64_t open_ws(h3rig_t *h) {
  int64_t sid;

  sid = h3r_connect(h, WS_PATH, "websocket");
  (void)h3r_pump_timed(h, cond_never, NULL, SETTLE_MS);
  return sid;
}

static void ws_send(h3rig_t *h, int64_t sid, int opcode, const uint8_t *payload, size_t len) {
  uint8_t frame[160];
  size_t n = client_ws_masked_frame(frame, opcode, payload, len);

  h3r_upload(h, sid, frame, n);
  (void)h3r_pump_timed(h, cond_never, NULL, SETTLE_MS);
}

START_TEST(websocket_connect_is_accepted_with_a_data_stream) {
  h3rig_t h;
  int64_t sid;

  h3r_open(&h);
  sid = open_ws(&h);
  ck_assert_int_eq(h3r_resp_for(&h, sid)->status, 200);
  ck_assert_int_eq(server_conn()->ws_active_count, 1);
  ck_assert_int_eq(ws_broadcast_has_sinks(), 1);
  h3r_close(&h);
}
END_TEST

START_TEST(websocket_connect_is_refused_without_the_status_page) {
  h3rig_t h;
  int64_t sid;

  h3r_open(&h);
  h.cfg.no_status = 1;
  sid = open_ws(&h);
  ck_assert_int_eq(h3r_resp_for(&h, sid)->status, 404);
  ck_assert_int_eq(server_conn()->ws_active_count, 0);
  h3r_close(&h);
}
END_TEST

START_TEST(websocket_ping_is_answered_with_a_pong) {
  h3rig_t h;
  int64_t sid;
  client_resp_t *st;
  static const uint8_t ping[] = {'h', 'i'};

  h3r_open(&h);
  sid = open_ws(&h);
  ws_send(&h, sid, WS_OP_PING, ping, sizeof ping);
  st = h3r_resp_for(&h, sid);
  ck_assert_uint_eq(st->body_len, 4u);
  ck_assert_uint_eq((uint8_t)st->body[0], 0x80 | WS_OP_PONG);
  ck_assert_mem_eq(st->body + 2, "hi", 2);
  h3r_close(&h);
}
END_TEST

START_TEST(websocket_close_is_echoed) {
  h3rig_t h;
  int64_t sid;
  client_resp_t *st;
  static const uint8_t code[] = {0x03, 0xE8};

  h3r_open(&h);
  sid = open_ws(&h);
  ws_send(&h, sid, WS_OP_CLOSE, code, sizeof code);
  st = h3r_resp_for(&h, sid);
  ck_assert_uint_eq(st->body_len, 4u);
  ck_assert_uint_eq((uint8_t)st->body[0], 0x80 | WS_OP_CLOSE);
  ck_assert_mem_eq(st->body + 2, code, sizeof code);
  h3r_close(&h);
}
END_TEST

START_TEST(websocket_client_snapshot_request_is_answered) {
  h3rig_t h;
  int64_t sid;
  client_resp_t *st;
  static const char req[] = "{\"type\":\"clients.get\"}";

  h3r_open(&h);
  sid = open_ws(&h);
  ws_send(&h, sid, WS_OP_TEXT, (const uint8_t *)req, sizeof req - 1);
  st = h3r_resp_for(&h, sid);
  ck_assert_uint_gt(st->body_len, 4u);
  ck_assert_ptr_nonnull(memmem(st->body, st->body_len, "clients", 7));
  h3r_close(&h);
}
END_TEST

START_TEST(websocket_unmasked_frame_is_ignored) {
  h3rig_t h;
  int64_t sid;
  static const uint8_t bad[] = {0x89, 0x02, 'h', 'i'};

  h3r_open(&h);
  sid = open_ws(&h);
  h3r_upload(&h, sid, bad, sizeof bad);
  (void)h3r_pump_timed(&h, cond_never, NULL, SETTLE_MS);
  ck_assert_uint_eq(h3r_resp_for(&h, sid)->body_len, 0u);
  ck_assert_int_eq(server_conn()->ws_active_count, 1);
  h3r_close(&h);
}
END_TEST

START_TEST(websocket_broadcast_reaches_the_stream) {
  h3rig_t h;
  int64_t sid;
  client_resp_t *st;

  h3r_open(&h);
  sid = open_ws(&h);
  ws_broadcast_publish("{\"type\":\"tick\"}");
  (void)h3r_pump_timed(&h, cond_never, NULL, SETTLE_MS);
  st = h3r_resp_for(&h, sid);
  ck_assert_uint_gt(st->body_len, 4u);
  ck_assert_ptr_nonnull(memmem(st->body, st->body_len, "tick", 4));
  h3r_close(&h);
}
END_TEST

START_TEST(websocket_stream_close_releases_the_sink) {
  h3rig_t h;
  int64_t sid;

  h3r_open(&h);
  sid = open_ws(&h);
  ck_assert_int_eq(ngtcp2_conn_shutdown_stream(h.qc, 0, sid, NGHTTP3_H3_REQUEST_CANCELLED), 0);
  (void)h3r_pump_timed(&h, cond_never, NULL, SETTLE_MS);
  ck_assert_int_eq(server_conn()->ws_active_count, 0);
  ck_assert_int_eq(ws_broadcast_has_sinks(), 0);
  h3r_close(&h);
}
END_TEST

START_TEST(connection_teardown_releases_open_websockets) {
  h3rig_t h;
  int64_t sid;

  h3r_open(&h);
  sid = open_ws(&h);
  ck_assert_int_gt((int)sid, -1);
  ck_assert_int_eq(ws_broadcast_has_sinks(), 1);
  h3conn_del(server_conn());
  ck_assert_int_eq(ws_broadcast_has_sinks(), 0);
  h3r_close(&h);
}
END_TEST

START_TEST(ts_push_streams_deliver_ring_data_and_release_the_subscription) {
  h3rig_t h;
  int64_t sid;
  h3_conn_t *c;
  h3_req_t *r;
  capture_ctx_t *ctx;
  pid_filter_t filter = {0};
  lcevc_select_t lcevc = {0};
  client_info_t info;
  uint8_t pkts[3 * TS_PKT];
  client_resp_t *st;
  int sub;

  h3r_open(&h);
  ts_push_init(1, 8);
  memset(&info, 0, sizeof info);
  ctx = capture_open(AF_INET, PARK_GROUP, PARK_PORT, NULL, 0, NULL, NULL, NULL, 0, 0);
  ck_assert_ptr_nonnull(ctx);
  sid = parked_request(&h, PARK_PATH);
  c = server_conn();
  r = find_req(c, sid);
  sub = ts_push_subscribe(ctx, &filter, CONN_PROTO_H3, -1, 0, 0, 0, &info, &lcevc);
  ck_assert_int_ge(sub, 0);
  ck_assert_int_eq(h3_tspush_dispatch(c, r, sub), 1);
  flush_server(&h);
  (void)h3r_pump_timed(&h, cond_never, NULL, SETTLE_MS);
  st = h3r_resp_for(&h, sid);
  ck_assert_int_eq(st->status, 200);
  ck_assert_str_eq(client_resp_header_value(st, "content-type"), "video/mp2t");
  ck_assert_uint_eq(st->total_bytes, 0u);

  for (size_t i = 0; i < sizeof pkts; i++) pkts[i] = (uint8_t)(i * 5 + 1);
  ts_push_h3_enqueue(sub, pkts, sizeof pkts);
  h3_tspush_wake(sub);
  (void)h3r_pump_timed(&h, cond_never, NULL, SETTLE_MS);
  ck_assert_uint_eq(st->total_bytes, sizeof pkts);
  ck_assert_mem_eq(st->body, pkts, sizeof pkts);

  h3_tspush_wake(-1);
  h3_tspush_wake(g_ts_subs_n);
  ck_assert_int_eq(ngtcp2_conn_shutdown_stream(h.qc, 0, sid, NGHTTP3_H3_REQUEST_CANCELLED), 0);
  (void)h3r_pump_timed(&h, cond_never, NULL, SETTLE_MS);
  capture_flush_deferred_quiescent();
  ck_assert_int_eq(atomic_load(&g_ts_subs[sub].alive), TS_SUB_FREE);
  capture_close(ctx);
  h3r_close(&h);
}
END_TEST

static void setup_registries(void) {
  ws_clients_init(8);
  hls_seg_init(8);
}

static Suite *handlers_suite(void) {
  Suite *s = suite_create("dipixy_h3_handlers");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 30);
  tcase_add_checked_fixture(tc, setup_registries, NULL);
  tcase_add_test(tc, response_carries_headers_and_the_whole_body);
  tcase_add_test(tc, large_body_is_streamed_in_full);
  tcase_add_test(tc, bodyless_response_ends_with_the_headers);
  tcase_add_loop_test(tc, status_codes_map_to_the_supported_set, 0, (int)(sizeof status_cases / sizeof status_cases[0]));
  tcase_add_test(tc, oversized_etag_is_clamped_to_the_header_buffer);
  tcase_add_loop_test(tc, cors_allowlist_echoes_only_listed_origins, 0, (int)(sizeof cors_cases / sizeof cors_cases[0]));
  tcase_add_loop_test(tc, cold_waiter_is_answered_when_its_deadline_passes, 0, (int)(sizeof cold_cases / sizeof cold_cases[0]));
  tcase_add_test(tc, cold_waiter_table_fills_and_releases_by_stream_and_connection);
  tcase_add_test(tc, llhls_waiter_is_answered_when_its_deadline_passes);
  tcase_add_test(tc, llhls_waiter_table_fills_and_releases_by_stream_and_connection);
  tcase_add_test(tc, websocket_connect_is_accepted_with_a_data_stream);
  tcase_add_test(tc, websocket_connect_is_refused_without_the_status_page);
  tcase_add_test(tc, websocket_ping_is_answered_with_a_pong);
  tcase_add_test(tc, websocket_close_is_echoed);
  tcase_add_test(tc, websocket_client_snapshot_request_is_answered);
  tcase_add_test(tc, websocket_unmasked_frame_is_ignored);
  tcase_add_test(tc, websocket_broadcast_reaches_the_stream);
  tcase_add_test(tc, websocket_stream_close_releases_the_sink);
  tcase_add_test(tc, connection_teardown_releases_open_websockets);
  tcase_add_test(tc, ts_push_streams_deliver_ring_data_and_release_the_subscription);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(handlers_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
