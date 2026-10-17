/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>

#include "h2_rig.h"

#define PATH_UNKNOWN "/no/such/route"

static size_t read_all_client(const rig_t *r, uint8_t *buf, size_t cap) {
  ssize_t n = read(r->sv[1], buf, cap);

  return n > 0 ? (size_t)n : 0;
}

START_TEST(clean_request_walks_the_callback_chain) {
  rig_t r;
  nghttp2_nv extra[] = {
      MAKE_NV_LIT("if-none-match", "\"abc\""),
      MAKE_NV_LIT("origin", "https://example.test"),
      MAKE_NV_LIT("authorization", "Basic Zm9vOmJhcg=="),
  };
  int32_t sid;
  h2_stream_t *s;

  rig_open(&r);
  sid = rig_request(&r, "GET", PATH_UNKNOWN, extra, 3);
  rig_feed_server_direct(&r);

  s = h2_find_stream(r.h2, sid);
  ck_assert_ptr_nonnull(s);
  ck_assert_str_eq(s->method, "GET");
  ck_assert_str_eq(s->path, PATH_UNKNOWN);
  ck_assert_str_eq(s->inm, "\"abc\"");
  ck_assert_str_eq(s->origin, "https://example.test");
  ck_assert_str_eq(s->authz, "Basic Zm9vOmJhcg==");
  ck_assert_str_eq(s->protocol, "");
  ck_assert_int_eq(s->dispatch_pending, 1);
  ck_assert_ptr_eq(r.h2->hdr_stream, s);
  ck_assert_int_eq(r.h2->pending_n, 1);
  ck_assert_ptr_eq(r.h2->pending[0], s);

  h2_dispatch_stream(r.h2, r.c, s);
  rig_settle_server_output(&r);
  ck_assert_int_eq(h2c_stream_for(&r.cl, sid)->status, 404);
  ck_assert_int_eq(h2c_stream_for(&r.cl, sid)->closed, 1);
  ck_assert_int_eq(h2_conn_active_count(r.h2), 0);
  rig_close(&r);
}
END_TEST

typedef struct {
  const char *method;
  const char *path;
  int expect_status;
} dispatch_case_t;

static const dispatch_case_t dispatch_cases[] = {
    {"GET", PATH_UNKNOWN, 404},
    {"HEAD", PATH_UNKNOWN, 404},
    {"POST", "/", 405},
    {"DELETE", PATH_UNKNOWN, 405},
    {"PUT", "/x", 405},
};

START_TEST(request_over_the_socket_is_dispatched_and_answered) {
  const dispatch_case_t *dc = &dispatch_cases[_i];
  rig_t r;
  int32_t sid;

  rig_open(&r);
  rig_exchange(&r);
  sid = rig_request(&r, dc->method, dc->path, NULL, 0);
  rig_exchange(&r);
  ck_assert_int_eq(h2c_stream_for(&r.cl, sid)->status, dc->expect_status);
  ck_assert_int_eq(h2c_stream_for(&r.cl, sid)->closed, 1);
  ck_assert_int_eq(h2_conn_active_count(r.h2), 0);
  rig_close(&r);
}
END_TEST

START_TEST(extended_connect_without_status_page_is_not_found) {
  rig_t r;
  nghttp2_nv extra[] = {MAKE_NV_LIT(":protocol", "websocket")};
  int32_t sid;

  rig_open(&r);
  r.cfg.no_status = 1;
  rig_exchange(&r);
  sid = rig_request(&r, "CONNECT", "/ui/ws/", extra, 1);
  rig_exchange(&r);
  ck_assert_int_eq(h2c_stream_for(&r.cl, sid)->status, 404);
  rig_close(&r);
}
END_TEST

START_TEST(extended_connect_with_wrong_path_is_not_found) {
  rig_t r;
  nghttp2_nv extra[] = {MAKE_NV_LIT(":protocol", "websocket")};
  int32_t sid;

  rig_open(&r);
  rig_exchange(&r);
  sid = rig_request(&r, "CONNECT", "/elsewhere", extra, 1);
  rig_exchange(&r);
  ck_assert_int_eq(h2c_stream_for(&r.cl, sid)->status, 404);
  rig_close(&r);
}
END_TEST

