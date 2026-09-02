/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE /* memmem */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipixy/hls/hls.h"
#include "dipixy/dash/dash.h"
#include "dipixy/ts/pidfilter.h"
#include "lib/helper/ioutil.h"

/* ctx: opaque key only (pointer identity) */
static int g_ctx_a, g_ctx_b;
#define CTX_A ((capture_ctx_t *)&g_ctx_a)
#define CTX_B ((capture_ctx_t *)&g_ctx_b)

static void no_filter(pid_filter_t *f) { memset(f, 0, sizeof *f); }

START_TEST(render_before_store_open_is_404) {
  pid_filter_t f;
  hls_resp_t r;
  no_filter(&f);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, SEG_CONTAINER_TS, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 404);
  ck_assert_ptr_null(r.body);
}
END_TEST

START_TEST(render_unrecognized_filename_not_handled) {
  pid_filter_t f;
  hls_resp_t r;
  no_filter(&f);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, SEG_CONTAINER_TS, "bogus.txt", 0, NULL, &r), 0);
}
END_TEST

START_TEST(index_lists_pushed_segment) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t data[188] = {0x47};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, 2.0, 6, SEG_CONTAINER_TS);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, SEG_CONTAINER_TS, data, sizeof data, 2.0), 0);

  ck_assert_int_eq(hls_render(CTX_A, &f, 0, SEG_CONTAINER_TS, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_str_eq(r.content_type, "application/vnd.apple.mpegurl");
  ck_assert_ptr_nonnull(r.body);
  ck_assert(memmem(r.body, r.body_len, "#EXTM3U", 7) != NULL);
  ck_assert(memmem(r.body, r.body_len, "seg00000.ts", 11) != NULL);
  hls_resp_body_release(r.body, r.zc);
}
END_TEST

START_TEST(segment_body_matches_pushed_bytes) {
  pid_filter_t f;
  hls_resp_t r;
  uint8_t data[188];
  int i;
  no_filter(&f);
  for (i = 0; i < 188; i++)
    data[i] = (uint8_t)i;
  hls_store_open(CTX_A, &f, 0, 2.0, 6, SEG_CONTAINER_TS);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, SEG_CONTAINER_TS, data, sizeof data, 2.0), 0);

  ck_assert_int_eq(hls_render(CTX_A, &f, 0, SEG_CONTAINER_TS, "seg00000.ts", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_str_eq(r.content_type, "video/mp2t");
  ck_assert_uint_eq(r.body_len, sizeof data);
  ck_assert_mem_eq(r.body, data, sizeof data);
  hls_resp_body_release(r.body, r.zc);
}
END_TEST

START_TEST(if_none_match_returns_304) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t data[188] = {0x47};
  char etag[48];
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, 2.0, 6, SEG_CONTAINER_TS);
  hls_push_segment(CTX_A, &f, 0, SEG_CONTAINER_TS, data, sizeof data, 2.0);

  ck_assert_int_eq(hls_render(CTX_A, &f, 0, SEG_CONTAINER_TS, "seg00000.ts", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  bufcpy(etag, sizeof etag, r.etag);
  hls_resp_body_release(r.body, r.zc);

  ck_assert_int_eq(hls_render(CTX_A, &f, 0, SEG_CONTAINER_TS, "seg00000.ts", 0, etag, &r), 1);
  ck_assert_int_eq(r.status, 304);
  ck_assert_ptr_null(r.body);
}
END_TEST

START_TEST(head_request_omits_body) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t data[188] = {0x47};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, 2.0, 6, SEG_CONTAINER_TS);
  hls_push_segment(CTX_A, &f, 0, SEG_CONTAINER_TS, data, sizeof data, 2.0);

  ck_assert_int_eq(hls_render(CTX_A, &f, 0, SEG_CONTAINER_TS, "seg00000.ts", 1, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_ptr_null(r.body);
}
END_TEST

START_TEST(store_close_then_render_is_404) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t data[188] = {0x47};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, 2.0, 6, SEG_CONTAINER_TS);
  hls_push_segment(CTX_A, &f, 0, SEG_CONTAINER_TS, data, sizeof data, 2.0);
  hls_store_close(CTX_A, &f, 0, SEG_CONTAINER_TS);

  ck_assert_int_eq(hls_render(CTX_A, &f, 0, SEG_CONTAINER_TS, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 404);
}
END_TEST

