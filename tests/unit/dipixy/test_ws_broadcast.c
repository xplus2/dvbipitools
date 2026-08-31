/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dipixy/ws/ws_broadcast.h"

static int g_calls;
static char g_last_payload[256];
static void *g_last_ctx;

/* ws_build_frame(): unmasked, payload < 126 bytes -> 2-byte header + raw payload */
static void record_sink(void *ctx, const uint8_t *frame, size_t flen) {
  size_t plen = flen - 2;
  g_calls++;
  g_last_ctx = ctx;
  memcpy(g_last_payload, frame + 2, plen);
  g_last_payload[plen] = '\0';
}

static void other_sink(void *ctx, const uint8_t *frame, size_t flen) {
  (void)ctx;
  (void)frame;
  (void)flen;
  g_calls++;
}

static const uint8_t *g_seen_frames[2];

static void capture_frame_ptr_sink(void *ctx, const uint8_t *frame, size_t flen) {
  (void)flen;
  g_seen_frames[(int)(intptr_t)ctx] = frame;
}

START_TEST(no_sinks_initially) {
  ck_assert_int_eq(ws_broadcast_has_sinks(), 0);
}
END_TEST

START_TEST(publish_with_no_sinks_is_a_noop) {
  ws_broadcast_publish("hello");
  ck_assert_int_eq(g_calls, 0);
}
END_TEST

START_TEST(registered_sink_receives_publish) {
  int ctx_val = 42;
  g_calls = 0;
  ws_broadcast_register(record_sink, &ctx_val);
  ck_assert_int_eq(ws_broadcast_has_sinks(), 1);
  ws_broadcast_publish("payload");
  ck_assert_int_eq(g_calls, 1);
  ck_assert_ptr_eq(g_last_ctx, &ctx_val);
  ck_assert_str_eq(g_last_payload, "payload");
}
END_TEST

START_TEST(multiple_sinks_all_receive_publish) {
  int ctx_a = 1, ctx_b = 2;
  g_calls = 0;
  ws_broadcast_register(record_sink, &ctx_a);
  ws_broadcast_register(other_sink, &ctx_b);
  ws_broadcast_publish("x");
  ck_assert_int_eq(g_calls, 2);
}
END_TEST

START_TEST(unregister_removes_sink) {
  int ctx_val = 7;
  g_calls = 0;
  ws_broadcast_register(record_sink, &ctx_val);
  ws_broadcast_unregister(record_sink, &ctx_val);
  ck_assert_int_eq(ws_broadcast_has_sinks(), 0);
  ws_broadcast_publish("gone");
  ck_assert_int_eq(g_calls, 0);
}
END_TEST

START_TEST(unregister_nonmatching_pair_is_noop) {
  int ctx_val = 9, other_ctx = 10;
  g_calls = 0;
  ws_broadcast_register(record_sink, &ctx_val);
  ws_broadcast_unregister(record_sink, &other_ctx); /* same fn, different ctx: no match */
  ws_broadcast_unregister(other_sink, &ctx_val);     /* different fn, same ctx: no match */
  ck_assert_int_eq(ws_broadcast_has_sinks(), 1);
  ws_broadcast_publish("still-there");
  ck_assert_int_eq(g_calls, 1);
}
END_TEST

START_TEST(publish_builds_the_frame_once_shared_across_sinks) {
  g_seen_frames[0] = g_seen_frames[1] = NULL;
  ws_broadcast_register(capture_frame_ptr_sink, (void *)(intptr_t)0);
  ws_broadcast_register(capture_frame_ptr_sink, (void *)(intptr_t)1);
  ws_broadcast_publish("shared");
  ck_assert_ptr_nonnull(g_seen_frames[0]);
  ck_assert_ptr_eq(g_seen_frames[0], g_seen_frames[1]);
}
END_TEST

START_TEST(registration_past_capacity_is_dropped) {
  int ctxs[300];
  int i;
  g_calls = 0;
  for (i = 0; i < 300; i++) {
    ctxs[i] = i;
    ws_broadcast_register(other_sink, &ctxs[i]);
  }
  ws_broadcast_publish("cap-test");
  ck_assert_int_eq(g_calls, 256); /* WS_BROADCAST_MAX_SINKS */
}
END_TEST

static Suite *ws_broadcast_suite(void) {
  Suite *s = suite_create("dipixy_ws_broadcast");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, no_sinks_initially);
  tcase_add_test(tc, publish_with_no_sinks_is_a_noop);
  tcase_add_test(tc, registered_sink_receives_publish);
  tcase_add_test(tc, multiple_sinks_all_receive_publish);
  tcase_add_test(tc, unregister_removes_sink);
  tcase_add_test(tc, unregister_nonmatching_pair_is_noop);
  tcase_add_test(tc, publish_builds_the_frame_once_shared_across_sinks);
  tcase_add_test(tc, registration_past_capacity_is_dropped);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(ws_broadcast_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
