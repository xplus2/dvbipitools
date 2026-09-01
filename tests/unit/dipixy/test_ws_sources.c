/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dipixy/args.h"
#include "dipixy/ts/channels/channels.h"
#include "dipixy/ws/ws_sources.h"

static void write_temp_m3u(char *path, const char *content) {
  char tmpl[] = "/tmp/dvbipitools_test_ws_sources_XXXXXX.m3u";
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

START_TEST(snapshot_stdin_only_unnamed) {
  config_t cfg;
  char *out = NULL;
  memset(&cfg, 0, sizeof cfg);
  cfg.stdin_ordinal = 1;

  ck_assert_int_eq(ws_sources_build_snapshot(&cfg, NULL, &out), 0);
  ck_assert_str_eq(out, "{\"type\":\"sources.snapshot\",\"sources\":[{\"kind\":\"stdin\",\"name\":null}]}");
}
END_TEST

START_TEST(snapshot_stdin_only_named) {
  config_t cfg;
  char *out = NULL;
  memset(&cfg, 0, sizeof cfg);
  cfg.stdin_ordinal = 1;
  cfg.stdin_name = "Main";

  ck_assert_int_eq(ws_sources_build_snapshot(&cfg, NULL, &out), 0);
  ck_assert_str_eq(out, "{\"type\":\"sources.snapshot\",\"sources\":[{\"kind\":\"stdin\",\"name\":\"Main\"}]}");
}
END_TEST

START_TEST(snapshot_rist_only) {
  config_t cfg;
  char *out = NULL;
  memset(&cfg, 0, sizeof cfg);
  cfg.rist_ordinal = 1;

  ck_assert_int_eq(ws_sources_build_snapshot(&cfg, NULL, &out), 0);
  ck_assert_str_eq(out, "{\"type\":\"sources.snapshot\",\"sources\":[{\"kind\":\"rist\",\"name\":null}]}");
}
END_TEST

START_TEST(snapshot_m3u_source_lists_items) {
  config_t cfg;
  source_def_t src;
  char path[160];
  channels_t *ch;
  char *out = NULL;

  write_temp_m3u(path,
                  "#EXTINF:-1,Chan A\n"
                  "rtp://@239.1.1.1:5000\n"
                  "#EXTINF:-1,Chan B\n"
                  "rtp://@239.1.1.2:5001\n");

  memset(&cfg, 0, sizeof cfg);
  memset(&src, 0, sizeof src);
  src.kind = SRC_M3U;
  src.value = path;
  src.ordinal = 1;
  cfg.sources = &src;
  cfg.n_sources = 1;

  ch = channels_build(&cfg);
  unlink(path);
  ck_assert_ptr_nonnull(ch);

  ck_assert_int_eq(ws_sources_build_snapshot(&cfg, ch, &out), 0);
  ck_assert_ptr_nonnull(strstr(out, "\"kind\":\"m3u\""));
  ck_assert_ptr_nonnull(strstr(out, "\"name\":null"));
  ck_assert_ptr_nonnull(strstr(out, "\"list_num\":1"));
  ck_assert_ptr_nonnull(strstr(out, "\"name\":\"Chan A\""));
  ck_assert_ptr_nonnull(strstr(out, "\"name\":\"Chan B\""));

  channels_free(ch);
}
END_TEST

START_TEST(snapshot_orders_by_ordinal_and_omits_items_for_stdin_rist) {
  config_t cfg;
  source_def_t src;
  char path[160];
  channels_t *ch;
  char *out = NULL;

  write_temp_m3u(path, "#EXTINF:-1,Only\nrtp://@239.1.1.1:5000\n");

  memset(&cfg, 0, sizeof cfg);
  memset(&src, 0, sizeof src);
  src.kind = SRC_M3U;
  src.value = path;
  src.ordinal = 2;
  cfg.sources = &src;
  cfg.n_sources = 1;
  cfg.stdin_ordinal = 1;
  cfg.rist_ordinal = 3;

  ch = channels_build(&cfg);
  unlink(path);
  ck_assert_ptr_nonnull(ch);

  ck_assert_int_eq(ws_sources_build_snapshot(&cfg, ch, &out), 0);

  {
    const char *stdin_pos = strstr(out, "\"kind\":\"stdin\"");
    const char *m3u_pos = strstr(out, "\"kind\":\"m3u\"");
    const char *rist_pos = strstr(out, "\"kind\":\"rist\"");
    ck_assert_ptr_nonnull(stdin_pos);
    ck_assert_ptr_nonnull(m3u_pos);
    ck_assert_ptr_nonnull(rist_pos);
    ck_assert(stdin_pos < m3u_pos);
    ck_assert(m3u_pos < rist_pos);
    /* stdin/rist have no list_num/items at all (list_num 0) */
    ck_assert(strstr(out, "\"list_num\":1") == NULL);
    ck_assert(strstr(out, "\"list_num\":3") == NULL);
  }

  channels_free(ch);
}
END_TEST

START_TEST(update_wraps_single_source_with_items) {
  config_t cfg;
  source_def_t src;
  char path[160];
  channels_t *ch;
  char *out = NULL;

  write_temp_m3u(path, "#EXTINF:-1,Only\nrtp://@239.1.1.1:5000\n");

  memset(&cfg, 0, sizeof cfg);
  memset(&src, 0, sizeof src);
  src.kind = SRC_XSPF;
  src.value = path; /* content is m3u but kind label under test is what matters here */
  src.ordinal = 1;
  src.name = "Reloaded";
  cfg.sources = &src;
  cfg.n_sources = 1;

  ch = channels_build(&cfg);
  unlink(path);
  ck_assert_ptr_nonnull(ch);

  ck_assert_int_eq(ws_sources_build_update(ch, &src, 1, &out), 0);
  ck_assert_ptr_nonnull(strstr(out, "\"type\":\"sources.update\""));
  ck_assert_ptr_nonnull(strstr(out, "\"kind\":\"xspf\""));
  ck_assert_ptr_nonnull(strstr(out, "\"name\":\"Reloaded\""));
  ck_assert_ptr_nonnull(strstr(out, "\"list_num\":1"));

  channels_free(ch);
}
END_TEST

START_TEST(source_kind_names_cover_all_kinds) {
  channels_t *ch;
  config_t cfg;
  source_def_t src;
  char path[160];
  static const struct {
    source_kind_t kind;
    const char *name;
  } cases[] = {
      {SRC_SDS, "sds"}, {SRC_M3U, "m3u"}, {SRC_XSPF, "xspf"}, {SRC_CSV, "csv"}, {SRC_XML, "xml"}, {SRC_HTTP, "http"},
  };
  size_t i;

  write_temp_m3u(path, "#EXTINF:-1,Only\nrtp://@239.1.1.1:5000\n");
  memset(&cfg, 0, sizeof cfg);
  memset(&src, 0, sizeof src);
  src.kind = SRC_M3U;
  src.value = path;
  src.ordinal = 1;
  cfg.sources = &src;
  cfg.n_sources = 1;
  ch = channels_build(&cfg);
  unlink(path);
  ck_assert_ptr_nonnull(ch);

  for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    char *out = NULL;
    source_def_t s2 = src;
    char expect[32];
    s2.kind = cases[i].kind;
    ck_assert_int_eq(ws_sources_build_update(ch, &s2, 1, &out), 0);
    snprintf(expect, sizeof expect, "\"kind\":\"%s\"", cases[i].name);
    ck_assert_ptr_nonnull(strstr(out, expect));
  }

  channels_free(ch);
}
END_TEST

static Suite *ws_sources_suite(void) {
  Suite *s = suite_create("dipixy_ws_sources");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, snapshot_stdin_only_unnamed);
  tcase_add_test(tc, snapshot_stdin_only_named);
  tcase_add_test(tc, snapshot_rist_only);
  tcase_add_test(tc, snapshot_m3u_source_lists_items);
  tcase_add_test(tc, snapshot_orders_by_ordinal_and_omits_items_for_stdin_rist);
  tcase_add_test(tc, update_wraps_single_source_with_items);
  tcase_add_test(tc, source_kind_names_cover_all_kinds);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(ws_sources_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
