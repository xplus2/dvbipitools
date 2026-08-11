/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE

#include <arpa/inet.h>
#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/net/dvbstp.h"
#include "dipisds/input.h"

static void write_temp_file(char *path, const char *suffix, const char *content) {
  char tmpl[128];
  int fd;
  FILE *f;
  snprintf(tmpl, sizeof tmpl, "/tmp/dvbipitools_test_input_XXXXXX%s", suffix);
  strcpy(path, tmpl);
  fd = mkstemps(path, (int)strlen(suffix));
  ck_assert_int_ge(fd, 0);
  f = fdopen(fd, "w");
  fputs(content, f);
  fclose(f);
}

START_TEST(csv_parses_name_uri_and_optional_ids) {
  char path[160];
  input_t in;
  write_temp_file(path, ".csv",
                   "Channel One,rtp://239.1.1.1:5000,1,2,101\n"
                   "Channel Two,udp://239.1.1.2:5001\n");

  ck_assert_int_eq(input_load(path, &in), 0);
  ck_assert_int_eq(in.kind, INPUT_SERVICES);
  ck_assert_int_eq(in.service_count, 2);

  ck_assert_str_eq(in.services[0].name, "Channel One");
  ck_assert_str_eq(in.services[0].address, "239.1.1.1");
  ck_assert_int_eq(in.services[0].family, AF_INET);
  ck_assert_uint_eq(in.services[0].port, 5000u);
  ck_assert_int_eq(in.services[0].rtp, 1);
  ck_assert_uint_eq(in.services[0].tsid, 1u);
  ck_assert_uint_eq(in.services[0].onid, 2u);
  ck_assert_uint_eq(in.services[0].sid, 101u);

  /* no tsid/onid/sid given: default tsid=1, onid=1, sid=index+1; udp:// -> rtp=0 */
  ck_assert_str_eq(in.services[1].name, "Channel Two");
  ck_assert_int_eq(in.services[1].rtp, 0);
  ck_assert_uint_eq(in.services[1].tsid, 1u);
  ck_assert_uint_eq(in.services[1].onid, 1u);
  ck_assert_uint_eq(in.services[1].sid, 2u);

  input_free(&in);
  unlink(path);
}
END_TEST

START_TEST(csv_rejects_line_with_only_name) {
  char path[160];
  input_t in;
  write_temp_file(path, ".csv", "Channel One\n");
  ck_assert_int_eq(input_load(path, &in), -1);
  unlink(path);
}
END_TEST

START_TEST(csv_rejects_bad_uri) {
  char path[160];
  input_t in;
  write_temp_file(path, ".csv", "Channel One,not-a-uri\n");
  ck_assert_int_eq(input_load(path, &in), -1);
  unlink(path);
}
END_TEST

START_TEST(csv_parses_ipv6_address) {
  char path[160];
  input_t in;
  write_temp_file(path, ".csv", "Channel One,rtp://[ff15::1]:5000\n");
  ck_assert_int_eq(input_load(path, &in), 0);
  ck_assert_int_eq(in.services[0].family, AF_INET6);
  ck_assert_str_eq(in.services[0].address, "ff15::1");
  input_free(&in);
  unlink(path);
}
END_TEST

START_TEST(m3u_pairs_extinf_attrs_with_following_uri) {
  char path[160];
  input_t in;
  write_temp_file(path, ".m3u",
                   "#EXTM3U\n"
                   "#EXTINF:-1 tsid=\"3\" onid=\"4\" sid=\"55\",Channel One\n"
                   "rtp://239.1.1.1:5000\n"
                   "#EXTINF:-1,Channel Two\n"
                   "udp://239.1.1.2:5001\n");

  ck_assert_int_eq(input_load(path, &in), 0);
  ck_assert_int_eq(in.service_count, 2);
  ck_assert_str_eq(in.services[0].name, "Channel One");
  ck_assert_uint_eq(in.services[0].tsid, 3u);
  ck_assert_uint_eq(in.services[0].onid, 4u);
  ck_assert_uint_eq(in.services[0].sid, 55u);

  /* no explicit sid: falls back to index+1 */
  ck_assert_str_eq(in.services[1].name, "Channel Two");
  ck_assert_uint_eq(in.services[1].tsid, 1u);
  ck_assert_uint_eq(in.services[1].sid, 2u);

  input_free(&in);
  unlink(path);
}
END_TEST

START_TEST(m3u_uri_without_preceding_extinf_is_ignored) {
  char path[160];
  input_t in;
  write_temp_file(path, ".m3u", "rtp://239.1.1.1:5000\n");
  ck_assert_int_eq(input_load(path, &in), 0);
  ck_assert_int_eq(in.service_count, 0);
  input_free(&in);
  unlink(path);
}
END_TEST

