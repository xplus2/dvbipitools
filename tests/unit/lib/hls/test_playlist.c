/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/sys/ioutil.h"
#include "lib/hls/playlist.h"

static http_url_t make_base(const char *host, unsigned port, const char *path) {
  http_url_t u;
  memset(&u, 0, sizeof u);
  bufcpy(u.host, sizeof u.host, host);
  u.port = port;
  bufcpy(u.path, sizeof u.path, path);
  return u;
}

START_TEST(hls_playlist_parse_realistic_live_manifest) {
  char body[] =
    "#EXTM3U\n#EXT-X-VERSION:3\n## Created with a test packager\n"
    "#EXT-X-MEDIA-SEQUENCE:279611805\n#EXT-X-INDEPENDENT-SEGMENTS\n#EXT-X-TARGETDURATION:6\n"
    "#EXT-X-PROGRAM-DATE-TIME:2026-09-15T23:39:05.600000Z\n"
    "#EXTINF:6.4, no desc\nstation-audio=96000-279611805.ts\n"
    "#EXTINF:6.4, no desc\nstation-audio=96000-279611806.ts\n";
  http_url_t base = make_base("live.example.com", 80, "/live/station-audio=96000.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_uint_eq(pl.target_duration, 6);
  ck_assert_uint_eq((unsigned)pl.media_sequence, 279611805u);
  ck_assert_int_eq(pl.endlist, 0);
  ck_assert_uint_eq(pl.n_segments, 2);
  ck_assert_str_eq(pl.segments[0].url, "http://live.example.com/live/station-audio=96000-279611805.ts");
  ck_assert_str_eq(pl.segments[1].url, "http://live.example.com/live/station-audio=96000-279611806.ts");
}
END_TEST

START_TEST(hls_playlist_parse_absolute_segment_url) {
  char body[] = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXTINF:4.0,\nhttp://cdn.example.com/seg1.ts\n";
  http_url_t base = make_base("cdn.example.com", 80, "/live/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_uint_eq(pl.n_segments, 1);
  ck_assert_str_eq(pl.segments[0].url, "http://cdn.example.com/seg1.ts");
}
END_TEST

START_TEST(hls_playlist_parse_endlist_flag) {
  char body[] = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXTINF:4.0,\nseg1.ts\n#EXT-X-ENDLIST\n";
  http_url_t base = make_base("example.com", 80, "/vod/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_int_eq(pl.endlist, 1);
  ck_assert_uint_eq(pl.n_segments, 1);
}
END_TEST

START_TEST(hls_playlist_parse_rejects_non_extm3u_body) {
  char body[] = "not a playlist at all\n";
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, NULL, &pl), 0);
}
END_TEST

START_TEST(hls_playlist_parse_ignores_uri_without_extinf) {
  char body[] = "#EXTM3U\n#EXT-X-TARGETDURATION:4\nseg1.ts\n#EXTINF:4.0,\nseg2.ts\n";
  http_url_t base = make_base("example.com", 80, "/live/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_uint_eq(pl.n_segments, 1);
  ck_assert_str_eq(pl.segments[0].url, "http://example.com/live/seg2.ts");
}
END_TEST

START_TEST(hls_playlist_parse_map_uri_marks_fmp4) {
  char body[] = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-MAP:URI=\"init.mp4\"\n#EXTINF:4.0,\nseg1.m4s\n";
  http_url_t base = make_base("example.com", 80, "/live/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_str_eq(pl.map_uri, "http://example.com/live/init.mp4");
}
END_TEST

START_TEST(hls_playlist_parse_no_map_uri_means_ts) {
  char body[] = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXTINF:4.0,\nseg1.ts\n";
  http_url_t base = make_base("example.com", 80, "/live/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_str_eq(pl.map_uri, "");
}
END_TEST

START_TEST(hls_playlist_parse_detects_low_latency_part) {
  char body[] = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-PART:DURATION=1.0,URI=\"p1.ts\"\n#EXTINF:4.0,\nseg1.ts\n";
  http_url_t base = make_base("example.com", 80, "/live/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_int_eq(pl.low_latency, 1);
}
END_TEST

START_TEST(hls_playlist_parse_detects_low_latency_preload_hint) {
  char body[] = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"p2.ts\"\n#EXTINF:4.0,\nseg1.ts\n";
  http_url_t base = make_base("example.com", 80, "/live/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_int_eq(pl.low_latency, 1);
}
END_TEST

START_TEST(hls_playlist_parse_detects_low_latency_block_reload) {
  char body[] = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXT-X-SERVER-CONTROL:CAN-BLOCK-RELOAD=YES\n#EXTINF:4.0,\nseg1.ts\n";
  http_url_t base = make_base("example.com", 80, "/live/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_int_eq(pl.low_latency, 1);
}
END_TEST

START_TEST(hls_playlist_parse_regular_hls_is_not_low_latency) {
  char body[] = "#EXTM3U\n#EXT-X-TARGETDURATION:4\n#EXTINF:4.0,\nseg1.ts\n";
  http_url_t base = make_base("example.com", 80, "/live/index.m3u8");
  hls_playlist_t pl;

  ck_assert_int_eq(hls_playlist_parse(body, &base, &pl), 1);
  ck_assert_int_eq(pl.low_latency, 0);
}
END_TEST

START_TEST(hls_body_is_master_detects_stream_inf) {
  char master[] = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1000000\nlow.m3u8\n";
  char media[] = "#EXTM3U\n#EXTINF:4.0,\nseg1.ts\n";
  ck_assert_int_eq(hls_body_is_master(master), 1);
  ck_assert_int_eq(hls_body_is_master(media), 0);
}
END_TEST

START_TEST(hls_master_parse_picks_highest_bandwidth) {
  char body[] =
    "#EXTM3U\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=800000,RESOLUTION=640x360\nlow.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=3000000,RESOLUTION=1920x1080\nhigh.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=1500000,RESOLUTION=1280x720\nmid.m3u8\n";
  http_url_t base = make_base("example.com", 80, "/live/master.m3u8");
  hls_master_t m;
  int best;

  ck_assert_int_eq(hls_master_parse(body, &base, &m), 1);
  ck_assert_uint_eq(m.n_variants, 3);
  best = hls_master_pick_highest(&m);
  ck_assert_int_ge(best, 0);
  ck_assert_uint_eq(m.variants[(unsigned)best].bandwidth, 3000000u);
  ck_assert_str_eq(m.variants[(unsigned)best].url, "http://example.com/live/high.m3u8");
}
END_TEST

START_TEST(hls_master_parse_resolution_breaks_bandwidth_tie) {
  char body[] =
    "#EXTM3U\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=2000000,RESOLUTION=1280x720\na.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=2000000,RESOLUTION=1920x1080\nb.m3u8\n";
  http_url_t base = make_base("example.com", 80, "/live/master.m3u8");
  hls_master_t m;
  int best;

  ck_assert_int_eq(hls_master_parse(body, &base, &m), 1);
  best = hls_master_pick_highest(&m);
  ck_assert_str_eq(m.variants[(unsigned)best].url, "http://example.com/live/b.m3u8");
}
END_TEST

START_TEST(hls_master_pick_highest_empty_returns_negative) {
  hls_master_t m;
  memset(&m, 0, sizeof m);
  ck_assert_int_eq(hls_master_pick_highest(&m), -1);
}
END_TEST

START_TEST(hls_master_parse_links_variant_to_audio_group) {
  char body[] =
    "#EXTM3U\n"
    "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aac\",NAME=\"English\",URI=\"audio.m3u8\"\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=3000000,AUDIO=\"aac\"\nvideo.m3u8\n";
  http_url_t base = make_base("example.com", 80, "/live/master.m3u8");
  hls_master_t m;
  const hls_audio_rendition_t *ar;

  ck_assert_int_eq(hls_master_parse(body, &base, &m), 1);
  ck_assert_uint_eq(m.n_variants, 1);
  ck_assert_uint_eq(m.n_audio_renditions, 1);
  ck_assert_str_eq(m.variants[0].audio_group_id, "aac");
  ar = hls_master_find_audio(&m, m.variants[0].audio_group_id);
  ck_assert_ptr_nonnull(ar);
  ck_assert_str_eq(ar->url, "http://example.com/live/audio.m3u8");
}
END_TEST

START_TEST(hls_master_parse_no_audio_attr_means_self_contained) {
  char body[] = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=3000000\nvideo.m3u8\n";
  http_url_t base = make_base("example.com", 80, "/live/master.m3u8");
  hls_master_t m;

  ck_assert_int_eq(hls_master_parse(body, &base, &m), 1);
  ck_assert_str_eq(m.variants[0].audio_group_id, "");
  ck_assert_ptr_null(hls_master_find_audio(&m, m.variants[0].audio_group_id));
}
END_TEST

static Suite *hls_playlist_suite(void) {
  Suite *s = suite_create("hls_playlist");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, hls_playlist_parse_realistic_live_manifest);
  tcase_add_test(tc, hls_playlist_parse_absolute_segment_url);
  tcase_add_test(tc, hls_playlist_parse_endlist_flag);
  tcase_add_test(tc, hls_playlist_parse_rejects_non_extm3u_body);
  tcase_add_test(tc, hls_playlist_parse_ignores_uri_without_extinf);
  tcase_add_test(tc, hls_playlist_parse_map_uri_marks_fmp4);
  tcase_add_test(tc, hls_playlist_parse_no_map_uri_means_ts);
  tcase_add_test(tc, hls_playlist_parse_detects_low_latency_part);
  tcase_add_test(tc, hls_playlist_parse_detects_low_latency_preload_hint);
  tcase_add_test(tc, hls_playlist_parse_detects_low_latency_block_reload);
  tcase_add_test(tc, hls_playlist_parse_regular_hls_is_not_low_latency);
  tcase_add_test(tc, hls_body_is_master_detects_stream_inf);
  tcase_add_test(tc, hls_master_parse_picks_highest_bandwidth);
  tcase_add_test(tc, hls_master_parse_resolution_breaks_bandwidth_tie);
  tcase_add_test(tc, hls_master_pick_highest_empty_returns_negative);
  tcase_add_test(tc, hls_master_parse_links_variant_to_audio_group);
  tcase_add_test(tc, hls_master_parse_no_audio_attr_means_self_contained);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(hls_playlist_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
