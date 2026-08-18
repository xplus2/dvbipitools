/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dipibcg/announce.h"
#include "lib/ioutil.h"

static void write_temp_file(char *path, const char *content) {
  int fd = mkstemp(path);
  FILE *f;
  ck_assert_int_ge(fd, 0);
  f = fdopen(fd, "w");
  fputs(content, f);
  fclose(f);
}

START_TEST(date_to_mjd_matches_unix_epoch) {
  /* the reference minutes_to_unix relies on: MJD 40587 == 1970-01-01 */
  ck_assert_int_eq(date_to_mjd(1970, 1, 1), 40587L);
}
END_TEST

START_TEST(date_to_mjd_increments_by_one_per_day) {
  ck_assert_int_eq(date_to_mjd(2020, 1, 2) - date_to_mjd(2020, 1, 1), 1);
}
END_TEST

START_TEST(date_to_mjd_handles_leap_day_rollover) {
  ck_assert_int_eq(date_to_mjd(2020, 3, 1) - date_to_mjd(2020, 2, 29), 1);
}
END_TEST

START_TEST(minutes_to_unix_zero_at_epoch) {
  ck_assert_int_eq(minutes_to_unix(date_to_mjd(1970, 1, 1) * 1440L), 0L);
}
END_TEST

START_TEST(minutes_to_unix_one_day_later) {
  ck_assert_int_eq(minutes_to_unix(date_to_mjd(1970, 1, 2) * 1440L), 86400L);
}
END_TEST

START_TEST(iso8601_parses_z_suffix) {
  long a, b;
  ck_assert_int_eq(iso8601_to_minutes("2020-01-01T00:00:00Z", &a), 0);
  ck_assert_int_eq(iso8601_to_minutes("2020-01-01T01:30:00Z", &b), 0);
  ck_assert_int_eq(b - a, 90);
}
END_TEST

START_TEST(iso8601_positive_offset_converts_to_utc) {
  long with_offset, utc;
  ck_assert_int_eq(iso8601_to_minutes("2020-01-01T02:00:00+02:00", &with_offset), 0);
  ck_assert_int_eq(iso8601_to_minutes("2020-01-01T00:00:00Z", &utc), 0);
  ck_assert_int_eq(with_offset, utc);
}
END_TEST

START_TEST(iso8601_negative_offset_converts_to_utc) {
  long with_offset, utc;
  ck_assert_int_eq(iso8601_to_minutes("2020-01-01T00:00:00-02:00", &with_offset), 0);
  ck_assert_int_eq(iso8601_to_minutes("2020-01-01T02:00:00Z", &utc), 0);
  ck_assert_int_eq(with_offset, utc);
}
END_TEST

START_TEST(iso8601_rejects_too_short) {
  long out;
  ck_assert_int_eq(iso8601_to_minutes("2020-01-01T00:00", &out), -1);
}
END_TEST

START_TEST(iso8601_rejects_bad_separators) {
  long out;
  ck_assert_int_eq(iso8601_to_minutes("2020/01/01T00:00:00Z", &out), -1);
}
END_TEST

START_TEST(iso8601_rejects_non_digit_fields) {
  long out;
  ck_assert_int_eq(iso8601_to_minutes("20XX-01-01T00:00:00Z", &out), -1);
}
END_TEST

START_TEST(iso8601_rejects_malformed_offset) {
  long out;
  ck_assert_int_eq(iso8601_to_minutes("2020-01-01T00:00:00+0200", &out), -1);
}
END_TEST

static bcg_channel_t *add_channel(bcg_doc_t *d, const char *id) {
  bcg_channel_t *c = bcg_add_channel(d);
  snprintf(c->id, sizeof c->id, "%s", id);
  return c;
}

static bcg_programme_t *add_programme(bcg_doc_t *d, const char *chan, const char *start, const char *stop) {
  bcg_programme_t *pr = bcg_add_programme(d);
  snprintf(pr->channel_id, sizeof pr->channel_id, "%s", chan);
  snprintf(pr->start, sizeof pr->start, "%s", start);
  pr->stop[0] = '\0';
  if (stop)
    snprintf(pr->stop, sizeof pr->stop, "%s", stop);
  return pr;
}

START_TEST(build_windowed_doc_copies_all_channels) {
  bcg_doc_t src, dst;
  bcg_doc_init(&src);
  add_channel(&src, "ch1");
  add_channel(&src, "ch2");

  ck_assert_int_eq(build_windowed_doc(&src, &dst, 1000, 60), 0);
  ck_assert_int_eq(dst.channel_count, 2);
  ck_assert_str_eq(dst.channels[0].id, "ch1");
  ck_assert_str_eq(dst.channels[1].id, "ch2");

  bcg_doc_free(&src);
  bcg_doc_free(&dst);
}
END_TEST

START_TEST(build_windowed_doc_filters_programmes_by_window) {
  bcg_doc_t src, dst;
  bcg_doc_init(&src);
  add_channel(&src, "ch1");
  /* ended before now: excluded */
  add_programme(&src, "ch1", "2020-01-01T00:00:00Z", "2020-01-01T00:00:00Z");
  /* starts after now+window: excluded */
  add_programme(&src, "ch1", "2020-01-02T12:00:00Z", NULL);
  /* malformed start: skipped */
  add_programme(&src, "ch1", "not-a-time", NULL);

  ck_assert_int_eq(build_windowed_doc(&src, &dst, 1000, 60), 0);
  ck_assert_int_eq(dst.programme_count, 0);

  bcg_doc_free(&src);
  bcg_doc_free(&dst);
}
END_TEST

