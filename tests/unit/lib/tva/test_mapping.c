/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/tva/mapping.h"

static void write_temp_csv(char *path, const char *content) {
  int fd = mkstemp(path);
  FILE *f;
  ck_assert_int_ge(fd, 0);
  f = fdopen(fd, "w");
  fputs(content, f);
  fclose(f);
}

START_TEST(mapping_load_and_lookup_round_trips) {
  char path[] = "/tmp/dvbipitools_test_mapping_XXXXXX";
  mapping_t m;
  char uri[128];
  unsigned tsid, onid, sid;

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

START_TEST(mapping_load_rejects_missing_file) {
  mapping_t m;
  ck_assert_int_eq(mapping_load("/nonexistent/dvbipitools_test_mapping.csv", &m), -1);
}
END_TEST

static Suite *mapping_suite(void) {
  Suite *s = suite_create("mapping");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, mapping_load_and_lookup_round_trips);
  tcase_add_test(tc, mapping_load_id_may_contain_commas);
  tcase_add_test(tc, mapping_load_rejects_malformed_line);
  tcase_add_test(tc, mapping_load_rejects_missing_file);
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