START_TEST(distinct_ctx_get_distinct_stores) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t data[188] = {0x47};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, 2.0, 6, SEG_CONTAINER_TS);
  hls_push_segment(CTX_A, &f, 0, SEG_CONTAINER_TS, data, sizeof data, 2.0);

  ck_assert_int_eq(hls_render(CTX_B, &f, 0, SEG_CONTAINER_TS, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 404);
}
END_TEST

START_TEST(fmp4_init_segment_roundtrip) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t init[16] = {'f', 't', 'y', 'p'};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, 2.0, 6, SEG_CONTAINER_FMP4);
  ck_assert_int_eq(hls_set_init_segment(CTX_A, &f, 0, SEG_CONTAINER_FMP4, init, sizeof init), 0);

  ck_assert_int_eq(hls_render(CTX_A, &f, 0, SEG_CONTAINER_FMP4, "init.mp4", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_str_eq(r.content_type, "video/mp4");
  ck_assert_uint_eq(r.body_len, sizeof init);
  ck_assert_mem_eq(r.body, init, sizeof init);
  hls_resp_body_release(r.body, r.zc);
}
END_TEST

START_TEST(llhls_part_roundtrip) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t part[64] = {0x47};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, 2.0, 6, SEG_CONTAINER_TS);
  hls_llhls_enable(CTX_A, &f, 0, SEG_CONTAINER_TS, 0.5);
  ck_assert_int_eq(hls_ll_store_ready(CTX_A, &f, 0, SEG_CONTAINER_TS), 0);
  ck_assert_int_eq(hls_part_available(CTX_A, &f, 0, SEG_CONTAINER_TS, 0, 0), 0);
  ck_assert_int_eq(hls_push_part(CTX_A, &f, 0, SEG_CONTAINER_TS, part, sizeof part, 0.5, 1), 0);
  ck_assert_int_eq(hls_ll_store_ready(CTX_A, &f, 0, SEG_CONTAINER_TS), 1);
  ck_assert_int_eq(hls_part_available(CTX_A, &f, 0, SEG_CONTAINER_TS, 0, 0), 1);

  ck_assert_int_eq(hls_render_ll(CTX_A, &f, 0, "seg0.0.ts", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_uint_eq(r.body_len, sizeof part);
  ck_assert_mem_eq(r.body, part, sizeof part);
  hls_resp_body_release(r.body, r.zc);

  ck_assert_int_eq(hls_render_ll(CTX_A, &f, 0, "index_ll.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert(memmem(r.body, r.body_len, "#EXT-X-PART-INF", 15) != NULL);
  hls_resp_body_release(r.body, r.zc);
}
END_TEST

START_TEST(llhls_finalized_segment_part_still_served) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t part[64] = {0x11};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, 2.0, 6, SEG_CONTAINER_TS);
  hls_llhls_enable(CTX_A, &f, 0, SEG_CONTAINER_TS, 0.5);
  hls_push_part(CTX_A, &f, 0, SEG_CONTAINER_TS, part, sizeof part, 0.5, 1);
  ck_assert_int_eq(hls_push_segment_ll(CTX_A, &f, 0, SEG_CONTAINER_TS, 0.5), 0);

  ck_assert_int_eq(hls_render_ll(CTX_A, &f, 0, "seg0.0.ts", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_uint_eq(r.body_len, sizeof part);
  ck_assert_mem_eq(r.body, part, sizeof part);
  hls_resp_body_release(r.body, r.zc);
}
END_TEST

START_TEST(dash_manifest_and_segment_roundtrip) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t init[8] = {'f', 't', 'y', 'p'};
  const uint8_t seg[256] = {0x47};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, 2.0, 6, SEG_CONTAINER_FMP4);
  hls_set_init_segment(CTX_A, &f, 0, SEG_CONTAINER_FMP4, init, sizeof init);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, SEG_CONTAINER_FMP4, seg, sizeof seg, 2.0), 0);

  ck_assert_int_eq(dash_render(CTX_A, &f, 0, 0, "http://example.invalid/", 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_str_eq(r.content_type, "application/dash+xml");
  ck_assert(memmem(r.body, r.body_len, "<MPD", 4) != NULL);
  hls_resp_body_release(r.body, r.zc);

  ck_assert_int_eq(dash_render_seg(CTX_A, &f, 0, "dseg0.m4s", 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_str_eq(r.content_type, "video/mp4");
  ck_assert_uint_eq(r.body_len, sizeof seg);
  ck_assert_mem_eq(r.body, seg, sizeof seg);
  hls_resp_body_release(r.body, r.zc);
}
END_TEST

/* regression: hls_push_segment_ll() start_ms/cum_ms */
START_TEST(lldash_second_segment_addressable_by_start_ms) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t chunk[64] = {0x00, 0x00, 0x00, 0x18, 'f', 't', 'y', 'p'};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, 2.0, 6, SEG_CONTAINER_FMP4);
  hls_llhls_enable(CTX_A, &f, 0, SEG_CONTAINER_FMP4, 0.5);
  hls_push_part(CTX_A, &f, 0, SEG_CONTAINER_FMP4, chunk, sizeof chunk, 0.5, 1);
  ck_assert_int_eq(hls_push_segment_ll(CTX_A, &f, 0, SEG_CONTAINER_FMP4, 0.5), 0);
  hls_push_part(CTX_A, &f, 0, SEG_CONTAINER_FMP4, chunk, sizeof chunk, 0.5, 1);
  ck_assert_int_eq(hls_push_segment_ll(CTX_A, &f, 0, SEG_CONTAINER_FMP4, 0.5), 0);
  ck_assert_int_eq(dash_render_seg(CTX_A, &f, 0, "dseg500.m4s", 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_uint_eq(r.body_len, sizeof chunk);
  hls_resp_body_release(r.body, r.zc);
}
END_TEST

START_TEST(store_ready_reflects_pushed_segments) {
  pid_filter_t f;
  const uint8_t data[188] = {0x47};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, 2.0, 6, SEG_CONTAINER_TS);
  ck_assert_int_eq(hls_store_ready(CTX_A, &f, 0, SEG_CONTAINER_TS), 0);
  hls_push_segment(CTX_A, &f, 0, SEG_CONTAINER_TS, data, sizeof data, 2.0);
  ck_assert_int_eq(hls_store_ready(CTX_A, &f, 0, SEG_CONTAINER_TS), 1);
}
END_TEST

START_TEST(seg_pool_cap_and_trim_idle_do_not_crash) {
  hls_set_seg_pool_cap(4);
  hls_seg_pool_trim_idle();
}
END_TEST

static Suite *hls_suite(void) {
  Suite *s = suite_create("dipixy_hls");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, render_before_store_open_is_404);
  tcase_add_test(tc, render_unrecognized_filename_not_handled);
  tcase_add_test(tc, index_lists_pushed_segment);
  tcase_add_test(tc, segment_body_matches_pushed_bytes);
  tcase_add_test(tc, if_none_match_returns_304);
  tcase_add_test(tc, head_request_omits_body);
  tcase_add_test(tc, store_close_then_render_is_404);
  tcase_add_test(tc, lldash_second_segment_addressable_by_start_ms);
  tcase_add_test(tc, distinct_ctx_get_distinct_stores);
  tcase_add_test(tc, fmp4_init_segment_roundtrip);
  tcase_add_test(tc, llhls_part_roundtrip);
  tcase_add_test(tc, llhls_finalized_segment_part_still_served);
  tcase_add_test(tc, dash_manifest_and_segment_roundtrip);
  tcase_add_test(tc, store_ready_reflects_pushed_segments);
  tcase_add_test(tc, seg_pool_cap_and_trim_idle_do_not_crash);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  hls_store_init(32);
  SRunner *sr = srunner_create(hls_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
