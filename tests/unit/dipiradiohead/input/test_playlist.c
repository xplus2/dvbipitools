/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipiradiohead/input/playlist.h"
#include "lib/helper/ioutil.h"

static http_url_t make_base(const char *host, unsigned port, const char *path) {
  http_url_t u;
  memset(&u, 0, sizeof u);
  bufcpy(u.host, sizeof u.host, host);
  u.port = port;
  bufcpy(u.path, sizeof u.path, path);
  return u;
}

START_TEST(playlist_extract_follows_absolute_m3u_url) {
  const char *body = "#EXTM3U\nhttp://example.com/stream.aac\n";
  char url[256];
  ck_assert_int_eq(playlist_extract((const unsigned char *)body, strlen(body), NULL, url, sizeof url), 1);
  ck_assert_str_eq(url, "http://example.com/stream.aac");
}
END_TEST

START_TEST(playlist_extract_ignores_plain_audio_body) {
  const char *body = "not-really-audio-but-thats-ok-here";
  http_url_t base = make_base("127.0.0.1", 8080, "/stream");
  char url[256];
  ck_assert_int_eq(playlist_extract((const unsigned char *)body, strlen(body), &base, url, sizeof url), 0);
}
END_TEST

START_TEST(playlist_extract_resolves_relative_hls_segment) {
  const char *body = "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXTINF:6.4, no desc\nstation-audio=96000-1.ts\n";
  http_url_t base = make_base("live.example.com", 80, "/live/station-audio=96000.m3u8");
  char url[256];
  ck_assert_int_eq(playlist_extract((const unsigned char *)body, strlen(body), &base, url, sizeof url), 1);
  ck_assert_str_eq(url, "http://live.example.com/live/station-audio=96000-1.ts");
}
END_TEST

START_TEST(playlist_extract_resolves_root_relative_hls_entry) {
  const char *body = "#EXTM3U\n#EXTINF:6.4,\n/live/seg1.ts\n";
  http_url_t base = make_base("example.com", 443, "/hls/channel/index.m3u8");
  char url[256];
  base.tls = 1;
  ck_assert_int_eq(playlist_extract((const unsigned char *)body, strlen(body), &base, url, sizeof url), 1);
  ck_assert_str_eq(url, "https://example.com/live/seg1.ts");
}
END_TEST

START_TEST(playlist_extract_includes_nondefault_port) {
  const char *body = "#EXTM3U\n#EXTINF:6.4,\nseg1.ts\n";
  http_url_t base = make_base("example.com", 8000, "/live/index.m3u8");
  char url[256];
  ck_assert_int_eq(playlist_extract((const unsigned char *)body, strlen(body), &base, url, sizeof url), 1);
  ck_assert_str_eq(url, "http://example.com:8000/live/seg1.ts");
}
END_TEST

START_TEST(playlist_extract_relative_needs_extm3u_marker) {
  const char *body = "seg1.ts\nseg2.ts\n";
  http_url_t base = make_base("example.com", 80, "/live/index.m3u8");
  char url[256];
  ck_assert_int_eq(playlist_extract((const unsigned char *)body, strlen(body), &base, url, sizeof url), 0);
}
END_TEST

START_TEST(playlist_extract_relative_without_base_fails) {
  const char *body = "#EXTM3U\n#EXTINF:6.4,\nseg1.ts\n";
  char url[256];
  ck_assert_int_eq(playlist_extract((const unsigned char *)body, strlen(body), NULL, url, sizeof url), 0);
}
END_TEST

START_TEST(playlist_extract_pls_stays_absolute_only) {
  const char *body = "[playlist]\nFile1=http://example.com/a.mp3\n";
  char url[256];
  ck_assert_int_eq(playlist_extract((const unsigned char *)body, strlen(body), NULL, url, sizeof url), 1);
  ck_assert_str_eq(url, "http://example.com/a.mp3");
}
END_TEST

static Suite *playlist_suite(void) {
  Suite *s = suite_create("playlist");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, playlist_extract_follows_absolute_m3u_url);
  tcase_add_test(tc, playlist_extract_ignores_plain_audio_body);
  tcase_add_test(tc, playlist_extract_resolves_relative_hls_segment);
  tcase_add_test(tc, playlist_extract_resolves_root_relative_hls_entry);
  tcase_add_test(tc, playlist_extract_includes_nondefault_port);
  tcase_add_test(tc, playlist_extract_relative_needs_extm3u_marker);
  tcase_add_test(tc, playlist_extract_relative_without_base_fails);
  tcase_add_test(tc, playlist_extract_pls_stays_absolute_only);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(playlist_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
