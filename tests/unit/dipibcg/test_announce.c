/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../log_capture.h"
#include "../metrics_sink.h"
#include "../run_helper.h"
#include "dipibcg/announce.h"
#include "lib/sys/ioutil.h"
#include "lib/sys/signal.h"

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
  bufcpy(c->id, sizeof c->id, id);
  return c;
}

static bcg_programme_t *add_programme(bcg_doc_t *d, const char *chan, const char *start, const char *stop) {
  bcg_programme_t *pr = bcg_add_programme(d);
  bufcpy(pr->channel_id, sizeof pr->channel_id, chan);
  bufcpy(pr->start, sizeof pr->start, start);
  pr->stop[0] = '\0';
  if (stop)
    bufcpy(pr->stop, sizeof pr->stop, stop);
  return pr;
}

START_TEST(build_windowed_doc_copies_all_channels) {
  bcg_doc_t src, dst;
  bcg_doc_init(&src);
  add_channel(&src, "ch1");
  add_channel(&src, "ch2");

  ck_assert_int_eq(build_windowed_doc(&src, &dst, 1000, 60, NULL, NULL, NULL), 0);
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

  ck_assert_int_eq(build_windowed_doc(&src, &dst, 1000, 60, NULL, NULL, NULL), 0);
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
  bufcpy(start_a, sizeof start_a, "2020-01-01T00:10:00Z");
  bufcpy(stop_a, sizeof stop_a, "2020-01-01T01:10:00Z");
  add_programme(&src, "ch1", start_a, stop_a);
  /* within window, no stop: end defaults to start */
  bufcpy(start_b, sizeof start_b, "2020-01-01T00:05:00Z");
  add_programme(&src, "ch1", start_b, NULL);

  ck_assert_int_eq(build_windowed_doc(&src, &dst, now, 60, NULL, NULL, NULL), 0);
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

#define GUIDE_BUF 8192
#define GUIDE_MAX_CHANNELS 4
#define PROG_MAX 8

typedef struct {
  const char *channels[GUIDE_MAX_CHANNELS];
  unsigned nchannels;
  const char *programmes[PROG_MAX];
  unsigned nprogrammes;
} guide_t;

static void xmltv_stamp(char *out, size_t cap, long offset_s) {
  time_t t = time(NULL) + offset_s;
  struct tm tm;

  gmtime_r(&t, &tm);
  strftime(out, cap, "%Y%m%d%H%M%S +0000", &tm);
}

static void guide_text(const guide_t *g, char *buf, size_t cap) {
  char start[32];
  char stop[32];
  size_t n = 0;

  xmltv_stamp(start, sizeof start, -600);
  xmltv_stamp(stop, sizeof stop, 3600);
  n += (size_t)snprintf(buf + n, cap - n, "<?xml version=\"1.0\"?>\n<tv>\n");
  for (unsigned i = 0; i < g->nchannels; i++)
    n += (size_t)snprintf(buf + n, cap - n, "<channel id=\"%s\"><display-name>Name %s</display-name></channel>\n", g->channels[i], g->channels[i]);
  for (unsigned i = 0; i < g->nprogrammes; i++)
    n += (size_t)snprintf(buf + n, cap - n, "<programme start=\"%s\" stop=\"%s\" channel=\"%s\"><title>Show %u</title></programme>\n", start, stop, g->programmes[i], i);
  snprintf(buf + n, cap - n, "</tv>\n");
}

static void map_text(const guide_t *g, char *buf, size_t cap) {
  size_t n = 0;

  for (unsigned i = 0; i < g->nchannels; i++)
    n += (size_t)snprintf(buf + n, cap - n, "%s,rtp://239.1.1.%u:5000,1,2,%u\n", g->channels[i], i + 1, 101 + i);
}

static const guide_t guide_one = {{"ch1"}, 1, {"ch1"}, 1};
static const guide_t guide_two = {{"ch1", "ch2"}, 2, {"ch1", "ch2"}, 2};
static const guide_t guide_unsorted = {{"zz", "aa", "mm"}, 3, {"aa", "aa", "zz", "ghost"}, 4};

typedef struct {
  const guide_t *initial;
  const guide_t *reloaded;
  const char *bad_map;
  const char *reload_msg;
  unsigned sources_up;
  unsigned services;
} reload_case_t;

static const reload_case_t reload_cases[] = {
  {&guide_one, &guide_two, NULL, "reloaded 2 channels, 2 programmes from", 1, 2},
  {&guide_one, NULL, "not,enough\n", "reload failed, keeping previous guide", 0, 1},
};

static void run_announce(const config_t *cfg, run_helper_t *h, sink_t *ms, int *rc, char *msg, size_t msg_cap) {
  pthread_t th;

  signals_install();
  log_capture_begin();
  ck_assert_int_eq(pthread_create(&th, NULL, run_helper_thread, h), 0);
  *rc = announce_run(cfg, &ms->mx);
  pthread_join(th, NULL);
  log_capture_end(msg, msg_cap);
}

START_TEST(announce_run_reloads_the_guide_on_sighup_or_keeps_the_previous_one) {
  const reload_case_t *c = &reload_cases[_i];
  char xmltv_path[] = "/tmp/dvbipitools_test_bcg_xmltv_XXXXXX";
  char map_path[] = "/tmp/dvbipitools_test_bcg_map_XXXXXX";
  char text[GUIDE_BUF];
  char reloaded[GUIDE_BUF];
  char msg[16384];
  run_helper_t h;
  config_t cfg;
  sink_t ms;
  seen_t seen;
  uint64_t v = 0;
  int rc;

  guide_text(c->initial, text, sizeof text);
  write_temp_file(xmltv_path, text);
  map_text(&guide_two, text, sizeof text);
  write_temp_file(map_path, text);
  if (c->reloaded) guide_text(c->reloaded, reloaded, sizeof reloaded);
  memset(&cfg, 0, sizeof cfg);
  cfg.input_path = xmltv_path;
  cfg.map_path = map_path;
  cfg.window_hours = 2;
  cfg.family = AF_INET;
  cfg.mcast_port = 3938;
  run_helper_group(cfg.mcast_group, sizeof cfg.mcast_group, 81);
  cfg.interval_s = 1;
  h.path = c->reloaded ? xmltv_path : map_path;
  h.rewrite = c->reloaded ? reloaded : c->bad_map;
  h.rewrite_ms = 200;
  h.stop_ms = 1250;
  sink_open(&ms, METRICS_COMPONENT_BCG, "bcg1", 0.1);
  run_announce(&cfg, &h, &ms, &rc, msg, sizeof msg);
  ck_assert_int_eq(rc, 0);
  ck_assert_ptr_nonnull(strstr(msg, c->reload_msg));
  ck_assert_ptr_nonnull(strstr(msg, "stopped after 2 cycles"));
  ck_assert_int_eq(sink_read(&ms, &seen), 1);
  ck_assert_int_eq(seen_last(&seen, METRICS_ID_BCG_SOURCES_UP, &v), 1);
  ck_assert_uint_eq(v, c->sources_up);
  ck_assert_int_eq(seen_last(&seen, METRICS_ID_BCG_SERVICES, &v), 1);
  ck_assert_uint_eq(v, c->services);
  sink_close(&ms);
  unlink(xmltv_path);
  unlink(map_path);
}
END_TEST

START_TEST(announce_run_counts_services_with_events_across_unsorted_channels) {
  char xmltv_path[] = "/tmp/dvbipitools_test_bcg_xmltv_XXXXXX";
  char map_path[] = "/tmp/dvbipitools_test_bcg_map_XXXXXX";
  char text[GUIDE_BUF];
  char msg[16384];
  run_helper_t h;
  config_t cfg;
  sink_t ms;
  seen_t seen;
  uint64_t v = 0;
  int rc;

  guide_text(&guide_unsorted, text, sizeof text);
  write_temp_file(xmltv_path, text);
  map_text(&guide_unsorted, text, sizeof text);
  write_temp_file(map_path, text);
  memset(&cfg, 0, sizeof cfg);
  cfg.input_path = xmltv_path;
  cfg.map_path = map_path;
  cfg.window_hours = 2;
  cfg.family = AF_INET;
  cfg.mcast_port = 3938;
  run_helper_group(cfg.mcast_group, sizeof cfg.mcast_group, 82);
  cfg.interval_s = 1;
  h.path = xmltv_path;
  h.rewrite = NULL;
  h.rewrite_ms = 0;
  h.stop_ms = 300;
  sink_open(&ms, METRICS_COMPONENT_BCG, "bcg1", 0.1);
  run_announce(&cfg, &h, &ms, &rc, msg, sizeof msg);
  ck_assert_int_eq(rc, 0);
  ck_assert_int_eq(sink_read(&ms, &seen), 1);
  ck_assert_int_eq(seen_last(&seen, METRICS_ID_BCG_SERVICES, &v), 1);
  ck_assert_uint_eq(v, 3u);
  ck_assert_int_eq(seen_last(&seen, METRICS_ID_BCG_SERVICES_WITH_EVENTS, &v), 1);
  ck_assert_uint_eq(v, 2u);
  ck_assert_int_eq(seen_last(&seen, METRICS_ID_BCG_EVENTS, &v), 1);
  ck_assert_uint_eq(v, 4u);
  ck_assert_int_eq(seen_last(&seen, METRICS_ID_BCG_PUBLICATIONS_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 1u);
  sink_close(&ms);
  unlink(xmltv_path);
  unlink(map_path);
}
END_TEST

START_TEST(version_track_bumps_only_when_content_changes) {
  static const unsigned char a[] = {1, 2, 3};
  static const unsigned char b[] = {1, 2, 4};
  static const unsigned char c[] = {1, 2};
  bcg_version_t v;

  memset(&v, 0, sizeof v);
  version_track(&v, a, sizeof a);
  ck_assert_uint_eq(v.version, 1u);
  version_track(&v, a, sizeof a);
  ck_assert_uint_eq(v.version, 1u);
  version_track(&v, b, sizeof b);
  ck_assert_uint_eq(v.version, 2u);
  version_track(&v, c, sizeof c);
  ck_assert_uint_eq(v.version, 3u);
  version_track(&v, c, sizeof c);
  ck_assert_uint_eq(v.version, 3u);
  free(v.last);
}
END_TEST

START_TEST(version_track_wraps_from_255_to_1) {
  static const unsigned char a[] = {1};
  static const unsigned char b[] = {2};
  bcg_version_t v;

  memset(&v, 0, sizeof v);
  v.version = 255;
  version_track(&v, a, sizeof a);
  ck_assert_uint_eq(v.version, 1u);
  version_track(&v, b, sizeof b);
  ck_assert_uint_eq(v.version, 2u);
  free(v.last);
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
  tcase_add_loop_test(tc, announce_run_reloads_the_guide_on_sighup_or_keeps_the_previous_one, 0, (int)(sizeof reload_cases / sizeof reload_cases[0]));
  tcase_add_test(tc, announce_run_counts_services_with_events_across_unsorted_channels);
  tcase_add_test(tc, version_track_bumps_only_when_content_changes);
  tcase_add_test(tc, version_track_wraps_from_255_to_1);
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
