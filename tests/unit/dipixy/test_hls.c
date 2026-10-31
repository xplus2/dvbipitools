/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE /* memmem */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dipixy/hls/hls.h"
#include "dipixy/hls/hls_int.h"
#include "dipixy/dash/dash.h"
#include "dipixy/segstore/priv.h"
#include "dipixy/ts/pidfilter.h"
#include "lib/sys/ioutil.h"

/* ctx: opaque key only (pointer identity) */
static int g_ctx_a, g_ctx_b;
#define CTX_A ((capture_ctx_t *)&g_ctx_a)
#define CTX_B ((capture_ctx_t *)&g_ctx_b)

static void no_filter(pid_filter_t *f) { memset(f, 0, sizeof *f); }

static const lcevc_select_t full = {LCEVC_SEL_FULL, 0, 0};

void *__real_malloc(size_t size);
static size_t g_fail_size;

void *__wrap_malloc(size_t size) {
  if (g_fail_size && size == g_fail_size) {
    g_fail_size = 0;
    return NULL;
  }
  return __real_malloc(size);
}

START_TEST(render_before_store_open_is_404) {
  pid_filter_t f;
  hls_resp_t r;
  no_filter(&f);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 404);
  ck_assert_ptr_null(r.body);
}
END_TEST

START_TEST(render_unrecognized_filename_not_handled) {
  pid_filter_t f;
  hls_resp_t r;
  no_filter(&f);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "bogus.txt", 0, NULL, &r), 0);
}
END_TEST

START_TEST(index_lists_pushed_segment) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t data[188] = {0x47};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_TS);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, data, sizeof data, 2.0), 0);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_str_eq(r.content_type, "application/vnd.apple.mpegurl");
  ck_assert_ptr_nonnull(r.body);
  ck_assert(memmem(r.body, r.body_len, "#EXTM3U", 7) != NULL);
  ck_assert(memmem(r.body, r.body_len, "seg00000.ts", 11) != NULL);
  hls_resp_body_release(r.body, r.owner, r.zc);
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
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_TS);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, data, sizeof data, 2.0), 0);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "seg00000.ts", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_str_eq(r.content_type, "video/mp2t");
  ck_assert_uint_eq(r.body_len, sizeof data);
  ck_assert_mem_eq(r.body, data, sizeof data);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(segment_ext_must_match_container_and_seq_must_not_wrap) {
  pid_filter_t f;
  hls_resp_t r;
  uint8_t data[188] = {0};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_TS);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, data, sizeof data, 2.0), 0);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "seg00000.m4s", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 404);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "seg4294967296.ts", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 404);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "seg99999999999999999999.ts", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 404);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, "seg00000.ts", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 404);
}
END_TEST

START_TEST(inm_match_handles_lists_weak_and_star) {
  ck_assert_int_eq(hls_inm_match("\"abc\"", "abc"), 1);
  ck_assert_int_eq(hls_inm_match("abc", "abc"), 1);
  ck_assert_int_eq(hls_inm_match("W/\"abc\"", "abc"), 1);
  ck_assert_int_eq(hls_inm_match("\"x\", W/\"abc\" ,\"y\"", "abc"), 1);
  ck_assert_int_eq(hls_inm_match("*", "abc"), 1);
  ck_assert_int_eq(hls_inm_match("\"x\",\"y\"", "abc"), 0);
  ck_assert_int_eq(hls_inm_match("\"abcd\"", "abc"), 0);
  ck_assert_int_eq(hls_inm_match("\"abc", "abc"), 0);
  ck_assert_int_eq(hls_inm_match(NULL, "abc"), 0);
}
END_TEST

START_TEST(if_none_match_returns_304) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t data[188] = {0x47};
  char etag[48];
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_TS);
  hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, data, sizeof data, 2.0);

  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "seg00000.ts", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  bufcpy(etag, sizeof etag, r.etag);
  hls_resp_body_release(r.body, r.owner, r.zc);

  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "seg00000.ts", 0, etag, &r), 1);
  ck_assert_int_eq(r.status, 304);
  ck_assert_ptr_null(r.body);
}
END_TEST

START_TEST(head_request_omits_body) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t data[188] = {0x47};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_TS);
  hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, data, sizeof data, 2.0);

  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "seg00000.ts", 1, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_ptr_null(r.body);
}
END_TEST

START_TEST(store_close_then_render_is_404) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t data[188] = {0x47};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_TS);
  hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, data, sizeof data, 2.0);
  hls_store_close(CTX_A, &f, 0, &full, SEG_CONTAINER_TS);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 404);
}
END_TEST

START_TEST(distinct_ctx_get_distinct_stores) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t data[188] = {0x47};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_TS);
  hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, data, sizeof data, 2.0);
  ck_assert_int_eq(hls_render(CTX_B, &f, 0, &full, SEG_CONTAINER_TS, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 404);
}
END_TEST

