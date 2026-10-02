/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipiradiohead/input/playlist.h"
#include "lib/sys/ioutil.h"

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

typedef struct {
  const char *name;
  const char *body;
  int want;
} hls_media_case_t;

static const hls_media_case_t hls_media_cases[] = {
    {"media playlist", "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXTINF:6.0,\nseg1.ts\n", 1},
    {"leading blank lines", "\n\r\n  \n#EXTM3U\n#EXTINF:6.0,\nseg1.ts\n", 1},
    {"extinf after other tags", "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-MEDIA-SEQUENCE:7\n#EXTINF:4,\nseg.ts\n", 1},
    {"master playlist without extinf", "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=64000\nlow.m3u8\n", 0},
    {"extinf without extm3u", "#EXTINF:6.0,\nseg1.ts\n", 0},
    {"plain m3u with urls", "#EXTM3U\nhttp://example.com/stream.aac\n", 0},
    {"pls", "[playlist]\nFile1=http://example.com/a.mp3\n", 0},
    {"empty body", "", 0},
};

START_TEST(playlist_is_hls_media_tells_media_from_other_bodies) {
  const hls_media_case_t *c = &hls_media_cases[_i];

  ck_assert_msg(playlist_is_hls_media((const unsigned char *)c->body, strlen(c->body)) == c->want, "%s", c->name);
}
END_TEST

typedef struct {
  const char *name;
  const char *body;
  const char *want;
} pls_key_case_t;

static const pls_key_case_t pls_key_cases[] = {
    {"digit suffix", "[playlist]\nFile1=http://example.com/a.mp3\n", "http://example.com/a.mp3"},
    {"lowercase key", "[playlist]\nfile1=http://example.com/b.mp3\n", "http://example.com/b.mp3"},
    {"mixed case key", "[playlist]\nFiLe12=http://example.com/c.mp3\n", "http://example.com/c.mp3"},
    {"later entry after titles", "[playlist]\nTitle1=x\nLength1=-1\nFILE2=https://example.com/d.mp3\n", "https://example.com/d.mp3"},
    {"first match wins", "[playlist]\nFile1=http://example.com/e.mp3\nFile2=http://example.com/f.mp3\n", "http://example.com/e.mp3"},
    {"no digit suffix", "[playlist]\nFile=http://example.com/g.mp3\n", NULL},
    {"letter instead of digit", "[playlist]\nFilex=http://example.com/h.mp3\n", NULL},
    {"space before equals", "[playlist]\nFile1 =http://example.com/i.mp3\n", NULL},
    {"value is not an url", "[playlist]\nFile1=/local/j.mp3\n", NULL},
    {"similar key", "[playlist]\nNumberOfEntries=1\nTitle1=file1=http://example.com/k.mp3\n", NULL},
};

START_TEST(playlist_extract_pls_file_key_variants) {
  const pls_key_case_t *c = &pls_key_cases[_i];
  char url[256] = "";
  int rc = playlist_extract((const unsigned char *)c->body, strlen(c->body), NULL, url, sizeof url);

  ck_assert_msg(rc == (c->want != NULL), "%s: rc %d", c->name, rc);
  if (c->want) ck_assert_msg(strcmp(url, c->want) == 0, "%s: url '%s'", c->name, url);
}
END_TEST

#define SNIFF_WINDOW 4096

static size_t build_padded_body(char *out, const char *head, const char *tail) {
  size_t n = strlen(head);

  memcpy(out, head, n);
  memset(out + n, 'x', SNIFF_WINDOW + 64 - n);
  n = SNIFF_WINDOW + 64;
  out[n++] = '\n';
  memcpy(out + n, tail, strlen(tail));
  n += strlen(tail);
  return n;
}

START_TEST(playlist_sniffing_ignores_everything_past_the_sniff_window) {
  static char body[SNIFF_WINDOW + 512];
  char url[256];
  size_t n;

  n = build_padded_body(body, "[playlist]\nTitle1=", "File1=http://example.com/late.mp3\n");
  ck_assert_int_eq(playlist_extract((const unsigned char *)body, n, NULL, url, sizeof url), 0);

  n = build_padded_body(body, "#EXTM3U\n#", "http://example.com/late.aac\n");
  ck_assert_int_eq(playlist_extract((const unsigned char *)body, n, NULL, url, sizeof url), 0);

  n = build_padded_body(body, "#EXTM3U\n#", "#EXTINF:6.0,\nseg.ts\n");
  ck_assert_int_eq(playlist_is_hls_media((const unsigned char *)body, n), 0);
}
END_TEST

START_TEST(playlist_sniffing_still_finds_entries_inside_the_sniff_window) {
  static char body[SNIFF_WINDOW + 512];
  char url[256];
  size_t n;

  n = build_padded_body(body, "[playlist]\nFile1=http://example.com/early.mp3\nTitle1=", "tail\n");
  ck_assert_int_eq(playlist_extract((const unsigned char *)body, n, NULL, url, sizeof url), 1);
  ck_assert_str_eq(url, "http://example.com/early.mp3");

  n = build_padded_body(body, "#EXTM3U\n#EXTINF:6.0,\nseg.ts\n#", "tail\n");
  ck_assert_int_eq(playlist_is_hls_media((const unsigned char *)body, n), 1);
}
END_TEST

START_TEST(playlist_sniffing_cuts_a_line_that_straddles_the_window) {
  static char body[SNIFF_WINDOW + 64];
  const char *head = "#EXTM3U\nhttp://example.com/";
  static char url[SNIFF_WINDOW + 64];
  size_t n = strlen(head);

  memcpy(body, head, n);
  memset(body + n, 'a', sizeof body - n);
  ck_assert_int_eq(playlist_extract((const unsigned char *)body, sizeof body, NULL, url, sizeof url), 1);
  ck_assert_uint_eq(strlen(url), (size_t)SNIFF_WINDOW - 1 - strlen("#EXTM3U\n"));
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
  tcase_add_loop_test(tc, playlist_is_hls_media_tells_media_from_other_bodies, 0, (int)(sizeof hls_media_cases / sizeof hls_media_cases[0]));
  tcase_add_loop_test(tc, playlist_extract_pls_file_key_variants, 0, (int)(sizeof pls_key_cases / sizeof pls_key_cases[0]));
  tcase_add_test(tc, playlist_sniffing_ignores_everything_past_the_sniff_window);
  tcase_add_test(tc, playlist_sniffing_still_finds_entries_inside_the_sniff_window);
  tcase_add_test(tc, playlist_sniffing_cuts_a_line_that_straddles_the_window);
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
