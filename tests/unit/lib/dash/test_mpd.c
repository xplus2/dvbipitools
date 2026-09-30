/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/dash/mpd.h"
#include "lib/helper/ioutil.h"

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
