/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/helper/playlist_in.h"

static void write_temp_file(char *path, const char *suffix, const char *content) {
  char tmpl[128];
  int fd;
  FILE *f;
  snprintf(tmpl, sizeof tmpl, "/tmp/dvbipitools_test_playlist_in_XXXXXX%s", suffix);
  strcpy(path, tmpl);
  fd = mkstemps(path, (int)strlen(suffix));
  ck_assert_int_ge(fd, 0);
  f = fdopen(fd, "w");
  ck_assert_ptr_nonnull(f);
  fputs(content, f);
  fclose(f);
}

START_TEST(m3u_dipiscan_shaped_parses_triplet) {
  char path[160];
  playlist_list_t *pl;
  write_temp_file(path, ".m3u",
                   "#EXTM3U\n"
                   "# 2026-08-25 12:00 UTC\n"
                   "# dipiscan -m 239.19.75.0\n"
                   "\n"
                   "#EXTINF:-1 tsid=\"1\" onid=\"2\" sid=\"3\",Channel One\n"
                   "rtp://@239.1.1.1:5000\n"
                   "\n"
                   "#EXT-X-ENDLIST\n");

  pl = playlist_in_parse_m3u(path);
  unlink(path);
  ck_assert_ptr_nonnull(pl);
  ck_assert_int_eq(pl->count, 1);
  ck_assert_str_eq(pl->items[0].name, "Channel One");
  ck_assert_str_eq(pl->items[0].uri, "rtp://@239.1.1.1:5000");
  ck_assert_uint_eq(pl->items[0].tsid, 1u);
  ck_assert_uint_eq(pl->items[0].onid, 2u);
  ck_assert_uint_eq(pl->items[0].sid, 3u);
  ck_assert_int_eq(pl->items[0].has_triplet, 1);
  playlist_list_free(pl);
}
END_TEST

START_TEST(m3u_generic_third_party_parses_without_triplet) {
  char path[160];
  playlist_list_t *pl;
  write_temp_file(path, ".m3u",
                   "#EXTM3U\n"
                   "#EXTINF:-1,Generic Channel\n"
                   "udp://239.2.2.2:6000\n");

  pl = playlist_in_parse_m3u(path);
  unlink(path);
  ck_assert_ptr_nonnull(pl);
  ck_assert_int_eq(pl->count, 1);
  ck_assert_str_eq(pl->items[0].name, "Generic Channel");
  ck_assert_str_eq(pl->items[0].uri, "udp://239.2.2.2:6000");
  ck_assert_int_eq(pl->items[0].has_triplet, 0);
  playlist_list_free(pl);
}
END_TEST

START_TEST(m3u_plain_uri_with_no_extinf_at_all) {
  char path[160];
  playlist_list_t *pl;
  write_temp_file(path, ".m3u", "rtp://239.3.3.3:7000\n");

  pl = playlist_in_parse_m3u(path);
  unlink(path);
  ck_assert_ptr_nonnull(pl);
  ck_assert_int_eq(pl->count, 1);
  ck_assert_str_eq(pl->items[0].name, "");
  ck_assert_str_eq(pl->items[0].uri, "rtp://239.3.3.3:7000");
  playlist_list_free(pl);
}
END_TEST

START_TEST(m3u_missing_file_returns_null) {
  ck_assert_ptr_null(playlist_in_parse_m3u("/nonexistent/path.m3u"));
}
END_TEST

START_TEST(csv_dipiscan_shaped_parses_triplet) {
  char path[160];
  playlist_list_t *pl;
  write_temp_file(path, ".csv",
                   "Channel One,rtp://239.1.1.1:5000,1,2,101\n"
                   "Channel Two,udp://239.1.1.2:5001\n");

  pl = playlist_in_parse_csv(path);
  unlink(path);
  ck_assert_ptr_nonnull(pl);
  ck_assert_int_eq(pl->count, 2);
  ck_assert_str_eq(pl->items[0].name, "Channel One");
  ck_assert_uint_eq(pl->items[0].sid, 101u);
  ck_assert_int_eq(pl->items[0].has_triplet, 1);
  ck_assert_str_eq(pl->items[1].name, "Channel Two");
  ck_assert_uint_eq(pl->items[1].tsid, 0u);
  ck_assert_int_eq(pl->items[1].has_triplet, 0);
  playlist_list_free(pl);
}
END_TEST