START_TEST(extended_connect_without_credentials_gets_a_challenge) {
  rig_t r;
  nghttp2_nv extra[] = {MAKE_NV_LIT(":protocol", "websocket")};
  client_resp_t *st;
  int32_t sid;
  int saw_challenge = 0;

  rig_open(&r);
  snprintf(r.cfg.http_auth, sizeof r.cfg.http_auth, "%s", "Basic Zm9vOmJhcg==");
  rig_exchange(&r);
  sid = rig_request(&r, "CONNECT", "/ui/ws/", extra, 1);
  rig_exchange(&r);
  st = h2c_stream_for(&r.cl, sid);
  ck_assert_int_eq(st->status, 401);
  for (int i = 0; i < st->hdr_count; i++) {
    if (!strcmp(st->hdr_name[i], "www-authenticate")) saw_challenge = 1;
  }
  ck_assert_int_eq(saw_challenge, 1);
  rig_close(&r);
}
END_TEST

START_TEST(stream_slots_are_exhausted_and_reused) {
  rig_t r;
  h2_stream_t *slots[H2_MAX_STREAMS];

  rig_open(&r);
  ck_assert_int_eq(h2_conn_active_count(r.h2), 0);
  for (int i = 0; i < H2_MAX_STREAMS; i++) {
    slots[i] = h2_alloc_stream(r.h2, 1001 + 2 * i);
    ck_assert_ptr_nonnull(slots[i]);
    ck_assert_int_eq(h2_conn_active_count(r.h2), i + 1);
  }
  ck_assert_ptr_null(h2_alloc_stream(r.h2, 5001));
  ck_assert_ptr_eq(h2_find_stream(r.h2, 1001 + 2 * 7), slots[7]);
  ck_assert_ptr_null(h2_find_stream(r.h2, 5001));

  strcpy(slots[7]->path, "/stale");
  h2_free_stream(r.h2, 1001 + 2 * 7);
  ck_assert_int_eq(h2_conn_active_count(r.h2), H2_MAX_STREAMS - 1);
  ck_assert_ptr_null(h2_find_stream(r.h2, 1001 + 2 * 7));
  h2_free_stream(r.h2, 1001 + 2 * 7);
  ck_assert_int_eq(h2_conn_active_count(r.h2), H2_MAX_STREAMS - 1);

  ck_assert_ptr_eq(h2_alloc_stream(r.h2, 5001), slots[7]);
  ck_assert_str_eq(slots[7]->path, "");
  ck_assert_int_eq(slots[7]->dispatch_pending, 0);
  rig_close(&r);
}
END_TEST

START_TEST(find_ignores_unknown_and_unused_ids) {
  rig_t r;

  rig_open(&r);
  ck_assert_ptr_null(h2_find_stream(r.h2, 1));
  ck_assert_ptr_nonnull(h2_alloc_stream(r.h2, 1));
  ck_assert_ptr_nonnull(h2_find_stream(r.h2, 1));
  ck_assert_ptr_null(h2_find_stream(r.h2, 3));
  h2_free_stream(r.h2, 3);
  ck_assert_int_eq(h2_conn_active_count(r.h2), 1);
  rig_close(&r);
}
END_TEST

START_TEST(request_is_refused_when_every_slot_is_taken) {
  rig_t r;
  int32_t sid;

  rig_open(&r);
  rig_exchange(&r);
  for (int i = 0; i < H2_MAX_STREAMS; i++) ck_assert_ptr_nonnull(h2_alloc_stream(r.h2, 1001 + 2 * i));
  sid = rig_request(&r, "GET", PATH_UNKNOWN, NULL, 0);
  rig_exchange(&r);
  ck_assert_int_eq(h2c_stream_for(&r.cl, sid)->closed, 1);
  ck_assert_uint_eq(h2c_stream_for(&r.cl, sid)->close_code, NGHTTP2_REFUSED_STREAM);
  ck_assert_int_eq(h2c_stream_for(&r.cl, sid)->status, 0);
  rig_close(&r);
}
END_TEST

