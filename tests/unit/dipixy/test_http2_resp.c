/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "h2_rig.h"
#include "resp_cases.h"
#include "dipixy/altsvc.h"
#include "dipixy/ws/ws_broadcast.h"
#include "dipixy/ws/ws_clients.h"
#include "dipixy/ws/ws_frame.h"
#include "../run_helper.h"

#define PATH_PLAIN "/plain"
#define WS_PATH "/ui/ws/"
#define COLD_GROUP run_helper_group_n(20)
#define COLD_PORT run_helper_port(20)
#define COLD_WAITERS_MAX 64
#define LLHLS_WAITERS_MAX 8

static int32_t open_stream(rig_t *r) {
  int32_t sid = rig_request(r, "GET", PATH_PLAIN, NULL, 0);

  rig_feed_server_direct(r);
  return sid;
}

static void settle(rig_t *r) {
  rig_settle_server_output(r);
  rig_exchange(r);
}

START_TEST(response_carries_headers_and_the_whole_body) {
  rig_t r;
  int32_t sid;
  client_resp_t *st;

  rig_open(&r);
  sid = open_stream(&r);
  h2_submit_resp(r.h2, sid, 200, "text/plain", "abc", MID_BODY, make_body(MID_BODY), 0, NULL);
  settle(&r);
  st = h2c_stream_for(&r.cl, sid);
  ck_assert_int_eq(st->status, 200);
  ck_assert_str_eq(client_resp_header_value(st, "content-type"), "text/plain");
  ck_assert_str_eq(client_resp_header_value(st, "content-length"), "1500");
  ck_assert_str_eq(client_resp_header_value(st, "etag"), "\"abc\"");
  ck_assert_str_eq(client_resp_header_value(st, "access-control-allow-origin"), "*");
  ck_assert_uint_eq(st->total_bytes, (size_t)MID_BODY);
  ck_assert_uint_eq(st->checksum, body_checksum(MID_BODY));
  ck_assert_int_eq(st->ended, 1);
  rig_close(&r);
}
END_TEST

START_TEST(large_body_is_streamed_across_flow_control_windows) {
  rig_t r;
  int32_t sid;
  client_resp_t *st;

  rig_open(&r);
  sid = open_stream(&r);
  h2_submit_resp(r.h2, sid, 200, "application/octet-stream", NULL, BIG_BODY, make_body(BIG_BODY), 0, NULL);
  settle(&r);
  st = h2c_stream_for(&r.cl, sid);
  ck_assert_uint_eq(st->total_bytes, (size_t)BIG_BODY);
  ck_assert_uint_eq(st->checksum, body_checksum(BIG_BODY));
  ck_assert_int_eq(st->ended, 1);
  ck_assert_ptr_null(client_resp_header_value(st, "etag"));
  rig_close(&r);
}
END_TEST

START_TEST(bodyless_response_ends_with_the_headers) {
  rig_t r;
  int32_t sid;
  client_resp_t *st;

  rig_open(&r);
  sid = open_stream(&r);
  h2_submit_resp(r.h2, sid, 304, NULL, "tag", 0, NULL, 0, NULL);
  settle(&r);
  st = h2c_stream_for(&r.cl, sid);
  ck_assert_int_eq(st->status, 304);
  ck_assert_ptr_null(client_resp_header_value(st, "content-type"));
  ck_assert_str_eq(client_resp_header_value(st, "content-length"), "0");
  ck_assert_str_eq(client_resp_header_value(st, "etag"), "\"tag\"");
  ck_assert_uint_eq(st->total_bytes, 0u);
  ck_assert_int_eq(st->ended, 1);
  rig_close(&r);
}
END_TEST

START_TEST(status_codes_map_to_the_supported_set) {
  const status_case_t *sc = &status_cases[_i];
  rig_t r;
  int32_t sid;

  rig_open(&r);
  sid = open_stream(&r);
  h2_submit_resp(r.h2, sid, sc->given, NULL, NULL, 0, NULL, 0, NULL);
  settle(&r);
  ck_assert_int_eq(h2c_stream_for(&r.cl, sid)->status, sc->expect);
  rig_close(&r);
}
END_TEST