START_TEST(fmp4_init_segment_roundtrip) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t init[16] = {'f', 't', 'y', 'p'};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_FMP4);
  ck_assert_int_eq(hls_set_init_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, CODEC_H264, init, sizeof init), 0);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, "init.mp4", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_str_eq(r.content_type, "video/mp4");
  ck_assert_uint_eq(r.body_len, sizeof init);
  ck_assert_mem_eq(r.body, init, sizeof init);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(llhls_part_roundtrip) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t part[64] = {0x47};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_TS);
  hls_llhls_enable(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, 0.5);
  ck_assert_int_eq(hls_ll_store_ready(CTX_A, &f, 0, &full, SEG_CONTAINER_TS), 0);
  ck_assert_int_eq(hls_part_available(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, 0, 0), 0);
  ck_assert_int_eq(hls_push_part(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, part, sizeof part, 0.5, 1), 0);
  ck_assert_int_eq(hls_ll_store_ready(CTX_A, &f, 0, &full, SEG_CONTAINER_TS), 1);
  ck_assert_int_eq(hls_part_available(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, 0, 0), 1);
  ck_assert_int_eq(hls_render_ll(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "seg0.0.ts", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_uint_eq(r.body_len, sizeof part);
  ck_assert_mem_eq(r.body, part, sizeof part);
  hls_resp_body_release(r.body, r.owner, r.zc);
  ck_assert_int_eq(hls_render_ll(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "index_ll.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert(memmem(r.body, r.body_len, "#EXT-X-PART-INF", 15) != NULL);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(llhls_large_part_is_served_by_reference) {
  pid_filter_t f;
  hls_resp_t r;
  static uint8_t p0[40000];
  static uint8_t p1[40000];
  memset(p0, 0x11, sizeof p0);
  memset(p1, 0x22, sizeof p1);
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_TS);
  hls_llhls_enable(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, 0.5);
  ck_assert_int_eq(hls_push_part(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, p0, sizeof p0, 0.5, 1), 0);
  ck_assert_int_eq(hls_push_part(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, p1, sizeof p1, 0.5, 0), 0);
  ck_assert_int_eq(hls_render_ll(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "seg0.1.ts", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_int_eq(r.zc, 1);
  ck_assert_ptr_nonnull(r.owner);
  ck_assert_ptr_eq(r.body, r.owner + sizeof p0);
  ck_assert_uint_eq(r.body_len, sizeof p1);
  ck_assert_mem_eq(r.body, p1, sizeof p1);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(llhls_part_with_matching_etag_is_not_modified) {
  pid_filter_t f;
  hls_resp_t r;
  char etag[48];
  const uint8_t part[64] = {0x47};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_TS);
  hls_llhls_enable(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, 0.5);
  ck_assert_int_eq(hls_push_part(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, part, sizeof part, 0.5, 1), 0);
  ck_assert_int_eq(hls_render_ll(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "seg0.0.ts", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_int_ne(r.etag[0], '\0');
  snprintf(etag, sizeof etag, "%s", r.etag);
  hls_resp_body_release(r.body, r.owner, r.zc);

  ck_assert_int_eq(hls_render_ll(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "seg0.0.ts", 0, etag, &r), 1);
  ck_assert_int_eq(r.status, 304);
  ck_assert_ptr_null(r.body);
  ck_assert_int_eq(hls_render_ll(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "seg0.0.ts", 0, "stale", &r), 1);
  ck_assert_int_eq(r.status, 200);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(llhls_lcevc_all_playlist_becomes_a_master_once_pids_are_known) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t part[64] = {0x47};
  const unsigned pids[2] = {101, 102};
  static const lcevc_select_t all = {LCEVC_SEL_ALL, 0, 0};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &all, 2.0, 6, SEG_CONTAINER_TS);
  hls_llhls_enable(CTX_A, &f, 0, &all, SEG_CONTAINER_TS, 0.5);
  ck_assert_int_eq(hls_push_part(CTX_A, &f, 0, &all, SEG_CONTAINER_TS, part, sizeof part, 0.5, 1), 0);
  ck_assert_int_eq(hls_set_lcevc_pids(CTX_A, &f, 0, &all, SEG_CONTAINER_TS, pids, 2), 0);

  ck_assert_int_eq(hls_render_ll(CTX_A, &f, 0, &all, SEG_CONTAINER_TS, "index_ll.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert(memmem(r.body, r.body_len, "EXT-X-STREAM-INF", 16) != NULL);
  ck_assert(memmem(r.body, r.body_len, "GROUP-ID=\"lcevc\"", 16) != NULL);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(llhls_finalized_segment_part_still_served) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t part[64] = {0x11};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_TS);
  hls_llhls_enable(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, 0.5);
  hls_push_part(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, part, sizeof part, 0.5, 1);
  ck_assert_int_eq(hls_push_segment_ll(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, 0.5), 0);
  ck_assert_int_eq(hls_render_ll(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "seg0.0.ts", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_uint_eq(r.body_len, sizeof part);
  ck_assert_mem_eq(r.body, part, sizeof part);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(dash_availability_start_time_follows_the_anchor) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t init[8] = {'f', 't', 'y', 'p'};
  const uint8_t seg[256] = {0x47};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_FMP4);
  hls_set_init_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, CODEC_H264, init, sizeof init);
  ck_assert_int_eq(hls_store_anchor_at(hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4), 1700000000123ULL), 0);
  ck_assert_int_eq(hls_store_anchor_at(hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4), 1800000000000ULL), 0);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, seg, sizeof seg, 2.0), 0);
  ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 0, "http://example.invalid/", 0, &r), 1);
  ck_assert(memmem(r.body, r.body_len, "availabilityStartTime=\"2023-11-14T22:13:20.123Z\"", 48) != NULL);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(dash_manifest_and_segment_roundtrip) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t init[8] = {'f', 't', 'y', 'p'};
  const uint8_t seg[256] = {0x47};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_FMP4);
  hls_set_init_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, CODEC_H264, init, sizeof init);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, seg, sizeof seg, 2.0), 0);
  ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 0, "http://example.invalid/", 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_str_eq(r.content_type, "application/dash+xml");
  ck_assert(memmem(r.body, r.body_len, "<MPD", 4) != NULL);
  hls_resp_body_release(r.body, r.owner, r.zc);
  ck_assert_int_eq(dash_render_seg(CTX_A, &f, 0, &full, "dseg0.m4s", 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_str_eq(r.content_type, "video/mp4");
  ck_assert_uint_eq(r.body_len, sizeof seg);
  ck_assert_mem_eq(r.body, seg, sizeof seg);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

static int body_has(const hls_resp_t *r, const char *needle) {
  return memmem(r->body, r->body_len, needle, strlen(needle)) != NULL;
}

START_TEST(discontinuity_restarts_the_hls_window) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t data[188] = {0x47};
  uint64_t base = 0;
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_TS);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, data, sizeof data, 2.0), 0);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, data, sizeof data, 2.0), 0);
  ck_assert_int_eq(hls_store_discontinuity_at(hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_TS), &base), 0);
  ck_assert_uint_eq(base, 4000u);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 404);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, data, sizeof data, 2.0), 0);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_int_eq(body_has(&r, "#EXT-X-MEDIA-SEQUENCE:3\n#EXT-X-DISCONTINUITY\n#EXTINF:2.000,\nseg00003.ts\n"), 1);
  ck_assert_int_eq(body_has(&r, "#EXT-X-DISCONTINUITY-SEQUENCE"), 0);
  ck_assert_int_eq(body_has(&r, "seg00000.ts"), 0);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(discontinuity_tag_leaves_with_its_segment_and_the_sequence_catches_up) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t data[188] = {0x47};
  uint64_t base = 0;
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 2, SEG_CONTAINER_TS);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, data, sizeof data, 2.0), 0);
  ck_assert_int_eq(hls_store_discontinuity_at(hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_TS), &base), 0);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, data, sizeof data, 2.0), 0);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, data, sizeof data, 2.0), 0);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(body_has(&r, "#EXT-X-DISCONTINUITY\n"), 1);
  ck_assert_int_eq(body_has(&r, "#EXT-X-DISCONTINUITY-SEQUENCE"), 0);
  hls_resp_body_release(r.body, r.owner, r.zc);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, data, sizeof data, 2.0), 0);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(body_has(&r, "#EXT-X-DISCONTINUITY\n"), 0);
  ck_assert_int_eq(body_has(&r, "#EXT-X-DISCONTINUITY-SEQUENCE:1\n"), 1);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(back_to_back_breaks_emit_one_tag_and_count_both) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t data[188] = {0x47};
  uint64_t base = 0;
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_TS);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, data, sizeof data, 2.0), 0);
  ck_assert_int_eq(hls_store_discontinuity_at(hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_TS), &base), 0);
  ck_assert_int_eq(hls_store_discontinuity_at(hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_TS), &base), 0);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, data, sizeof data, 2.0), 0);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(body_has(&r, "#EXT-X-DISCONTINUITY-SEQUENCE:1\n"), 1);
  ck_assert_int_eq(body_has(&r, "#EXT-X-DISCONTINUITY\n#EXTINF"), 1);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(fmp4_map_uri_follows_the_init_generation) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t init[8] = {'f', 't', 'y', 'p'};
  const uint8_t seg[256] = {0x47};
  uint64_t base = 0;
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_FMP4);
  ck_assert_int_eq(hls_set_init_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, CODEC_H264, init, sizeof init), 0);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, seg, sizeof seg, 2.0), 0);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(body_has(&r, "#EXT-X-MAP:URI=\"init.mp4?g=1\"\n"), 1);
  ck_assert_int_eq(body_has(&r, "#EXT-X-DISCONTINUITY\n"), 0);
  hls_resp_body_release(r.body, r.owner, r.zc);
  ck_assert_int_eq(hls_store_discontinuity_at(hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4), &base), 0);
  ck_assert_int_eq(hls_set_init_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, CODEC_HEVC, init, sizeof init), 0);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, seg, sizeof seg, 2.0), 0);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(body_has(&r, "#EXT-X-DISCONTINUITY\n#EXT-X-MAP:URI=\"init.mp4?g=2\"\n#EXTINF"), 1);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(plain_playlist_carries_the_lcevc_selector_into_segment_uris) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t seg[256] = {0x47};
  static const lcevc_select_t pid = {LCEVC_SEL_N, 101, 1};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &pid, 2.0, 6, SEG_CONTAINER_TS);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &pid, SEG_CONTAINER_TS, seg, sizeof seg, 2.0), 0);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &pid, SEG_CONTAINER_TS, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(body_has(&r, "seg00000.ts?lcevc=101\n"), 1);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(fmp4_map_uri_carries_the_lcevc_selector) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t init[8] = {'f', 't', 'y', 'p'};
  const uint8_t seg[256] = {0x47};
  static const lcevc_select_t base = {LCEVC_SEL_BASE, 0, 0};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &base, 2.0, 6, SEG_CONTAINER_FMP4);
  ck_assert_int_eq(hls_set_init_segment(CTX_A, &f, 0, &base, SEG_CONTAINER_FMP4, CODEC_H264, init, sizeof init), 0);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &base, SEG_CONTAINER_FMP4, seg, sizeof seg, 2.0), 0);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &base, SEG_CONTAINER_FMP4, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(body_has(&r, "#EXT-X-MAP:URI=\"init.mp4?g=1&lcevc=base\"\n"), 1);
  ck_assert_int_eq(body_has(&r, "seg00000.m4s?lcevc=base\n"), 1);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(llhls_playlist_carries_the_lcevc_selector_into_part_uris) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t part[188] = {0x47};
  static const lcevc_select_t base = {LCEVC_SEL_BASE, 0, 0};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &base, 2.0, 6, SEG_CONTAINER_TS);
  hls_llhls_enable(CTX_A, &f, 0, &base, SEG_CONTAINER_TS, 0.5);
  ck_assert_int_eq(hls_push_part(CTX_A, &f, 0, &base, SEG_CONTAINER_TS, part, sizeof part, 0.5, 1), 0);
  ck_assert_int_eq(hls_render_ll(CTX_A, &f, 0, &base, SEG_CONTAINER_TS, "index_ll.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(body_has(&r, "URI=\"seg00000.0.ts?lcevc=base\",INDEPENDENT=YES"), 1);
  ck_assert_int_eq(body_has(&r, "PRELOAD-HINT:TYPE=PART,URI=\"seg00000.1.ts?lcevc=base\""), 1);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(llhls_discontinuity_precedes_the_first_live_part) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t data[188] = {0x47};
  const uint8_t part[188] = {0x47};
  uint64_t base = 0;
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_TS);
  hls_llhls_enable(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, 0.5);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, data, sizeof data, 2.0), 0);
  ck_assert_int_eq(hls_store_discontinuity_at(hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_TS), &base), 0);
  ck_assert_int_eq(hls_push_part(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, part, sizeof part, 0.5, 1), 0);
  ck_assert_int_eq(hls_render_ll(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "index_ll.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_int_eq(body_has(&r, "#EXT-X-DISCONTINUITY\n#EXT-X-PART:"), 1);
  ck_assert_int_eq(body_has(&r, "#EXT-X-DISCONTINUITY-SEQUENCE"), 0);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(llhls_fmp4_playlist_maps_the_init_and_lists_m4s_parts) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t init[8] = {'f', 't', 'y', 'p'};
  const uint8_t part[64] = {0x11};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_FMP4);
  hls_llhls_enable(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, 0.5);
  ck_assert_int_eq(hls_set_init_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, CODEC_H264, init, sizeof init), 0);
  ck_assert_int_eq(hls_push_part(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, part, sizeof part, 0.5, 1), 0);
  ck_assert_int_eq(hls_push_part(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, part, sizeof part, 0.5, 0), 0);
  ck_assert_int_eq(hls_push_segment_ll(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, 1.0), 0);
  ck_assert_int_eq(hls_push_part(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, part, sizeof part, 0.5, 1), 0);
  ck_assert_int_eq(hls_render_ll(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, "index_ll.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_int_eq(body_has(&r, "#EXT-X-VERSION:7\n"), 1);
  ck_assert_int_eq(body_has(&r, "#EXT-X-MAP:URI=\"init.mp4?g=1\"\n"), 1);
  ck_assert_int_eq(body_has(&r, "URI=\"seg00000.0.m4s\",INDEPENDENT=YES"), 1);
  ck_assert_int_eq(body_has(&r, "URI=\"seg00000.1.m4s\"\n"), 1);
  ck_assert_int_eq(body_has(&r, "#EXTINF:1.000,\nseg00000.m4s\n"), 1);
  ck_assert_int_eq(body_has(&r, "URI=\"seg00001.0.m4s\""), 1);
  ck_assert_int_eq(body_has(&r, "#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"seg00001.1.m4s\""), 1);
  ck_assert_int_eq(body_has(&r, ".ts"), 0);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(llhls_fmp4_parts_are_served_as_mp4_and_only_by_their_container) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t part[64] = {0x11};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_FMP4);
  hls_llhls_enable(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, 0.5);
  ck_assert_int_eq(hls_push_part(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, part, sizeof part, 0.5, 1), 0);
  ck_assert_int_eq(hls_render_ll(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, "seg00000.0.m4s", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_str_eq(r.content_type, "video/mp4");
  ck_assert_uint_eq(r.body_len, sizeof part);
  hls_resp_body_release(r.body, r.owner, r.zc);
  ck_assert_int_eq(hls_render_ll(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, "seg00000.0.ts", 0, NULL, &r), 0);
  ck_assert_int_eq(hls_render_ll(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "seg00000.0.m4s", 0, NULL, &r), 0);
}
END_TEST

START_TEST(llhls_fmp4_discontinuity_precedes_the_init_map) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t init[8] = {'f', 't', 'y', 'p'};
  const uint8_t part[64] = {0x11};
  uint64_t base = 0;
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_FMP4);
  hls_llhls_enable(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, 0.5);
  hls_set_init_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, CODEC_H264, init, sizeof init);
  hls_push_part(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, part, sizeof part, 0.5, 1);
  hls_push_segment_ll(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, 0.5);
  ck_assert_int_eq(hls_store_discontinuity_at(hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4), &base), 0);
  ck_assert_int_eq(hls_set_init_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, CODEC_HEVC, init, sizeof init), 0);
  ck_assert_int_eq(hls_push_part(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, part, sizeof part, 0.5, 1), 0);
  ck_assert_int_eq(hls_render_ll(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, "index_ll.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(body_has(&r, "#EXT-X-DISCONTINUITY\n#EXT-X-MAP:URI=\"init.mp4?g=2\"\n#EXT-X-PART:"), 1);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(discontinuity_opens_a_new_dash_period_on_continuing_time) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t init[8] = {'f', 't', 'y', 'p'};
  const uint8_t seg[256] = {0x47};
  uint64_t base = 0;
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_FMP4);
  hls_set_init_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, CODEC_H264, init, sizeof init);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, seg, sizeof seg, 2.0), 0);
  ck_assert_int_eq(hls_store_discontinuity_at(hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4), &base), 0);
  ck_assert_uint_eq(base, 2000u);
  hls_set_init_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, CODEC_HEVC, init, sizeof init);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, seg, sizeof seg, 2.0), 0);
  ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 0, "http://example.invalid/", 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_int_eq(body_has(&r, "<Period id=\"1\" start=\"PT2.000S\">"), 1);
  ck_assert_int_eq(body_has(&r, "presentationTimeOffset=\"2000\""), 1);
  ck_assert_int_eq(body_has(&r, "<S t=\"2000\" d=\"2000\"/>"), 1);
  hls_resp_body_release(r.body, r.owner, r.zc);
  ck_assert_int_eq(dash_render_seg(CTX_A, &f, 0, &full, "dseg2000.m4s", 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(discontinuity_on_a_store_without_segments_is_a_noop) {
  pid_filter_t f;
  uint64_t base = 99;
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_TS);
  ck_assert_int_eq(hls_store_discontinuity_at(hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_TS), &base), 0);
  ck_assert_uint_eq(base, 0u);
}
END_TEST

