/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "dipixy/hls/hls.h"
#include "dipixy/segment/priv.h"
#include "dipixy/segment/mp4push.h"

/* fakes: mp4push.c's own external references, none of them linked here */

static pthread_mutex_t g_fake_lock = PTHREAD_MUTEX_INITIALIZER;
static hls_seg_ctx_t g_fake_seg;
static int g_fake_seg_present;
static capture_ctx_t *g_fake_ctx;
static unsigned g_fake_pmt;
static unsigned char g_fake_init_data[64];
static size_t g_fake_init_len;

void hls_seg_registry_lock(void) { pthread_mutex_lock(&g_fake_lock); }
void hls_seg_registry_unlock(void) { pthread_mutex_unlock(&g_fake_lock); }

hls_seg_ctx_t *hls_seg_find_locked(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, seg_container_t container) {
  (void)filter;
  if (!g_fake_seg_present || ctx != g_fake_ctx || pmt_pid != g_fake_pmt || container != SEG_CONTAINER_FMP4) return NULL;
  return &g_fake_seg;
}

int hls_render(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, seg_container_t container,
               const char *filename, int is_head, const char *if_none_match, hls_resp_t *out) {
  (void)ctx;
  (void)filter;
  (void)pmt_pid;
  (void)container;
  (void)filename;
  (void)is_head;
  (void)if_none_match;
  memset(out, 0, sizeof *out);
  if (!g_fake_init_len) {
    out->status = 404;
    return 1;
  }
  out->status = 200;
  out->content_type = "video/mp4";
  out->body = malloc(g_fake_init_len);
  memcpy(out->body, g_fake_init_data, g_fake_init_len);
  out->body_len = g_fake_init_len;
  out->zc = 0;
  return 1;
}

void hls_resp_body_release(uint8_t *body, int zc) {
  (void)zc;
  free(body);
}

void capture_wait_pumps_quiescent(void) {}

conn_t *conn_for_fd(int fd) {
  (void)fd;
  return NULL;
}

int conn_send_buffered(conn_t *c, const void *a, size_t alen, const void *b, size_t blen) {
  (void)c;
  (void)a;
  (void)alen;
  (void)b;
  (void)blen;
  return 0;
}

int conn_queue(conn_t *c, const void *data, size_t len) {
  (void)c;
  (void)data;
  (void)len;
  return 0;
}

void ws_clients_add_bytes(int handle, size_t n) {
  (void)handle;
  (void)n;
}

void h2_mp4push_wake(int sub_idx) { (void)sub_idx; }
void h3_mp4push_wake(int sub_idx) { (void)sub_idx; }

static int g_ctx_marker;

static void setup(void) {
  memset(&g_fake_seg, 0, sizeof g_fake_seg);
  g_fake_seg.mp4push_sub_head = -1;
  g_fake_seg_present = 1;
  g_fake_ctx = (capture_ctx_t *)&g_ctx_marker;
  g_fake_pmt = 0;
  g_fake_init_len = 0;
}

START_TEST(subscribe_fails_without_a_registered_segmenter) {
  g_fake_seg_present = 0;
  for (int i = 0; i < 200; i++)
    ck_assert_int_eq(mp4push_subscribe(g_fake_ctx, &(pid_filter_t){0}, 0, 1), -1);
}
END_TEST

START_TEST(subscribe_links_onto_the_segmenter_chain) {
  int idx = mp4push_subscribe(g_fake_ctx, &(pid_filter_t){0}, 0, 1);
  ck_assert_int_ge(idx, 0);
  ck_assert_int_eq(g_fake_seg.mp4push_sub_head, idx);
  mp4push_sub_close(idx);
  ck_assert_int_eq(g_fake_seg.mp4push_sub_head, -1);
}
END_TEST

START_TEST(subscribe_wrong_key_does_not_match) {
  g_fake_pmt = 7;
  ck_assert_int_eq(mp4push_subscribe(g_fake_ctx, &(pid_filter_t){0}, 3, 1), -1);
}
END_TEST

START_TEST(proto2_subscribe_preseeds_ring_with_init_segment) {
  memcpy(g_fake_init_data, "ftypISOM", 8);
  g_fake_init_len = 8;
  int idx = mp4push_subscribe(g_fake_ctx, &(pid_filter_t){0}, 0, 2);
  ck_assert_int_ge(idx, 0);
  ck_assert_int_eq(mp4push_ring_pending(idx), 1);
  unsigned char buf[64];
  size_t n = mp4push_ring_read(idx, buf, sizeof buf);
  ck_assert_uint_eq(n, 8);
  ck_assert_mem_eq(buf, "ftypISOM", 8);
  ck_assert_int_eq(mp4push_ring_pending(idx), 0);
  mp4push_sub_close(idx);
}
END_TEST

