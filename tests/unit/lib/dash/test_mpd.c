/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/dash/mpd.h"
#include "lib/sys/ioutil.h"

static http_url_t make_base(const char *host, unsigned port, const char *path) {
  http_url_t u;
  memset(&u, 0, sizeof u);
  bufcpy(u.host, sizeof u.host, host);
  u.port = port;
  bufcpy(u.path, sizeof u.path, path);
  return u;
}

START_TEST(dash_mpd_parse_matches_dipixy_writer_shape) {
  char body[] =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\" type=\"dynamic\">\n"
    "  <Period id=\"0\" start=\"PT0S\">\n"
    "    <AdaptationSet mimeType=\"video/mp4\" segmentAlignment=\"true\" startWithSAP=\"1\">\n"
    "      <Representation id=\"video\" codecs=\"avc1.640028\" bandwidth=\"3000000\">\n"
    "        <SegmentTemplate initialization=\"init.mp4\" media=\"dseg$Time$.m4s\" timescale=\"1000\">\n"
    "          <SegmentTimeline>\n"
    "            <S t=\"0\" d=\"6000\"/>\n"
    "          </SegmentTimeline>\n"
    "        </SegmentTemplate>\n"
    "      </Representation>\n"
    "    </AdaptationSet>\n"
    "  </Period>\n"
    "</MPD>\n";
  http_url_t base = make_base("origin.example", 80, "/live/stream.mpd");
  dash_mpd_t mpd;
  const dash_representation_t *r;

  ck_assert_int_eq(dash_mpd_parse(body, &base, &mpd), 1);
  ck_assert_uint_eq(mpd.n_periods, 1);
  ck_assert_uint_eq(mpd.periods[0].n_adaptation_sets, 1);
  ck_assert_str_eq(mpd.periods[0].adaptation_sets[0].mime_type, "video/mp4");
  ck_assert_uint_eq(mpd.periods[0].adaptation_sets[0].n_representations, 1);

  r = &mpd.periods[0].adaptation_sets[0].representations[0];
  ck_assert_str_eq(r->id, "video");
  ck_assert_uint_eq(r->bandwidth, 3000000u);
  ck_assert_str_eq(r->init_url, "http://origin.example/live/init.mp4");
  ck_assert_str_eq(r->media_url_tmpl, "http://origin.example/live/dseg$Time$.m4s");
  ck_assert_uint_eq(r->timescale, 1000u);
}
END_TEST

START_TEST(dash_mpd_parse_multiple_representations_and_adaptation_sets) {
  char body[] =
    "<MPD type=\"static\">\n"
    "  <Period id=\"0\">\n"
    "    <AdaptationSet mimeType=\"video/mp4\">\n"
    "      <Representation id=\"lo\" bandwidth=\"800000\" width=\"640\" height=\"360\">\n"
    "        <SegmentTemplate initialization=\"lo_init.mp4\" media=\"lo_$Number$.m4s\" timescale=\"90000\" duration=\"540000\" startNumber=\"1\"/>\n"
    "      </Representation>\n"
    "      <Representation id=\"hi\" bandwidth=\"3000000\" width=\"1920\" height=\"1080\">\n"
    "        <SegmentTemplate initialization=\"hi_init.mp4\" media=\"hi_$Number$.m4s\" timescale=\"90000\" duration=\"540000\" startNumber=\"1\"/>\n"
    "      </Representation>\n"
    "    </AdaptationSet>\n"
    "    <AdaptationSet mimeType=\"audio/mp4\">\n"
    "      <Representation id=\"aud\" bandwidth=\"128000\">\n"
    "        <SegmentTemplate initialization=\"aud_init.mp4\" media=\"aud_$Number$.m4s\" timescale=\"48000\" duration=\"288000\" startNumber=\"1\"/>\n"
    "      </Representation>\n"
    "    </AdaptationSet>\n"
    "  </Period>\n"
    "</MPD>\n";
  http_url_t base = make_base("cdn.example", 443, "/vod/index.mpd");
  dash_mpd_t mpd;
  int best;

  ck_assert_int_eq(dash_mpd_parse(body, &base, &mpd), 1);
  ck_assert_uint_eq(mpd.periods[0].n_adaptation_sets, 2);
  ck_assert_uint_eq(mpd.periods[0].adaptation_sets[0].n_representations, 2);
  ck_assert_uint_eq(mpd.periods[0].adaptation_sets[1].n_representations, 1);

  best = dash_pick_highest(&mpd.periods[0].adaptation_sets[0]);
  ck_assert_int_ge(best, 0);
  ck_assert_str_eq(mpd.periods[0].adaptation_sets[0].representations[(unsigned)best].id, "hi");

  {
    const dash_representation_t *lo = &mpd.periods[0].adaptation_sets[0].representations[0];
    ck_assert_uint_eq(lo->duration, 540000ULL);
    ck_assert_uint_eq(lo->start_number, 1ULL);
  }
}
END_TEST