START_TEST(build_windowed_doc_includes_in_range_and_no_stop_programmes) {
  bcg_doc_t src, dst;
  long now = date_to_mjd(2020, 1, 1) * 1440L;
  char start_a[32], stop_a[32], start_b[32];

  bcg_doc_init(&src);
  add_channel(&src, "ch1");
  /* within window, has a stop time */
  snprintf(start_a, sizeof start_a, "2020-01-01T00:10:00Z");
  snprintf(stop_a, sizeof stop_a, "2020-01-01T01:10:00Z");
  add_programme(&src, "ch1", start_a, stop_a);
  /* within window, no stop: end defaults to start */
  snprintf(start_b, sizeof start_b, "2020-01-01T00:05:00Z");
  add_programme(&src, "ch1", start_b, NULL);

  ck_assert_int_eq(build_windowed_doc(&src, &dst, now, 60), 0);
  ck_assert_int_eq(dst.programme_count, 2);

  bcg_doc_free(&src);
  bcg_doc_free(&dst);
}
END_TEST

START_TEST(load_doc_applies_mapping_to_matching_channel) {
  char xmltv_path[] = "/tmp/dvbipitools_test_announce_xmltv_XXXXXX";
  char map_path[] = "/tmp/dvbipitools_test_announce_map_XXXXXX";
  config_t cfg;
  bcg_doc_t doc;

  write_temp_file(xmltv_path,
                   "<?xml version=\"1.0\"?>\n<tv>\n"
                   "  <channel id=\"channel1\"><display-name>Channel One</display-name></channel>\n"
                   "  <programme start=\"20200101120000 +0000\" channel=\"channel1\">"
                   "<title>News</title></programme>\n"
                   "</tv>\n");
  write_temp_file(map_path, "channel1,rtp://239.1.1.1:5000,1,2,101\n");

  memset(&cfg, 0, sizeof cfg);
  cfg.input_path = xmltv_path;
  cfg.map_path = map_path;

  ck_assert_int_eq(load_doc(&cfg, &doc), 0);
  ck_assert_int_eq(doc.channel_count, 1);
  ck_assert_str_eq(doc.channels[0].id, "channel1");
  ck_assert_str_eq(doc.channels[0].uri, "rtp://239.1.1.1:5000");
  ck_assert_uint_eq(doc.channels[0].tsid, 1u);
  ck_assert_uint_eq(doc.channels[0].onid, 2u);
  ck_assert_uint_eq(doc.channels[0].sid, 101u);
  ck_assert_int_eq(doc.programme_count, 1);
  ck_assert_str_eq(doc.programmes[0].title, "News");

  bcg_doc_free(&doc);
  unlink(xmltv_path);
  unlink(map_path);
}
END_TEST

START_TEST(load_doc_rejects_missing_input) {
  char map_path[] = "/tmp/dvbipitools_test_announce_map_XXXXXX";
  config_t cfg;
  bcg_doc_t doc;

  write_temp_file(map_path, "channel1,rtp://239.1.1.1:5000,1,2,101\n");
  memset(&cfg, 0, sizeof cfg);
  cfg.input_path = "/nonexistent/dvbipitools_test_announce.xml";
  cfg.map_path = map_path;

  ck_assert_int_eq(load_doc(&cfg, &doc), -1);
  unlink(map_path);
}
END_TEST

START_TEST(load_doc_rejects_missing_map) {
  char xmltv_path[] = "/tmp/dvbipitools_test_announce_xmltv_XXXXXX";
  config_t cfg;
  bcg_doc_t doc;

  write_temp_file(xmltv_path, "<?xml version=\"1.0\"?>\n<tv></tv>\n");
  memset(&cfg, 0, sizeof cfg);
  cfg.input_path = xmltv_path;
  cfg.map_path = "/nonexistent/dvbipitools_test_announce.csv";

  ck_assert_int_eq(load_doc(&cfg, &doc), -1);
  unlink(xmltv_path);
}
END_TEST

static Suite *announce_suite(void) {
  Suite *s = suite_create("dipibcg_announce");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, date_to_mjd_matches_unix_epoch);
  tcase_add_test(tc, date_to_mjd_increments_by_one_per_day);
  tcase_add_test(tc, date_to_mjd_handles_leap_day_rollover);
  tcase_add_test(tc, minutes_to_unix_zero_at_epoch);
  tcase_add_test(tc, minutes_to_unix_one_day_later);
  tcase_add_test(tc, iso8601_parses_z_suffix);
  tcase_add_test(tc, iso8601_positive_offset_converts_to_utc);
  tcase_add_test(tc, iso8601_negative_offset_converts_to_utc);
  tcase_add_test(tc, iso8601_rejects_too_short);
  tcase_add_test(tc, iso8601_rejects_bad_separators);
  tcase_add_test(tc, iso8601_rejects_non_digit_fields);
  tcase_add_test(tc, iso8601_rejects_malformed_offset);
  tcase_add_test(tc, build_windowed_doc_copies_all_channels);
  tcase_add_test(tc, build_windowed_doc_filters_programmes_by_window);
  tcase_add_test(tc, build_windowed_doc_includes_in_range_and_no_stop_programmes);
  tcase_add_test(tc, load_doc_applies_mapping_to_matching_channel);
  tcase_add_test(tc, load_doc_rejects_missing_input);
  tcase_add_test(tc, load_doc_rejects_missing_map);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(announce_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