START_TEST(dash_lcevc_all_emits_one_adaptation_set_per_alternative) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t init[8] = {'f', 't', 'y', 'p'};
  const uint8_t seg[256] = {0x47};
  const unsigned pids[2] = {101, 102};
  static const lcevc_select_t all = {LCEVC_SEL_ALL, 0, 0};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &all, 2.0, 6, SEG_CONTAINER_FMP4);
  hls_set_init_segment(CTX_A, &f, 0, &all, SEG_CONTAINER_FMP4, CODEC_H264, init, sizeof init);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &all, SEG_CONTAINER_FMP4, seg, sizeof seg, 2.0), 0);

  ck_assert_int_eq(dash_render(CTX_A, &f, 0, &all, 0, "http://example.invalid/", 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_int_eq(memmem(r.body, r.body_len, "dependencyId", 12) == NULL, 1);
  ck_assert_int_eq(memmem(r.body, r.body_len, "init.mp4\"", 9) != NULL, 1);
  hls_resp_body_release(r.body, r.owner, r.zc);

  ck_assert_int_eq(hls_set_lcevc_pids(CTX_A, &f, 0, &all, SEG_CONTAINER_FMP4, pids, 2), 0);
  ck_assert_int_eq(dash_render(CTX_A, &f, 0, &all, 0, "http://example.invalid/", 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_int_eq(memmem(r.body, r.body_len, "adaptation-set-switching", 24) != NULL, 1);
  ck_assert_int_eq(memmem(r.body, r.body_len, "dependencyId=\"base\"", 19) != NULL, 1);
  ck_assert_int_eq(memmem(r.body, r.body_len, "init.mp4?lcevc=base", 19) != NULL, 1);
  ck_assert_int_eq(memmem(r.body, r.body_len, "init.mp4?lcevc=101", 18) != NULL, 1);
  ck_assert_int_eq(memmem(r.body, r.body_len, "init.mp4?lcevc=102", 18) != NULL, 1);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

/* esds of AAC-LC: audio codecs mp4a.40.2 */
static const uint8_t split_audio_init[32] = {'e', 's', 'd', 's', [15] = 0x40, [30] = 0x10};

static void open_split_stores(const pid_filter_t *f, unsigned pmt, const lcevc_select_t *sel) {
  const uint8_t vinit[8] = {'f', 't', 'y', 'p'};
  const uint8_t seg[256] = {0x47};
  hls_set_init_codecs_cb(dash_compute_codecs);
  hls_store_open(CTX_A, f, pmt, sel, 2.0, 6, SEG_CONTAINER_FMP4);
  hls_store_open(CTX_A, f, pmt, sel, 2.0, 6, SEG_CONTAINER_FMP4_VIDEO);
  hls_store_open(CTX_A, f, pmt, sel, 2.0, 6, SEG_CONTAINER_FMP4_AUDIO);
  hls_set_init_segment(CTX_A, f, pmt, sel, SEG_CONTAINER_FMP4, CODEC_H264, split_audio_init, sizeof split_audio_init);
  hls_set_init_segment(CTX_A, f, pmt, sel, SEG_CONTAINER_FMP4_VIDEO, CODEC_H264, vinit, sizeof vinit);
  hls_set_init_segment(CTX_A, f, pmt, sel, SEG_CONTAINER_FMP4_AUDIO, CODEC_H264, split_audio_init, sizeof split_audio_init);
  ck_assert_int_eq(hls_set_audio_params_at(hls_store_find(CTX_A, f, pmt, sel, SEG_CONTAINER_FMP4_AUDIO), 48000, HLS_CHCFG_COUNT, 2), 0);
  ck_assert_int_eq(hls_push_segment(CTX_A, f, pmt, sel, SEG_CONTAINER_FMP4_VIDEO, seg, sizeof seg, 2.0), 0);
  ck_assert_int_eq(hls_push_segment(CTX_A, f, pmt, sel, SEG_CONTAINER_FMP4_AUDIO, seg, 100, 2.0), 0);
  ck_assert_int_eq(hls_push_segment(CTX_A, f, pmt, sel, SEG_CONTAINER_FMP4, seg, sizeof seg, 2.0), 0);
}

START_TEST(dash_manifest_splits_video_and_audio_when_track_stores_list_segments) {
  pid_filter_t f;
  hls_resp_t r;
  no_filter(&f);
  open_split_stores(&f, 0, &full);
  ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 0, "http://example.invalid/", 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert(body_has(&r, "contentType=\"video\""));
  ck_assert(body_has(&r, "contentType=\"audio\" mimeType=\"audio/mp4\""));
  ck_assert(body_has(&r, "initialization=\"dinitv.mp4\" media=\"dsegv$Time$.m4s\""));
  ck_assert(body_has(&r, "initialization=\"dinita.mp4\" media=\"dsega$Time$.m4s\""));
  ck_assert(body_has(&r, "id=\"audio\" codecs=\"mp4a.40.2\" audioSamplingRate=\"48000\""));
  ck_assert(body_has(&r, "id=\"video\" codecs=\"avc1.640028\" bandwidth"));
  ck_assert(body_has(&r, "audio_channel_configuration:2011\" value=\"2\""));
  ck_assert(!body_has(&r, "initialization=\"init.mp4\""));
  hls_resp_body_release(r.body, r.owner, r.zc);
  hls_set_init_codecs_cb(NULL);
}
END_TEST

static const hls_video_info_t video_1080p25 = {1920, 1080, 25, 1, 0, 1};

START_TEST(dash_manifest_describes_the_video_and_claims_the_dvb_profile) {
  pid_filter_t f;
  hls_resp_t r;
  no_filter(&f);
  open_split_stores(&f, 0, &full);
  ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 0, "http://example.invalid/", 0, &r), 1);
  ck_assert(!body_has(&r, "dvb-dash"));
  hls_resp_body_release(r.body, r.owner, r.zc);
  ck_assert_int_eq(hls_set_video_info_at(hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4_VIDEO), &video_1080p25), 0);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, (const uint8_t[]){0x47}, 1, 2.0), 0);
  ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 0, "http://example.invalid/", 0, &r), 1);
  ck_assert(body_has(&r, "profiles=\"urn:mpeg:dash:profile:isoff-live:2011,urn:dvb:dash:profile:dvb-dash:2014,urn:dvb:dash:profile:dvb-dash:isoff-ext-live:2014\""));
  ck_assert(body_has(&r, "<AdaptationSet id=\"1\" contentType=\"video\""));
  ck_assert(body_has(&r, "<AdaptationSet id=\"2\" contentType=\"audio\""));
  ck_assert(body_has(&r, " width=\"1920\" height=\"1080\" frameRate=\"25\">"));
  ck_assert(!body_has(&r, "scanType"));
  hls_resp_body_release(r.body, r.owner, r.zc);
  hls_set_init_codecs_cb(NULL);
}
END_TEST