START_TEST(dash_pick_highest_empty_returns_negative) {
  dash_adaptation_set_t as;
  memset(&as, 0, sizeof as);
  ck_assert_int_eq(dash_pick_highest(&as), -1);
}
END_TEST

START_TEST(dash_media_url_substitutes_number) {
  dash_representation_t r;
  char out[256];
  memset(&r, 0, sizeof r);
  bufcpy(r.media_url_tmpl, sizeof r.media_url_tmpl, "http://cdn.example/lo_$Number$.m4s");
  ck_assert_int_eq(dash_media_url(&r, 42, 0, out, sizeof out), 1);
  ck_assert_str_eq(out, "http://cdn.example/lo_42.m4s");
}
END_TEST

START_TEST(dash_media_url_substitutes_time) {
  dash_representation_t r;
  char out[256];
  memset(&r, 0, sizeof r);
  bufcpy(r.media_url_tmpl, sizeof r.media_url_tmpl, "http://origin.example/dseg$Time$.m4s");
  ck_assert_int_eq(dash_media_url(&r, 0, 6000, out, sizeof out), 1);
  ck_assert_str_eq(out, "http://origin.example/dseg6000.m4s");
}
END_TEST

START_TEST(dash_mpd_parse_rejects_non_mpd_body) {
  char body[] = "not an mpd at all";
  dash_mpd_t mpd;
  ck_assert_int_eq(dash_mpd_parse(body, NULL, &mpd), 0);
}
END_TEST

START_TEST(dash_mpd_parse_detects_low_latency_via_service_description) {
  char body[] =
    "<MPD type=\"dynamic\">\n"
    "  <ServiceDescription id=\"0\">\n"
    "    <Latency target=\"3500\" min=\"2000\" max=\"10000\" referenceId=\"0\"/>\n"
    "  </ServiceDescription>\n"
    "  <Period id=\"0\" start=\"PT0S\">\n"
    "    <AdaptationSet mimeType=\"video/mp4\">\n"
    "      <Representation id=\"video\" bandwidth=\"3000000\">\n"
    "        <SegmentTemplate initialization=\"init.mp4\" media=\"dseg$Time$.m4s\" timescale=\"1000\"\n"
    "          availabilityTimeOffset=\"4.500\" availabilityTimeComplete=\"false\">\n"
    "        </SegmentTemplate>\n"
    "      </Representation>\n"
    "    </AdaptationSet>\n"
    "  </Period>\n"
    "</MPD>\n";
  http_url_t base = make_base("origin.example", 80, "/live/stream.mpd");
  dash_mpd_t mpd;

  ck_assert_int_eq(dash_mpd_parse(body, &base, &mpd), 1);
  ck_assert_int_eq(mpd.is_low_latency, 1);
  ck_assert_uint_eq(mpd.latency_target_ms, 3500u);
  ck_assert_uint_eq(mpd.latency_min_ms, 2000u);
  ck_assert_uint_eq(mpd.latency_max_ms, 10000u);
  ck_assert_uint_eq(mpd.periods[0].adaptation_sets[0].representations[0].availability_time_offset_ms, 4500ULL);
}
END_TEST

