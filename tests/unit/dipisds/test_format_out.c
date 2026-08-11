/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dipisds/format_out.h"

static sds_service_t make_service(const char *name, const char *addr, int family, unsigned port, int rtp) {
  sds_service_t s;
  memset(&s, 0, sizeof s);
  snprintf(s.name, sizeof s.name, "%s", name);
  snprintf(s.address, sizeof s.address, "%s", addr);
  s.family = family;
  s.port = port;
  s.rtp = rtp;
  s.tsid = 1;
  s.onid = 2;
  s.sid = 101;
  return s;
}

static char *read_all_and_close(FILE *f) {
  static char buf[1024];
  size_t n;
  rewind(f);
  n = fread(buf, 1, sizeof buf - 1, f);
  buf[n] = '\0';
  fclose(f);
  return buf;
}

START_TEST(m3u_item_writes_extinf_and_uri_line) {
  FILE *f = tmpfile();
  sds_service_t s = make_service("Channel One", "239.1.1.1", AF_INET, 5000, 1);
  ck_assert_ptr_nonnull(f);
  format_out_item(f, OUT_M3U, &s);
  ck_assert_str_eq(read_all_and_close(f),
                    "#EXTINF:-1 tsid=\"1\" onid=\"2\" sid=\"101\",Channel One\n"
                    "rtp://@239.1.1.1:5000\n");
}
END_TEST

START_TEST(m3u_item_brackets_ipv6_address) {
  FILE *f = tmpfile();
  sds_service_t s = make_service("Channel One", "ff15::1", AF_INET6, 5000, 0);
  ck_assert_ptr_nonnull(f);
  format_out_item(f, OUT_M3U, &s);
  ck_assert_str_eq(read_all_and_close(f),
                    "#EXTINF:-1 tsid=\"1\" onid=\"2\" sid=\"101\",Channel One\n"
                    "udp://@[ff15::1]:5000\n");
}
END_TEST

START_TEST(csv_item_strips_commas_from_name) {
  FILE *f = tmpfile();
  sds_service_t s = make_service("Chan, One", "239.1.1.1", AF_INET, 5000, 1);
  ck_assert_ptr_nonnull(f);
  format_out_item(f, OUT_CSV, &s);
  ck_assert_str_eq(read_all_and_close(f), "Chan One,rtp://@239.1.1.1:5000,1,2,101\n");
}
END_TEST

START_TEST(xspf_item_escapes_title_and_writes_extension) {
  FILE *f = tmpfile();
  sds_service_t s = make_service("A & B", "239.1.1.1", AF_INET, 5000, 1);
  ck_assert_ptr_nonnull(f);
  format_out_item(f, OUT_XSPF, &s);
  ck_assert_str_eq(read_all_and_close(f),
                    "  <track><location>rtp://@239.1.1.1:5000</location><title>A &amp; B</title>"
                    "<extension application=\"urn:dvbipitools:dvb-triplet\" tsid=\"1\" onid=\"2\" sid=\"101\"/></track>\n");
}
END_TEST

START_TEST(xml_and_null_item_write_nothing) {
  FILE *f = tmpfile();
  sds_service_t s = make_service("Channel One", "239.1.1.1", AF_INET, 5000, 1);
  ck_assert_ptr_nonnull(f);
  format_out_item(f, OUT_XML, &s);
  format_out_item(f, OUT_NULL, &s);
  ck_assert_str_eq(read_all_and_close(f), "");
}
END_TEST

START_TEST(raw_passthrough_only_for_xml_format) {
  FILE *f = tmpfile();
  const unsigned char data[] = "<BroadcastDiscovery/>";
  ck_assert_ptr_nonnull(f);
  format_out_raw(f, OUT_XML, data, sizeof data - 1);
  ck_assert_str_eq(read_all_and_close(f), "<BroadcastDiscovery/>\n");
}
END_TEST

START_TEST(raw_passthrough_ignored_for_non_xml_format) {
  FILE *f = tmpfile();
  const unsigned char data[] = "<BroadcastDiscovery/>";
  ck_assert_ptr_nonnull(f);
  format_out_raw(f, OUT_M3U, data, sizeof data - 1);
  ck_assert_str_eq(read_all_and_close(f), "");
}
END_TEST

START_TEST(m3u_init_and_close_write_header_and_footer) {
  FILE *f = tmpfile();
  char *out;
  ck_assert_ptr_nonnull(f);
  format_out_init(f, OUT_M3U, "dipisds --listen");
  format_out_close(f, OUT_M3U);
  out = read_all_and_close(f);
  ck_assert_ptr_nonnull(strstr(out, "#EXTM3U"));
}
END_TEST

START_TEST(csv_init_and_close_write_nothing) {
  FILE *f = tmpfile();
  ck_assert_ptr_nonnull(f);
  format_out_init(f, OUT_CSV, "dipisds --listen");
  format_out_close(f, OUT_CSV);
  ck_assert_str_eq(read_all_and_close(f), "");
}
END_TEST

static Suite *format_out_suite(void) {
  Suite *s = suite_create("dipisds_format_out");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, m3u_item_writes_extinf_and_uri_line);
  tcase_add_test(tc, m3u_item_brackets_ipv6_address);
  tcase_add_test(tc, csv_item_strips_commas_from_name);
  tcase_add_test(tc, xspf_item_escapes_title_and_writes_extension);
  tcase_add_test(tc, xml_and_null_item_write_nothing);
  tcase_add_test(tc, raw_passthrough_only_for_xml_format);
  tcase_add_test(tc, raw_passthrough_ignored_for_non_xml_format);
  tcase_add_test(tc, m3u_init_and_close_write_header_and_footer);
  tcase_add_test(tc, csv_init_and_close_write_nothing);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(format_out_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