START_TEST(oversized_etag_is_clamped_to_the_header_buffer) {
  rig_t r;
  int32_t sid;
  char long_etag[200];
  const char *got;

  memset(long_etag, 'e', sizeof long_etag - 1);
  long_etag[sizeof long_etag - 1] = '\0';
  rig_open(&r);
  sid = open_stream(&r);
  h2_submit_resp(r.h2, sid, 200, NULL, long_etag, 0, NULL, 0, NULL);
  settle(&r);
  got = client_resp_header_value(h2c_stream_for(&r.cl, sid), "etag");
  ck_assert_ptr_nonnull(got);
  ck_assert_uint_eq(strlen(got), 56u);
  ck_assert_int_eq(got[0], '"');
  ck_assert_int_eq(got[strlen(got) - 1], '"');
  rig_close(&r);
}
END_TEST

START_TEST(empty_etag_is_omitted) {
  rig_t r;
  int32_t sid;

  rig_open(&r);
  sid = open_stream(&r);
  h2_submit_resp(r.h2, sid, 200, NULL, "", 0, NULL, 0, NULL);
  settle(&r);
  ck_assert_ptr_null(client_resp_header_value(h2c_stream_for(&r.cl, sid), "etag"));
  rig_close(&r);
}
END_TEST

START_TEST(cors_allowlist_echoes_only_listed_origins) {
  const cors_case_t *cc = &cors_cases[_i];
  rig_t r;
  int32_t sid;
  client_resp_t *st;

  rig_open(&r);
  r.cfg.cors_origins = cors_list;
  sid = open_stream(&r);
  h2_submit_resp(r.h2, sid, 200, NULL, NULL, 0, NULL, 0, cc->origin);
  settle(&r);
  st = h2c_stream_for(&r.cl, sid);
  if (cc->expect_allow) ck_assert_str_eq(client_resp_header_value(st, "access-control-allow-origin"), cc->expect_allow);
  else ck_assert_ptr_null(client_resp_header_value(st, "access-control-allow-origin"));
  if (cc->expect_vary) ck_assert_str_eq(client_resp_header_value(st, "vary"), "Origin");
  else ck_assert_ptr_null(client_resp_header_value(st, "vary"));
  rig_close(&r);
}
END_TEST

START_TEST(alt_svc_is_advertised_when_enabled) {
  rig_t r;
  int32_t sid;

  rig_open(&r);
  altsvc_set(4433);
  sid = open_stream(&r);
  h2_submit_resp(r.h2, sid, 200, NULL, NULL, 0, NULL, 0, NULL);
  settle(&r);
  ck_assert_ptr_nonnull(client_resp_header_value(h2c_stream_for(&r.cl, sid), "alt-svc"));
  altsvc_set(0);
  rig_close(&r);
}
END_TEST

START_TEST(consecutive_bodies_reuse_the_source_pool) {
  rig_t r;
  int32_t sids[3];

  rig_open(&r);
  for (int i = 0; i < 3; i++) {
    sids[i] = open_stream(&r);
    h2_submit_resp(r.h2, sids[i], 200, NULL, NULL, 300, make_body(300), 0, NULL);
    settle(&r);
    ck_assert_uint_eq(h2c_stream_for(&r.cl, sids[i])->checksum, body_checksum(300));
    ck_assert_int_eq(h2c_stream_for(&r.cl, sids[i])->ended, 1);
  }
  rig_close(&r);
}
END_TEST

typedef struct {
  hls_cold_kind_t kind;
  const char *file;
  int expect_status;
} cold_case_t;

static const cold_case_t cold_cases[] = {
    {HLS_COLD_HLS, "index.m3u8", 404},
    {HLS_COLD_LLHLS, "index_ll.m3u8", 404},
    {HLS_COLD_DASH, "manifest.mpd", 404},
    {HLS_COLD_MP4, "", 501},
};

static capture_ctx_t *open_cold_ctx(void) {
  capture_ctx_t *ctx = capture_open(AF_INET, COLD_GROUP, COLD_PORT, NULL, 0, NULL, NULL, NULL, 0, 0);

  ck_assert_ptr_nonnull(ctx);
  return ctx;
}

START_TEST(cold_waiter_is_answered_when_its_deadline_passes) {
  const cold_case_t *cc = &cold_cases[_i];
  rig_t r;
  int32_t sid;
  capture_ctx_t *ctx = open_cold_ctx();
  pid_filter_t filter = {0};
  lcevc_select_t lcevc = {0};
  hls_cold_park_req_t req = {ctx, &filter, 0, &lcevc, cc->file, cc->kind, SEG_CONTAINER_TS, 0, 0, 0, NULL, 0, -1};

  rig_open(&r);
  sid = open_stream(&r);
  ck_assert_int_eq(h2_hls_cold_try_park(r.h2, sid, &req), 1);
  rig_settle_server_output(&r);
  ck_assert_int_eq(h2c_stream_for(&r.cl, sid)->status, 0);
  h2_hls_cold_flush_waiters();
  rig_exchange(&r);
  ck_assert_int_eq(h2c_stream_for(&r.cl, sid)->status, cc->expect_status);
  h2_hls_cold_flush_waiters();
  rig_close(&r);
  capture_close(ctx);
}
END_TEST