START_TEST(data_before_headers_is_a_connection_error) {
  rig_t r;
  uint8_t frame[64];
  const uint8_t payload[] = {'x', 'y'};
  size_t n;

  rig_open(&r);
  rig_exchange(&r);
  n = h2c_raw_frame(frame, NGHTTP2_DATA, NGHTTP2_FLAG_NONE, 1, payload, sizeof payload);
  rig_client_send_raw(&r, frame, n);
  if (rig_server_alive(&r)) rig_server_read(&r);
  rig_client_recv(&r);
  ck_assert_int_eq(r.cl.goaway, 1);
  ck_assert_uint_eq(r.cl.goaway_code, NGHTTP2_PROTOCOL_ERROR);
  if (rig_server_alive(&r)) ck_assert_int_eq(h2_conn_active_count(r.h2), 0);
  rig_close(&r);
}
END_TEST

START_TEST(headers_after_stream_close_are_ignored) {
  rig_t r;
  uint8_t frame[H2C_IO_BUF];
  h2c_frame_t out[8];
  nghttp2_nv nva[] = {
      MAKE_NV_LIT(":method", "GET"),
      MAKE_NV_LIT(":scheme", "https"),
      MAKE_NV_LIT(":path", PATH_UNKNOWN),
  };
  int32_t sid;
  size_t n;
  size_t frames;

  rig_open(&r);
  rig_exchange(&r);
  sid = rig_request(&r, "GET", PATH_UNKNOWN, NULL, 0);
  rig_exchange(&r);
  ck_assert_int_eq(h2c_stream_for(&r.cl, sid)->closed, 1);

  n = h2c_raw_headers(frame, sid, nva, 3);
  rig_client_send_raw(&r, frame, n);
  if (rig_server_alive(&r)) rig_server_read(&r);
  frames = rig_scan_frames(&r, out, 8);
  ck_assert_uint_le(frames, 1u);
  if (frames == 1u) {
    ck_assert(out[0].type == NGHTTP2_RST_STREAM || out[0].type == NGHTTP2_GOAWAY);
    if (out[0].type == NGHTTP2_RST_STREAM) {
      ck_assert_int_eq(out[0].sid, sid);
      ck_assert_uint_eq(out[0].code, NGHTTP2_STREAM_CLOSED);
    }
  }
  if (rig_server_alive(&r)) {
    ck_assert_int_eq(h2_conn_active_count(r.h2), 0);
    ck_assert_int_eq(r.h2->pending_n, 0);
  }
  rig_close(&r);
}
END_TEST

START_TEST(duplicate_path_pseudo_header_is_rejected_with_protocol_error) {
  rig_t r;
  uint8_t frame[H2C_IO_BUF];
  h2c_frame_t out[8];
  nghttp2_nv nva[] = {
      MAKE_NV_LIT(":method", "GET"),
      MAKE_NV_LIT(":scheme", "https"),
      MAKE_NV_LIT(":path", "/first"),
      MAKE_NV_LIT(":path", "/second"),
  };
  size_t n;
  size_t frames;

  rig_open(&r);
  rig_exchange(&r);
  n = h2c_raw_headers(frame, 1, nva, 4);
  rig_client_send_raw(&r, frame, n);
  if (rig_server_alive(&r)) rig_server_read(&r);
  frames = rig_scan_frames(&r, out, 8);
  ck_assert_uint_eq(frames, 1u);
  ck_assert(out[0].type == NGHTTP2_RST_STREAM || out[0].type == NGHTTP2_GOAWAY);
  ck_assert_uint_eq(out[0].code, NGHTTP2_PROTOCOL_ERROR);
  if (out[0].type == NGHTTP2_RST_STREAM) {
    ck_assert_int_eq(out[0].sid, 1);
    ck_assert_int_eq(h2_conn_active_count(r.h2), 0);
  }
  ck_assert_int_eq(r.h2->pending_n, 0);
  rig_close(&r);
}
END_TEST