START_TEST(dash_manifest_signals_interlace_fractions_and_sap) {
  static const hls_video_info_t i = {1920, 1080, 30000, 1001, 1, 3};
  pid_filter_t f;
  hls_resp_t r;
  no_filter(&f);
  open_split_stores(&f, 0, &full);
  ck_assert_int_eq(hls_set_video_info_at(hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4_VIDEO), &i), 0);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, (const uint8_t[]){0x47}, 1, 2.0), 0);
  ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 0, "http://example.invalid/", 0, &r), 1);
  ck_assert(body_has(&r, " frameRate=\"30000/1001\" scanType=\"interlaced\">"));
  ck_assert(body_has(&r, "contentType=\"video\" mimeType=\"video/mp4\" segmentAlignment=\"true\" startWithSAP=\"3\""));
  ck_assert(!body_has(&r, "dvb-dash")); /* SAP 3 */
  hls_resp_body_release(r.body, r.owner, r.zc);
  hls_set_init_codecs_cb(NULL);
}
END_TEST

START_TEST(dash_manifest_without_dvb_audio_keeps_the_plain_profile) {
  static const uint8_t mp2_init[32] = {'e', 's', 'd', 's', [15] = 0x6B};
  pid_filter_t f;
  hls_resp_t r;
  no_filter(&f);
  open_split_stores(&f, 0, &full);
  ck_assert_int_eq(hls_set_video_info_at(hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4_VIDEO), &video_1080p25), 0);
  hls_set_init_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, CODEC_H264, mp2_init, sizeof mp2_init);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, (const uint8_t[]){0x47}, 1, 2.0), 0);
  ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 0, "http://example.invalid/", 0, &r), 1);
  ck_assert(body_has(&r, "codecs=\"mp4a.6B\""));
  ck_assert(!body_has(&r, "dvb-dash"));
  hls_resp_body_release(r.body, r.owner, r.zc);
  hls_set_init_codecs_cb(NULL);
}
END_TEST

START_TEST(dash_manifest_profile_follows_the_aac_signalling) {
  static const struct {
    uint8_t aot_byte;
    unsigned rate;
    int dvb;
  } cases[] = {{0x10, 48000, 1}, {0x10, 24000, 0}, {0x28, 48000, 1}, {0xE8, 48000, 1}};
  pid_filter_t f;
  hls_resp_t r;
  uint8_t init[32] = {'e', 's', 'd', 's', [15] = 0x40};
  no_filter(&f);
  open_split_stores(&f, 0, &full);
  ck_assert_int_eq(hls_set_video_info_at(hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4_VIDEO), &video_1080p25), 0);
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    init[30] = cases[i].aot_byte;
    hls_set_init_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, CODEC_H264, init, sizeof init);
    ck_assert_int_eq(hls_set_audio_params_at(hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4_AUDIO), cases[i].rate, HLS_CHCFG_COUNT, 2), 0);
    ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, (const uint8_t[]){0x47}, 1, 2.0), 0);
    ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 0, "http://example.invalid/", 0, &r), 1);
    ck_assert_int_eq(body_has(&r, "dvb-dash"), cases[i].dvb);
    hls_resp_body_release(r.body, r.owner, r.zc);
  }
  hls_set_init_codecs_cb(NULL);
}
END_TEST

START_TEST(dash_plain_manifest_follows_the_requesting_clients_utc_url) {
  pid_filter_t f;
  hls_resp_t r;
  no_filter(&f);
  open_split_stores(&f, 0, &full);
  ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 0, "http://a.example/time", 0, &r), 1);
  ck_assert(body_has(&r, "value=\"http://a.example/time\""));
  hls_resp_body_release(r.body, r.owner, r.zc);
  ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 0, "http://b.example/time", 0, &r), 1);
  ck_assert(body_has(&r, "value=\"http://b.example/time\""));
  ck_assert(!body_has(&r, "a.example"));
  hls_resp_body_release(r.body, r.owner, r.zc);
  hls_set_init_codecs_cb(NULL);
}
END_TEST

START_TEST(dash_vvc_codecs_come_from_the_sps) {
  /* vvcC, FullBox, record without native PTL, 1 array: profile 1, main tier, level 83, frame_only */
  static const uint8_t init[] = {'v', 'v', 'c', 'C', 0, 0, 0, 0, 0xFE, 1, 0x8F, 0, 1, 0, 8, 0x00, 0x79, 0x00, 0x00, 0x02, 0x53, 0x80, 0x00};
  char v[32];
  char a[32];
  dash_compute_codecs(init, sizeof init, CODEC_VVC, v, sizeof v, a, sizeof a);
  ck_assert_str_eq(v, "vvc1.1.L83.CQA");
}
END_TEST

START_TEST(dash_split_audio_rate_and_channels_are_exact) {
  static const struct {
    unsigned rate;
    unsigned channels;
  } cases[] = {{96000, 6}, {88200, 2}, {192000, 7}, {44100, 1}};
  pid_filter_t f;
  hls_resp_t r;
  no_filter(&f);
  open_split_stores(&f, 0, &full);
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    char want_rate[40];
    char want_ch[16];
    ck_assert_int_eq(hls_set_audio_params_at(hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4_AUDIO), cases[i].rate, HLS_CHCFG_COUNT, cases[i].channels), 0);
    ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, (const uint8_t[]){0x47}, 1, 2.0), 0);
    snprintf(want_rate, sizeof want_rate, "audioSamplingRate=\"%u\"", cases[i].rate);
    snprintf(want_ch, sizeof want_ch, "value=\"%u\"/>", cases[i].channels);
    ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 0, "http://example.invalid/", 0, &r), 1);
    ck_assert_int_eq(r.status, 200);
    ck_assert(body_has(&r, want_rate));
    ck_assert(body_has(&r, want_ch));
    hls_resp_body_release(r.body, r.owner, r.zc);
  }
  hls_set_init_codecs_cb(NULL);
}
END_TEST

