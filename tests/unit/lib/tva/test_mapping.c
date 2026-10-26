/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../alloc_fail.h"
#include "lib/tva/mapping.h"
#include "../../log_capture.h"
#include "../../cfg_fixture.h"

START_TEST(mapping_load_accepts_quoted_fields) {
  char path[] = "/tmp/dvbipitools_test_mapping_XXXXXX";
  mapping_t m;
  char uri[128];
  unsigned tsid, onid, sid;

  write_temp_csv(path, "\"a,b\",\"rtp://h/x,y\",1,2,3\n"
                       "\"q\"\"x\",rtp://h/z,4,5,6\n"
                       "legacy,id,rtp://h/w,7,8,9\n");
  ck_assert_int_eq(mapping_load(path, &m), 0);
  ck_assert_int_eq(mapping_lookup(&m, "a,b", uri, sizeof uri, &tsid, &onid, &sid), 0);
  ck_assert_str_eq(uri, "rtp://h/x,y");
  ck_assert_uint_eq(sid, 3u);
  ck_assert_int_eq(mapping_lookup(&m, "q\"x", uri, sizeof uri, &tsid, &onid, &sid), 0);
  ck_assert_str_eq(uri, "rtp://h/z");
  ck_assert_int_eq(mapping_lookup(&m, "legacy,id", uri, sizeof uri, &tsid, &onid, &sid), 0);
  ck_assert_str_eq(uri, "rtp://h/w");
  mapping_free(&m);
  unlink(path);
}
END_TEST

START_TEST(mapping_load_rejects_unterminated_quote) {
  char path[] = "/tmp/dvbipitools_test_mapping_XXXXXX";
  mapping_t m;
  write_temp_csv(path, "\"abc,rtp://h/x,1,2,3\n");
  ck_assert_int_ne(mapping_load(path, &m), 0);
  unlink(path);
}
END_TEST

START_TEST(mapping_load_and_lookup_round_trips) {
  char path[] = "/tmp/dvbipitools_test_mapping_XXXXXX";
  mapping_t m;
  char uri[128];
  unsigned tsid;
  unsigned onid;
  unsigned sid;

  write_temp_csv(path, "# comment line, ignored\n\n"
                  "channel1,rtp://239.1.1.1:5000,1,2,101\n"
                  "channel2,rtp://239.1.1.2:5000,1,2,102\n");

  ck_assert_int_eq(mapping_load(path, &m), 0);
  ck_assert_int_eq(m.count, 2);

  ck_assert_int_eq(mapping_lookup(&m, "channel1", uri, sizeof uri, &tsid, &onid, &sid), 0);
  ck_assert_str_eq(uri, "rtp://239.1.1.1:5000");
  ck_assert_uint_eq(tsid, 1u);
  ck_assert_uint_eq(onid, 2u);
  ck_assert_uint_eq(sid, 101u);

  ck_assert_int_eq(mapping_lookup(&m, "channel2", uri, sizeof uri, &tsid, &onid, &sid), 0);
  ck_assert_uint_eq(sid, 102u);

  ck_assert_int_eq(mapping_lookup(&m, "nope", uri, sizeof uri, &tsid, &onid, &sid), -1);

  mapping_free(&m);
  unlink(path);
}
END_TEST

START_TEST(mapping_load_id_may_contain_commas) {
  char path[] = "/tmp/dvbipitools_test_mapping_XXXXXX";
  mapping_t m;
  char uri[128];
  unsigned tsid, onid, sid;

  /* split_last4 anchors on the last 4 commas, so the id itself may contain commas */
  write_temp_csv(path, "Channel, One,rtp://239.1.1.1:5000,1,2,101\n");

  ck_assert_int_eq(mapping_load(path, &m), 0);
  ck_assert_int_eq(mapping_lookup(&m, "Channel, One", uri, sizeof uri, &tsid, &onid, &sid), 0);
  ck_assert_str_eq(uri, "rtp://239.1.1.1:5000");

  mapping_free(&m);
  unlink(path);
}
END_TEST

