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

START_TEST(xml_detects_package_discovery_root) {
  char path[160];
  input_t in;
  write_temp_file(path, ".xml", "<PackageDiscovery>content</PackageDiscovery>\n");
  ck_assert_int_eq(input_load(path, &in), 0);
  ck_assert_int_eq(in.kind, INPUT_RAW_XML);
  ck_assert_uint_eq(in.raw_payload_id, DVBSTP_PAYLOAD_PACKAGE_DISCOVERY);
  input_free(&in);
  unlink(path);
}
END_TEST

START_TEST(xml_detects_regionalisation_discovery_root) {
  char path[160];
  input_t in;
  write_temp_file(path, ".xml", "<RegionalisationDiscovery>content</RegionalisationDiscovery>\n");
  ck_assert_int_eq(input_load(path, &in), 0);
  ck_assert_int_eq(in.kind, INPUT_RAW_XML);
  ck_assert_uint_eq(in.raw_payload_id, DVBSTP_PAYLOAD_REGIONALISATION_DISCOVERY);
  input_free(&in);
  unlink(path);
}
END_TEST

START_TEST(xml_detects_rms_fus_discovery_root) {
  char path[160];
  input_t in;
  write_temp_file(path, ".xml", "<RMSFUSDiscovery>content</RMSFUSDiscovery>\n");
  ck_assert_int_eq(input_load(path, &in), 0);
  ck_assert_int_eq(in.kind, INPUT_RAW_XML);
  ck_assert_uint_eq(in.raw_payload_id, DVBSTP_PAYLOAD_RMSFUS_DISCOVERY);
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

START_TEST(packages_file_parses_id_name_lang_visible_and_services) {
  char path[160];
  sds_package_t pkgs[4];
  int count = -1;
  write_temp_file(path, ".csv",
                   "1,Bundle One,eng,1,Channel One|Channel Two\n"
                   "2,Bundle Two,deu,,Channel Three\n");
  ck_assert_int_eq(input_load_packages(path, pkgs, 4, &count), 0);
  ck_assert_int_eq(count, 2);
  ck_assert_uint_eq(pkgs[0].id, 1u);
  ck_assert_str_eq(pkgs[0].name, "Bundle One");
  ck_assert_int_eq(memcmp(pkgs[0].lang, "eng", 3), 0);
  ck_assert_int_eq(pkgs[0].visible, 1);
  ck_assert_int_eq(pkgs[0].service_count, 2);
  ck_assert_str_eq(pkgs[0].service_names[0], "Channel One");
  ck_assert_str_eq(pkgs[0].service_names[1], "Channel Two");
  ck_assert_int_eq(pkgs[1].visible, 1); /* empty field defaults to visible */
  unlink(path);
}
END_TEST

START_TEST(packages_file_rejects_missing_field) {
  char path[160];
  sds_package_t pkgs[4];
  int count = -1;
  write_temp_file(path, ".csv", "1,Bundle One,eng,1\n"); /* no services field */
  ck_assert_int_eq(input_load_packages(path, pkgs, 4, &count), -1);
  unlink(path);
}
END_TEST

START_TEST(packages_file_rejects_no_services) {
  char path[160];
  sds_package_t pkgs[4];
  int count = -1;
  write_temp_file(path, ".csv", "1,Bundle One,eng,1,\n");
  ck_assert_int_eq(input_load_packages(path, pkgs, 4, &count), -1);
  unlink(path);
}
END_TEST

START_TEST(cells_file_parses_id_country_and_ca_chain) {
  char path[160];
  sds_cell_t cells[4];
  int count = -1;
  write_temp_file(path, ".csv", "Paris East,FR,1:IDF,3:Paris\n");
  ck_assert_int_eq(input_load_cells(path, cells, 4, &count), 0);
  ck_assert_int_eq(count, 1);
  ck_assert_str_eq(cells[0].id, "Paris East");
  ck_assert_int_eq(memcmp(cells[0].country, "FR", 2), 0);
  ck_assert_int_eq(cells[0].ca_depth, 2);
  ck_assert_uint_eq(cells[0].ca[0].type, 1u);
  ck_assert_str_eq(cells[0].ca[0].value, "IDF");
  ck_assert_uint_eq(cells[0].ca[1].type, 3u);
  ck_assert_str_eq(cells[0].ca[1].value, "Paris");
  unlink(path);
}
END_TEST

START_TEST(cells_file_rejects_no_ca_entries) {
  char path[160];
  sds_cell_t cells[4];
  int count = -1;
  write_temp_file(path, ".csv", "Paris East,FR\n");
  ck_assert_int_eq(input_load_cells(path, cells, 4, &count), -1);
  unlink(path);
}
END_TEST

START_TEST(cells_file_rejects_bad_country_code) {
  char path[160];
  sds_cell_t cells[4];
  int count = -1;
  write_temp_file(path, ".csv", "Paris East,France,1:IDF\n");
  ck_assert_int_eq(input_load_cells(path, cells, 4, &count), -1);
  unlink(path);
}
END_TEST

static void write_big_file(char *path, const char *suffix, const char *line_fmt, int lines) {
  size_t cap = (size_t)lines * 96 + 16;
  char *buf = malloc(cap);
  size_t n = 0;

  ck_assert_ptr_nonnull(buf);
  buf[0] = '\0';
  for (int i = 0; i < lines; i++) n += (size_t)snprintf(buf + n, cap - n, line_fmt, i % 200, i % 200);
  write_temp_file(path, suffix, buf);
  free(buf);
}

typedef struct {
  const char *uri;
  int ok;
  int family;
  unsigned port;
  int rtp;
} uri_case_t;

static const uri_case_t uri_cases[] = {
  {"udp://@239.1.1.1:5000", 1, AF_INET, 5000, 0},
  {"rtp://[ff3e::1]:6000", 1, AF_INET6, 6000, 1},
  {"udp://[ff3e::1", 0, 0, 0, 0},
  {"udp://[ff3e::1]6000", 0, 0, 0, 0},
  {"udp://[]:6000", 0, 0, 0, 0},
  {"udp://239.1.1.1", 0, 0, 0, 0},
  {"udp://:5000", 0, 0, 0, 0},
  {"udp://239.1.1.1:0", 0, 0, 0, 0},
  {"udp://239.1.1.1:70000", 0, 0, 0, 0},
  {"udp://239.1.1.1:50x", 0, 0, 0, 0},
  {"http://239.1.1.1:5000", 0, 0, 0, 0},
  {"udp://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:5000", 0, 0, 0, 0},
  {"udp://[aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa]:5000", 0, 0, 0, 0},
};

START_TEST(csv_multicast_uri_forms) {
  const uri_case_t *c = &uri_cases[_i];
  char path[160];
  char line[256];
  input_t in;
  int rc;

  snprintf(line, sizeof line, "Name,%s\n", c->uri);
  write_temp_file(path, ".csv", line);
  rc = input_load(path, &in);
  ck_assert_msg((rc == 0) == (c->ok != 0), "%s: rc %d", c->uri, rc);
  if (c->ok) {
    ck_assert_int_eq(in.services[0].family, c->family);
    ck_assert_uint_eq(in.services[0].port, c->port);
    ck_assert_int_eq(in.services[0].rtp, c->rtp);
    input_free(&in);
  }
  unlink(path);
}
END_TEST

START_TEST(csv_skips_blank_lines_and_stops_at_the_service_limit) {
  char path[160];
  input_t in;

  write_temp_file(path, ".csv", "\nOne,udp://239.1.1.1:5000\n\nTwo,udp://239.1.1.2:5000\n");
  ck_assert_int_eq(input_load(path, &in), 0);
  ck_assert_int_eq(in.service_count, 2);
  input_free(&in);
  unlink(path);
  write_big_file(path, ".csv", "S%d,udp://239.1.1.%d:5000\n", SDS_MAX_SERVICES + 1);
  ck_assert_int_eq(input_load(path, &in), -1);
  unlink(path);
}
END_TEST

START_TEST(m3u_reads_extinf_attributes_defaults_and_limits) {
  char path[160];
  input_t in;

  write_temp_file(path, ".m3u",
                  "#EXTM3U\n\n#EXTINF:-1 tsid=\"7\" onid=\"8\" sid=\"9\",Named\nrtp://239.1.1.1:5000\n# comment\n"
                  "#EXTINF:-1,Second\nudp://239.1.1.2:5000\n");
  ck_assert_int_eq(input_load(path, &in), 0);
  ck_assert_int_eq(in.service_count, 2);
  ck_assert_uint_eq(in.services[0].tsid, 7u);
  ck_assert_uint_eq(in.services[0].onid, 8u);
  ck_assert_uint_eq(in.services[0].sid, 9u);
  ck_assert_uint_eq(in.services[1].tsid, 1u);
  ck_assert_uint_eq(in.services[1].sid, 2u);
  input_free(&in);
  unlink(path);

  write_temp_file(path, ".m3u", "#EXTINF:-1,Bad\nhttp://example.com/x\n");
  ck_assert_int_eq(input_load(path, &in), -1);
  unlink(path);

  write_big_file(path, ".m3u", "#EXTINF:-1,S%d\nudp://239.1.1.%d:5000\n", SDS_MAX_SERVICES + 1);
  ck_assert_int_eq(input_load(path, &in), -1);
  unlink(path);
}
END_TEST

START_TEST(xspf_handles_missing_titles_truncation_and_broken_tracks) {
  char path[160];
  char big[1024];
  input_t in;
  size_t n;

  write_temp_file(path, ".xspf", "<playlist><trackList><track><location>udp://239.1.1.1:5000</location></track></trackList></playlist>");
  ck_assert_int_eq(input_load(path, &in), 0);
  ck_assert_str_eq(in.services[0].name, "");
  ck_assert_uint_eq(in.services[0].sid, 1u);
  input_free(&in);
  unlink(path);

  n = (size_t)snprintf(big, sizeof big, "<track><location>udp://239.1.1.1:5000</location><title>");
  for (int i = 0; i < SDS_MAX_NAME + 20; i++) big[n++] = 'x';
  n += (size_t)snprintf(big + n, sizeof big - n, "</title></track>");
  write_temp_file(path, ".xspf", big);
  ck_assert_int_eq(input_load(path, &in), 0);
  ck_assert_uint_eq(strlen(in.services[0].name), (size_t)SDS_MAX_NAME - 1);
  input_free(&in);
  unlink(path);

  write_temp_file(path, ".xspf", "<track><location>udp://239.1.1.1:5000</track>");
  ck_assert_int_eq(input_load(path, &in), -1);
  unlink(path);

  write_temp_file(path, ".xspf", "<track><location>udp://239.1.1.1:5000</location>");
  ck_assert_int_eq(input_load(path, &in), 0);
  ck_assert_int_eq(in.service_count, 0);
  input_free(&in);
  unlink(path);

  write_temp_file(path, ".xspf", "<track><location>ftp://x</location></track>");
  ck_assert_int_eq(input_load(path, &in), -1);
  unlink(path);
}
END_TEST

START_TEST(xspf_stops_at_the_service_limit) {
  char path[160];
  char *buf = malloc((size_t)(SDS_MAX_SERVICES + 1) * 80);
  size_t n = 0;
  input_t in;

  ck_assert_ptr_nonnull(buf);
  for (int i = 0; i < SDS_MAX_SERVICES + 1; i++) n += (size_t)sprintf(buf + n, "<track><location>udp://239.1.1.%d:5000</location></track>", i % 200);
  write_temp_file(path, ".xspf", buf);
  free(buf);
  ck_assert_int_eq(input_load(path, &in), -1);
  unlink(path);
}
END_TEST

typedef struct {
  const char *name;
  const char *content;
  int max;
} bad_pkg_t;

static const bad_pkg_t bad_pkgs[] = {
  {"bad id", "x,Bundle,eng,1,A\n", 4},
  {"short lang", "1,Bundle,en,1,A\n", 4},
  {"over the package limit", "1,A,eng,1,S\n2,B,eng,1,S\n", 1},
};

START_TEST(packages_file_rejects_bad_lines_and_overflow) {
  const bad_pkg_t *c = &bad_pkgs[_i];
  char path[160];
  sds_package_t pkgs[4];
  int count = -1;

  write_temp_file(path, ".csv", c->content);
  ck_assert_msg(input_load_packages(path, pkgs, c->max, &count) == -1, "%s", c->name);
  unlink(path);
}
END_TEST

START_TEST(packages_file_handles_blank_lines_invisible_and_service_overflow) {
  char path[160];
  char big[1024];
  sds_package_t pkgs[4];
  int count = -1;
  size_t n;

  write_temp_file(path, ".csv", "\n1,Hidden,eng,0,A|B\n\n");
  ck_assert_int_eq(input_load_packages(path, pkgs, 4, &count), 0);
  ck_assert_int_eq(count, 1);
  ck_assert_int_eq(pkgs[0].visible, 0);
  unlink(path);

  n = (size_t)snprintf(big, sizeof big, "1,Many,eng,1,");
  for (int i = 0; i < SDS_MAX_PKG_SERVICES + 1; i++) n += (size_t)snprintf(big + n, sizeof big - n, "s%d|", i);
  big[n++] = '\n';
  big[n] = '\0';
  write_temp_file(path, ".csv", big);
  ck_assert_int_eq(input_load_packages(path, pkgs, 4, &count), -1);
  unlink(path);

  ck_assert_int_eq(input_load_packages("/nonexistent/dvbipitools_pkgs.csv", pkgs, 4, &count), -1);
}
END_TEST

static const bad_pkg_t bad_cells[] = {
  {"missing country", "Paris\n", 4},
  {"entry without a colon", "Paris,FR,IDF\n", 4},
  {"non numeric type", "Paris,FR,x:IDF\n", 4},
  {"over the cell limit", "A,FR,1:X\nB,FR,1:Y\n", 1},
  {"too many ca entries", "A,FR,1:a,1:b,1:c,1:d,1:e,1:f,1:g,1:h,1:i\n", 4},
};

START_TEST(cells_file_rejects_bad_lines_and_overflow) {
  const bad_pkg_t *c = &bad_cells[_i];
  char path[160];
  sds_cell_t cells[4];
  int count = -1;

  write_temp_file(path, ".csv", c->content);
  ck_assert_msg(input_load_cells(path, cells, c->max, &count) == -1, "%s", c->name);
  unlink(path);
}
END_TEST

START_TEST(cells_file_skips_blank_lines_and_reports_a_missing_file) {
  char path[160];
  sds_cell_t cells[4];
  int count = -1;

  write_temp_file(path, ".csv", "\nA,FR,1:X\n\nB,DE,1:Y\n");
  ck_assert_int_eq(input_load_cells(path, cells, 4, &count), 0);
  ck_assert_int_eq(count, 2);
  unlink(path);
  ck_assert_int_eq(input_load_cells("/nonexistent/dvbipitools_cells.csv", cells, 4, &count), -1);
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
  tcase_add_test(tc, xml_detects_package_discovery_root);
  tcase_add_test(tc, xml_detects_regionalisation_discovery_root);
  tcase_add_test(tc, xml_detects_rms_fus_discovery_root);
  tcase_add_test(tc, xml_rejects_unknown_root_element);
  tcase_add_test(tc, unrecognized_suffix_is_rejected);
  tcase_add_test(tc, missing_file_is_rejected);
  tcase_add_test(tc, packages_file_parses_id_name_lang_visible_and_services);
  tcase_add_test(tc, packages_file_rejects_missing_field);
  tcase_add_test(tc, packages_file_rejects_no_services);
  tcase_add_test(tc, cells_file_parses_id_country_and_ca_chain);
  tcase_add_test(tc, cells_file_rejects_no_ca_entries);
  tcase_add_test(tc, cells_file_rejects_bad_country_code);
  tcase_add_loop_test(tc, csv_multicast_uri_forms, 0, (int)(sizeof uri_cases / sizeof uri_cases[0]));
  tcase_add_test(tc, csv_skips_blank_lines_and_stops_at_the_service_limit);
  tcase_add_test(tc, m3u_reads_extinf_attributes_defaults_and_limits);
  tcase_add_test(tc, xspf_handles_missing_titles_truncation_and_broken_tracks);
  tcase_add_test(tc, xspf_stops_at_the_service_limit);
  tcase_add_loop_test(tc, packages_file_rejects_bad_lines_and_overflow, 0, (int)(sizeof bad_pkgs / sizeof bad_pkgs[0]));
  tcase_add_test(tc, packages_file_handles_blank_lines_invisible_and_service_overflow);
  tcase_add_loop_test(tc, cells_file_rejects_bad_lines_and_overflow, 0, (int)(sizeof bad_cells / sizeof bad_cells[0]));
  tcase_add_test(tc, cells_file_skips_blank_lines_and_reports_a_missing_file);
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