START_TEST(dash_split_channel_configuration_scheme_follows_the_codec) {
  static const struct {
    hls_chcfg_t scheme;
    unsigned value;
    const char *want;
  } cases[] = {
    {HLS_CHCFG_COUNT, 2, "schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\" value=\"2\""},
    {HLS_CHCFG_DOLBY_2011, 0xF801, "schemeIdUri=\"tag:dolby.com,2014:dash:audio_channel_configuration:2011\" value=\"F801\""},
    {HLS_CHCFG_DOLBY_2011, 0x4000, "schemeIdUri=\"tag:dolby.com,2014:dash:audio_channel_configuration:2011\" value=\"4000\""},
    {HLS_CHCFG_DOLBY_2015, 0x0000C7, "schemeIdUri=\"tag:dolby.com,2015:dash:audio_channel_configuration:2015\" value=\"0000C7\""},
    {HLS_CHCFG_DOLBY_2015, 0x800000, "schemeIdUri=\"tag:dolby.com,2015:dash:audio_channel_configuration:2015\" value=\"800000\""},
    {HLS_CHCFG_DTS_2012, 8, "schemeIdUri=\"tag:dts.com,2014:dash:audio_channel_configuration:2012\" value=\"8\""},
    {HLS_CHCFG_CICP, 12, "schemeIdUri=\"urn:mpeg:mpegB:cicp:ChannelConfiguration\" value=\"12\""},
  };
  pid_filter_t f;
  hls_resp_t r;
  no_filter(&f);
  open_split_stores(&f, 0, &full);
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    ck_assert_int_eq(hls_set_audio_params_at(hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4_AUDIO), 48000, cases[i].scheme, cases[i].value), 0);
    ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, (const uint8_t[]){0x47}, 1, 2.0), 0);
    ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 0, "http://example.invalid/", 0, &r), 1);
    ck_assert_int_eq(r.status, 200);
    ck_assert_msg(body_has(&r, cases[i].want), "case %zu", i);
    hls_resp_body_release(r.body, r.owner, r.zc);
  }
  ck_assert_int_eq(hls_set_audio_params_at(hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4_AUDIO), 48000, HLS_CHCFG_NONE, 0), 0);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, (const uint8_t[]){0x47}, 1, 2.0), 0);
  ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 0, "http://example.invalid/", 0, &r), 1);
  ck_assert(!body_has(&r, "AudioChannelConfiguration"));
  hls_resp_body_release(r.body, r.owner, r.zc);
  hls_set_init_codecs_cb(NULL);
}
END_TEST

START_TEST(dash_split_bandwidth_follows_each_track) {
  pid_filter_t f;
  hls_resp_t r;
  const char *vb;
  const char *ab;
  no_filter(&f);
  open_split_stores(&f, 0, &full);
  ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 0, "http://example.invalid/", 0, &r), 1);
  vb = memmem(r.body, r.body_len, "id=\"video\"", 10);
  ab = memmem(r.body, r.body_len, "id=\"audio\"", 10);
  ck_assert_ptr_nonnull(vb);
  ck_assert_ptr_nonnull(ab);
  ck_assert(memmem(vb, 120, "bandwidth=\"1024\"", 16) != NULL);
  ck_assert(memmem(ab, 120, "bandwidth=\"400\"", 15) != NULL);
  hls_resp_body_release(r.body, r.owner, r.zc);
  hls_set_init_codecs_cb(NULL);
}
END_TEST

START_TEST(dash_manifest_is_unavailable_with_audio_but_without_track_stores) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t seg[256] = {0x47};
  no_filter(&f);
  hls_set_init_codecs_cb(dash_compute_codecs);
  hls_store_open(CTX_A, &f, 5, &full, 2.0, 6, SEG_CONTAINER_FMP4);
  hls_set_init_segment(CTX_A, &f, 5, &full, SEG_CONTAINER_FMP4, CODEC_H264, split_audio_init, sizeof split_audio_init);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 5, &full, SEG_CONTAINER_FMP4, seg, sizeof seg, 2.0), 0);
  ck_assert_int_eq(dash_render(CTX_A, &f, 5, &full, 0, "http://example.invalid/", 0, &r), 1);
  ck_assert_int_eq(r.status, 404);
  hls_resp_body_release(r.body, r.owner, r.zc);
  hls_set_init_codecs_cb(NULL);
}
END_TEST

START_TEST(dash_manifest_is_unavailable_when_track_stores_lag_a_period) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t seg[256] = {0x47};
  uint64_t base;
  no_filter(&f);
  open_split_stores(&f, 0, &full);
  ck_assert_int_eq(hls_store_discontinuity_at(hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4), &base), 0);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, seg, sizeof seg, 2.0), 0);
  ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 0, "http://example.invalid/", 0, &r), 1);
  ck_assert_int_eq(r.status, 404);
  hls_resp_body_release(r.body, r.owner, r.zc);
  hls_set_init_codecs_cb(NULL);
}
END_TEST

START_TEST(dash_split_lcevc_all_adds_one_audio_set_after_the_alternatives) {
  pid_filter_t f;
  hls_resp_t r;
  const unsigned pids[2] = {101, 102};
  static const lcevc_select_t all = {LCEVC_SEL_ALL, 0, 0};
  no_filter(&f);
  open_split_stores(&f, 0, &all);
  ck_assert_int_eq(hls_set_lcevc_pids(CTX_A, &f, 0, &all, SEG_CONTAINER_FMP4, pids, 2), 0);
  ck_assert_int_eq(dash_render(CTX_A, &f, 0, &all, 0, "http://example.invalid/", 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert(body_has(&r, "initialization=\"dinitv.mp4?lcevc=base\""));
  ck_assert(body_has(&r, "initialization=\"dinitv.mp4?lcevc=101\""));
  ck_assert(body_has(&r, "initialization=\"dinitv.mp4?lcevc=102\""));
  ck_assert(body_has(&r, "<AdaptationSet id=\"4\" contentType=\"audio\""));
  ck_assert(body_has(&r, "initialization=\"dinita.mp4\" media=\"dsega$Time$.m4s\""));
  hls_resp_body_release(r.body, r.owner, r.zc);
  hls_set_init_codecs_cb(NULL);
}
END_TEST

START_TEST(dash_split_lldash_manifest_gives_each_set_its_own_reference_time) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t chunk[64] = {0x00, 0x00, 0x00, 0x18, 'f', 't', 'y', 'p'};
  static const seg_container_t kinds[] = {SEG_CONTAINER_FMP4_VIDEO, SEG_CONTAINER_FMP4_AUDIO, SEG_CONTAINER_FMP4};
  no_filter(&f);
  open_split_stores(&f, 0, &full);
  hls_llhls_enable(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, 0.5);
  for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++) {
    hls_push_part(CTX_A, &f, 0, &full, kinds[i], chunk, sizeof chunk, 0.5, 1);
    ck_assert_int_eq(hls_push_segment_ll(CTX_A, &f, 0, &full, kinds[i], 0.5), 0);
  }
  ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 1, "http://t.example/", 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert(body_has(&r, "<ProducerReferenceTime id=\"0\" inband=\"false\" type=\"encoder\" wallClockTime=\""));
  ck_assert(body_has(&r, "<ProducerReferenceTime id=\"1\""));
  ck_assert(body_has(&r, "</ProducerReferenceTime>\n      <Resync type=\"0\""));
  ck_assert(body_has(&r, "<UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-xsdate:2014\""));
  ck_assert(body_has(&r, "dinita.mp4"));
  ck_assert(memcmp((const char *)r.body + r.body_len - 7, "</MPD>\n", 7) == 0);
  hls_resp_body_release(r.body, r.owner, r.zc);
  hls_set_init_codecs_cb(NULL);
}
END_TEST

START_TEST(dash_lldash_manifest_carries_the_requesting_clients_utc_url) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t chunk[64] = {0x00, 0x00, 0x00, 0x18, 'f', 't', 'y', 'p'};
  static const char *const urls[] = {"http://a.example/time", "https://b.example/time", "http://c.example/time", "https://d.example/time", "http://e.example/time", "https://f.example/time"};
  no_filter(&f);
  open_split_stores(&f, 0, &full);
  hls_llhls_enable(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, 0.5);
  hls_push_part(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, chunk, sizeof chunk, 0.5, 1);
  ck_assert_int_eq(hls_push_segment_ll(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, 0.5), 0);
  for (int round = 0; round < 2; round++) {
    for (size_t i = 0; i < sizeof urls / sizeof urls[0]; i++) {
      char want[96];
      snprintf(want, sizeof want, "value=\"%s\"", urls[i]);
      ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 1, urls[i], 0, &r), 1);
      ck_assert_int_eq(r.status, 200);
      ck_assert(body_has(&r, want));
      for (size_t k = 0; k < sizeof urls / sizeof urls[0]; k++) {
        char other[96];
        if (k == i) continue;
        snprintf(other, sizeof other, "value=\"%s\"", urls[k]);
        ck_assert(!body_has(&r, other));
      }
      hls_resp_body_release(r.body, r.owner, r.zc);
    }
  }
  hls_set_init_codecs_cb(NULL);
}
END_TEST