START_TEST(m3u_rejects_malformed_extinf) {
  char path[160];
  input_t in;
  write_temp_file(path, ".m3u", "#EXTINF:no-comma-here\n");
  ck_assert_int_eq(input_load(path, &in), -1);
  unlink(path);
}
END_TEST

START_TEST(xspf_parses_track_location_title_and_ids) {
  char path[160];
  input_t in;
  write_temp_file(path, ".xspf",
                   "<playlist><trackList>"
                   "<track tsid=\"3\" onid=\"4\" sid=\"55\">"
                   "<location>rtp://239.1.1.1:5000</location>"
                   "<title>Channel One</title>"
                   "</track>"
                   "</trackList></playlist>\n");

  ck_assert_int_eq(input_load(path, &in), 0);
  ck_assert_int_eq(in.kind, INPUT_SERVICES);
  ck_assert_int_eq(in.service_count, 1);
  ck_assert_str_eq(in.services[0].name, "Channel One");
  ck_assert_str_eq(in.services[0].address, "239.1.1.1");
  ck_assert_uint_eq(in.services[0].tsid, 3u);
  ck_assert_uint_eq(in.services[0].onid, 4u);
  ck_assert_uint_eq(in.services[0].sid, 55u);

  input_free(&in);
  unlink(path);
}
END_TEST

START_TEST(xspf_rejects_track_without_location) {
  char path[160];
  input_t in;
  write_temp_file(path, ".xspf", "<playlist><trackList><track><title>x</title></track></trackList></playlist>\n");
  ck_assert_int_eq(input_load(path, &in), -1);
  unlink(path);
}
END_TEST

START_TEST(xml_detects_broadcast_discovery_root) {
  char path[160];
  input_t in;
  write_temp_file(path, ".xml", "<BroadcastDiscovery>content</BroadcastDiscovery>\n");
  ck_assert_int_eq(input_load(path, &in), 0);
  ck_assert_int_eq(in.kind, INPUT_RAW_XML);
  ck_assert_uint_eq(in.raw_payload_id, DVBSTP_PAYLOAD_BROADCAST_DISCOVERY);
  ck_assert_uint_gt(in.raw_xml_len, 0u);
  input_free(&in);
  unlink(path);
}
END_TEST

START_TEST(xml_detects_service_provider_discovery_root) {
  char path[160];
  input_t in;
  write_temp_file(path, ".xml", "<ServiceProviderDiscovery>content</ServiceProviderDiscovery>\n");
  ck_assert_int_eq(input_load(path, &in), 0);
  ck_assert_int_eq(in.kind, INPUT_RAW_XML);
  ck_assert_uint_eq(in.raw_payload_id, DVBSTP_PAYLOAD_SP_DISCOVERY);
  input_free(&in);
  unlink(path);
}
END_TEST

START_TEST(xml_rejects_unknown_root_element) {
  char path[160];
  input_t in;
  write_temp_file(path, ".xml", "<SomethingElse/>\n");
  ck_assert_int_eq(input_load(path, &in), -1);
  unlink(path);
}
END_TEST

START_TEST(unrecognized_suffix_is_rejected) {
  char path[160];
  input_t in;
  write_temp_file(path, ".txt", "irrelevant\n");
  ck_assert_int_eq(input_load(path, &in), -1);
  unlink(path);
}
END_TEST

START_TEST(missing_file_is_rejected) {
  input_t in;
  ck_assert_int_eq(input_load("/nonexistent/dvbipitools_test_input.csv", &in), -1);
}
END_TEST

static Suite *input_suite(void) {
  Suite *s = suite_create("dipisds_input");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, csv_parses_name_uri_and_optional_ids);
  tcase_add_test(tc, csv_rejects_line_with_only_name);
  tcase_add_test(tc, csv_rejects_bad_uri);
  tcase_add_test(tc, csv_parses_ipv6_address);
  tcase_add_test(tc, m3u_pairs_extinf_attrs_with_following_uri);
  tcase_add_test(tc, m3u_uri_without_preceding_extinf_is_ignored);
  tcase_add_test(tc, m3u_rejects_malformed_extinf);
  tcase_add_test(tc, xspf_parses_track_location_title_and_ids);
  tcase_add_test(tc, xspf_rejects_track_without_location);
  tcase_add_test(tc, xml_detects_broadcast_discovery_root);
  tcase_add_test(tc, xml_detects_service_provider_discovery_root);
  tcase_add_test(tc, xml_rejects_unknown_root_element);
  tcase_add_test(tc, unrecognized_suffix_is_rejected);
  tcase_add_test(tc, missing_file_is_rejected);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(input_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