START_TEST(dash_mpd_parse_detects_low_latency_via_ato_alone) {
  char body[] =
    "<MPD type=\"dynamic\">\n"
    "  <Period id=\"0\">\n"
    "    <AdaptationSet mimeType=\"video/mp4\">\n"
    "      <Representation id=\"video\" bandwidth=\"3000000\">\n"
    "        <SegmentTemplate initialization=\"init.mp4\" media=\"dseg$Time$.m4s\" timescale=\"1000\" availabilityTimeOffset=\"1.5\"/>\n"
    "      </Representation>\n"
    "    </AdaptationSet>\n"
    "  </Period>\n"
    "</MPD>\n";
  http_url_t base = make_base("origin.example", 80, "/live/stream.mpd");
  dash_mpd_t mpd;

  ck_assert_int_eq(dash_mpd_parse(body, &base, &mpd), 1);
  ck_assert_int_eq(mpd.is_low_latency, 1);
  ck_assert_uint_eq(mpd.periods[0].adaptation_sets[0].representations[0].availability_time_offset_ms, 1500ULL);
}
END_TEST

START_TEST(dash_mpd_parse_regular_dash_is_not_low_latency) {
  char body[] =
    "<MPD type=\"static\">\n"
    "  <Period id=\"0\">\n"
    "    <AdaptationSet mimeType=\"video/mp4\">\n"
    "      <Representation id=\"video\" bandwidth=\"3000000\">\n"
    "        <SegmentTemplate initialization=\"init.mp4\" media=\"seg_$Number$.m4s\" timescale=\"1000\" duration=\"6000\"/>\n"
    "      </Representation>\n"
    "    </AdaptationSet>\n"
    "  </Period>\n"
    "</MPD>\n";
  http_url_t base = make_base("origin.example", 80, "/vod/stream.mpd");
  dash_mpd_t mpd;

  ck_assert_int_eq(dash_mpd_parse(body, &base, &mpd), 1);
  ck_assert_int_eq(mpd.is_low_latency, 0);
}
END_TEST

START_TEST(dash_mpd_parse_reads_type_and_minimum_update_period) {
  char body[] =
    "<MPD xmlns=\"urn:mpeg:dash:schema:mpd:2011\"\n"
    "     type=\"dynamic\"\n"
    "     minimumUpdatePeriod=\"PT6.0S\">\n"
    "  <Period id=\"0\"><AdaptationSet mimeType=\"video/mp4\"><Representation id=\"v\" bandwidth=\"1\">\n"
    "  <SegmentTemplate initialization=\"i.mp4\" media=\"m$Number$.m4s\" timescale=\"1000\" duration=\"6000\"/>\n"
    "  </Representation></AdaptationSet></Period>\n"
    "</MPD>\n";
  dash_mpd_t mpd;
  ck_assert_int_eq(dash_mpd_parse(body, NULL, &mpd), 1);
  ck_assert_int_eq(mpd.is_dynamic, 1);
  ck_assert_uint_eq(mpd.minimum_update_period_ms, 6000u);
}
END_TEST

START_TEST(dash_mpd_parse_static_type) {
  char body[] =
    "<MPD type=\"static\">\n"
    "  <Period id=\"0\"><AdaptationSet mimeType=\"video/mp4\"><Representation id=\"v\" bandwidth=\"1\">\n"
    "  <SegmentTemplate initialization=\"i.mp4\" media=\"m$Number$.m4s\" timescale=\"1000\" duration=\"6000\"/>\n"
    "  </Representation></AdaptationSet></Period>\n"
    "</MPD>\n";
  dash_mpd_t mpd;
  ck_assert_int_eq(dash_mpd_parse(body, NULL, &mpd), 1);
  ck_assert_int_eq(mpd.is_dynamic, 0);
}
END_TEST

