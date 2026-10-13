/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dipixmltv/suggest.h"
#include "../log_capture.h"

START_TEST(suggest_map_classifies_exact_fuzzy_and_unmatched) {
  FILE *xmltv_f, *scan_f, *out_f;
  char *buf;
  size_t len;

  xmltv_f = tmpfile();
  ck_assert_ptr_nonnull(xmltv_f);
  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<tv>\n"
    "<channel id=\"channel1\"><display-name>Channel One</display-name></channel>\n"
    "<channel id=\"channel2\"><display-name>Channel 2</display-name></channel>\n"
    "<channel id=\"mystery\"><display-name>Mystery Channel</display-name></channel>\n"
    "</tv>\n",
    xmltv_f);
  rewind(xmltv_f);

  scan_f = tmpfile();
  ck_assert_ptr_nonnull(scan_f);
  fputs("Channel One,rtp://239.1.1.1:5000,1,2,101\nChannel 2 HD,rtp://239.1.1.2:5000,1,2,102\n", scan_f);
  rewind(scan_f);

  out_f = open_memstream(&buf, &len);
  ck_assert_int_eq(suggest_map(xmltv_f, scan_f, out_f), 0);
  fclose(out_f);
  fclose(xmltv_f);
  fclose(scan_f);

  ck_assert_ptr_nonnull(strstr(buf, "channel1,rtp://239.1.1.1:5000,1,2,101\n"));
  ck_assert_ptr_nonnull(strstr(buf, "# channel2 (Channel 2) -> closest: Channel 2 HD, rtp://239.1.1.2:5000,1,2,102\n"));
  ck_assert_ptr_nonnull(strstr(buf, "# UNMATCHED: mystery (Mystery Channel)\n"));

  free(buf);
}
END_TEST

START_TEST(suggest_map_fuzzy_prefers_smallest_length_difference) {
  FILE *xmltv_f = tmpfile(), *scan_f = tmpfile(), *out_f;
  char *buf;
  size_t len;
  ck_assert_ptr_nonnull(xmltv_f);
  ck_assert_ptr_nonnull(scan_f);
  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<tv>\n"
    "<channel id=\"alpha\"><display-name>Alpha</display-name></channel>\n</tv>\n",
    xmltv_f);
  rewind(xmltv_f);
  fputs("Alpha Sport Plus Extra,rtp://239.1.1.1:5000,1,2,101\n"
    "Alpha 1 HD,rtp://239.1.1.2:5000,1,2,102\n"
    "Alpha 2,rtp://239.1.1.3:5000,1,2,103\n"
    "Alpha 3,rtp://239.1.1.4:5000,1,2,104\n",
    scan_f);
  rewind(scan_f);
  out_f = open_memstream(&buf, &len);
  ck_assert_int_eq(suggest_map(xmltv_f, scan_f, out_f), 0);
  fclose(out_f);
  fclose(xmltv_f);
  fclose(scan_f);
  ck_assert_ptr_nonnull(strstr(buf, "-> closest: Alpha 2, rtp://239.1.1.3:5000,1,2,103\n"));
  free(buf);
}
END_TEST

START_TEST(suggest_map_ignores_empty_names) {
  FILE *xmltv_f, *scan_f, *out_f;
  char *buf;
  size_t len;

  xmltv_f = tmpfile();
  ck_assert_ptr_nonnull(xmltv_f);
  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<tv>\n"
    "<channel id=\"blank\"><display-name></display-name></channel>\n"
    "<channel id=\"channel1\"><display-name>Channel One</display-name></channel>\n"
    "</tv>\n",
    xmltv_f);
  rewind(xmltv_f);

  scan_f = tmpfile();
  ck_assert_ptr_nonnull(scan_f);
  fputs(",rtp://239.1.1.9:5000,1,2,109\nOther,rtp://239.1.1.3:5000,1,2,103\n", scan_f);
  rewind(scan_f);

  out_f = open_memstream(&buf, &len);
  ck_assert_int_eq(suggest_map(xmltv_f, scan_f, out_f), 0);
  fclose(out_f);
  fclose(xmltv_f);
  fclose(scan_f);

  ck_assert_ptr_nonnull(strstr(buf, "# UNMATCHED: blank ()\n"));
  ck_assert_ptr_nonnull(strstr(buf, "# UNMATCHED: channel1 (Channel One)\n"));
  ck_assert_ptr_null(strstr(buf, "closest"));
  ck_assert_ptr_null(strstr(buf, "109"));

  free(buf);
}
END_TEST