static size_t raw_headers_open(uint8_t *out, int32_t sid, const nghttp2_nv *nva, size_t n) {
  nghttp2_hd_deflater *d;
  uint8_t block[1024];
  ssize_t blen;
  ck_assert_int_eq(nghttp2_hd_deflate_new(&d, 4096), 0);
  blen = nghttp2_hd_deflate_hd(d, block, sizeof block, nva, n);
  ck_assert_int_gt((int)blen, 0);
  nghttp2_hd_deflate_del(d);
  return h2c_raw_frame(out, NGHTTP2_HEADERS, NGHTTP2_FLAG_END_HEADERS, sid, block, (size_t)blen);
}

static int count_response_headers(rig_t *r, int32_t sid) {
  h2c_frame_t out[16];
  size_t frames = rig_scan_frames(r, out, 16);
  int n = 0;
  for (size_t i = 0; i < frames; i++)
    if (out[i].type == NGHTTP2_HEADERS && out[i].sid == sid) n++;
  return n;
}

static void post_with_chunks(rig_t *r, int32_t sid, const char *path, size_t chunk, size_t nchunks, int end_with_empty) {
  uint8_t frame[H2C_IO_BUF];
  uint8_t payload[4096];
  nghttp2_nv nva[] = {
      MAKE_NV_LIT(":method", "POST"),
      MAKE_NV_LIT(":scheme", "https"),
      MAKE_NV_LIT(":authority", "localhost"),
      {(uint8_t *)":path", (uint8_t *)path, 5, strlen(path), NGHTTP2_NV_FLAG_NONE},
  };
  size_t n = raw_headers_open(frame, sid, nva, 4);
  memset(payload, 'b', sizeof payload);
  rig_client_send_raw(r, frame, n);
  if (rig_server_alive(r)) rig_server_read(r);
  for (size_t i = 0; i < nchunks; i++) {
    int last = (i + 1 == nchunks) && !end_with_empty;
    n = h2c_raw_frame(frame, NGHTTP2_DATA, last ? NGHTTP2_FLAG_END_STREAM : NGHTTP2_FLAG_NONE, sid, payload, chunk);
    rig_client_send_raw(r, frame, n);
    if (rig_server_alive(r)) rig_server_read(r);
  }
  if (end_with_empty) {
    n = h2c_raw_frame(frame, NGHTTP2_DATA, NGHTTP2_FLAG_END_STREAM, sid, payload, 0);
    rig_client_send_raw(r, frame, n);
    if (rig_server_alive(r)) rig_server_read(r);
  }
}

START_TEST(post_body_is_collected_until_end_stream_then_dispatched) {
  static const struct {
    size_t chunk;
    size_t nchunks;
    int empty_end;
  } t[] = {{100, 1, 0}, {1000, 3, 0}, {1000, 2, 1}, {4096, 3, 0}};
  rig_t r;
  rig_open(&r);
  rig_exchange(&r);
  post_with_chunks(&r, 1, "/dlna/cd_control", t[_i].chunk, t[_i].nchunks, t[_i].empty_end);
  ck_assert(rig_server_alive(&r));
  ck_assert_int_eq(count_response_headers(&r, 1), 1);
  rig_close(&r);
}
END_TEST

START_TEST(post_body_over_the_limit_is_flagged_and_still_answered) {
  rig_t r;
  rig_open(&r);
  rig_exchange(&r);
  post_with_chunks(&r, 1, "/dlna/cd_control", 4096, 4, 0);
  ck_assert(rig_server_alive(&r));
  ck_assert_int_eq(count_response_headers(&r, 1), 1);
  rig_close(&r);
}
END_TEST