START_TEST(dash_effective_availability_ms_subtracts_offset) {
  ck_assert_uint_eq(dash_effective_availability_ms(6000, 4500), 1500ULL);
  ck_assert_uint_eq(dash_effective_availability_ms(1000, 4500), 0ULL);
}
END_TEST

START_TEST(dash_media_url_expands_identifier_bandwidth_and_widths) {
  dash_representation_t r;
  char out[256];
  memset(&r, 0, sizeof r);
  strcpy(r.id, "v1");
  r.bandwidth = 2500000;
  strcpy(r.media_url_tmpl, "http://h/$RepresentationID$/$Bandwidth$/s$Number%05d$_$Time%08d$$$.m4s");
  strcpy(r.init_url, "http://h/$RepresentationID$/init-$Bandwidth$.mp4");
  ck_assert_int_eq(dash_media_url(&r, 42, 6000, out, sizeof out), 1);
  ck_assert_str_eq(out, "http://h/v1/2500000/s00042_00006000$.m4s");
  ck_assert_int_eq(dash_init_url(&r, out, sizeof out), 1);
  ck_assert_str_eq(out, "http://h/v1/init-2500000.mp4");
}
END_TEST

START_TEST(dash_mpd_parse_timeline_with_repeats_and_implicit_times) {
  char body[] =
    "<MPD type=\"dynamic\"><Period><AdaptationSet mimeType=\"video/mp4\">"
    "<SegmentTemplate initialization=\"i.mp4\" media=\"s$Time$.m4s\" timescale=\"1000\" startNumber=\"5\" presentationTimeOffset=\"250\">"
    "<SegmentTimeline><S t=\"1000\" d=\"2000\" r=\"2\"/><S d=\"3000\"/><S t=\"12000\" d=\"2000\" r=\"-1\"/><S t=\"16000\" d=\"1000\"/></SegmentTimeline>"
    "</SegmentTemplate><Representation id=\"a\" bandwidth=\"1\"/></AdaptationSet></Period></MPD>";
  dash_mpd_t mpd;
  const dash_representation_t *r;
  unsigned long long t;
  unsigned long long d;
  unsigned long long k;
  ck_assert_int_eq(dash_mpd_parse(body, NULL, &mpd), 1);
  r = &mpd.periods[0].adaptation_sets[0].representations[0];
  ck_assert_uint_eq(r->n_timeline, 4u);
  ck_assert_uint_eq(r->timeline[1].t, 7000ULL);
  ck_assert_uint_eq(r->timeline[2].r, 1ULL);
  ck_assert_uint_eq(r->presentation_time_offset, 250ULL);
  ck_assert_uint_eq(dash_timeline_first(r), 1000ULL);
  ck_assert_int_eq(dash_timeline_last(r, &t, &d, &k), 1);
  ck_assert_uint_eq(t, 16000ULL);
  ck_assert_uint_eq(d, 1000ULL);
  ck_assert_uint_eq(k, 6ULL);
  ck_assert_int_eq(dash_timeline_find(r, 5000, &d, &k), 1);
  ck_assert_uint_eq(k, 2ULL);
  ck_assert_uint_eq(d, 2000ULL);
  ck_assert_int_eq(dash_timeline_find(r, 14000, &d, &k), 1);
  ck_assert_uint_eq(k, 5ULL);
  ck_assert_int_eq(dash_timeline_find(r, 5500, &d, &k), 0);
  ck_assert_uint_eq(dash_segment_duration_ms(r), 1000ULL);
}
END_TEST

START_TEST(dash_mpd_parse_open_ended_repeat_closer_than_one_duration_is_single) {
  char body[] =
    "<MPD type=\"dynamic\"><Period><AdaptationSet mimeType=\"video/mp4\">"
    "<SegmentTemplate initialization=\"i.mp4\" media=\"s$Time$.m4s\" timescale=\"1000\">"
    "<SegmentTimeline><S t=\"0\" d=\"2000\" r=\"-1\"/><S t=\"1500\" d=\"2000\"/></SegmentTimeline>"
    "</SegmentTemplate><Representation id=\"a\" bandwidth=\"1\"/></AdaptationSet></Period></MPD>";
  dash_mpd_t mpd;
  const dash_representation_t *r;
  ck_assert_int_eq(dash_mpd_parse(body, NULL, &mpd), 1);
  r = &mpd.periods[0].adaptation_sets[0].representations[0];
  ck_assert_uint_eq(r->timeline[0].r, 0ULL);
}
END_TEST