START_TEST(cold_waiter_table_fills_and_releases_by_stream_and_connection) {
  rig_t r;
  capture_ctx_t *ctx = open_cold_ctx();
  pid_filter_t filter = {0};
  lcevc_select_t lcevc = {0};
  hls_cold_park_req_t req = {ctx, &filter, 0, &lcevc, "index.m3u8", HLS_COLD_HLS, SEG_CONTAINER_TS, 0, 0, 0, NULL, 60000, -1};

  rig_open(&r);
  for (int i = 0; i < COLD_WAITERS_MAX; i++) ck_assert_int_eq(h2_hls_cold_try_park(r.h2, 1 + 2 * i, &req), 1);
  ck_assert_int_eq(h2_hls_cold_try_park(r.h2, 999, &req), 0);
  h2_hls_cold_on_stream_close(r.h2, 1);
  ck_assert_int_eq(h2_hls_cold_try_park(r.h2, 999, &req), 1);
  ck_assert_int_eq(h2_hls_cold_try_park(r.h2, 1001, &req), 0);
  h2_hls_cold_on_stream_close(r.h2, 4242);
  ck_assert_int_eq(h2_hls_cold_try_park(r.h2, 1001, &req), 0);
  h2_hls_cold_on_conn_close(r.h2);
  for (int i = 0; i < COLD_WAITERS_MAX; i++) ck_assert_int_eq(h2_hls_cold_try_park(r.h2, 1 + 2 * i, &req), 1);
  h2_hls_cold_on_conn_close(r.h2);
  rig_close(&r);
  capture_close(ctx);
}
END_TEST

START_TEST(llhls_waiter_is_answered_when_its_deadline_passes) {
  rig_t r;
  int32_t sid;
  capture_ctx_t *ctx = open_cold_ctx();
  pid_filter_t filter = {0};
  lcevc_select_t lcevc = {0};
  llhls_park_req_t req = {ctx, &filter, 0, &lcevc, "index_ll.m3u8", 0, 0, NULL, NULL, 5, 1, 0, -1};

  rig_open(&r);
  sid = open_stream(&r);
  ck_assert_int_eq(h2_llhls_try_park(r.h2, r.c, sid, &req), 1);
  h2_llhls_flush_waiters();
  rig_exchange(&r);
  ck_assert_int_eq(h2c_stream_for(&r.cl, sid)->status, 404);
  rig_close(&r);
  capture_close(ctx);
}
END_TEST

START_TEST(llhls_waiter_table_fills_and_releases_by_stream_and_connection) {
  rig_t r;
  capture_ctx_t *ctx = open_cold_ctx();
  pid_filter_t filter = {0};
  lcevc_select_t lcevc = {0};
  llhls_park_req_t req = {ctx, &filter, 0, &lcevc, "index_ll.m3u8", 0, 0, NULL, NULL, 5, 1, 60000, -1};

  rig_open(&r);
  for (int i = 0; i < LLHLS_WAITERS_MAX; i++) ck_assert_int_eq(h2_llhls_try_park(r.h2, r.c, 1 + 2 * i, &req), 1);
  ck_assert_int_eq(h2_llhls_try_park(r.h2, r.c, 999, &req), 0);
  h2_llhls_on_stream_close(r.h2, 3);
  ck_assert_int_eq(h2_llhls_try_park(r.h2, r.c, 999, &req), 1);
  h2_llhls_on_conn_close(r.h2);
  for (int i = 0; i < LLHLS_WAITERS_MAX; i++) ck_assert_int_eq(h2_llhls_try_park(r.h2, r.c, 1 + 2 * i, &req), 1);
  h2_llhls_on_conn_close(r.h2);
  rig_close(&r);
  capture_close(ctx);
}
END_TEST

static int32_t open_ws(rig_t *r) {
  int32_t sid;

  ws_clients_init(8);
  sid = h2c_connect(&r->cl, WS_PATH, "websocket");
  rig_exchange(r);
  return sid;
}

static void ws_send(rig_t *r, int32_t sid, int opcode, const uint8_t *payload, size_t len) {
  uint8_t frame[160];
  size_t n = client_ws_masked_frame(frame, opcode, payload, len);

  h2c_upload(&r->cl, sid, frame, n);
  rig_exchange(r);
}