START_TEST(reactor_loop_routes_h2_connections_by_event_type) {
  rig_t r;
  reactor_listeners_t rl;
  struct epoll_event in = {.events = EPOLLIN};
  struct epoll_event out = {.events = EPOLLOUT};
  struct epoll_event hup = {.events = EPOLLHUP};
  int32_t sid;

  memset(&rl, 0, sizeof rl);
  rig_open(&r);
  rig_exchange(&r);
  in.data.ptr = r.c;
  out.data.ptr = r.c;
  hup.data.ptr = r.c;
  sid = rig_request(&r, "GET", PATH_UNKNOWN, NULL, 0);
  rig_client_send(&r);
  reactor_handle_event(r.epfd, &rl, 0, &in);
  rig_client_recv(&r);
  ck_assert_int_eq(h2c_stream_for(&r.cl, sid)->status, 404);
  reactor_handle_event(r.epfd, &rl, 0, &out);
  ck_assert_int_eq(rig_server_alive(&r), 1);
  reactor_handle_event(r.epfd, &rl, 0, &hup);
  ck_assert_int_eq(rig_server_alive(&r), 0);
  rig_close(&r);
}
END_TEST

START_TEST(peer_goaway_without_streams_closes_gracefully) {
  rig_t r;

  rig_open(&r);
  rig_exchange(&r);
  ck_assert_int_eq(nghttp2_submit_goaway(r.cl.cli, NGHTTP2_FLAG_NONE, 0, NGHTTP2_NO_ERROR, NULL, 0), 0);
  rig_client_send(&r);
  rig_server_read(&r);
  ck_assert_int_eq(rig_server_alive(&r), 0);
  rig_close(&r);
}
END_TEST

START_TEST(idle_close_sends_goaway) {
  rig_t r;
  h2c_frame_t frames[8];
  size_t n;
  int goaway = 0;

  rig_open(&r);
  rig_exchange(&r);
  atomic_store(&r.c->close_requested, 1);
  shutdown(r.sv[1], SHUT_WR);
  rig_server_read(&r);
  ck_assert_int_eq(rig_server_alive(&r), 0);
  n = rig_scan_frames(&r, frames, 8);
  for (size_t i = 0; i < n; i++)
    if (frames[i].type == NGHTTP2_GOAWAY) goaway = 1;
  ck_assert_int_eq(goaway, 1);
  rig_close(&r);
}
END_TEST

START_TEST(garbage_input_closes_the_connection) {
  rig_t r;
  static const uint8_t junk[] = "this is not an http/2 preface at all, just junk bytes";
  uint8_t sink[H2C_IO_BUF];

  rig_open(&r);
  rig_client_send_raw(&r, junk, sizeof junk);
  rig_server_read(&r);
  (void)read_all_client(&r, sink, sizeof sink);
  ck_assert_int_eq(rig_server_alive(&r), 0);
  rig_close(&r);
}
END_TEST

START_TEST(peer_hangup_closes_the_connection) {
  rig_t r;

  rig_open(&r);
  rig_exchange(&r);
  shutdown(r.sv[1], SHUT_WR);
  rig_server_read(&r);
  ck_assert_int_eq(rig_server_alive(&r), 0);
  rig_close(&r);
}
END_TEST

static h2_stream_t *ops_rig_stream(rig_t *r, int32_t *sid_out) {
  int32_t sid = rig_request(r, "GET", PATH_UNKNOWN, NULL, 0);
  h2_stream_t *s;

  rig_feed_server_direct(r);
  s = h2_find_stream(r->h2, sid);
  ck_assert_ptr_nonnull(s);
  *sid_out = sid;
  return s;
}

START_TEST(ops_respond_content_submits_the_body) {
  rig_t r;
  int32_t sid;
  h2_stream_t *s;
  h2_req_ctx_t ctx;
  uint8_t *body = malloc(5);

  ck_assert_ptr_nonnull(body);
  memcpy(body, "hello", 5);
  rig_open(&r);
  s = ops_rig_stream(&r, &sid);
  ctx.c = r.c;
  ctx.stream = s;
  h2_httpng_ops.respond_content(r.h2, &ctx, 200, "text/plain", NULL, 0, body, 5, NULL);
  h2_flush_tx(r.h2, r.c);
  rig_settle_server_output(&r);
  ck_assert_int_eq(h2c_stream_for(&r.cl, sid)->status, 200);
  rig_close(&r);
}
END_TEST

