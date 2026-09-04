/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dipixy/args.h"
#include "dipixy/core/playlist.h"
#include "dipixy/ts/channels/channels.h"
#include "dipixy/ts/pidfilter.h"

static void write_temp_file(char *path, const char *content) {
  char tmpl[] = "/tmp/dvbipitools_test_playlist_XXXXXX.m3u";
  int fd;
  FILE *f;
  strcpy(path, tmpl);
  fd = mkstemps(path, 4);
  ck_assert_int_ge(fd, 0);
  f = fdopen(fd, "w");
  ck_assert_ptr_nonnull(f);
  fputs(content, f);
  fclose(f);
}

START_TEST(path_parse_accepts_known_tokens) {
  route_fmt_t fmt;
  playlist_type_t ptype;

  ck_assert_int_eq(playlist_path_parse("/export/hls/m3u", &fmt, &ptype), 0);
  ck_assert_int_eq(fmt, ROUTE_FMT_HLS);
  ck_assert_int_eq(ptype, PLAYLIST_M3U);

  ck_assert_int_eq(playlist_path_parse("/export/dash/xspf", &fmt, &ptype), 0);
  ck_assert_int_eq(fmt, ROUTE_FMT_DASH);
  ck_assert_int_eq(ptype, PLAYLIST_XSPF);
}
END_TEST

START_TEST(path_parse_rejects_bad_shape) {
  route_fmt_t fmt;
  playlist_type_t ptype;

  ck_assert_int_eq(playlist_path_parse("/export/hls", &fmt, &ptype), -1);
  ck_assert_int_eq(playlist_path_parse("/export/hls/m3u/extra", &fmt, &ptype), -1);
  ck_assert_int_eq(playlist_path_parse("/export/notafmt/m3u", &fmt, &ptype), -1);
  ck_assert_int_eq(playlist_path_parse("/export/hls/notatype", &fmt, &ptype), -1);
  ck_assert_int_eq(playlist_path_parse("/list/1/item/1/hls", &fmt, &ptype), -1);
}
END_TEST

START_TEST(fmt_disabled_follows_no_flags) {
  config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(playlist_fmt_disabled(&cfg, ROUTE_FMT_HLS), 0);
  cfg.no_hls = 1;
  ck_assert_int_eq(playlist_fmt_disabled(&cfg, ROUTE_FMT_HLS), 1);
  ck_assert_int_eq(playlist_fmt_disabled(&cfg, ROUTE_FMT_HLS_FMP4), 1);
  ck_assert_int_eq(playlist_fmt_disabled(&cfg, ROUTE_FMT_DASH), 0);
  cfg.no_rawaudio = 1;
  ck_assert_int_eq(playlist_fmt_disabled(&cfg, ROUTE_FMT_RAWAUDIO), 1);
}
END_TEST

START_TEST(query_has_flag_detects_bare_and_valued) {
  ck_assert_int_eq(playlist_query_has_flag(NULL, "plain"), 0);
  ck_assert_int_eq(playlist_query_has_flag("", "plain"), 0);
  ck_assert_int_eq(playlist_query_has_flag("plain", "plain"), 1);
  ck_assert_int_eq(playlist_query_has_flag("input=1&plain", "plain"), 1);
  ck_assert_int_eq(playlist_query_has_flag("plain&input=1", "plain"), 1);
  ck_assert_int_eq(playlist_query_has_flag("plain=1", "plain"), 1);
  ck_assert_int_eq(playlist_query_has_flag("notplain", "plain"), 0);
  ck_assert_int_eq(playlist_query_has_flag("input=1", "plain"), 0);
}
END_TEST

static channels_t *build_two_lists(char *path_a, char *path_b, source_def_t src[2]) {
  config_t cfg;

  write_temp_file(path_a,
      "#EXTINF:-1 tsid=\"1\" onid=\"2\" sid=\"3\" tvg-logo=\"http://icons/a.png\",Channel A\n"
      "rtp://@239.1.1.1:5000\n"
      "#EXTINF:-1,Channel B\n"
      "udp://@239.1.1.2:5001\n");
  write_temp_file(path_b, "#EXTINF:-1,Channel C\nrtp://@239.1.1.3:5002\n");

  memset(&cfg, 0, sizeof cfg);
  memset(src, 0, 2 * sizeof *src);
  src[0].kind = SRC_M3U;
  src[0].value = path_a;
  src[0].ordinal = 1;
  src[1].kind = SRC_M3U;
  src[1].value = path_b;
  src[1].ordinal = 2;
  cfg.sources = src;
  cfg.n_sources = 2;
  return channels_build(&cfg);
}