START_TEST(csv_missing_file_returns_null) {
  ck_assert_ptr_null(playlist_in_parse_csv("/nonexistent/path.csv"));
}
END_TEST

START_TEST(xspf_dipiscan_shaped_parses_triplet) {
  char path[160];
  playlist_list_t *pl;
  write_temp_file(path, ".xspf",
                   "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                   "<playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\">\n"
                   "  <title>scan 2026-08-25 12:00 UTC</title>\n"
                   "  <track><location>rtp://@239.1.1.1:5000</location><title>Channel One</title>"
                   "<extension application=\"urn:dvbipitools:dvb-triplet\" tsid=\"1\" onid=\"2\" sid=\"3\"/></track>\n"
                   "</playlist>\n");

  pl = playlist_in_parse_xspf(path);
  unlink(path);
  ck_assert_ptr_nonnull(pl);
  ck_assert_int_eq(pl->count, 1);
  ck_assert_str_eq(pl->items[0].name, "Channel One");
  ck_assert_str_eq(pl->items[0].uri, "rtp://@239.1.1.1:5000");
  ck_assert_uint_eq(pl->items[0].tsid, 1u);
  ck_assert_uint_eq(pl->items[0].onid, 2u);
  ck_assert_uint_eq(pl->items[0].sid, 3u);
  ck_assert_int_eq(pl->items[0].has_triplet, 1);
  playlist_list_free(pl);
}
END_TEST

START_TEST(xspf_generic_third_party_without_extension) {
  char path[160];
  playlist_list_t *pl;
  write_temp_file(path, ".xspf",
                   "<?xml version=\"1.0\"?>\n"
                   "<playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\">\n"
                   "  <trackList>\n"
                   "    <track><location>udp://239.4.4.4:8000</location><title>Generic</title></track>\n"
                   "  </trackList>\n"
                   "</playlist>\n");

  pl = playlist_in_parse_xspf(path);
  unlink(path);
  ck_assert_ptr_nonnull(pl);
  ck_assert_int_eq(pl->count, 1);
  ck_assert_str_eq(pl->items[0].name, "Generic");
  ck_assert_str_eq(pl->items[0].uri, "udp://239.4.4.4:8000");
  ck_assert_int_eq(pl->items[0].has_triplet, 0);
  playlist_list_free(pl);
}
END_TEST

START_TEST(xspf_multiple_tracks_parse_in_order) {
  char path[160];
  playlist_list_t *pl;
  write_temp_file(path, ".xspf",
                   "<playlist version=\"1\" xmlns=\"http://xspf.org/ns/0/\">\n"
                   "  <track><location>rtp://239.5.5.5:1000</location><title>A</title></track>\n"
                   "  <track><location>rtp://239.5.5.6:1001</location><title>B</title></track>\n"
                   "</playlist>\n");

  pl = playlist_in_parse_xspf(path);
  unlink(path);
  ck_assert_ptr_nonnull(pl);
  ck_assert_int_eq(pl->count, 2);
  ck_assert_str_eq(pl->items[0].name, "A");
  ck_assert_str_eq(pl->items[1].name, "B");
  playlist_list_free(pl);
}
END_TEST

START_TEST(xspf_missing_file_returns_null) {
  ck_assert_ptr_null(playlist_in_parse_xspf("/nonexistent/path.xspf"));
}
END_TEST

static Suite *playlist_in_suite(void) {
  Suite *s = suite_create("lib_playlist_in");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, m3u_dipiscan_shaped_parses_triplet);
  tcase_add_test(tc, m3u_generic_third_party_parses_without_triplet);
  tcase_add_test(tc, m3u_plain_uri_with_no_extinf_at_all);
  tcase_add_test(tc, m3u_missing_file_returns_null);
  tcase_add_test(tc, csv_dipiscan_shaped_parses_triplet);
  tcase_add_test(tc, csv_missing_file_returns_null);
  tcase_add_test(tc, xspf_dipiscan_shaped_parses_triplet);
  tcase_add_test(tc, xspf_generic_third_party_without_extension);
  tcase_add_test(tc, xspf_multiple_tracks_parse_in_order);
  tcase_add_test(tc, xspf_missing_file_returns_null);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(playlist_in_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