START_TEST(dash_track_segments_and_inits_are_served_from_their_store) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t vinit[8] = {'f', 't', 'y', 'p'};
  no_filter(&f);
  open_split_stores(&f, 0, &full);
  ck_assert_int_eq(dash_render_seg(CTX_A, &f, 0, &full, "dsegv0.m4s", 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_str_eq(r.content_type, "video/mp4");
  ck_assert_uint_eq(r.body_len, 256);
  hls_resp_body_release(r.body, r.owner, r.zc);
  ck_assert_int_eq(dash_render_seg(CTX_A, &f, 0, &full, "dsega0.m4s", 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_str_eq(r.content_type, "audio/mp4");
  ck_assert_uint_eq(r.body_len, 100);
  hls_resp_body_release(r.body, r.owner, r.zc);
  ck_assert_int_eq(dash_render_seg(CTX_A, &f, 0, &full, "dinitv.mp4", 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_str_eq(r.content_type, "video/mp4");
  ck_assert_uint_eq(r.body_len, sizeof vinit);
  hls_resp_body_release(r.body, r.owner, r.zc);
  ck_assert_int_eq(dash_render_seg(CTX_A, &f, 0, &full, "dinita.mp4", 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_str_eq(r.content_type, "audio/mp4");
  ck_assert_uint_eq(r.body_len, sizeof split_audio_init);
  hls_resp_body_release(r.body, r.owner, r.zc);
  ck_assert_int_eq(dash_render_seg(CTX_A, &f, 0, &full, "dsega1.m4s", 0, &r), 1);
  ck_assert_int_eq(r.status, 404);
  hls_resp_body_release(r.body, r.owner, r.zc);
  ck_assert_int_eq(dash_render_seg(CTX_A, &f, 7, &full, "dinitv.mp4", 0, &r), 1);
  ck_assert_int_eq(r.status, 404);
  hls_resp_body_release(r.body, r.owner, r.zc);
  hls_set_init_codecs_cb(NULL);
}
END_TEST

START_TEST(store_sync_takes_the_timeline_position_and_drops_segments) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t seg[64] = {0x47};
  uint64_t base;
  hls_store_t *muxed;
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_FMP4);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_FMP4_VIDEO);
  muxed = hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4_VIDEO, seg, sizeof seg, 2.0), 0);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, seg, sizeof seg, 2.0), 0);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, seg, sizeof seg, 2.0), 0);
  ck_assert_int_eq(hls_store_anchor_at(muxed, 1700000000000ULL), 0);
  ck_assert_int_eq(hls_store_discontinuity_at(muxed, &base), 0);
  ck_assert_uint_eq(base, 4000);
  ck_assert_int_eq(hls_store_sync_at(hls_store_find(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4_VIDEO), muxed), 0);
  ck_assert_int_eq(dash_render_seg(CTX_A, &f, 0, &full, "dsegv0.m4s", 0, &r), 1);
  ck_assert_int_eq(r.status, 404);
  hls_resp_body_release(r.body, r.owner, r.zc);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4_VIDEO, seg, sizeof seg, 2.0), 0);
  ck_assert_int_eq(dash_render_seg(CTX_A, &f, 0, &full, "dsegv4000.m4s", 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(store_sync_from_an_empty_store_restarts_at_zero) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t seg[64] = {0x47};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 9, &full, 2.0, 6, SEG_CONTAINER_FMP4);
  hls_store_open(CTX_A, &f, 9, &full, 2.0, 6, SEG_CONTAINER_FMP4_AUDIO);
  ck_assert_int_eq(hls_store_sync_at(hls_store_find(CTX_A, &f, 9, &full, SEG_CONTAINER_FMP4_AUDIO), hls_store_find(CTX_A, &f, 9, &full, SEG_CONTAINER_FMP4)), 0);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 9, &full, SEG_CONTAINER_FMP4_AUDIO, seg, sizeof seg, 2.0), 0);
  ck_assert_int_eq(dash_render_seg(CTX_A, &f, 9, &full, "dsega0.m4s", 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(llhls_enable_on_the_muxed_store_reaches_the_track_stores) {
  pid_filter_t f;
  const uint8_t chunk[64] = {0x00, 0x00, 0x00, 0x18, 'f', 't', 'y', 'p'};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 11, &full, 2.0, 6, SEG_CONTAINER_FMP4);
  hls_store_open(CTX_A, &f, 11, &full, 2.0, 6, SEG_CONTAINER_FMP4_VIDEO);
  hls_store_open(CTX_A, &f, 11, &full, 2.0, 6, SEG_CONTAINER_FMP4_AUDIO);
  hls_push_part(CTX_A, &f, 11, &full, SEG_CONTAINER_FMP4_VIDEO, chunk, sizeof chunk, 0.5, 1);
  hls_push_part(CTX_A, &f, 11, &full, SEG_CONTAINER_FMP4_AUDIO, chunk, sizeof chunk, 0.5, 1);
  ck_assert_int_eq(hls_ll_store_ready(CTX_A, &f, 11, &full, SEG_CONTAINER_FMP4_VIDEO), 0);
  hls_llhls_enable(CTX_A, &f, 11, &full, SEG_CONTAINER_FMP4, 0.5);
  ck_assert_int_eq(hls_ll_store_ready(CTX_A, &f, 11, &full, SEG_CONTAINER_FMP4_VIDEO), 1);
  ck_assert_int_eq(hls_ll_store_ready(CTX_A, &f, 11, &full, SEG_CONTAINER_FMP4_AUDIO), 1);
}
END_TEST

/* regression: hls_push_segment_ll() start_ms/cum_ms */
START_TEST(lldash_second_segment_addressable_by_start_ms) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t chunk[64] = {0x00, 0x00, 0x00, 0x18, 'f', 't', 'y', 'p'};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_FMP4);
  hls_llhls_enable(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, 0.5);
  hls_push_part(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, chunk, sizeof chunk, 0.5, 1);
  ck_assert_int_eq(hls_push_segment_ll(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, 0.5), 0);
  hls_push_part(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, chunk, sizeof chunk, 0.5, 1);
  ck_assert_int_eq(hls_push_segment_ll(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, 0.5), 0);
  ck_assert_int_eq(dash_render_seg(CTX_A, &f, 0, &full, "dseg500.m4s", 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_uint_eq(r.body_len, sizeof chunk);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(store_ready_reflects_pushed_segments) {
  pid_filter_t f;
  const uint8_t data[188] = {0x47};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_TS);
  ck_assert_int_eq(hls_store_ready(CTX_A, &f, 0, &full, SEG_CONTAINER_TS), 0);
  hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, data, sizeof data, 2.0);
  ck_assert_int_eq(hls_store_ready(CTX_A, &f, 0, &full, SEG_CONTAINER_TS), 1);
}
END_TEST

START_TEST(lcevc_all_emits_master_playlist_once_pids_known) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t data[188] = {0x47};
  const unsigned pids[2] = {101, 102};
  static const lcevc_select_t all = {LCEVC_SEL_ALL, 0, 0};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &all, 2.0, 6, SEG_CONTAINER_TS);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &all, SEG_CONTAINER_TS, data, sizeof data, 2.0), 0);

  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &all, SEG_CONTAINER_TS, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert(memmem(r.body, r.body_len, "EXT-X-STREAM-INF", 16) == NULL);
  hls_resp_body_release(r.body, r.owner, r.zc);

  ck_assert_int_eq(hls_set_lcevc_pids(CTX_A, &f, 0, &all, SEG_CONTAINER_TS, pids, 2), 0);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &all, SEG_CONTAINER_TS, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert(memmem(r.body, r.body_len, "EXT-X-STREAM-INF", 16) != NULL);
  ck_assert(memmem(r.body, r.body_len, "GROUP-ID=\"lcevc\"", 16) != NULL);
  ck_assert(memmem(r.body, r.body_len, "lcevc=base", 10) != NULL);
  ck_assert(memmem(r.body, r.body_len, "lcevc=101", 9) != NULL);
  ck_assert(memmem(r.body, r.body_len, "lcevc=102", 9) != NULL);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(seg_pool_cap_and_trim_idle_do_not_crash) {
  hls_set_seg_pool_cap(4);
  hls_seg_pool_trim_idle();
}
END_TEST

START_TEST(av1_ts_container_playlist_uses_version_7) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t data[188] = {0x47};
  const uint8_t init[4] = {0};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_TS);
  hls_set_init_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, CODEC_AV1, init, sizeof init);
  ck_assert_int_eq(hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, data, sizeof data, 2.0), 0);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert(memmem(r.body, r.body_len, "#EXT-X-VERSION:7", 16) != NULL);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