START_TEST(render_m3u_builds_http_play_paths_with_triplet_and_icon) {
  char path_a[160], path_b[160];
  source_def_t src[2];
  config_t cfg;
  channels_t *ch = build_two_lists(path_a, path_b, src);
  char *out;
  size_t out_len;
  pid_filter_t nofilter = {.count = 0};

  ck_assert_ptr_nonnull(ch);
  memset(&cfg, 0, sizeof cfg);
  cfg.sources = src;
  cfg.n_sources = 2;
  cfg.listen.port = 9080;

  ck_assert_int_eq(playlist_render(&cfg, ch, 0, NULL, NULL, &nofilter, ROUTE_FMT_HLS, PLAYLIST_M3U, &out, &out_len), 0);
  ck_assert_ptr_nonnull(strstr(out, "http://127.0.0.1:9080/list/1/item/1/hls"));
  ck_assert_ptr_nonnull(strstr(out, "http://127.0.0.1:9080/list/1/item/2/hls"));
  ck_assert_ptr_nonnull(strstr(out, "http://127.0.0.1:9080/list/2/item/1/hls"));
  ck_assert_ptr_nonnull(strstr(out, "tsid=\"1\" onid=\"2\" sid=\"3\""));
  ck_assert_ptr_nonnull(strstr(out, "tvg-logo=\"http://icons/a.png\""));

  free(out);
  unlink(path_a);
  unlink(path_b);
  channels_free(ch);
}
END_TEST

START_TEST(render_xspf_uses_image_element_for_icon) {
  char path_a[160], path_b[160];
  source_def_t src[2];
  config_t cfg;
  channels_t *ch = build_two_lists(path_a, path_b, src);
  char *out;
  size_t out_len;
  pid_filter_t nofilter = {.count = 0};

  memset(&cfg, 0, sizeof cfg);
  cfg.sources = src;
  cfg.n_sources = 2;
  cfg.listen.port = 9080;

  ck_assert_int_eq(playlist_render(&cfg, ch, 0, NULL, NULL, &nofilter, ROUTE_FMT_DASH, PLAYLIST_XSPF, &out, &out_len), 0);
  ck_assert_ptr_nonnull(strstr(out, "<image>http://icons/a.png</image>"));
  ck_assert_ptr_null(strstr(out, "<image></image>"));

  free(out);
  unlink(path_a);
  unlink(path_b);
  channels_free(ch);
}
END_TEST

START_TEST(render_input_param_restricts_to_listed_ordinals) {
  char path_a[160], path_b[160];
  source_def_t src[2];
  config_t cfg;
  channels_t *ch = build_two_lists(path_a, path_b, src);
  char *out;
  size_t out_len;
  pid_filter_t nofilter = {.count = 0};

  memset(&cfg, 0, sizeof cfg);
  cfg.sources = src;
  cfg.n_sources = 2;
  cfg.listen.port = 9080;

  ck_assert_int_eq(playlist_render(&cfg, ch, 0, NULL, "input=2", &nofilter, ROUTE_FMT_TS, PLAYLIST_M3U, &out, &out_len), 0);
  ck_assert_ptr_null(strstr(out, "/list/1/item/"));
  ck_assert_ptr_nonnull(strstr(out, "/list/2/item/1/ts"));

  free(out);
  unlink(path_a);
  unlink(path_b);
  channels_free(ch);
}
END_TEST

START_TEST(render_filter_forwarded_to_http_entries_only) {
  char path_a[160], path_b[160];
  source_def_t src[2];
  config_t cfg;
  channels_t *ch = build_two_lists(path_a, path_b, src);
  char *out;
  size_t out_len;
  pid_filter_t filter = {.count = 0};

  pid_filter_parse("101,0x20", &filter);
  ck_assert_int_gt(filter.count, 0);

  memset(&cfg, 0, sizeof cfg);
  cfg.sources = src;
  cfg.n_sources = 2;
  cfg.listen.port = 9080;

  ck_assert_int_eq(playlist_render(&cfg, ch, 0, NULL, NULL, &filter, ROUTE_FMT_SPTS, PLAYLIST_M3U, &out, &out_len), 0);
  ck_assert_ptr_nonnull(strstr(out, "/list/1/item/1/spts?filter=32,101"));
  free(out);

  ck_assert_int_eq(playlist_render(&cfg, ch, 0, NULL, "keep_multicast", &filter, ROUTE_FMT_SPTS, PLAYLIST_M3U, &out, &out_len), 0);
  ck_assert_ptr_nonnull(strstr(out, "rtp://239.1.1.1:5000"));
  ck_assert_ptr_null(strstr(out, "rtp://239.1.1.1:5000?filter"));
  free(out);

  unlink(path_a);
  unlink(path_b);
  channels_free(ch);
}
END_TEST