START_TEST(proto2_subscribe_without_init_segment_ready_still_succeeds) {
  int idx = mp4push_subscribe(g_fake_ctx, &(pid_filter_t){0}, 0, 2);
  ck_assert_int_ge(idx, 0);
  ck_assert_int_eq(mp4push_ring_pending(idx), 0);
  mp4push_sub_close(idx);
}
END_TEST

START_TEST(deliver_fans_out_to_every_live_subscriber) {
  int a = mp4push_subscribe(g_fake_ctx, &(pid_filter_t){0}, 0, 2);
  int b = mp4push_subscribe(g_fake_ctx, &(pid_filter_t){0}, 0, 3);
  ck_assert_int_ge(a, 0);
  ck_assert_int_ge(b, 0);

  mp4push_deliver(&g_fake_seg, (const unsigned char *)"fragment1", 9);

  unsigned char buf[16];
  ck_assert_uint_eq(mp4push_ring_read(a, buf, sizeof buf), 9);
  ck_assert_mem_eq(buf, "fragment1", 9);
  ck_assert_uint_eq(mp4push_ring_read(b, buf, sizeof buf), 9);
  ck_assert_mem_eq(buf, "fragment1", 9);

  mp4push_sub_close(a);
  mp4push_deliver(&g_fake_seg, (const unsigned char *)"fragment2", 9);
  ck_assert_int_eq(mp4push_ring_pending(a), 0); /* closed: no longer fed */
  ck_assert_uint_eq(mp4push_ring_read(b, buf, sizeof buf), 9);
  ck_assert_mem_eq(buf, "fragment2", 9);

  mp4push_sub_close(b);
}
END_TEST

START_TEST(deliver_overflow_marks_ring_errored) {
  int idx = mp4push_subscribe(g_fake_ctx, &(pid_filter_t){0}, 0, 2);
  ck_assert_int_ge(idx, 0);
  unsigned char *big = malloc(200000);
  memset(big, 'x', 200000);
  mp4push_deliver(&g_fake_seg, big, 200000);
  ck_assert_int_eq(mp4push_ring_errored(idx), 1);
  free(big);
  mp4push_sub_close(idx);
}
END_TEST

START_TEST(sub_close_on_invalid_or_free_slot_is_a_safe_noop) {
  mp4push_sub_close(-1);
  mp4push_sub_close(999999);
  mp4push_sub_close(0); /* never subscribed in this process: free already */
}
END_TEST

START_TEST(ring_and_bind_queries_on_invalid_slot_return_safe_defaults) {
  ck_assert_int_eq(mp4push_sub_fd(-1), -1);
  ck_assert_int_eq(mp4push_sub_fd(999999), -1);
  ck_assert_ptr_null(mp4push_sub_h3c(-1));
  ck_assert_int_eq(mp4push_sub_h3_sid(-1), -1);
  ck_assert_int_eq(mp4push_ring_pending(-1), 0);
  ck_assert_int_eq(mp4push_ring_errored(-1), 0);
  unsigned char buf[8];
  ck_assert_uint_eq(mp4push_ring_read(-1, buf, sizeof buf), 0);
  size_t len = 123;
  ck_assert_ptr_null(mp4push_ring_peek(-1, &len));
  ck_assert_uint_eq(len, 0);
  mp4push_ring_advance(-1, 4); /* no crash */
  mp4push_h2_bind(-1, 5, 0, -1);
  mp4push_h3_bind(-1, NULL, 0, 0, -1);
}
END_TEST

START_TEST(register_and_flush_ready_on_out_of_range_tid_is_a_safe_noop) {
  mp4push_register_reactor_efd(-1, 5);
  mp4push_register_reactor_efd(9999, 5);
  mp4push_flush_ready(-1);
  mp4push_flush_ready(9999);
}
END_TEST

static Suite *mp4push_suite(void) {
  Suite *s = suite_create("mp4push");
  TCase *tc = tcase_create("core");
  tcase_add_checked_fixture(tc, setup, NULL);
  tcase_add_test(tc, subscribe_fails_without_a_registered_segmenter);
  tcase_add_test(tc, subscribe_links_onto_the_segmenter_chain);
  tcase_add_test(tc, subscribe_wrong_key_does_not_match);
  tcase_add_test(tc, proto2_subscribe_preseeds_ring_with_init_segment);
  tcase_add_test(tc, proto2_subscribe_without_init_segment_ready_still_succeeds);
  tcase_add_test(tc, deliver_fans_out_to_every_live_subscriber);
  tcase_add_test(tc, deliver_overflow_marks_ring_errored);
  tcase_add_test(tc, sub_close_on_invalid_or_free_slot_is_a_safe_noop);
  tcase_add_test(tc, ring_and_bind_queries_on_invalid_slot_return_safe_defaults);
  tcase_add_test(tc, register_and_flush_ready_on_out_of_range_tid_is_a_safe_noop);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  mp4push_init(64);
  SRunner *sr = srunner_create(mp4push_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