typedef struct {
  const char *name;
  codec_t vcodec;
  uint8_t init[48];
  size_t init_len;
  const char *want_v;
  const char *want_a;
} codec_case_t;

static const codec_case_t codec_cases[] = {
    {"hevc", CODEC_HEVC, {0}, 8, "hvc1.1.6.L93.B0", ""},
    {"hevc main10 with box", CODEC_HEVC, {0, 'h', 'v', 'c', 'C', 1, 0x02, 0x20, 0, 0, 0, 0xB0, 0, 0, 0, 0, 0, 120}, 18, "hvc1.2.4.L120.B0", ""},
    {"vvc", CODEC_VVC, {0}, 8, "vvc1.1.L83.CQA", ""},
    {"av1 without box", CODEC_AV1, {0}, 8, "av01.0.04M.08", ""},
    {"av1 10 bit high tier", CODEC_AV1, {0, 0, 'a', 'v', '1', 'C', 0, 0x28, 0xC0}, 9, "av01.1.08H.10", ""},
    {"av1 12 bit", CODEC_AV1, {0, 0, 'a', 'v', '1', 'C', 0, 0x4D, 0x20}, 9, "av01.2.13M.12", ""},
    {"avc without box", CODEC_H264, {0}, 8, "avc1.640028", ""},
    {"avc with box", CODEC_H264, {0, 'a', 'v', 'c', 'C', 1, 0x4D, 0x40, 0x1F}, 9, "avc1.4d401f", ""},
    {"ac-3", CODEC_H264, {'a', 'c', '-', '3'}, 4, "avc1.640028", "ac-3"},
    {"ec-3", CODEC_H264, {'e', 'c', '-', '3'}, 4, "avc1.640028", "ec-3"},
    {"opus", CODEC_H264, {'O', 'p', 'u', 's'}, 4, "avc1.640028", "opus"},
    {"dts core", CODEC_H264, {'d', 't', 's', 'c'}, 4, "avc1.640028", "dtsc"},
    {"dts hd", CODEC_H264, {'d', 't', 's', 'h'}, 4, "avc1.640028", "dtsh"},
    {"dts express", CODEC_H264, {'d', 't', 's', 'e'}, 4, "avc1.640028", "dtse"},
    {"dts lossless", CODEC_H264, {'d', 't', 's', 'l'}, 4, "avc1.640028", "dtsl"},
    {"truehd", CODEC_H264, {'m', 'l', 'p', 'a'}, 4, "avc1.640028", "mlpa"},
    {"ac-4", CODEC_H264, {'d', 'a', 'c', '4', 0x01, 0x40}, 6, "avc1.640028", "ac-4.05.00.00"},
    {"aac lc", CODEC_H264, {'e', 's', 'd', 's', [15] = 0x40, [30] = 0x10}, 32, "avc1.640028", "mp4a.40.2"},
    {"esds non-aac", CODEC_H264, {'e', 's', 'd', 's', [15] = 0x6B, [30] = 0x10}, 32, "avc1.640028", "mp4a.6B"},
    {"esds truncated", CODEC_H264, {'e', 's', 'd', 's', [15] = 0x40}, 20, "avc1.640028", ""},
    {"ac-4 truncated", CODEC_H264, {'d', 'a', 'c', '4', 0x01}, 5, "avc1.640028", ""},
};

START_TEST(dash_compute_codecs_covers_every_codec_family) {
  const codec_case_t *cc = &codec_cases[_i];
  char vcodec[32];
  char acodec[32];

  dash_compute_codecs(cc->init, cc->init_len, cc->vcodec, vcodec, sizeof vcodec, acodec, sizeof acodec);
  ck_assert_msg(!strcmp(vcodec, cc->want_v), "%s: video '%s' want '%s'", cc->name, vcodec, cc->want_v);
  ck_assert_msg(!strcmp(acodec, cc->want_a), "%s: audio '%s' want '%s'", cc->name, acodec, cc->want_a);
}
END_TEST

START_TEST(lldash_manifest_advertises_chunked_availability_and_utc_timing) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t chunk[64] = {0x00, 0x00, 0x00, 0x18, 'f', 't', 'y', 'p'};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_FMP4);
  hls_llhls_enable(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, 0.5);
  hls_push_part(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, chunk, sizeof chunk, 0.5, 1);
  ck_assert_int_eq(hls_push_segment_ll(CTX_A, &f, 0, &full, SEG_CONTAINER_FMP4, 0.5), 0);

  ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 1, "http://t.example/?a=<b>&c=\"d\"'e", 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert(memmem(r.body, r.body_len, "availabilityTimeOffset=\"1.500\"", 29) != NULL);
  ck_assert(memmem(r.body, r.body_len, "availabilityTimeComplete=\"false\"", 31) != NULL);
  ck_assert(memmem(r.body, r.body_len, "<UTCTiming", 10) != NULL);
  ck_assert(memmem(r.body, r.body_len, "a=&lt;b&gt;&amp;c=&quot;d&quot;&apos;e", 36) != NULL);
  hls_resp_body_release(r.body, r.owner, r.zc);

  ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 0, "http://t.example/", 0, &r), 1);
  ck_assert(memmem(r.body, r.body_len, "availabilityTimeOffset", 22) == NULL);
  ck_assert(body_has(&r, "<UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-xsdate:2014\" value=\"http://t.example/\"/>"));
  ck_assert(!body_has(&r, "ProducerReferenceTime"));
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

#define MPD_CAP 8192

static void fill_ll_store(pid_filter_t *f, int segs) {
  const uint8_t chunk[64] = {0x00, 0x00, 0x00, 0x18, 'f', 't', 'y', 'p'};
  no_filter(f);
  hls_store_open(CTX_A, f, 0, &full, 2.0, 30, SEG_CONTAINER_FMP4);
  hls_llhls_enable(CTX_A, f, 0, &full, SEG_CONTAINER_FMP4, 0.5);
  for (int i = 0; i < segs; i++) {
    hls_push_part(CTX_A, f, 0, &full, SEG_CONTAINER_FMP4, chunk, sizeof chunk, 0.5, 1);
    ck_assert_int_eq(hls_push_segment_ll(CTX_A, f, 0, &full, SEG_CONTAINER_FMP4, 0.5), 0);
  }
}

START_TEST(lldash_manifest_stays_in_buffer_with_longest_escaped_utc_url) {
  pid_filter_t f;
  hls_resp_t r;
  char url[320];
  memset(url, '"', sizeof url - 1);
  url[sizeof url - 1] = 0;
  fill_ll_store(&f, 30);

  ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 1, url, 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_uint_le(r.body_len, MPD_CAP);
  ck_assert_uint_gt(r.body_len, 7);
  ck_assert(memcmp((const char *)r.body + r.body_len - 7, "</MPD>\n", 7) == 0);
  ck_assert(memmem(r.body, r.body_len, "<UTCTiming", 10) != NULL);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(lldash_manifest_omits_utc_timing_when_url_cannot_fit) {
  pid_filter_t f;
  hls_resp_t r;
  char url[2048];
  memset(url, '"', sizeof url - 1);
  url[sizeof url - 1] = 0;
  fill_ll_store(&f, 30);

  ck_assert_int_eq(dash_render(CTX_A, &f, 0, &full, 1, url, 0, &r), 1);
  ck_assert_int_eq(r.status, 200);
  ck_assert_uint_le(r.body_len, MPD_CAP);
  ck_assert(memcmp((const char *)r.body + r.body_len - 7, "</MPD>\n", 7) == 0);
  ck_assert(memmem(r.body, r.body_len, "<UTCTiming", 10) == NULL);
  hls_resp_body_release(r.body, r.owner, r.zc);
}
END_TEST

START_TEST(dash_compute_codecs_av01_string) {
  static const uint8_t init[] = {0, 0, 0, 0, 'a', 'v', '1', 'C', 0x81, 0x00, 0x0C, 0x00};
  char vcodec[32];
  char acodec[32];
  dash_compute_codecs(init, sizeof init, CODEC_AV1, vcodec, sizeof vcodec, acodec, sizeof acodec);
  ck_assert_str_eq(vcodec, "av01.0.00M.08");
}
END_TEST

START_TEST(async_retire_oom_keeps_the_snapshot_alive) {
  hls_snapshot_t *snap = snap_clone(NULL);

  ck_assert_ptr_nonnull(snap);
  snap->next_seq = 42;
  g_fail_size = sizeof(slot_retire_node_t);
  snap_retire_async(snap);
  ck_assert_uint_eq(g_fail_size, 0);
  hls_store_slot_reclaim_sweep();
  ck_assert_uint_eq(snap->next_seq, 42);
  snap_free(snap);
}
END_TEST

START_TEST(async_retire_defers_free_to_the_sweep) {
  hls_snapshot_t *snap = snap_clone(NULL);

  ck_assert_ptr_nonnull(snap);
  snap_retire_async(snap);
  hls_store_slot_reclaim_sweep();
}
END_TEST

START_TEST(store_close_oom_leaves_the_store_open) {
  pid_filter_t f;
  hls_resp_t r;
  const uint8_t data[188] = {0x47};
  no_filter(&f);
  hls_store_open(CTX_A, &f, 0, &full, 2.0, 6, SEG_CONTAINER_TS);
  hls_push_segment(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, data, sizeof data, 2.0);
  g_fail_size = sizeof(slot_retire_node_t);
  hls_store_close(CTX_A, &f, 0, &full, SEG_CONTAINER_TS);
  ck_assert_uint_eq(g_fail_size, 0);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 200);
  hls_resp_body_release(r.body, r.owner, r.zc);
  hls_store_close(CTX_A, &f, 0, &full, SEG_CONTAINER_TS);
  ck_assert_int_eq(hls_render(CTX_A, &f, 0, &full, SEG_CONTAINER_TS, "index.m3u8", 0, NULL, &r), 1);
  ck_assert_int_eq(r.status, 404);
  hls_store_slot_reclaim_sweep();
}
END_TEST