START_TEST(dash_mpd_parse_trailing_open_ended_repeat_runs_to_the_period_end) {
  char body[] =
    "<MPD type=\"static\" mediaPresentationDuration=\"PT10S\"><Period><AdaptationSet mimeType=\"video/mp4\">"
    "<SegmentTemplate initialization=\"i.mp4\" media=\"s$Time$.m4s\" timescale=\"1000\">"
    "<SegmentTimeline><S t=\"0\" d=\"2000\" r=\"-1\"/></SegmentTimeline>"
    "</SegmentTemplate><Representation id=\"a\" bandwidth=\"1\"/></AdaptationSet></Period></MPD>";
  dash_mpd_t mpd;
  const dash_representation_t *r;
  ck_assert_int_eq(dash_mpd_parse(body, NULL, &mpd), 1);
  r = &mpd.periods[0].adaptation_sets[0].representations[0];
  ck_assert_uint_eq(r->timeline[0].r, 4ULL);
}
END_TEST

START_TEST(dash_mpd_parse_timeline_drops_oldest_beyond_capacity) {
  size_t cap = DASH_MAX_TIMELINE * 40 + 512;
  char *body = malloc(cap);
  char *p = body;
  dash_mpd_t *mpd = malloc(sizeof *mpd);
  const dash_representation_t *r;
  unsigned long long d;
  unsigned long long k;
  if (body == NULL || mpd == NULL) {
    free(body);
    free(mpd);
    ck_abort_msg("oom");
    return;
  }
  p += snprintf(p, cap, "<MPD><Period><AdaptationSet><SegmentTemplate media=\"s$Time$\" timescale=\"1\"><SegmentTimeline>");
  for (unsigned i = 0; i < DASH_MAX_TIMELINE + 3; i++) p += snprintf(p, cap - (size_t)(p - body), "<S t=\"%u\" d=\"1\"/>", i * 10);
  snprintf(p, cap - (size_t)(p - body), "</SegmentTimeline></SegmentTemplate><Representation id=\"a\"/></AdaptationSet></Period></MPD>");
  ck_assert_int_eq(dash_mpd_parse(body, NULL, mpd), 1);
  r = &mpd->periods[0].adaptation_sets[0].representations[0];
  ck_assert_uint_eq(r->n_timeline, (unsigned)DASH_MAX_TIMELINE);
  ck_assert_uint_eq(r->timeline_skipped, 3ULL);
  ck_assert_int_eq(dash_timeline_find(r, 30, &d, &k), 1);
  ck_assert_uint_eq(k, 3ULL);
  ck_assert_int_eq(dash_timeline_find(r, 20, &d, &k), 0);
  free(mpd);
  free(body);
}
END_TEST

START_TEST(dash_mpd_parse_reads_availability_start_and_buffer_depth) {
  char body[] =
    "<MPD type=\"dynamic\" availabilityStartTime=\"2024-03-05T10:20:30.250Z\" timeShiftBufferDepth=\"PT1M30S\" minimumUpdatePeriod=\"PT2.5S\">"
    "<Period><AdaptationSet><Representation id=\"a\"/></AdaptationSet></Period></MPD>";
  dash_mpd_t mpd;
  ck_assert_int_eq(dash_mpd_parse(body, NULL, &mpd), 1);
  ck_assert_int_eq(mpd.has_availability_start, 1);
  ck_assert_uint_eq(mpd.availability_start_ms, 1709634030250ULL);
  ck_assert_uint_eq(mpd.time_shift_buffer_ms, 90000ULL);
  ck_assert_uint_eq(mpd.minimum_update_period_ms, 2500u);
}
END_TEST