START_TEST(ops_push_dispatch_reports_full_slot_tables) {
  rig_t r;
  int32_t sid;
  h2_stream_t *s;
  h2_req_ctx_t ctx;
  int i;

  rig_open(&r);
  s = ops_rig_stream(&r, &sid);
  ctx.c = r.c;
  ctx.stream = s;
  for (i = 0; i < H2_TSPUSH_MAX; i++) r.h2->tspush[i].sid = 1000 + i;
  for (i = 0; i < H2_DASHCHUNK_MAX; i++) r.h2->dashchunk[i].sid = 2000 + i;
  ck_assert_int_eq(h2_httpng_ops.tspush_dispatch(r.h2, &ctx, 0, 0), 0);
  ck_assert_int_eq(h2_httpng_ops.dashchunk_dispatch(r.h2, &ctx, 0, 0), 0);
  for (i = 0; i < H2_TSPUSH_MAX; i++) r.h2->tspush[i].sid = 0;
  for (i = 0; i < H2_DASHCHUNK_MAX; i++) r.h2->dashchunk[i].sid = 0;
  rig_close(&r);
}
END_TEST

START_TEST(ops_llhls_try_park_parks_and_clears_on_close) {
  rig_t r;
  int32_t sid;
  h2_stream_t *s;
  h2_req_ctx_t ctx;
  pid_filter_t filter;
  lcevc_select_t lcevc;
  llhls_park_req_t req;

  rig_open(&r);
  s = ops_rig_stream(&r, &sid);
  ctx.c = r.c;
  ctx.stream = s;
  memset(&filter, 0, sizeof filter);
  memset(&lcevc, 0, sizeof lcevc);
  memset(&req, 0, sizeof req);
  req.filter = &filter;
  req.lcevc = &lcevc;
  req.filename = "x.m3u8";
  req.timeout_ms = 1000;
  ck_assert_int_eq(h2_httpng_ops.llhls_try_park(r.h2, &ctx, &req), 1);
  h2_llhls_on_stream_close(r.h2, sid);
  rig_close(&r);
}
END_TEST

static Suite *http2_suite(void) {
  Suite *s = suite_create("dipixy_http2");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 20);
  tcase_add_test(tc, clean_request_walks_the_callback_chain);
  tcase_add_loop_test(tc, request_over_the_socket_is_dispatched_and_answered, 0, (int)(sizeof dispatch_cases / sizeof dispatch_cases[0]));
  tcase_add_test(tc, extended_connect_without_status_page_is_not_found);
  tcase_add_test(tc, extended_connect_with_wrong_path_is_not_found);
  tcase_add_test(tc, extended_connect_without_credentials_gets_a_challenge);
  tcase_add_test(tc, stream_slots_are_exhausted_and_reused);
  tcase_add_test(tc, find_ignores_unknown_and_unused_ids);
  tcase_add_test(tc, request_is_refused_when_every_slot_is_taken);
  tcase_add_test(tc, data_before_headers_is_a_connection_error);
  tcase_add_test(tc, headers_after_stream_close_are_ignored);
  tcase_add_test(tc, duplicate_path_pseudo_header_is_rejected_with_protocol_error);
  tcase_add_loop_test(tc, post_body_is_collected_until_end_stream_then_dispatched, 0, 4);
  tcase_add_test(tc, post_body_over_the_limit_is_flagged_and_still_answered);
  tcase_add_test(tc, reactor_loop_routes_h2_connections_by_event_type);
  tcase_add_test(tc, peer_goaway_without_streams_closes_gracefully);
  tcase_add_test(tc, idle_close_sends_goaway);
  tcase_add_test(tc, garbage_input_closes_the_connection);
  tcase_add_test(tc, peer_hangup_closes_the_connection);
  tcase_add_test(tc, ops_respond_content_submits_the_body);
  tcase_add_test(tc, ops_push_dispatch_reports_full_slot_tables);
  tcase_add_test(tc, ops_llhls_try_park_parks_and_clears_on_close);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(http2_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
