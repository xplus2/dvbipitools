#define _GNU_SOURCE
/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <check.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lib/helper/dateutil.h"
#include "../log_capture.h"
#include "../metrics_sink.h"
#include "../run_helper.h"
#include "dipibcg/announce.h"
#include "dipibcg/announce/announce_int.h"
#include "../alloc_fail.h"
#include "../bcg_schema_fixture.h"
#include "dipibcg/schema.h"
#include "lib/bim/bcgau.h"
#include "lib/bim/bcgip.h"
#include "lib/bim/container.h"
#include "lib/bim/dvbstream.h"
#include "lib/helper/xml_util.h"
#include "lib/net/dvbstp.h"
#include "lib/net/multicast.h"
#include "lib/sys/ioutil.h"
#include "lib/sys/signal.h"
#include "../fx_net.h"

static void write_temp_file(char *path, const char *content) {
  int fd = mkstemp(path);
  FILE *f;
  if (fd < 0) abort();
  f = fdopen(fd, "w");
  if (!f) abort();
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

static int build_windowed(bcg_doc_t *src, bcg_doc_t *dst, long now, int64_t window_min) {
  prog_time_t *times;
  int *sel = NULL;
  int n;
  int rc;
  if (prog_times_build(src, &times)) return -1;
  if (src->programme_count > 0) sel = malloc(sizeof *sel * (size_t)src->programme_count);
  window_select(src, times, now, window_min, sel, &n, NULL, NULL, NULL);
  rc = window_view(src, times, sel, n, dst);
  free(sel);
  free(times);
  return rc;
}

START_TEST(window_view_copies_all_channels) {
  bcg_doc_t src, dst;
  bcg_doc_init(&src);
  add_channel(&src, "ch1");
  add_channel(&src, "ch2");

  ck_assert_int_eq(build_windowed(&src, &dst, 1000, 60), 0);
  ck_assert_int_eq(dst.channel_count, 2);
  ck_assert_str_eq(dst.channels[0].id, "ch1");
  ck_assert_str_eq(dst.channels[1].id, "ch2");

  bcg_doc_free(&src);
  window_view_free(&dst);
}
END_TEST

START_TEST(window_view_filters_programmes_by_window) {
  bcg_doc_t src, dst;
  bcg_doc_init(&src);
  add_channel(&src, "ch1");
  /* ended before now: excluded */
  add_programme(&src, "ch1", "2020-01-01T00:00:00Z", "2020-01-01T00:00:00Z");
  /* starts after now+window: excluded */
  add_programme(&src, "ch1", "2020-01-02T12:00:00Z", NULL);
  /* malformed start: skipped */
  add_programme(&src, "ch1", "not-a-time", NULL);

  ck_assert_int_eq(build_windowed(&src, &dst, 1000, 60), 0);
  ck_assert_int_eq(dst.programme_count, 0);

  bcg_doc_free(&src);
  window_view_free(&dst);
}
END_TEST

START_TEST(window_view_huge_window_does_not_overflow) {
  bcg_doc_t src;
  bcg_doc_t dst;
  bcg_doc_init(&src);
  add_channel(&src, "ch1");
  add_programme(&src, "ch1", "2020-01-02T12:00:00Z", "2020-01-02T13:00:00Z");

  ck_assert_int_eq(build_windowed(&src, &dst, 1000, (int64_t)UINT_MAX * 60), 0);
  ck_assert_int_eq(dst.programme_count, 1);

  bcg_doc_free(&src);
  window_view_free(&dst);
}
END_TEST

START_TEST(window_view_includes_in_range_and_no_stop_programmes) {
  bcg_doc_t src, dst;
  long now = date_to_mjd(2020, 1, 1) * 1440L;
  char start_a[32], stop_a[32], start_b[32];

  bcg_doc_init(&src);
  add_channel(&src, "ch1");
  /* within window, has a stop time */
  bufcpy(start_a, sizeof start_a, "2020-01-01T00:10:00Z");
  bufcpy(stop_a, sizeof stop_a, "2020-01-01T01:10:00Z");
  add_programme(&src, "ch1", start_a, stop_a);
  /* within window, no stop: open-ended */
  bufcpy(start_b, sizeof start_b, "2020-01-01T00:05:00Z");
  add_programme(&src, "ch1", start_b, NULL);

  ck_assert_int_eq(build_windowed(&src, &dst, now, 60), 0);
  ck_assert_int_eq(dst.programme_count, 2);
  ck_assert_str_eq(dst.programmes[1].stop, start_a);

  bcg_doc_free(&src);
  window_view_free(&dst);
}
END_TEST

START_TEST(window_view_replaces_invalid_stop_by_next_start) {
  bcg_doc_t src;
  bcg_doc_t dst;
  long now = date_to_mjd(2020, 1, 1) * 1440L + 600;

  bcg_doc_init(&src);
  add_channel(&src, "ch1");
  add_programme(&src, "ch1", "2020-01-01T09:30:00Z", "garbage");
  add_programme(&src, "ch1", "2020-01-01T10:30:00Z", "2020-01-01T11:30:00Z");
  add_programme(&src, "ch1", "2020-01-01T08:00:00Z", "2020-01-01T09:00:00Z");

  ck_assert_int_eq(build_windowed(&src, &dst, now, 60), 0);
  ck_assert_int_eq(dst.programme_count, 2);
  ck_assert_str_eq(dst.programmes[0].start, "2020-01-01T09:30:00Z");
  ck_assert_str_eq(dst.programmes[0].stop, "2020-01-01T10:30:00Z");

  bcg_doc_free(&src);
  window_view_free(&dst);
}
END_TEST

/* TS 102 323 8.5: last stopless programme of a channel has no duration, left out */
START_TEST(window_view_ends_stopless_programme_at_next_start) {
  bcg_doc_t src;
  bcg_doc_t dst;
  long now = date_to_mjd(2020, 1, 1) * 1440L + 555;

  bcg_doc_init(&src);
  add_channel(&src, "ch1");
  add_channel(&src, "ch2");
  add_programme(&src, "ch1", "2020-01-01T09:30:00Z", NULL);
  add_programme(&src, "ch2", "2020-01-01T05:00:00Z", NULL);
  add_programme(&src, "ch1", "2020-01-01T05:00:00Z", NULL);
  add_programme(&src, "ch1", "2020-01-01T09:00:00Z", NULL);

  ck_assert_int_eq(build_windowed(&src, &dst, now, 60), 0);
  ck_assert_int_eq(dst.programme_count, 1);
  ck_assert_str_eq(dst.programmes[0].start, "2020-01-01T09:00:00Z");
  ck_assert_str_eq(dst.programmes[0].stop, "2020-01-01T09:30:00Z");

  bcg_doc_free(&src);
  window_view_free(&dst);
}
END_TEST

START_TEST(window_select_reports_schedule_bounds_and_indices) {
  bcg_doc_t src;
  prog_time_t *times;
  int sel[3];
  int n;
  int have;
  long ss;
  long se;
  long now = date_to_mjd(2020, 1, 1) * 1440L + 600;

  bcg_doc_init(&src);
  add_channel(&src, "ch1");
  add_programme(&src, "ch1", "2020-01-01T08:00:00Z", "2020-01-01T09:00:00Z");
  add_programme(&src, "ch1", "2020-01-01T09:30:00Z", "2020-01-01T10:30:00Z");
  add_programme(&src, "ch1", "bad", NULL);
  ck_assert_int_eq(prog_times_build(&src, &times), 0);
  ck_assert(!times[2].valid);
  window_select(&src, times, now, 60, sel, &n, &ss, &se, &have);
  ck_assert_int_eq(n, 1);
  ck_assert_int_eq(sel[0], 1);
  ck_assert_int_eq(have, 1);
  ck_assert_int_eq(se - ss, 60);

  free(times);
  bcg_doc_free(&src);
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

  ck_assert_int_eq(load_doc(&cfg, 1, &doc), 0);
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

/* duplicate channel id: first wins, warned */
START_TEST(load_doc_drops_duplicate_channel_ids) {
  char xmltv_path[] = "/tmp/dvbipitools_test_announce_xmltv_XXXXXX";
  char map_path[] = "/tmp/dvbipitools_test_announce_map_XXXXXX";
  char msg[4096];
  config_t cfg;
  bcg_doc_t doc;

  write_temp_file(xmltv_path,
                   "<tv><channel id=\"ch1\"><display-name>First</display-name></channel>"
                   "<channel id=\"ch2\"><display-name>Two</display-name></channel>"
                   "<channel id=\"ch1\"><display-name>Second</display-name></channel></tv>\n");
  write_temp_file(map_path, "ch1,rtp://239.1.1.1:5000,1,2,101\n");
  memset(&cfg, 0, sizeof cfg);
  cfg.input_path = xmltv_path;
  cfg.map_path = map_path;
  log_capture_begin();
  ck_assert_int_eq(load_doc(&cfg, 0, &doc), 0);
  log_capture_end(msg, sizeof msg);
  ck_assert_ptr_nonnull(strstr(msg, "GW000774"));
  ck_assert_ptr_nonnull(strstr(msg, "GW000778")); /* ch2 not in map */
  ck_assert_int_eq(doc.channel_count, 2);
  ck_assert_str_eq(doc.channels[0].id, "ch1");
  ck_assert_str_eq(doc.channels[0].names[0], "First");
  ck_assert_str_eq(doc.channels[1].id, "ch2");
  bcg_doc_free(&doc);
  for (long n = 0;; n++) {
    int hit;
    int rc;
    alloc_fail_at(n);
    rc = load_doc(&cfg, 1, &doc);
    hit = alloc_fail_end();
    if (!rc) bcg_doc_free(&doc);
    if (!hit) break;
  }
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

  ck_assert_int_eq(load_doc(&cfg, 1, &doc), -1);
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

  ck_assert_int_eq(load_doc(&cfg, 1, &doc), -1);
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

/* cfg without schema gets fixture dir */
static void run_announce(const config_t *cfg, run_helper_t *h, sink_t *ms, int *rc, char *msg, size_t msg_cap) {
  pthread_t th;
  config_t c2 = *cfg;
  char dir[64] = "";

  if (!c2.nschemas) {
    bcg_fixture_make(dir);
    c2.schemas[0] = dir;
    c2.nschemas = 1;
  }
  signals_install();
  log_capture_begin();
  ck_assert_int_eq(pthread_create(&th, NULL, run_helper_thread, h), 0);
  *rc = announce_run(&c2, &ms->mx);
  pthread_join(th, NULL);
  log_capture_end(msg, msg_cap);
  if (dir[0]) bcg_fixture_remove(dir);
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

/* reloaded guide not buildable (channel above 64 KiB repository): previous guide kept, announcer runs on */
START_TEST(announce_run_keeps_previous_guide_when_reload_fails_to_build) {
  char xmltv_path[] = "/tmp/dvbipitools_test_bcg_xmltv_XXXXXX";
  char map_path[] = "/tmp/dvbipitools_test_bcg_map_XXXXXX";
  char text[GUIDE_BUF];
  char start[32];
  char stop[32];
  char msg[16384];
  size_t cap = 80000;
  char *big = malloc(cap);
  size_t n;
  run_helper_t h;
  config_t cfg;
  sink_t ms;
  seen_t seen;
  uint64_t v = 0;
  int rc;

  ck_assert_ptr_nonnull(big);
  guide_text(&guide_one, text, sizeof text);
  write_temp_file(xmltv_path, text);
  map_text(&guide_one, text, sizeof text);
  write_temp_file(map_path, text);
  xmltv_stamp(start, sizeof start, -600);
  xmltv_stamp(stop, sizeof stop, 3600);
  n = (size_t)snprintf(big, cap, "<tv><channel id=\"ch1\"/><programme start=\"%s\" stop=\"%s\" channel=\"ch1\"><title>", start, stop);
  memset(big + n, 'x', 70000);
  snprintf(big + n + 70000, cap - n - 70000, "</title></programme></tv>\n");
  memset(&cfg, 0, sizeof cfg);
  cfg.input_path = xmltv_path;
  cfg.map_path = map_path;
  cfg.window_hours = 2;
  cfg.family = AF_INET;
  cfg.mcast_port = 3943;
  run_helper_group(cfg.mcast_group, sizeof cfg.mcast_group, 88);
  cfg.interval_s = 1;
  h.path = xmltv_path;
  h.rewrite = big;
  h.rewrite_ms = 200;
  h.stop_ms = 1250;
  sink_open(&ms, METRICS_COMPONENT_BCG, "bcg1", 0.1);
  run_announce(&cfg, &h, &ms, &rc, msg, sizeof msg);
  ck_assert_int_eq(rc, 0);
  ck_assert_ptr_nonnull(strstr(msg, "GE100145"));
  ck_assert_ptr_nonnull(strstr(msg, "guide build failed"));
  ck_assert_ptr_nonnull(strstr(msg, "reload failed, keeping previous guide"));
  ck_assert_ptr_nonnull(strstr(msg, "stopped after 2 cycles"));
  ck_assert_int_eq(sink_read(&ms, &seen), 1);
  ck_assert_int_eq(seen_last(&seen, METRICS_ID_BCG_EVENTS, &v), 1);
  ck_assert_uint_eq(v, 1u);
  ck_assert_int_eq(seen_last(&seen, METRICS_ID_BCG_PUBLICATION_ERRORS_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 0u);
  sink_close(&ms);
  free(big);
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
  /* announced only: "ghost" has no channel */
  ck_assert_int_eq(seen_last(&seen, METRICS_ID_BCG_EVENTS, &v), 1);
  ck_assert_uint_eq(v, 3u);
  ck_assert_int_eq(seen_last(&seen, METRICS_ID_BCG_PUBLICATIONS_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 1u);
  sink_close(&ms);
  unlink(xmltv_path);
  unlink(map_path);
}
END_TEST

START_TEST(announce_run_sends_one_container_per_channel) {
  char xmltv_path[] = "/tmp/dvbipitools_test_bcg_xmltv_XXXXXX";
  char map_path[] = "/tmp/dvbipitools_test_bcg_map_XXXXXX";
  char text[GUIDE_BUF];
  char msg[16384];
  run_helper_t h;
  config_t cfg;
  sink_t ms;
  int rc;

  guide_text(&guide_unsorted, text, sizeof text);
  write_temp_file(xmltv_path, text);
  map_text(&guide_unsorted, text, sizeof text);
  write_temp_file(map_path, text);
  memset(&cfg, 0, sizeof cfg);
  cfg.input_path = xmltv_path;
  cfg.map_path = map_path;
  cfg.window_hours = 2;
  cfg.verbose = 1;
  cfg.family = AF_INET;
  cfg.mcast_port = 3938;
  run_helper_group(cfg.mcast_group, sizeof cfg.mcast_group, 83);
  cfg.interval_s = 1;
  h.path = xmltv_path;
  h.rewrite = NULL;
  h.rewrite_ms = 0;
  h.stop_ms = 300;
  sink_open(&ms, METRICS_COMPONENT_BCG, "bcg1", 0.1);
  run_announce(&cfg, &h, &ms, &rc, msg, sizeof msg);
  ck_assert_int_eq(rc, 0);
  ck_assert_ptr_nonnull(strstr(msg, "in 3 containers"));
  sink_close(&ms);
  unlink(xmltv_path);
  unlink(map_path);
}
END_TEST

typedef struct {
  const char *group;
  unsigned port;
  unsigned want; /* payload id */
  int found;
  dvbstp_header_t hdr;
  char publisher[BCG_ID_LEN];
  const bcg_schema_t *sch;
  unsigned char *payload; /* want DATA_CONTAINER: the first segment */
  size_t plen;
  mcast_t *m; /* joined before announcer starts, closed by thread */
} init_rx_t;

/* TVAMain container: publisher into rx. 1 ok, -1 not decodable */
static int tva_main_rx(init_rx_t *rx, const unsigned char *data, size_t len) {
  unsigned char *c;
  size_t clen;
  const unsigned char *au;
  const unsigned char *sb;
  size_t aul;
  size_t sl;
  bim_fragref_t *refs = NULL;
  unsigned fmt = 0;
  int nrefs = 0;
  int nfuu = 0;
  int rc = -1;
  if (bim_bcg_unwrap(data, len, BCG_INFLATE, &c, &clen)) return -1;
  if (!bim_container_parse(c, clen, &au, &aul, &sb, &sl) && sb && !bim_container_parse_refs(c, clen, &fmt, &refs, &nrefs)) {
    bitreader_t br;
    strrepo_reader_t sr;
    bcg_doc_t d;
    bcg_doc_init(&d);
    bitreader_init(&br, au, aul);
    if (!strrepo_reader_init(&sr, sb, sl) && !bcgau_decode_refs(&rx->sch->au, &br, &sr, refs, nrefs, &d, &nfuu)) {
      memcpy(rx->publisher, d.publisher, sizeof rx->publisher);
      rc = 1;
    }
    bcg_doc_free(&d);
  }
  free(refs);
  free(c);
  return rc;
}

/* index list container 0x0000, TS 102 323 9.2.2. 1 ok, -1 not */
static int index_list_rx(const unsigned char *data, size_t len) {
  unsigned char *c;
  size_t clen;
  const unsigned char *b;
  size_t blen;
  int rc;
  if (bim_bcg_unwrap(data, len, BCG_INFLATE, &c, &clen)) return -1;
  rc = !bim_container_find(c, clen, BIM_STRUCT_INDEX_LIST, BIM_STRUCT_ID_UNUSED, &b, &blen) && blen > 0 ? 1 : -1;
  free(c);
  return rc;
}

/* first segment of rx->want, found = 1 if decodable */
static void *init_rx_thread(void *arg) {
  init_rx_t *rx = arg;
  mcast_t *m = rx->m;
  dvbstp_reasm_t *r = dvbstp_reasm_new();
  for (int i = 0; m && r && i < 30 && !rx->found; i++) {
    unsigned char buf[2048];
    ssize_t n = mcast_recv(m, buf, sizeof buf, NULL);
    const unsigned char *data;
    size_t len;
    if (n > 0 && dvbstp_reasm_feed(r, buf, (size_t)n, &rx->hdr, &data, &len) && rx->hdr.payload_id == rx->want &&
        (rx->want != DVBSTP_PAYLOAD_BCG_INDEX_CONTAINER || rx->hdr.segment_id == BIM_INDEX_LIST_CONTAINER)) {
      if (rx->want == DVBSTP_PAYLOAD_BCG_TVAMAIN) {
        rx->found = tva_main_rx(rx, data, len);
      } else if (rx->want == DVBSTP_PAYLOAD_BCG_INDEX_CONTAINER) {
        rx->found = index_list_rx(data, len);
      } else if (rx->want == DVBSTP_PAYLOAD_BCG_DATA_CONTAINER) {
        rx->payload = malloc(len);
        if (rx->payload) memcpy(rx->payload, data, len);
        rx->plen = len;
        rx->found = rx->payload ? 1 : -1;
      } else {
        bim_tvainit_t ti;
        bim_decoderinit_t di;
        bim_err_t err;
        rx->found = !bim_bcg_tvainit_parse(data, len, &ti, &di, &err) ? 1 : -1;
        if (rx->found == 1) bim_decoderinit_free(&di);
      }
    }
  }
  dvbstp_reasm_free(r);
  if (m) mcast_close(m);
  return NULL;
}

/* TS 102 539 7.3 (TS 102 323 table 53), 7.4 TVAMain with publisher, index list container */
START_TEST(announce_run_sends_dvb_tva_init) {
  static const unsigned want[] = {DVBSTP_PAYLOAD_BCG_TVA_INIT, DVBSTP_PAYLOAD_BCG_TVAMAIN, DVBSTP_PAYLOAD_BCG_INDEX_CONTAINER};
  char xmltv_path[] = "/tmp/dvbipitools_test_bcg_xmltv_XXXXXX";
  char map_path[] = "/tmp/dvbipitools_test_bcg_map_XXXXXX";
  char text[GUIDE_BUF];
  char msg[16384];
  run_helper_t h;
  config_t cfg;
  sink_t ms;
  init_rx_t rx;
  bcg_schema_t sch;
  char dir[64];
  pthread_t th;
  int rc;

  bcg_fixture_make(dir);
  guide_text(&guide_one, text, sizeof text);
  write_temp_file(xmltv_path, text);
  map_text(&guide_one, text, sizeof text);
  write_temp_file(map_path, text);
  memset(&cfg, 0, sizeof cfg);
  cfg.input_path = xmltv_path;
  cfg.map_path = map_path;
  cfg.window_hours = 2;
  cfg.family = AF_INET;
  cfg.mcast_port = 3939;
  run_helper_group(cfg.mcast_group, sizeof cfg.mcast_group, 84);
  cfg.interval_s = 1;
  if (_i == 1) cfg.publisher = "example.com/My%20Guide";
  if (_i == 2) cfg.index_mask = (1u << BIM_INDEX_KINDS) - 1;
  cfg.schemas[0] = dir;
  cfg.nschemas = 1;
  ck_assert_int_eq(bcg_schema_open(&sch, &cfg, BCG_FIXTURE_NS, 1), 0);
  memset(&rx, 0, sizeof rx);
  rx.sch = &sch;
  rx.want = want[_i];
  rx.group = cfg.mcast_group;
  rx.port = cfg.mcast_port;
  rx.m = mcast_open(AF_INET, rx.group, rx.port, NULL, 100);
  ck_assert_ptr_nonnull(rx.m);
  ck_assert_int_eq(pthread_create(&th, NULL, init_rx_thread, &rx), 0);
  h.path = xmltv_path;
  h.rewrite = NULL;
  h.rewrite_ms = 0;
  h.stop_ms = 600;
  sink_open(&ms, METRICS_COMPONENT_BCG, "bcg1", 0.1);
  run_announce(&cfg, &h, &ms, &rc, msg, sizeof msg);
  pthread_join(th, NULL);
  ck_assert_int_eq(rc, 0);
  ck_assert_int_eq(rx.found, 1);
  if (_i == 1) ck_assert_str_eq(rx.publisher, "example.com/My%20Guide");
  ck_assert_uint_eq(rx.hdr.segment_id, _i == 2 ? BIM_INDEX_LIST_CONTAINER : 0);
  /* DVB-TVA-init versioned by content, first build 1 */
  if (_i == 0) ck_assert_uint_eq(rx.hdr.segment_version, 1);
  ck_assert_uint_eq(rx.hdr.compr, 1);
  bcg_schema_close(&sch);
  bcg_fixture_remove(dir);
  sink_close(&ms);
  unlink(xmltv_path);
  unlink(map_path);
}
END_TEST

#define TVA_INPUT \
  "<TVAMain xmlns=\"" BCG_FIXTURE_NS "\"><ProgramDescription><ProgramInformationTable>" \
  "<ProgramInformation programId=\"crid://example.com/a\"><BasicDescription><Title>Alpha</Title></BasicDescription></ProgramInformation>" \
  "</ProgramInformationTable><ServiceInformationTable><ServiceInformation serviceId=\"ch1\"><Name>One</Name><ServiceURL>dvb://1.2.65</ServiceURL>" \
  "</ServiceInformation></ServiceInformationTable></ProgramDescription></TVAMain>"

typedef struct {
  bcg_fmt_t format;
  bcg_enc_t encoding;
  const char *want; /* plain: in the XML, bim: in the decoded fragments' titles */
} input_case_t;

static const input_case_t input_cases[] = {
  {FMT_TVA, ENC_BIM, "Alpha"},
  {FMT_TVA, ENC_PLAIN, "programId=\"crid://example.com/a\""},
  {FMT_XMLTV, ENC_PLAIN, "<ServiceInformation serviceId=\"ch1\""},
  {FMT_BIM, ENC_PLAIN, "<Title>Alpha</Title>"},
  {FMT_BIM, ENC_BIM, "Alpha"},
};

/* -f tva|bim|xmltv, --encoding bim|plain: first data container content */
START_TEST(announce_run_sends_each_input_format_in_each_encoding) {
  const input_case_t *ic = &input_cases[_i];
  char xmltv_path[] = "/tmp/dvbipitools_test_bcg_xmltv_XXXXXX";
  char map_path[] = "/tmp/dvbipitools_test_bcg_map_XXXXXX";
  char text[GUIDE_BUF];
  char msg[16384];
  char dir[64];
  run_helper_t h;
  config_t cfg;
  sink_t ms;
  init_rx_t rx;
  pthread_t th;
  int rc;

  bcg_fixture_make(dir);
  memset(&cfg, 0, sizeof cfg);
  cfg.schemas[0] = dir;
  cfg.nschemas = 1;
  if (ic->format == FMT_XMLTV) {
    guide_text(&guide_one, text, sizeof text);
    write_temp_file(xmltv_path, text);
    map_text(&guide_one, text, sizeof text);
    write_temp_file(map_path, text);
    cfg.map_path = map_path;
  } else if (ic->format == FMT_TVA) {
    write_temp_file(xmltv_path, TVA_INPUT);
  } else {
    bcg_schema_t sch;
    dvbtva_roots_t r;
    xml_doc_t *x = NULL;
    unsigned char *bim = NULL;
    size_t len = 0;
    size_t sizes[2];
    bim_err_t err;
    FILE *f;
    ck_assert_int_eq(bcg_schema_open(&sch, &cfg, BCG_FIXTURE_NS, 1), 0);
    ck_assert_int_eq(xml_doc_parse(TVA_INPUT, strlen(TVA_INPUT), &x, NULL), 0);
    r = (dvbtva_roots_t){sch.au.c, &sch.au.ns};
    ck_assert_int_eq(dvbstream_write(&r, &sch.au.eo, xml_doc_root(x), 0, &bim, &len, NULL, sizes, &err), DVBSTREAM_OK);
    ck_assert_int_ge(mkstemp(xmltv_path), 0);
    f = fopen(xmltv_path, "wb");
    ck_assert_ptr_nonnull(f);
    ck_assert_uint_eq(fwrite(bim, 1, len, f), len);
    fclose(f);
    free(bim);
    xml_doc_free(x);
    bcg_schema_close(&sch);
  }
  cfg.input_path = xmltv_path;
  cfg.format = ic->format;
  cfg.encoding = ic->encoding;
  cfg.window_hours = 2;
  cfg.family = AF_INET;
  cfg.mcast_port = 3940;
  run_helper_group(cfg.mcast_group, sizeof cfg.mcast_group, 85);
  cfg.interval_s = 1;
  memset(&rx, 0, sizeof rx);
  rx.want = DVBSTP_PAYLOAD_BCG_DATA_CONTAINER;
  rx.group = cfg.mcast_group;
  rx.port = cfg.mcast_port;
  rx.m = mcast_open(AF_INET, rx.group, rx.port, NULL, 100);
  ck_assert_ptr_nonnull(rx.m);
  ck_assert_int_eq(pthread_create(&th, NULL, init_rx_thread, &rx), 0);
  h.path = xmltv_path;
  h.rewrite = NULL;
  h.rewrite_ms = 0;
  h.stop_ms = 600;
  sink_open(&ms, METRICS_COMPONENT_BCG, "bcg1", 0.1);
  run_announce(&cfg, &h, &ms, &rc, msg, sizeof msg);
  pthread_join(th, NULL);
  ck_assert_int_eq(rc, 0);
  ck_assert_int_eq(rx.found, 1);
  ck_assert_uint_eq(rx.hdr.compr, ic->encoding == ENC_PLAIN ? 0 : 1);
  if (ic->encoding == ENC_PLAIN) {
    ck_assert_ptr_nonnull(memmem(rx.payload, rx.plen, ic->want, strlen(ic->want)));
  } else {
    unsigned char *c;
    size_t cl;
    const unsigned char *au;
    const unsigned char *sb;
    size_t aul;
    size_t sl;
    bim_fragref_t *refs = NULL;
    unsigned fmt;
    int nrefs;
    bcg_schema_t sch;
    bitreader_t br;
    strrepo_reader_t sr;
    bcgau_parsed_t p;
    ck_assert_int_eq(bcg_schema_open(&sch, &cfg, BCG_FIXTURE_NS, 1), 0);
    ck_assert_int_eq(bim_bcg_unwrap(rx.payload, rx.plen, BCG_INFLATE, &c, &cl), 0);
    ck_assert_int_eq(bim_container_parse(c, cl, &au, &aul, &sb, &sl), 0);
    ck_assert_int_eq(bim_container_parse_refs(c, cl, &fmt, &refs, &nrefs), 0);
    bitreader_init(&br, au, aul);
    ck_assert_int_eq(strrepo_reader_init(&sr, sb, sl), 0);
    ck_assert_int_eq(bcgau_parse(&sch.au, &br, &sr, refs, nrefs, &p), 0);
    ck_assert_uint_ge(p.n, 2);
    ck_assert_ptr_nonnull(strstr(p.f[0].root->kids[0]->kids[0]->text, ic->want));
    bcgau_parsed_free(&p);
    bcg_schema_close(&sch);
    free(refs);
    free(c);
  }
  free(rx.payload);
  bcg_fixture_remove(dir);
  sink_close(&ms);
  unlink(xmltv_path);
  if (ic->format == FMT_XMLTV) unlink(map_path);
}
END_TEST

static const char ext[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" targetNamespace=\"urn:test:ext\"><xs:element name=\"Extra\" type=\"xs:string\"/></xs:schema>";

/* TS 102 323 9.4.4: extra DecoderInit schemas found in schema paths, unknown skipped */
START_TEST(schema_extras_find_known_schemas_and_skip_unknown_ones) {
  const char *uris[3] = {BCG_FIXTURE_NS, "urn:test:ext", "urn:test:none"};
  config_t cfg;
  bcg_schema_t sch;
  char dir[64];
  char path[300];
  bcg_fixture_make(dir);
  bcg_fixture_put(dir, "ext.xsd", ext);
  memset(&cfg, 0, sizeof cfg);
  cfg.schemas[0] = dir;
  cfg.nschemas = 1;
  ck_assert_int_eq(bcg_schema_open(&sch, &cfg, BCG_FIXTURE_NS, 1), 0);
  ck_assert_int_eq(bcg_schema_extras(&sch, &cfg, uris, 1, 1), 0);
  ck_assert_int_eq(sch.au.dop.nschemas, 1);
  ck_assert_int_eq(bcg_schema_extras(&sch, &cfg, uris, DVBTVA_SCHEMAS + 1, 1), -1);
  ck_assert_int_eq(bcg_schema_extras(&sch, &cfg, uris, 3, 1), 0);
  ck_assert_int_eq(sch.au.dop.nschemas, 3);
  ck_assert_ptr_eq(sch.au.dop.schemas, sch.tab.v);
  ck_assert_int_eq(sch.tab.v[0].known, 1);
  ck_assert_int_eq(sch.tab.v[1].known, 1);
  ck_assert_int_eq(sch.tab.v[2].known, 0);
  bcg_schema_close(&sch);
  snprintf(path, sizeof path, "%s/ext.xsd", dir);
  unlink(path);
  bcg_fixture_remove(dir);
}
END_TEST

/* used schemas listed in DVB-TVA-init, become chunk schemas */
START_TEST(schema_encode_lists_the_schemas_the_document_uses) {
  static const char one[] = "<R xmlns=\"" BCG_FIXTURE_NS "\" xmlns:x=\"urn:test:ext\"><x:Extra/></R>";
  static const char none[] = "<R xmlns=\"" BCG_FIXTURE_NS "\" xmlns:y=\"urn:test:none\"><y:Other/></R>";
  config_t cfg;
  bcg_schema_t sch;
  xml_doc_t *doc = NULL;
  char dir[64];
  char path[300];
  bcg_fixture_make(dir);
  bcg_fixture_put(dir, "ext.xsd", ext);
  memset(&cfg, 0, sizeof cfg);
  cfg.schemas[0] = dir;
  cfg.nschemas = 1;
  ck_assert_int_eq(bcg_schema_open(&sch, &cfg, BCG_FIXTURE_NS, 1), 0);
  ck_assert_int_eq(xml_doc_parse(one, strlen(one), &doc, NULL), 0);
  ck_assert_int_eq(bcg_schema_encode(&sch, &cfg, xml_doc_root(doc), 1), 0);
  ck_assert_int_eq(sch.au.eo.nschemas, 2);
  ck_assert_int_eq(sch.au.eo.multi, 1);
  ck_assert_ptr_eq(sch.au.eo.schemas, sch.etab.v);
  ck_assert_ptr_nonnull(memmem(sch.init, sch.init_len, "urn:test:ext", 12));
  ck_assert_uint_eq(sch.au.eo.nschema_uris, 1);
  ck_assert_str_eq(sch.au.eo.schema_uris[0], "urn:test:ext");
  xml_doc_free(doc);
  ck_assert_int_eq(xml_doc_parse(none, strlen(none), &doc, NULL), 0);
  ck_assert_int_eq(bcg_schema_encode(&sch, &cfg, xml_doc_root(doc), 1), 0);
  ck_assert_int_eq(sch.au.eo.nschemas, 1);
  ck_assert_int_eq(sch.au.eo.multi, 0);
  ck_assert_ptr_null(memmem(sch.init, sch.init_len, "urn:test:ext", 12));
  ck_assert_ptr_null(memmem(sch.init, sch.init_len, "urn:test:none", 13));
  ck_assert_uint_eq(sch.au.eo.nschema_uris, 0);
  xml_doc_free(doc);
  bcg_schema_close(&sch);
  snprintf(path, sizeof path, "%s/ext.xsd", dir);
  unlink(path);
  bcg_fixture_remove(dir);
}
END_TEST

START_TEST(schema_open_encode_and_cast_report_failures) {
  config_t cfg;
  bcg_schema_t sch;
  xml_doc_t *doc = NULL;
  bim_node_t *root;
  char dir[64];
  char empty[] = "/tmp/dvbipitools_test_bcg_emptyschXXXXXX";
  char doc_text[2400];
  char *w;
  bcg_fixture_make(dir);
  memset(&cfg, 0, sizeof cfg);
  cfg.schemas[0] = dir;
  cfg.nschemas = 1;
  ck_assert_int_eq(bcg_schema_open(&sch, &cfg, BCG_FIXTURE_NS, 1), 0);

  root = bim_node_new("R");
  root->ns = BCG_FIXTURE_NS;
  ck_assert_int_eq(bim_node_add_attr_ns(root, XSI_NS, "type", strdup("p:Nope")), 0);
  root->nsdecls = calloc(1, sizeof *root->nsdecls);
  ck_assert_ptr_nonnull(root->nsdecls);
  root->nsdecls[0].prefix = "p";
  root->nsdecls[0].uri = BCG_FIXTURE_NS;
  root->nnsdecls = 1;
  ck_assert_int_eq(bcg_cast_bind(&sch, root, 1), -1);
  ck_assert_int_eq(bcg_cast_bind(&sch, root, 0), -1);
  bim_node_free(root);

  w = doc_text;
  w += snprintf(w, 100, "<R xmlns=\"%s\"", BCG_FIXTURE_NS);
  for (int i = 0; i < DVBTVA_SCHEMAS + 2; i++) w += snprintf(w, 80, " xmlns:n%d=\"urn:many:%d\"", i, i);
  w += snprintf(w, 20, ">");
  for (int i = 0; i < DVBTVA_SCHEMAS + 2; i++) w += snprintf(w, 40, "<n%d:E/>", i);
  snprintf(w, 20, "</R>");
  ck_assert_int_eq(xml_doc_parse(doc_text, strlen(doc_text), &doc, NULL), 0);
  ck_assert_int_eq(bcg_schema_encode(&sch, &cfg, xml_doc_root(doc), 1), -1);
  ck_assert_int_eq(bcg_schema_encode(&sch, &cfg, xml_doc_root(doc), 0), -1);
  xml_doc_free(doc);
  bcg_schema_close(&sch);

  ck_assert_ptr_nonnull(mkdtemp(empty));
  cfg.schemas[0] = empty;
  ck_assert_int_eq(bcg_schema_open(&sch, &cfg, BCG_FIXTURE_NS, 1), -1);
  ck_assert_int_eq(bcg_schema_open(&sch, &cfg, BCG_FIXTURE_NS, 0), -1);
  rmdir(empty);

  cfg.schemas[0] = dir;
  for (long n = 0;; n++) {
    int hit;
    int rc;
    alloc_fail_at(n);
    rc = bcg_schema_open(&sch, &cfg, BCG_FIXTURE_NS, 1);
    hit = alloc_fail_end();
    if (!rc) bcg_schema_close(&sch);
    if (!hit) break;
  }
  bcg_fixture_remove(dir);
}
END_TEST

/* TS 102 822-3-2 4.6.1.1: ids persistent per fragment, version only on change, entries ascending */
START_TEST(frag_assign_keeps_ids_and_bumps_changed_versions) {
  unsigned char au[] = {1, 2, 3, 4, 5, 6};
  static const unsigned char strs[] = {'x', 'y', 'z'};
  bcg_announce_ctx_t ctx;
  bcg_doc_t doc;
  bcg_channel_t *c;
  bcg_programme_t *pr;
  bcgau_ref_t refs[3];
  bcgau_ref_t dup[2];
  uint32_t ids[3];

  memset(&ctx, 0, sizeof ctx);
  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "a");
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "b");
  pr = bcg_add_programme(&doc);
  bufcpy(pr->channel_id, sizeof pr->channel_id, "a");
  bufcpy(pr->start, sizeof pr->start, "2030-01-01T12:00:00Z");
  for (int build = 0; build < 3; build++) {
    const bcgau_ref_t in[3] = {
      {.au_off = 0, .str_off = 0, .kind = BCGAU_SERVICE, .index = 1},
      {.au_off = 2, .str_off = 1, .kind = BCGAU_PROGRAM, .index = 0},
      {.au_off = 4, .str_off = 2, .kind = BCGAU_SCHEDULE, .index = 0},
    };
    if (build == 2) au[2] = 9; /* programme fragment changes */
    memcpy(refs, in, sizeof refs);
    ctx.frag_gen++;
    ck_assert_int_eq(frag_assign(&ctx, &doc, au, sizeof au, strs, sizeof strs, refs, 3), 0);
    frag_prune(&ctx);
    ck_assert_uint_lt(refs[0].frag_id, refs[1].frag_id);
    ck_assert_uint_lt(refs[1].frag_id, refs[2].frag_id);
    for (int i = 0; i < 3; i++) {
      if (!build) ids[i] = refs[i].frag_id;
      ck_assert_uint_eq(refs[i].frag_id, ids[i]);
      ck_assert_uint_eq(refs[i].version, build == 2 && refs[i].kind == BCGAU_PROGRAM ? 2 : 1);
    }
  }
  ck_assert_int_eq(ctx.nfrags, 3);
  dup[0] = (bcgau_ref_t){.au_off = 0, .str_off = 0, .kind = BCGAU_SERVICE, .index = 1};
  dup[1] = (bcgau_ref_t){.au_off = 3, .str_off = 1, .kind = BCGAU_SERVICE, .index = 1};
  ctx.frag_gen++;
  ck_assert_int_eq(frag_assign(&ctx, &doc, au, sizeof au, strs, sizeof strs, dup, 2), 0);
  frag_prune(&ctx);
  ck_assert_uint_ne(dup[0].frag_id, dup[1].frag_id);
  ck_assert_int_eq(ctx.nfrags, 2); /* programme and schedule pruned, duplicate added */
  frag_free(&ctx);
  bcg_doc_free(&doc);
}
END_TEST

/* TS 102 822-3-2 4.6.1.1: fragment_version increments modulo 255 */
START_TEST(frag_assign_wraps_version_modulo_255) {
  unsigned char au[] = {0, 0};
  static const unsigned char strs[] = {'x'};
  bcg_announce_ctx_t ctx;
  bcg_doc_t doc;
  bcg_channel_t *c;
  bcgau_ref_t refs[1];
  memset(&ctx, 0, sizeof ctx);
  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "a");
  for (unsigned k = 1; k <= 256; k++) {
    au[0] = (unsigned char)k;
    au[1] = (unsigned char)(k >> 8);
    refs[0] = (bcgau_ref_t){.au_off = 0, .str_off = 0, .kind = BCGAU_SERVICE, .index = 0};
    ctx.frag_gen++;
    ck_assert_int_eq(frag_assign(&ctx, &doc, au, sizeof au, strs, sizeof strs, refs, 1), 0);
    ck_assert_uint_eq(refs[0].version, k % 255);
  }
  frag_free(&ctx);
  bcg_doc_free(&doc);
}
END_TEST

START_TEST(frag_assign_reports_exhaustion_truncation_and_oom) {
  unsigned char au[] = {1, 2, 3, 4};
  static const unsigned char strs[] = {'x', 'y'};
  bcg_announce_ctx_t ctx;
  bcg_doc_t doc;
  bcg_channel_t *c;
  bcgau_ref_t refs[1];
  dvbtva_fuu_t fuu;
  bim_node_t *root;
  int rc;
  memset(&ctx, 0, sizeof ctx);
  bcg_doc_init(&doc);
  c = bcg_add_channel(&doc);
  bufcpy(c->id, sizeof c->id, "short");
  refs[0] = (bcgau_ref_t){.au_off = 0, .str_off = 0, .kind = BCGAU_SERVICE, .index = 0};
  ctx.frag_gen = 1;
  ctx.frag_next = 0xFFFFFFu;
  ck_assert_int_eq(frag_assign(&ctx, &doc, au, sizeof au, strs, sizeof strs, refs, 1), -1);
  ctx.frag_next = 0;
  for (long n = 0;; n++) {
    int hit;
    memset(&ctx, 0, sizeof ctx);
    ctx.frag_gen = 1;
    refs[0] = (bcgau_ref_t){.au_off = 0, .str_off = 0, .kind = BCGAU_SERVICE, .index = 0};
    alloc_fail_at(n);
    rc = frag_assign(&ctx, &doc, au, sizeof au, strs, sizeof strs, refs, 1);
    hit = alloc_fail_end();
    (void)rc;
    frag_free(&ctx);
    if (!hit) break;
  }
  root = bim_node_new("R");
  ck_assert_int_eq(bim_node_add_attr_ns(root, NULL, "a", strdup("v")), 0);
  fuu = (dvbtva_fuu_t){0x0001, root};
  {
    char big[TVA_CRID_BUF + 16];
    char tiny[4];
    ck_assert_int_eq(key_node(&fuu, 0, big, sizeof big), 0);
    ck_assert_str_eq(big, "T:1:v");
    ck_assert_int_eq(key_node(&fuu, 0, tiny, sizeof tiny), 1);
  }
  bim_node_free(root);
  bcg_doc_free(&doc);
}
END_TEST

START_TEST(programme_times_and_views_survive_allocation_failures) {
  bcg_doc_t doc;
  bcg_doc_t view;
  prog_time_t *times = NULL;
  int sel[2] = {0, 1};
  int rc;
  bcg_programme_t *p;
  bcg_doc_init(&doc);
  for (int i = 0; i < 3; i++) {
    p = bcg_add_programme(&doc);
    bufcpy(p->channel_id, sizeof p->channel_id, i == 2 ? "b" : "a");
    bufcpy(p->start, sizeof p->start, i == 1 ? "garbage" : "2030-01-01T12:00:00Z");
  }
  for (int k = 0; k < 2; k++) {
    alloc_fail_at(k);
    rc = prog_times_build(&doc, &times);
    alloc_fail_end();
    ck_assert_int_eq(rc, -1);
    ck_assert_ptr_null(times);
  }
  ck_assert_int_eq(prog_times_build(&doc, &times), 0);
  alloc_fail_at(0);
  rc = window_view(&doc, times, sel, 2, &view);
  alloc_fail_end();
  ck_assert_int_eq(rc, -1);
  ck_assert_int_eq(window_view(&doc, times, sel, 0, &view), 0);
  window_view_free(&view);
  free(times);
  bcg_doc_free(&doc);
}
END_TEST

START_TEST(version_track_survives_allocation_failure) {
  static const unsigned char a[] = {1, 2, 3};
  bcg_version_t v;
  memset(&v, 0, sizeof v);
  alloc_fail_at(0);
  version_track(&v, a, sizeof a);
  alloc_fail_end();
  ck_assert_uint_eq(v.version, 0u);
  version_track(&v, a, 0);
  ck_assert_uint_eq(v.version, 1u);
  version_track(&v, a, sizeof a);
  ck_assert_uint_eq(v.version, 2u);
  free(v.last);
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
  ck_assert_int_eq(v.unsent, 1);
  v.unsent = 0;
  version_track(&v, a, sizeof a);
  ck_assert_uint_eq(v.version, 1u);
  ck_assert_int_eq(v.unsent, 0);
  version_track(&v, b, sizeof b);
  ck_assert_int_eq(v.unsent, 1);
  ck_assert_uint_eq(v.version, 2u);
  version_track(&v, c, sizeof c);
  ck_assert_uint_eq(v.version, 3u);
  version_track(&v, c, sizeof c);
  ck_assert_uint_eq(v.version, 3u);
  free(v.last);
}
END_TEST

START_TEST(version_track_wraps_from_255_to_0) {
  static const unsigned char a[] = {1};
  static const unsigned char b[] = {2};
  bcg_version_t v;

  memset(&v, 0, sizeof v);
  v.version = 255;
  version_track(&v, a, sizeof a);
  ck_assert_uint_eq(v.version, 0u);
  version_track(&v, b, sizeof b);
  ck_assert_uint_eq(v.version, 1u);
  free(v.last);
}
END_TEST

ssize_t __real_sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen);
ssize_t __real_sendmsg(int fd, const struct msghdr *msg, int flags);
static int g_send_fail;

ssize_t __wrap_sendto(int fd, const void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen) {
  if (g_send_fail && is_group(to)) {
    errno = ENETUNREACH;
    return -1;
  }
  return __real_sendto(fd, buf, len, flags, to, tolen);
}

ssize_t __wrap_sendmsg(int fd, const struct msghdr *msg, int flags) {
  if (g_send_fail && is_group((const struct sockaddr *)msg->msg_name)) {
    errno = ENETUNREACH;
    return -1;
  }
  return __real_sendmsg(fd, msg, flags);
}

static void tva_cfg(config_t *cfg, const char *dir, const char *path, bcg_enc_t enc) {
  memset(cfg, 0, sizeof *cfg);
  cfg->schemas[0] = dir;
  cfg->nschemas = 1;
  cfg->input_path = path;
  cfg->format = FMT_TVA;
  cfg->encoding = enc;
  cfg->window_hours = 2;
  cfg->family = AF_INET;
  cfg->mcast_port = 3941;
  run_helper_group(cfg->mcast_group, sizeof cfg->mcast_group, 86);
  cfg->interval_s = 1;
}

START_TEST(announce_run_reloads_fragment_input_or_keeps_the_previous_one) {
  static const char *const rewrites[2] = {TVA_INPUT, "<not tva"};
  char path[] = "/tmp/dvbipitools_test_bcg_tvaload_XXXXXX";
  char dir[64];
  char msg[16384];
  run_helper_t h;
  config_t cfg;
  sink_t ms;
  int rc;
  bcg_fixture_make(dir);
  write_temp_file(path, TVA_INPUT);
  tva_cfg(&cfg, dir, path, ENC_BIM);
  h.path = path;
  h.rewrite = rewrites[_i];
  h.rewrite_ms = 300;
  h.stop_ms = 1700;
  sink_open(&ms, METRICS_COMPONENT_BCG, "bcg1", 0.1);
  run_announce(&cfg, &h, &ms, &rc, msg, sizeof msg);
  ck_assert_int_eq(rc, 0);
  ck_assert_ptr_nonnull(strstr(msg, _i ? "reload failed, keeping previous guide" : "reloaded"));
  sink_close(&ms);
  bcg_fixture_remove(dir);
  unlink(path);
}
END_TEST

START_TEST(announce_run_refuses_to_start_without_a_usable_schema) {
  char xmltv_path[] = "/tmp/dvbipitools_test_bcg_xmltv_XXXXXX";
  char map_path[] = "/tmp/dvbipitools_test_bcg_map_XXXXXX";
  char empty[] = "/tmp/dvbipitools_test_bcg_emptyXXXXXX";
  char text[GUIDE_BUF];
  char msg[16384];
  run_helper_t h;
  config_t cfg;
  sink_t ms;
  int rc;
  ck_assert_ptr_nonnull(mkdtemp(empty));
  guide_text(&guide_one, text, sizeof text);
  write_temp_file(xmltv_path, text);
  map_text(&guide_one, text, sizeof text);
  write_temp_file(map_path, text);
  memset(&cfg, 0, sizeof cfg);
  cfg.schemas[0] = empty;
  cfg.nschemas = 1;
  cfg.input_path = xmltv_path;
  cfg.map_path = map_path;
  cfg.format = FMT_XMLTV;
  cfg.window_hours = 2;
  cfg.family = AF_INET;
  cfg.mcast_port = 3942;
  run_helper_group(cfg.mcast_group, sizeof cfg.mcast_group, 87);
  cfg.interval_s = 1;
  h.path = xmltv_path;
  h.rewrite = NULL;
  h.rewrite_ms = 0;
  h.stop_ms = 300;
  sink_open(&ms, METRICS_COMPONENT_BCG, "bcg1", 0.1);
  run_announce(&cfg, &h, &ms, &rc, msg, sizeof msg);
  ck_assert_int_eq(rc, 1);
  sink_close(&ms);
  rmdir(empty);
  unlink(xmltv_path);
  unlink(map_path);
}
END_TEST

static void write_bin(char *path, const unsigned char *p, size_t n) {
  int fd = mkstemp(path);
  FILE *f;
  if (fd < 0) abort();
  f = fdopen(fd, "wb");
  if (!f) abort();
  ck_assert_uint_eq(fwrite(p, 1, n, f), n);
  fclose(f);
}

static void ctx_free(bcg_announce_ctx_t *c) {
  dvbtva_au_free(c->tfrag, (size_t)c->ntfrag);
  if (c->sch && c->sch->set) bcg_schema_close(c->sch);
  free(c->sch);
  memset(c, 0, sizeof *c);
}

/* as reload: namespace of loaded schema required, result replaces it */
static int ctx_load(bcg_announce_ctx_t *c) {
  bcg_schema_t *s;
  dvbtva_fuu_t *f;
  int n;
  if (tvadoc_load(c->cfg, 1, c->sch ? c->sch->ns : NULL, &s, &f, &n)) return -1;
  dvbtva_au_free(c->tfrag, (size_t)c->ntfrag);
  if (c->sch) bcg_schema_close(c->sch);
  free(c->sch);
  c->sch = s;
  c->tfrag = f;
  c->ntfrag = n;
  return 0;
}

static unsigned char *bim_stream(const config_t *cfg, const char *xml, size_t *len) {
  bcg_schema_t sch;
  dvbtva_roots_t r;
  xml_doc_t *x = NULL;
  unsigned char *bim = NULL;
  size_t sizes[2];
  bim_err_t err;
  ck_assert_int_eq(bcg_schema_open(&sch, cfg, BCG_FIXTURE_NS, 1), 0);
  ck_assert_int_eq(xml_doc_parse(xml, strlen(xml), &x, NULL), 0);
  r = (dvbtva_roots_t){sch.au.c, &sch.au.ns};
  ck_assert_int_eq(dvbstream_write(&r, &sch.au.eo, xml_doc_root(x), 0, &bim, len, NULL, sizes, &err), DVBSTREAM_OK);
  xml_doc_free(x);
  bcg_schema_close(&sch);
  return bim;
}

static int load_file(bcg_announce_ctx_t *ctx, config_t *cfg, bcg_fmt_t fmt, const char *path) {
  cfg->format = fmt;
  cfg->input_path = path;
  return ctx_load(ctx);
}

START_TEST(tvadoc_load_reports_each_input_failure) {
  char dir[64];
  char good[] = "/tmp/dvbipitools_test_bcg_tdgood_XXXXXX";
  char bad_xml[] = "/tmp/dvbipitools_test_bcg_tdbad_XXXXXX";
  char no_ns[] = "/tmp/dvbipitools_test_bcg_tdnons_XXXXXX";
  char other_ns[] = "/tmp/dvbipitools_test_bcg_tdother_XXXXXX";
  char foreign_root[] = "/tmp/dvbipitools_test_bcg_tdroot_XXXXXX";
  char rule_bad[] = "/tmp/dvbipitools_test_bcg_tdrule_XXXXXX";
  char garbage[] = "/tmp/dvbipitools_test_bcg_tdgarb_XXXXXX";
  char stream[] = "/tmp/dvbipitools_test_bcg_tdbim_XXXXXX";
  char patched[] = "/tmp/dvbipitools_test_bcg_tdpatch_XXXXXX";
  char long_ns_path[] = "/tmp/dvbipitools_test_bcg_tdlong_XXXXXX";
  unsigned char junk[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
  char long_doc[700];
  unsigned char *bim;
  unsigned char *pat;
  unsigned char *at;
  size_t len;
  config_t cfg;
  bcg_announce_ctx_t ctx;
  bcg_fixture_make(dir);
  memset(&cfg, 0, sizeof cfg);
  cfg.schemas[0] = dir;
  cfg.nschemas = 1;
  cfg.window_hours = 2;
  write_temp_file(good, TVA_INPUT);
  write_temp_file(bad_xml, "<not tva");
  write_temp_file(no_ns, "<TVAMain/>");
  write_temp_file(other_ns, "<TVAMain xmlns=\"urn:other:ns\"/>");
  write_temp_file(foreign_root, "<Foreign xmlns=\"" BCG_FIXTURE_NS "\"/>");
  write_temp_file(rule_bad, "<TVAMain xmlns=\"" BCG_FIXTURE_NS "\"><ProgramDescription><ServiceInformationTable><ServiceInformation serviceId=\"ch1\">"
                            "<Name>One</Name><ServiceURL>rtp://239.1.1.1:5000</ServiceURL></ServiceInformation></ServiceInformationTable></ProgramDescription></TVAMain>");
  write_bin(garbage, junk, sizeof junk);
  snprintf(long_doc, sizeof long_doc, "<TVAMain xmlns=\"urn:");
  memset(long_doc + strlen(long_doc), 'x', 400);
  strcpy(long_doc + strlen(long_doc), "\"/>");
  write_temp_file(long_ns_path, long_doc);
  bim = bim_stream(&cfg, TVA_INPUT, &len);
  write_bin(stream, bim, len);
  pat = malloc(len);
  ck_assert_ptr_nonnull(pat);
  memcpy(pat, bim, len);
  at = memmem(pat, len, BCG_FIXTURE_NS, strlen(BCG_FIXTURE_NS));
  ck_assert_ptr_nonnull(at);
  at[strlen(BCG_FIXTURE_NS) - 1] ^= 1;
  write_bin(patched, pat, len);

  memset(&ctx, 0, sizeof ctx);
  ctx.cfg = &cfg;
  ck_assert_int_eq(load_file(&ctx, &cfg, FMT_TVA, "/nonexistent/in.xml"), -1);
  ck_assert_int_eq(load_file(&ctx, &cfg, FMT_TVA, bad_xml), -1);
  ck_assert_int_eq(load_file(&ctx, &cfg, FMT_TVA, no_ns), -1);
  ck_assert_int_eq(load_file(&ctx, &cfg, FMT_TVA, long_ns_path), -1);
  ck_assert_int_eq(load_file(&ctx, &cfg, FMT_TVA, good), 0);
  ck_assert_int_gt(ctx.ntfrag, 0);
  ck_assert_int_eq(load_file(&ctx, &cfg, FMT_TVA, other_ns), -1);
  ck_assert_int_eq(load_file(&ctx, &cfg, FMT_TVA, foreign_root), -1);
  /* TS 102 323 8.6: ServiceURL a DVB locator */
  ck_assert_int_eq(load_file(&ctx, &cfg, FMT_TVA, rule_bad), -1);
  ck_assert_int_eq(load_file(&ctx, &cfg, FMT_BIM, garbage), -1);
  ck_assert_int_eq(load_file(&ctx, &cfg, FMT_BIM, stream), 0);
  ck_assert_int_eq(load_file(&ctx, &cfg, FMT_BIM, patched), -1);
  ctx_free(&ctx);

  memset(&ctx, 0, sizeof ctx);
  ctx.cfg = &cfg;
  ck_assert_int_eq(load_file(&ctx, &cfg, FMT_BIM, patched), -1);
  ck_assert_int_eq(load_file(&ctx, &cfg, FMT_BIM, stream), 0);
  ctx_free(&ctx);

  for (long n = 0;; n++) {
    int hit;
    memset(&ctx, 0, sizeof ctx);
    ctx.cfg = &cfg;
    cfg.format = FMT_TVA;
    cfg.input_path = good;
    alloc_fail_at(n);
    (void)ctx_load(&ctx);
    hit = alloc_fail_end();
    ctx_free(&ctx);
    if (!hit) break;
  }
  for (long n = 0;; n++) {
    int hit;
    memset(&ctx, 0, sizeof ctx);
    ctx.cfg = &cfg;
    ck_assert_int_eq(load_file(&ctx, &cfg, FMT_TVA, good), 0);
    alloc_fail_at(n);
    (void)ctx_load(&ctx);
    hit = alloc_fail_end();
    ctx_free(&ctx);
    if (!hit) break;
  }
  for (long n = 0;; n++) {
    int hit;
    memset(&ctx, 0, sizeof ctx);
    ctx.cfg = &cfg;
    cfg.format = FMT_BIM;
    cfg.input_path = stream;
    alloc_fail_at(n);
    (void)ctx_load(&ctx);
    hit = alloc_fail_end();
    ctx_free(&ctx);
    if (!hit) break;
  }
  free(pat);
  free(bim);
  bcg_fixture_remove(dir);
  unlink(good);
  unlink(bad_xml);
  unlink(no_ns);
  unlink(other_ns);
  unlink(foreign_root);
  unlink(rule_bad);
  unlink(garbage);
  unlink(stream);
  unlink(patched);
  unlink(long_ns_path);
}
END_TEST

START_TEST(announce_run_counts_failed_publications) {
  char path[] = "/tmp/dvbipitools_test_bcg_tvafail_XXXXXX";
  char dir[64];
  char msg[16384];
  run_helper_t h;
  config_t cfg;
  sink_t ms;
  seen_t seen;
  uint64_t v = 0;
  int rc;
  bcg_fixture_make(dir);
  write_temp_file(path, TVA_INPUT);
  tva_cfg(&cfg, dir, path, ENC_BIM);
  cfg.publisher = "example.com/Guide";
  cfg.index_mask = (1u << BIM_INDEX_KINDS) - 1;
  h.path = path;
  h.rewrite = NULL;
  h.rewrite_ms = 0;
  h.stop_ms = 400;
  sink_open(&ms, METRICS_COMPONENT_BCG, "bcg1", 0.1);
  g_send_fail = 1;
  run_announce(&cfg, &h, &ms, &rc, msg, sizeof msg);
  g_send_fail = 0;
  (void)rc;
  ck_assert_int_eq(sink_read(&ms, &seen), 1);
  ck_assert_int_eq(seen_last(&seen, METRICS_ID_BCG_PUBLICATION_ERRORS_TOTAL, &v), 1);
  ck_assert_uint_ge(v, 1u);
  sink_close(&ms);
  bcg_fixture_remove(dir);
  unlink(path);
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
  tcase_add_test(tc, window_view_copies_all_channels);
  tcase_add_test(tc, window_view_filters_programmes_by_window);
  tcase_add_test(tc, window_view_huge_window_does_not_overflow);
  tcase_add_test(tc, window_view_includes_in_range_and_no_stop_programmes);
  tcase_add_test(tc, window_view_replaces_invalid_stop_by_next_start);
  tcase_add_test(tc, window_view_ends_stopless_programme_at_next_start);
  tcase_add_test(tc, window_select_reports_schedule_bounds_and_indices);
  tcase_add_test(tc, load_doc_applies_mapping_to_matching_channel);
  tcase_add_test(tc, load_doc_drops_duplicate_channel_ids);
  tcase_add_test(tc, load_doc_rejects_missing_input);
  tcase_add_test(tc, load_doc_rejects_missing_map);
  tcase_add_loop_test(tc, announce_run_reloads_the_guide_on_sighup_or_keeps_the_previous_one, 0, (int)(sizeof reload_cases / sizeof reload_cases[0]));
  tcase_add_test(tc, announce_run_keeps_previous_guide_when_reload_fails_to_build);
  tcase_add_test(tc, announce_run_counts_services_with_events_across_unsorted_channels);
  tcase_add_test(tc, announce_run_sends_one_container_per_channel);
  tcase_add_loop_test(tc, announce_run_sends_dvb_tva_init, 0, 3);
  tcase_add_loop_test(tc, announce_run_sends_each_input_format_in_each_encoding, 0, (int)(sizeof input_cases / sizeof input_cases[0]));
  tcase_add_test(tc, schema_extras_find_known_schemas_and_skip_unknown_ones);
  tcase_add_test(tc, schema_encode_lists_the_schemas_the_document_uses);
  tcase_add_test(tc, frag_assign_keeps_ids_and_bumps_changed_versions);
  tcase_add_loop_test(tc, announce_run_reloads_fragment_input_or_keeps_the_previous_one, 0, 2);
  tcase_add_test(tc, announce_run_refuses_to_start_without_a_usable_schema);
  tcase_add_test(tc, announce_run_counts_failed_publications);
  tcase_add_test(tc, tvadoc_load_reports_each_input_failure);
  tcase_add_test(tc, schema_open_encode_and_cast_report_failures);
  tcase_add_test(tc, version_track_bumps_only_when_content_changes);
  tcase_add_test(tc, frag_assign_wraps_version_modulo_255);
  tcase_add_test(tc, frag_assign_reports_exhaustion_truncation_and_oom);
  tcase_add_test(tc, version_track_survives_allocation_failure);
  tcase_add_test(tc, programme_times_and_views_survive_allocation_failures);
  tcase_add_test(tc, version_track_wraps_from_255_to_0);
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