START_TEST(websocket_connect_is_accepted_with_a_data_stream) {
  rig_t r;
  int32_t sid;

  rig_open(&r);
  sid = open_ws(&r);
  ck_assert_int_eq(h2c_stream_for(&r.cl, sid)->status, 200);
  ck_assert_int_eq(r.h2->ws[0].sid, sid);
  ck_assert_int_eq(ws_broadcast_has_sinks(), 1);
  rig_close(&r);
}
END_TEST

START_TEST(websocket_ping_is_answered_with_a_pong) {
  rig_t r;
  int32_t sid;
  client_resp_t *st;
  static const uint8_t ping[] = {'h', 'i'};

  rig_open(&r);
  sid = open_ws(&r);
  ws_send(&r, sid, WS_OP_PING, ping, sizeof ping);
  st = h2c_stream_for(&r.cl, sid);
  ck_assert_uint_eq(st->body_len, 4u);
  ck_assert_uint_eq((uint8_t)st->body[0], 0x80 | WS_OP_PONG);
  ck_assert_uint_eq((uint8_t)st->body[1], 2u);
  ck_assert_mem_eq(st->body + 2, "hi", 2);
  rig_close(&r);
}
END_TEST

START_TEST(websocket_close_is_echoed) {
  rig_t r;
  int32_t sid;
  client_resp_t *st;
  static const uint8_t code[] = {0x03, 0xE8};

  rig_open(&r);
  sid = open_ws(&r);
  ws_send(&r, sid, WS_OP_CLOSE, code, sizeof code);
  st = h2c_stream_for(&r.cl, sid);
  ck_assert_uint_eq(st->body_len, 4u);
  ck_assert_uint_eq((uint8_t)st->body[0], 0x80 | WS_OP_CLOSE);
  ck_assert_mem_eq(st->body + 2, code, sizeof code);
  rig_close(&r);
}
END_TEST

START_TEST(websocket_client_snapshot_request_is_answered) {
  rig_t r;
  int32_t sid;
  client_resp_t *st;
  static const char req[] = "{\"type\":\"clients.get\"}";

  rig_open(&r);
  sid = open_ws(&r);
  ws_send(&r, sid, WS_OP_TEXT, (const uint8_t *)req, sizeof req - 1);
  st = h2c_stream_for(&r.cl, sid);
  ck_assert_uint_gt(st->body_len, 4u);
  ck_assert_uint_eq((uint8_t)st->body[0], 0x80 | WS_OP_TEXT);
  ck_assert_ptr_nonnull(memmem(st->body, st->body_len, "clients", 7));
  rig_close(&r);
}
END_TEST

START_TEST(websocket_unrelated_text_gets_no_reply) {
  rig_t r;
  int32_t sid;
  static const char msg[] = "{\"type\":\"nothing\"}";

  rig_open(&r);
  sid = open_ws(&r);
  ws_send(&r, sid, WS_OP_TEXT, (const uint8_t *)msg, sizeof msg - 1);
  ck_assert_uint_eq(h2c_stream_for(&r.cl, sid)->body_len, 0u);
  rig_close(&r);
}
END_TEST

START_TEST(websocket_unmasked_frame_is_rejected_quietly) {
  rig_t r;
  int32_t sid;
  static const uint8_t bad[] = {0x89, 0x02, 'h', 'i'};

  rig_open(&r);
  sid = open_ws(&r);
  h2c_upload(&r.cl, sid, bad, sizeof bad);
  rig_exchange(&r);
  ck_assert_uint_eq(h2c_stream_for(&r.cl, sid)->body_len, 0u);
  ck_assert_int_eq(r.h2->ws[0].sid, sid);
  rig_close(&r);
}
END_TEST

START_TEST(websocket_broadcast_reaches_the_stream) {
  rig_t r;
  int32_t sid;
  client_resp_t *st;

  rig_open(&r);
  sid = open_ws(&r);
  ws_broadcast_publish("{\"type\":\"tick\"}");
  h2_handle_writable(r.epfd, r.c);
  rig_exchange(&r);
  st = h2c_stream_for(&r.cl, sid);
  ck_assert_uint_gt(st->body_len, 4u);
  ck_assert_uint_eq((uint8_t)st->body[0], 0x80 | WS_OP_TEXT);
  ck_assert_ptr_nonnull(memmem(st->body, st->body_len, "tick", 4));
  rig_close(&r);
}
END_TEST