START_TEST(dash_mpd_parse_offset_availability_start_is_converted_to_utc) {
  char body[] = "<MPD type=\"dynamic\" availabilityStartTime=\"2024-03-05T11:20:30+01:00\"><Period><AdaptationSet><Representation id=\"a\"/></AdaptationSet></Period></MPD>";
  dash_mpd_t mpd;
  ck_assert_int_eq(dash_mpd_parse(body, NULL, &mpd), 1);
  ck_assert_uint_eq(mpd.availability_start_ms, 1709634030000ULL);
}
END_TEST

START_TEST(dash_mpd_parse_resolves_base_url_levels) {
  char body[] =
    "<MPD><BaseURL>http://cdn.example/root/</BaseURL><Period><BaseURL>p1/</BaseURL>"
    "<AdaptationSet mimeType=\"video/mp4\"><BaseURL>v/</BaseURL>"
    "<SegmentTemplate initialization=\"init.mp4\" media=\"s$Number$.m4s\"/>"
    "<Representation id=\"a\"/><Representation id=\"b\"><BaseURL>b/</BaseURL></Representation></AdaptationSet></Period></MPD>";
  http_url_t base = make_base("origin.example", 80, "/live/stream.mpd");
  dash_mpd_t mpd;
  const dash_adaptation_set_t *as;
  ck_assert_int_eq(dash_mpd_parse(body, &base, &mpd), 1);
  as = &mpd.periods[0].adaptation_sets[0];
  ck_assert_str_eq(as->representations[0].init_url, "http://cdn.example/root/p1/v/init.mp4");
  ck_assert_str_eq(as->representations[0].media_url_tmpl, "http://cdn.example/root/p1/v/s$Number$.m4s");
  ck_assert_str_eq(as->representations[1].media_url_tmpl, "http://cdn.example/root/p1/v/b/s$Number$.m4s");
  ck_assert_uint_eq(as->representations[0].start_number, 1ULL);
}
END_TEST

START_TEST(dash_mpd_parse_period_starts_chain) {
  char body[] =
    "<MPD type=\"dynamic\" availabilityStartTime=\"2024-03-05T10:00:00Z\">"
    "<Period id=\"a\" start=\"PT0S\" duration=\"PT1M\"><AdaptationSet><Representation id=\"a\"/></AdaptationSet></Period>"
    "<Period id=\"b\" duration=\"PT30S\"><AdaptationSet><Representation id=\"a\"/></AdaptationSet></Period>"
    "<Period id=\"c\" start=\"PT2M\"><AdaptationSet><Representation id=\"a\"/></AdaptationSet></Period></MPD>";
  dash_mpd_t mpd;
  ck_assert_int_eq(dash_mpd_parse(body, NULL, &mpd), 1);
  ck_assert_uint_eq(mpd.n_periods, 3u);
  ck_assert_uint_eq(mpd.periods[0].start_ms, 0ULL);
  ck_assert_uint_eq(mpd.periods[1].start_ms, 60000ULL);
  ck_assert_uint_eq(mpd.periods[2].start_ms, 120000ULL);
}
END_TEST

START_TEST(dash_edge_index_follows_wall_clock) {
  dash_representation_t r;
  unsigned long long idx = 99;
  memset(&r, 0, sizeof r);
  r.timescale = 1000;
  r.duration = 2000;
  ck_assert_int_eq(dash_edge_index(&r, 1999, &idx), 0);
  ck_assert_int_eq(dash_edge_index(&r, 2000, &idx), 1);
  ck_assert_uint_eq(idx, 0ULL);
  ck_assert_int_eq(dash_edge_index(&r, 5000, &idx), 1);
  ck_assert_uint_eq(idx, 1ULL);
  r.availability_time_offset_ms = 1000;
  ck_assert_int_eq(dash_edge_index(&r, 5000, &idx), 1);
  ck_assert_uint_eq(idx, 2ULL);
  ck_assert_uint_eq(dash_index_available_ms(&r, 2), 5000ULL);
  ck_assert_uint_eq(dash_index_available_ms(&r, 0), 1000ULL);
  r.presentation_time_offset = 500;
  ck_assert_uint_eq(dash_index_time(&r, 3), 6500ULL);
}
END_TEST

