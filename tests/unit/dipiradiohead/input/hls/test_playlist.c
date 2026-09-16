/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipiradiohead/input/hls/playlist.h"
#include "lib/helper/ioutil.h"

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

static Suite *hls_playlist_suite(void) {
  Suite *s = suite_create("hls_playlist");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, hls_playlist_parse_realistic_live_manifest);
  tcase_add_test(tc, hls_playlist_parse_absolute_segment_url);
  tcase_add_test(tc, hls_playlist_parse_endlist_flag);
  tcase_add_test(tc, hls_playlist_parse_rejects_non_extm3u_body);
  tcase_add_test(tc, hls_playlist_parse_ignores_uri_without_extinf);
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
