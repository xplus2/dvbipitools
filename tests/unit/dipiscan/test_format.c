/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipiscan/format.h"
#include "lib/sds_xml.h"

static FILE *open_capture(char **buf, size_t *len) {
  return open_memstream(buf, len);
}

START_TEST(format_m3u_writes_header_item_footer) {
  char *buf;
  size_t len;
  FILE *f = open_capture(&buf, &len);

  format_init(f, OUT_M3U, "dipiscan -f m3u", NULL);
  format_item(f, OUT_M3U, "Channel One", "rtp://239.1.1.1:5000", AF_INET, "239.1.1.1", 5000, 1, 1, 2, 101);
  format_close(f, OUT_M3U);
  fclose(f);

  ck_assert_ptr_nonnull(strstr(buf, "#EXTM3U"));
  ck_assert_ptr_nonnull(strstr(buf, "tsid=\"1\" onid=\"2\" sid=\"101\",Channel One"));
  ck_assert_ptr_nonnull(strstr(buf, "rtp://239.1.1.1:5000"));
  ck_assert_ptr_nonnull(strstr(buf, "#EXT-X-ENDLIST"));
  free(buf);
}
END_TEST

START_TEST(format_csv_strips_commas_from_name) {
  char *buf;
  size_t len;
  FILE *f = open_capture(&buf, &len);

  format_init(f, OUT_CSV, "inv", NULL);
  format_item(f, OUT_CSV, "News, Weather", "rtp://239.1.1.1:5000", AF_INET, "239.1.1.1", 5000, 1, 1, 2, 101);
  format_close(f, OUT_CSV);
  fclose(f);

  ck_assert_str_eq(buf, "News Weather,rtp://239.1.1.1:5000,1,2,101\n");
  free(buf);
}
END_TEST

START_TEST(format_xspf_escapes_and_wraps_playlist) {
  char *buf;
  size_t len;
  FILE *f = open_capture(&buf, &len);

  format_init(f, OUT_XSPF, "inv", NULL);
  format_item(f, OUT_XSPF, "News & Weather", "rtp://239.1.1.1:5000", AF_INET, "239.1.1.1", 5000, 1, 1, 2, 101);
  format_close(f, OUT_XSPF);
  fclose(f);

  ck_assert_ptr_nonnull(strstr(buf, "<playlist version=\"1\""));
  ck_assert_ptr_nonnull(strstr(buf, "<location>rtp://239.1.1.1:5000</location>"));
  ck_assert_ptr_nonnull(strstr(buf, "<title>News &amp; Weather</title>"));
  ck_assert_ptr_nonnull(strstr(buf, "</playlist>"));
  free(buf);
}
END_TEST

START_TEST(format_xml_produces_parseable_sds_broadcast_doc) {
  char *buf;
  size_t len;
  FILE *f = open_capture(&buf, &len);
  sds_service_t out[4];
  int n;

  format_init(f, OUT_XML, "inv", "example.invalid");
  format_item(f, OUT_XML, "Channel One", "unused", AF_INET, "239.1.1.1", 5000, 1, 1, 2, 101);
  format_item(f, OUT_XML, "Channel 2 HD", "unused", AF_INET, "239.1.1.2", 5001, 0, 1, 2, 102);
  format_close(f, OUT_XML);
  fclose(f);

  n = sds_parse_broadcast(buf, out, 4);
  ck_assert_int_eq(n, 2);
  ck_assert_str_eq(out[0].name, "Channel One");
  ck_assert_str_eq(out[0].address, "239.1.1.1");
  ck_assert_uint_eq(out[0].port, 5000u);
  ck_assert_int_eq(out[0].rtp, 1);
  ck_assert_str_eq(out[1].name, "Channel 2 HD");
  ck_assert_uint_eq(out[1].sid, 102u);
  ck_assert_int_eq(out[1].rtp, 0);
  free(buf);
}
END_TEST

START_TEST(format_null_produces_no_output) {
  char *buf;
  size_t len;
  FILE *f = open_capture(&buf, &len);

  format_init(f, OUT_NULL, "inv", NULL);
  format_item(f, OUT_NULL, "X", "uri", AF_INET, "239.1.1.1", 5000, 1, 1, 1, 1);
  format_close(f, OUT_NULL);
  fclose(f);

  ck_assert_uint_eq(len, 0u);
  free(buf);
}
END_TEST

static Suite *format_suite(void) {
  Suite *s = suite_create("dipiscan_format");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, format_m3u_writes_header_item_footer);
  tcase_add_test(tc, format_csv_strips_commas_from_name);
  tcase_add_test(tc, format_xspf_escapes_and_wraps_playlist);
  tcase_add_test(tc, format_xml_produces_parseable_sds_broadcast_doc);
  tcase_add_test(tc, format_null_produces_no_output);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(format_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