START_TEST(mapping_load_rejects_malformed_line) {
  char path[] = "/tmp/dvbipitools_test_mapping_XXXXXX";
  mapping_t m;
  write_temp_csv(path, "not,enough,fields\n");
  ck_assert_int_eq(mapping_load(path, &m), -1);
  unlink(path);
}
END_TEST

START_TEST(mapping_load_rejects_bad_tsid) {
  char path[] = "/tmp/dvbipitools_test_mapping_XXXXXX";
  mapping_t m;
  write_temp_csv(path, "channel1,rtp://239.1.1.1:5000,abc,2,101\n");
  ck_assert_int_eq(mapping_load(path, &m), -1);
  unlink(path);
}
END_TEST

START_TEST(mapping_load_rejects_missing_file) {
  mapping_t m;
  ck_assert_int_eq(mapping_load("/nonexistent/dvbipitools_test_mapping.csv", &m), -1);
}
END_TEST

START_TEST(mapping_duplicate_id_first_wins_and_warns) {
  char path[] = "/tmp/dvbipitools_test_mapping_XXXXXX";
  char logbuf[LOG_CAPTURE_BUF];
  char uri[64];
  unsigned tsid;
  unsigned onid;
  unsigned sid;
  mapping_t m;
  write_temp_csv(path, "a,uri1,1,1,1\nb,uriB,2,2,2\na,uri2,3,3,3\na,uri3,4,4,4\n");

  log_capture_begin();
  ck_assert_int_eq(mapping_load(path, &m), 0);
  log_capture_end(logbuf, sizeof logbuf);
  ck_assert_int_eq(log_count_of(logbuf, "LW700163"), 2);
  ck_assert_int_eq(mapping_lookup(&m, "a", uri, sizeof uri, &tsid, &onid, &sid), 0);
  ck_assert_str_eq(uri, "uri1");
  mapping_free(&m);
  unlink(path);
}
END_TEST

START_TEST(mapping_load_rejects_overlong_line_with_right_lineno) {
  char path[] = "/tmp/dvbipitools_test_mapping_XXXXXX";
  char logbuf[LOG_CAPTURE_BUF];
  char content[1400];
  mapping_t m;
  size_t n = (size_t)snprintf(content, sizeof content, "a,uri1,1,1,1\n");
  memset(content + n, 'x', 1100);
  strcpy(content + n + 1100, "\nb,uri2,2,2,2\n");
  write_temp_csv(path, content);

  log_capture_begin();
  ck_assert_int_eq(mapping_load(path, &m), -1);
  log_capture_end(logbuf, sizeof logbuf);
  ck_assert_int_eq(log_count_of(logbuf, "LE400205"), 1);
  ck_assert_ptr_nonnull(strstr(logbuf, "line 2:"));
  ck_assert_ptr_null(strstr(logbuf, "LE000327"));
  unlink(path);
}
END_TEST

START_TEST(mapping_load_id_length_limit) {
  char path[] = "/tmp/dvbipitools_test_mapping_XXXXXX";
  char logbuf[LOG_CAPTURE_BUF];
  char content[600];
  char uri[64];
  unsigned tsid;
  unsigned onid;
  unsigned sid;
  mapping_t m;
  size_t n;

  memset(content, 'i', 255);
  strcpy(content + 255, ",uri1,1,1,1\n");
  write_temp_csv(path, content);
  ck_assert_int_eq(mapping_load(path, &m), 0);
  content[255] = '\0';
  ck_assert_int_eq(mapping_lookup(&m, content, uri, sizeof uri, &tsid, &onid, &sid), 0);
  mapping_free(&m);
  unlink(path);

  memset(content, 'i', 256);
  n = 256;
  strcpy(content + n, ",uri1,1,1,1\n");
  strcpy(path, "/tmp/dvbipitools_test_mapping_XXXXXX");
  write_temp_csv(path, content);
  log_capture_begin();
  ck_assert_int_eq(mapping_load(path, &m), -1);
  log_capture_end(logbuf, sizeof logbuf);
  ck_assert_int_eq(log_count_of(logbuf, "LE400206"), 1);
  unlink(path);
}
END_TEST