START_TEST(llhls_full_window_keeps_live_edge_and_drops_old_parts) {
  static ll_playlist_snap_t snap;
  static char buf[16384];
  const char *p;
  int parts = 0;
  int extinf = 0;
  size_t n;
  memset(&snap, 0, sizeof snap);
  snap.td = 6;
  snap.part_target = 0.1875;
  snap.oldest_seq = 100;
  snap.seg_count = HLS_MAX_SEGS;
  for (int i = 0; i < HLS_MAX_SEGS; i++) {
    snap.segs[i].seq = 100 + i;
    snap.segs[i].duration = 6.0;
    snap.segs[i].part_count = HLS_MAX_PARTS;
    for (int j = 0; j < HLS_MAX_PARTS; j++) snap.segs[i].part_duration[j] = 0.1875;
  }
  snap.live_msn = 110;
  snap.live_count = 5;
  for (int j = 0; j < 5; j++) snap.live_duration[j] = 0.1875;
  n = format_ll_playlist(&snap, buf, sizeof buf - 1);
  buf[n] = '\0';
  for (p = buf; (p = strstr(p, "#EXT-X-PART:")) != NULL; p++) parts++;
  for (p = buf; (p = strstr(p, "#EXTINF:")) != NULL; p++) extinf++;
  ck_assert_int_eq(extinf, HLS_MAX_SEGS);
  ck_assert_int_eq(parts, 3 * HLS_MAX_PARTS + 5);
  ck_assert_ptr_eq(strstr(buf, "URI=\"seg00100.0.ts\""), NULL);
  ck_assert_ptr_eq(strstr(buf, "URI=\"seg00106.31.ts\""), NULL);
  ck_assert_ptr_ne(strstr(buf, "URI=\"seg00107.0.ts\""), NULL);
  ck_assert_ptr_ne(strstr(buf, "URI=\"seg00110.4.ts\""), NULL);
  ck_assert_ptr_ne(strstr(buf, "#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"seg00110.5.ts\""), NULL);
}
END_TEST

static Suite *hls_suite(void) {
  Suite *s = suite_create("dipixy_hls");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, render_before_store_open_is_404);
  tcase_add_test(tc, render_unrecognized_filename_not_handled);
  tcase_add_test(tc, index_lists_pushed_segment);
  tcase_add_test(tc, segment_body_matches_pushed_bytes);
  tcase_add_test(tc, segment_ext_must_match_container_and_seq_must_not_wrap);
  tcase_add_test(tc, inm_match_handles_lists_weak_and_star);
  tcase_add_test(tc, if_none_match_returns_304);
  tcase_add_test(tc, head_request_omits_body);
  tcase_add_test(tc, store_close_then_render_is_404);
  tcase_add_test(tc, lldash_second_segment_addressable_by_start_ms);
  tcase_add_test(tc, dash_lcevc_all_emits_one_adaptation_set_per_alternative);
  tcase_add_test(tc, dash_manifest_splits_video_and_audio_when_track_stores_list_segments);
  tcase_add_test(tc, dash_split_bandwidth_follows_each_track);
  tcase_add_test(tc, dash_split_channel_configuration_scheme_follows_the_codec);
  tcase_add_test(tc, dash_split_audio_rate_and_channels_are_exact);
  tcase_add_test(tc, dash_manifest_is_unavailable_with_audio_but_without_track_stores);
  tcase_add_test(tc, dash_manifest_is_unavailable_when_track_stores_lag_a_period);
  tcase_add_test(tc, dash_split_lcevc_all_adds_one_audio_set_after_the_alternatives);
  tcase_add_test(tc, dash_split_lldash_manifest_gives_each_set_its_own_reference_time);
  tcase_add_test(tc, dash_lldash_manifest_carries_the_requesting_clients_utc_url);
  tcase_add_test(tc, dash_track_segments_and_inits_are_served_from_their_store);
  tcase_add_test(tc, store_sync_takes_the_timeline_position_and_drops_segments);
  tcase_add_test(tc, store_sync_from_an_empty_store_restarts_at_zero);
  tcase_add_test(tc, llhls_enable_on_the_muxed_store_reaches_the_track_stores);
  tcase_add_test(tc, distinct_ctx_get_distinct_stores);
  tcase_add_test(tc, fmp4_init_segment_roundtrip);
  tcase_add_test(tc, llhls_part_roundtrip);
  tcase_add_test(tc, llhls_large_part_is_served_by_reference);
  tcase_add_test(tc, llhls_part_with_matching_etag_is_not_modified);
  tcase_add_test(tc, llhls_lcevc_all_playlist_becomes_a_master_once_pids_are_known);
  tcase_add_test(tc, llhls_finalized_segment_part_still_served);
  tcase_add_test(tc, dash_availability_start_time_follows_the_anchor);
  tcase_add_test(tc, dash_manifest_and_segment_roundtrip);
  tcase_add_test(tc, discontinuity_restarts_the_hls_window);
  tcase_add_test(tc, discontinuity_tag_leaves_with_its_segment_and_the_sequence_catches_up);
  tcase_add_test(tc, back_to_back_breaks_emit_one_tag_and_count_both);
  tcase_add_test(tc, fmp4_map_uri_follows_the_init_generation);
  tcase_add_test(tc, llhls_discontinuity_precedes_the_first_live_part);
  tcase_add_test(tc, plain_playlist_carries_the_lcevc_selector_into_segment_uris);
  tcase_add_test(tc, fmp4_map_uri_carries_the_lcevc_selector);
  tcase_add_test(tc, llhls_playlist_carries_the_lcevc_selector_into_part_uris);
  tcase_add_test(tc, llhls_fmp4_playlist_maps_the_init_and_lists_m4s_parts);
  tcase_add_test(tc, llhls_fmp4_parts_are_served_as_mp4_and_only_by_their_container);
  tcase_add_test(tc, llhls_fmp4_discontinuity_precedes_the_init_map);
  tcase_add_test(tc, discontinuity_opens_a_new_dash_period_on_continuing_time);
  tcase_add_test(tc, discontinuity_on_a_store_without_segments_is_a_noop);
  tcase_add_test(tc, store_ready_reflects_pushed_segments);
  tcase_add_test(tc, lcevc_all_emits_master_playlist_once_pids_known);
  tcase_add_test(tc, seg_pool_cap_and_trim_idle_do_not_crash);
  tcase_add_test(tc, av1_ts_container_playlist_uses_version_7);
  tcase_add_test(tc, dash_compute_codecs_av01_string);
  tcase_add_loop_test(tc, dash_compute_codecs_covers_every_codec_family, 0, (int)(sizeof codec_cases / sizeof codec_cases[0]));
  tcase_add_test(tc, lldash_manifest_advertises_chunked_availability_and_utc_timing);
  tcase_add_test(tc, lldash_manifest_stays_in_buffer_with_longest_escaped_utc_url);
  tcase_add_test(tc, lldash_manifest_omits_utc_timing_when_url_cannot_fit);
  tcase_add_test(tc, async_retire_oom_keeps_the_snapshot_alive);
  tcase_add_test(tc, async_retire_defers_free_to_the_sweep);
  tcase_add_test(tc, store_close_oom_leaves_the_store_open);
  tcase_add_test(tc, llhls_full_window_keeps_live_edge_and_drops_old_parts);
  tcase_add_test(tc, dash_manifest_describes_the_video_and_claims_the_dvb_profile);
  tcase_add_test(tc, dash_manifest_signals_interlace_fractions_and_sap);
  tcase_add_test(tc, dash_manifest_without_dvb_audio_keeps_the_plain_profile);
  tcase_add_test(tc, dash_manifest_profile_follows_the_aac_signalling);
  tcase_add_test(tc, dash_plain_manifest_follows_the_requesting_clients_utc_url);
  tcase_add_test(tc, dash_vvc_codecs_come_from_the_sps);
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