START_TEST(dash_mpd_live_period_picks_by_wall_clock) {
  dash_mpd_t mpd;
  memset(&mpd, 0, sizeof mpd);
  mpd.is_dynamic = 1;
  mpd.n_periods = 3;
  mpd.periods[1].start_ms = 1000;
  mpd.periods[2].start_ms = 2000;
  ck_assert_uint_eq(dash_mpd_live_period(&mpd), 2u);
  mpd.has_availability_start = 1;
  mpd.availability_start_ms = dash_wall_ms() - 1500;
  ck_assert_uint_eq(dash_mpd_live_period(&mpd), 1u);
  mpd.is_dynamic = 0;
  ck_assert_uint_eq(dash_mpd_live_period(&mpd), 0u);
}
END_TEST

START_TEST(dash_mpd_parse_reads_multi_unit_durations) {
  char body[] = "<MPD type=\"dynamic\" minimumUpdatePeriod=\"PT1M30.5S\"><Period><AdaptationSet><Representation id=\"a\"/></AdaptationSet></Period></MPD>";
  dash_mpd_t mpd;
  ck_assert_int_eq(dash_mpd_parse(body, NULL, &mpd), 1);
  ck_assert_uint_eq(mpd.minimum_update_period_ms, 90500u);
}
END_TEST

static Suite *dash_mpd_suite(void) {
  Suite *s = suite_create("dash_mpd");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, dash_mpd_parse_matches_dipixy_writer_shape);
  tcase_add_test(tc, dash_mpd_parse_multiple_representations_and_adaptation_sets);
  tcase_add_test(tc, dash_pick_highest_empty_returns_negative);
  tcase_add_test(tc, dash_media_url_substitutes_number);
  tcase_add_test(tc, dash_media_url_substitutes_time);
  tcase_add_test(tc, dash_mpd_parse_rejects_non_mpd_body);
  tcase_add_test(tc, dash_mpd_parse_detects_low_latency_via_service_description);
  tcase_add_test(tc, dash_mpd_parse_detects_low_latency_via_ato_alone);
  tcase_add_test(tc, dash_mpd_parse_regular_dash_is_not_low_latency);
  tcase_add_test(tc, dash_effective_availability_ms_subtracts_offset);
  tcase_add_test(tc, dash_mpd_parse_reads_type_and_minimum_update_period);
  tcase_add_test(tc, dash_mpd_parse_static_type);
  tcase_add_test(tc, dash_media_url_expands_identifier_bandwidth_and_widths);
  tcase_add_test(tc, dash_mpd_parse_timeline_with_repeats_and_implicit_times);
  tcase_add_test(tc, dash_mpd_parse_open_ended_repeat_closer_than_one_duration_is_single);
  tcase_add_test(tc, dash_mpd_parse_trailing_open_ended_repeat_runs_to_the_period_end);
  tcase_add_test(tc, dash_mpd_parse_timeline_drops_oldest_beyond_capacity);
  tcase_add_test(tc, dash_mpd_parse_reads_availability_start_and_buffer_depth);
  tcase_add_test(tc, dash_mpd_parse_offset_availability_start_is_converted_to_utc);
  tcase_add_test(tc, dash_mpd_parse_resolves_base_url_levels);
  tcase_add_test(tc, dash_mpd_parse_period_starts_chain);
  tcase_add_test(tc, dash_edge_index_follows_wall_clock);
  tcase_add_test(tc, dash_mpd_live_period_picks_by_wall_clock);
  tcase_add_test(tc, dash_mpd_parse_reads_multi_unit_durations);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(dash_mpd_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