START_TEST(websocket_table_full_answers_service_unavailable) {
  rig_t r;
  int32_t sids[H2_WS_MAX + 1];

  rig_open(&r);
  ws_clients_init(8);
  for (int i = 0; i <= H2_WS_MAX; i++) sids[i] = h2c_connect(&r.cl, WS_PATH, "websocket");
  rig_exchange(&r);
  for (int i = 0; i < H2_WS_MAX; i++) ck_assert_int_eq(h2c_stream_for(&r.cl, sids[i])->status, 200);
  ck_assert_int_eq(h2c_stream_for(&r.cl, sids[H2_WS_MAX])->status, 503);
  rig_close(&r);
}
END_TEST

START_TEST(websocket_stream_close_releases_the_slot_and_sink) {
  rig_t r;
  int32_t sid;

  rig_open(&r);
  sid = open_ws(&r);
  ck_assert_int_eq(nghttp2_submit_rst_stream(r.cl.cli, NGHTTP2_FLAG_NONE, sid, NGHTTP2_CANCEL), 0);
  rig_exchange(&r);
  ck_assert_int_eq(r.h2->ws[0].sid, 0);
  ck_assert_int_eq(ws_broadcast_has_sinks(), 0);
  rig_close(&r);
}
END_TEST

START_TEST(websocket_data_on_other_streams_is_ignored) {
  rig_t r;
  static const uint8_t data[] = {'x'};

  rig_open(&r);
  h2_ws_data_chunk(r.h2, 77, data, sizeof data);
  h2_ws_on_stream_close(r.h2, 77);
  ck_assert_int_eq(ws_broadcast_has_sinks(), 0);
  rig_close(&r);
}
END_TEST

START_TEST(connection_close_releases_open_websockets) {
  rig_t r;
  int32_t sid;

  rig_open(&r);
  sid = open_ws(&r);
  ck_assert_int_gt(sid, 0);
  ck_assert_int_eq(ws_broadcast_has_sinks(), 1);
  h2_conn_close(r.epfd, r.c);
  ck_assert_int_eq(ws_broadcast_has_sinks(), 0);
  ck_assert_int_eq(rig_server_alive(&r), 0);
  rig_close(&r);
}
END_TEST

static Suite *resp_suite(void) {
  Suite *s = suite_create("dipixy_http2_resp");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 20);
  tcase_add_test(tc, response_carries_headers_and_the_whole_body);
  tcase_add_test(tc, large_body_is_streamed_across_flow_control_windows);
  tcase_add_test(tc, bodyless_response_ends_with_the_headers);
  tcase_add_loop_test(tc, status_codes_map_to_the_supported_set, 0, (int)(sizeof status_cases / sizeof status_cases[0]));
  tcase_add_test(tc, oversized_etag_is_clamped_to_the_header_buffer);
  tcase_add_test(tc, empty_etag_is_omitted);
  tcase_add_loop_test(tc, cors_allowlist_echoes_only_listed_origins, 0, (int)(sizeof cors_cases / sizeof cors_cases[0]));
  tcase_add_test(tc, alt_svc_is_advertised_when_enabled);
  tcase_add_test(tc, consecutive_bodies_reuse_the_source_pool);
  tcase_add_loop_test(tc, cold_waiter_is_answered_when_its_deadline_passes, 0, (int)(sizeof cold_cases / sizeof cold_cases[0]));
  tcase_add_test(tc, cold_waiter_table_fills_and_releases_by_stream_and_connection);
  tcase_add_test(tc, llhls_waiter_is_answered_when_its_deadline_passes);
  tcase_add_test(tc, llhls_waiter_table_fills_and_releases_by_stream_and_connection);
  tcase_add_test(tc, websocket_connect_is_accepted_with_a_data_stream);
  tcase_add_test(tc, websocket_ping_is_answered_with_a_pong);
  tcase_add_test(tc, websocket_close_is_echoed);
  tcase_add_test(tc, websocket_client_snapshot_request_is_answered);
  tcase_add_test(tc, websocket_unrelated_text_gets_no_reply);
  tcase_add_test(tc, websocket_unmasked_frame_is_rejected_quietly);
  tcase_add_test(tc, websocket_broadcast_reaches_the_stream);
  tcase_add_test(tc, websocket_table_full_answers_service_unavailable);
  tcase_add_test(tc, websocket_stream_close_releases_the_slot_and_sink);
  tcase_add_test(tc, websocket_data_on_other_streams_is_ignored);
  tcase_add_test(tc, connection_close_releases_open_websockets);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(resp_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
