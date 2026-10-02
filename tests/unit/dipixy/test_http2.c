/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>

#include "h2_rig.h"

#define PATH_UNKNOWN "/no/such/route"

static size_t read_all_client(rig_t *r, uint8_t *buf, size_t cap) {
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
  ck_assert_str_eq(s->inm, "abc");
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
  ck_assert_uint_eq(frames, 0u);
  ck_assert_int_eq(rig_server_alive(&r), 1);
  ck_assert_int_eq(h2_conn_active_count(r.h2), 0);
  ck_assert_int_eq(r.h2->pending_n, 0);
  rig_close(&r);
}
END_TEST

START_TEST(duplicate_path_pseudo_header_resets_the_stream) {
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
  ck_assert_uint_eq(out[0].type, NGHTTP2_RST_STREAM);
  ck_assert_int_eq(out[0].sid, 1);
  ck_assert_uint_eq(out[0].code, NGHTTP2_PROTOCOL_ERROR);
  ck_assert_int_eq(h2_conn_active_count(r.h2), 0);
  ck_assert_int_eq(r.h2->pending_n, 0);
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

START_TEST(peer_goaway_marks_the_connection_done) {
  rig_t r;

  rig_open(&r);
  rig_exchange(&r);
  ck_assert_int_eq(nghttp2_submit_goaway(r.cl.cli, NGHTTP2_FLAG_NONE, 0, NGHTTP2_NO_ERROR, NULL, 0), 0);
  rig_feed_server_direct(&r);
  ck_assert_int_eq(r.h2->done, 1);
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
  tcase_add_test(tc, duplicate_path_pseudo_header_resets_the_stream);
  tcase_add_test(tc, reactor_loop_routes_h2_connections_by_event_type);
  tcase_add_test(tc, peer_goaway_marks_the_connection_done);
  tcase_add_test(tc, garbage_input_closes_the_connection);
  tcase_add_test(tc, peer_hangup_closes_the_connection);
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