START_TEST(render_host_override_and_scheme_follow_request) {
  char path_a[160], path_b[160];
  source_def_t src[2];
  config_t cfg;
  channels_t *ch = build_two_lists(path_a, path_b, src);
  char *out;
  size_t out_len;
  pid_filter_t nofilter = {.count = 0};

  memset(&cfg, 0, sizeof cfg);
  cfg.sources = src;
  cfg.n_sources = 2;
  cfg.listen.port = 9080;
  cfg.listen_tls.port = 9443;

  ck_assert_int_eq(playlist_render(&cfg, ch, 0, "example.org:8080", NULL, &nofilter, ROUTE_FMT_TS, PLAYLIST_M3U, &out, &out_len), 0);
  ck_assert_ptr_nonnull(strstr(out, "http://example.org:9080/list/1/item/1/ts"));
  free(out);

  ck_assert_int_eq(playlist_render(&cfg, ch, 1, NULL, "host=example.org", &nofilter, ROUTE_FMT_TS, PLAYLIST_M3U, &out, &out_len), 0);
  ck_assert_ptr_nonnull(strstr(out, "https://example.org:9443/list/1/item/1/ts"));
  free(out);

  ck_assert_int_eq(playlist_render(&cfg, ch, 0, NULL, NULL, &nofilter, ROUTE_FMT_TS, PLAYLIST_M3U, &out, &out_len), 0);
  ck_assert_ptr_nonnull(strstr(out, "http://127.0.0.1:9080/list/1/item/1/ts"));
  free(out);

  unlink(path_a);
  unlink(path_b);
  channels_free(ch);
}
END_TEST

START_TEST(render_named_source_uses_name_in_path) {
  char path_a[160];
  source_def_t src[1];
  config_t cfg;
  channels_t *ch;
  char *out;
  size_t out_len;
  pid_filter_t nofilter = {.count = 0};

  write_temp_file(path_a, "#EXTINF:-1,Channel A\nrtp://@239.1.1.1:5000\n");
  memset(&cfg, 0, sizeof cfg);
  memset(src, 0, sizeof src);
  src[0].kind = SRC_M3U;
  src[0].value = path_a;
  src[0].ordinal = 1;
  src[0].name = "mychan";
  cfg.sources = src;
  cfg.n_sources = 1;
  cfg.listen.port = 9080;
  ch = channels_build(&cfg);
  ck_assert_ptr_nonnull(ch);

  ck_assert_int_eq(playlist_render(&cfg, ch, 0, NULL, NULL, &nofilter, ROUTE_FMT_HLS, PLAYLIST_M3U, &out, &out_len), 0);
  ck_assert_ptr_nonnull(strstr(out, "/mychan/item/1/hls"));
  ck_assert_ptr_null(strstr(out, "/list/1/"));

  free(out);
  unlink(path_a);
  channels_free(ch);
}
END_TEST

START_TEST(render_stdin_singleton_has_no_item_segment) {
  config_t cfg;
  channels_t *ch;
  char *out;
  size_t out_len;
  pid_filter_t nofilter = {.count = 0};

  memset(&cfg, 0, sizeof cfg);
  cfg.stdin_ordinal = 1;
  cfg.listen.port = 9080;
  ch = channels_build(&cfg);
  ck_assert_ptr_nonnull(ch);
  ck_assert_int_eq(playlist_render(&cfg, ch, 0, NULL, NULL, &nofilter, ROUTE_FMT_TS, PLAYLIST_M3U, &out, &out_len), 0);
  ck_assert_ptr_nonnull(strstr(out, "http://127.0.0.1:9080/stdin/ts"));
  free(out);
  channels_free(ch);
}
END_TEST

Suite *playlist_suite(void) {
  Suite *s = suite_create("dipixy_playlist");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, path_parse_accepts_known_tokens);
  tcase_add_test(tc, path_parse_rejects_bad_shape);
  tcase_add_test(tc, fmt_disabled_follows_no_flags);
  tcase_add_test(tc, query_has_flag_detects_bare_and_valued);
  tcase_add_test(tc, render_m3u_builds_http_play_paths_with_triplet_and_icon);
  tcase_add_test(tc, render_xspf_uses_image_element_for_icon);
  tcase_add_test(tc, render_input_param_restricts_to_listed_ordinals);
  tcase_add_test(tc, render_filter_forwarded_to_http_entries_only);
  tcase_add_test(tc, render_host_override_and_scheme_follow_request);
  tcase_add_test(tc, render_named_source_uses_name_in_path);
  tcase_add_test(tc, render_stdin_singleton_has_no_item_segment);
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