START_TEST(suggest_map_matches_names_with_commas) {
  FILE *xmltv_f = tmpfile(), *scan_f = tmpfile(), *out_f;
  char *buf;
  size_t len;
  ck_assert_ptr_nonnull(xmltv_f);
  ck_assert_ptr_nonnull(scan_f);
  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<tv>\n"
    "<channel id=\"c1\"><display-name>Foo, Bar TV</display-name></channel>\n</tv>\n",
    xmltv_f);
  rewind(xmltv_f);
  fputs("Foo Bar TV,rtp://239.1.1.1:5000,1,2,101\n", scan_f);
  rewind(scan_f);
  out_f = open_memstream(&buf, &len);
  ck_assert_int_eq(suggest_map(xmltv_f, scan_f, out_f), 0);
  fclose(out_f);
  fclose(xmltv_f);
  fclose(scan_f);
  ck_assert_ptr_nonnull(strstr(buf, "c1,rtp://239.1.1.1:5000,1,2,101\n"));
  ck_assert_ptr_null(strstr(buf, "closest"));
  free(buf);
}
END_TEST

static int suggest_with_scan(const char *scan, char *logbuf, size_t logcap) {
  FILE *xmltv_f = tmpfile(), *scan_f = tmpfile(), *out_f;
  char *buf;
  size_t len;
  int rc;
  ck_assert_ptr_nonnull(xmltv_f);
  ck_assert_ptr_nonnull(scan_f);
  fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<tv>\n<channel id=\"c1\"><display-name>One</display-name></channel>\n</tv>\n", xmltv_f);
  rewind(xmltv_f);
  fputs(scan, scan_f);
  rewind(scan_f);
  out_f = open_memstream(&buf, &len);
  log_capture_begin();
  rc = suggest_map(xmltv_f, scan_f, out_f);
  log_capture_end(logbuf, logcap);
  fclose(out_f);
  fclose(xmltv_f);
  fclose(scan_f);
  free(buf);
  return rc;
}

START_TEST(suggest_map_rejects_overlong_scan_line) {
  char logbuf[LOG_CAPTURE_BUF];
  char scan[1400];
  size_t n = (size_t)snprintf(scan, sizeof scan, "One,rtp://239.1.1.1:5000,1,2,3\n");
  memset(scan + n, 'x', 1100);
  strcpy(scan + n + 1100, "\nTwo,rtp://239.1.1.2:5000,1,2,3\n");
  ck_assert_int_eq(suggest_with_scan(scan, logbuf, sizeof logbuf), -1);
  ck_assert_int_eq(log_count_of(logbuf, "XS400207"), 1);
  ck_assert_ptr_nonnull(strstr(logbuf, "line 2:"));
}
END_TEST

START_TEST(suggest_map_rejects_overlong_scan_name) {
  char logbuf[LOG_CAPTURE_BUF];
  char scan[600];
  memset(scan, 'n', 256);
  strcpy(scan + 256, ",rtp://239.1.1.1:5000,1,2,3\n");
  ck_assert_int_eq(suggest_with_scan(scan, logbuf, sizeof logbuf), -1);
  ck_assert_int_eq(log_count_of(logbuf, "XS400208"), 1);
}
END_TEST

START_TEST(suggest_map_rejects_bad_scan_numbers) {
  char logbuf[LOG_CAPTURE_BUF];
  const char *bad[] = {"One,u,abc,2,3\n", "One,u,1,2,70000\n", "One,u,1,-2,3\n", "One,u,1,2,3x\n", "One,u,1,2,\n", "One,u,99999999999999999999,2,3\n"};
  for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
    ck_assert_int_eq(suggest_with_scan(bad[i], logbuf, sizeof logbuf), -1);
    ck_assert_int_eq(log_count_of(logbuf, "XS400209"), 1);
  }
  ck_assert_int_eq(suggest_with_scan("One,u,65535,0,3\n", logbuf, sizeof logbuf), 0);
  ck_assert_int_eq(suggest_with_scan("One,u\n", logbuf, sizeof logbuf), 0);
}
END_TEST

static Suite *suggest_suite(void) {
  Suite *s = suite_create("suggest");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, suggest_map_classifies_exact_fuzzy_and_unmatched);
  tcase_add_test(tc, suggest_map_fuzzy_prefers_smallest_length_difference);
  tcase_add_test(tc, suggest_map_ignores_empty_names);
  tcase_add_test(tc, suggest_map_matches_names_with_commas);
  tcase_add_test(tc, suggest_map_rejects_overlong_scan_line);
  tcase_add_test(tc, suggest_map_rejects_overlong_scan_name);
  tcase_add_test(tc, suggest_map_rejects_bad_scan_numbers);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(suggest_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