START_TEST(mapping_survives_allocation_failures_and_writes_rows) {
  char path[] = "/tmp/dvbipitools_test_mapping_XXXXXX";
  mapping_t m;
  char uri[128];
  unsigned tsid, onid, sid;
  bcg_channel_t c;
  char *buf = NULL;
  size_t sz = 0;
  FILE *f;
  int rc;
  write_temp_csv(path, "a,rtp://h/a,1,2,3\nb,rtp://h/b,4,5,6\n\"q\",\"x\"junk,1,2,3\n");
  ck_assert_int_ne(mapping_load(path, &m), 0);
  unlink(path);
  strcpy(path, "/tmp/dvbipitools_test_mapping_XXXXXX");
  write_temp_csv(path, "a,rtp://h/a,1,2,3\nb,rtp://h/b,4,5,6\n");
  for (long n = 0;; n++) {
    int hit;
    alloc_fail_at(n);
    rc = mapping_load(path, &m);
    hit = alloc_fail_end();
    if (!rc) {
      ck_assert_int_eq(mapping_lookup(&m, "b", uri, sizeof uri, &tsid, &onid, &sid), 0);
      ck_assert_uint_eq(sid, 6u);
      ck_assert_int_eq(mapping_lookup(&m, "zz", uri, sizeof uri, &tsid, &onid, &sid), -1);
      mapping_free(&m);
    }
    if (!hit) break;
  }
  unlink(path);
  memset(&c, 0, sizeof c);
  snprintf(c.id, sizeof c.id, "plain");
  snprintf(c.uri, sizeof c.uri, "rtp://h/p");
  c.tsid = 1;
  c.onid = 2;
  c.sid = 3;
  f = open_memstream(&buf, &sz);
  ck_assert_ptr_nonnull(f);
  mapping_write_row(f, &c);
  snprintf(c.id, sizeof c.id, "we\"ird,id");
  mapping_write_row(f, &c);
  snprintf(c.id, sizeof c.id, "ok");
  snprintf(c.uri, sizeof c.uri, "rtp://h/a,b");
  mapping_write_row(f, &c);
  fclose(f);
  ck_assert_ptr_nonnull(strstr(buf, "plain,rtp://h/p,1,2,3\n"));
  ck_assert_ptr_nonnull(strstr(buf, "\"we\"\"ird,id\",\"rtp://h/p\",1,2,3\n"));
  ck_assert_ptr_nonnull(strstr(buf, "\"ok\",\"rtp://h/a,b\",1,2,3\n"));
  free(buf);
}
END_TEST

static Suite *mapping_suite(void) {
  Suite *s = suite_create("mapping");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, mapping_load_and_lookup_round_trips);
  tcase_add_test(tc, mapping_load_accepts_quoted_fields);
  tcase_add_test(tc, mapping_load_rejects_unterminated_quote);
  tcase_add_test(tc, mapping_load_id_may_contain_commas);
  tcase_add_test(tc, mapping_load_rejects_malformed_line);
  tcase_add_test(tc, mapping_load_rejects_bad_tsid);
  tcase_add_test(tc, mapping_load_rejects_missing_file);
  tcase_add_test(tc, mapping_load_rejects_overlong_line_with_right_lineno);
  tcase_add_test(tc, mapping_load_id_length_limit);
  tcase_add_test(tc, mapping_survives_allocation_failures_and_writes_rows);
  tcase_add_test(tc, mapping_duplicate_id_first_wins_and_warns);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(mapping_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
