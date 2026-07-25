/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dipixmltv/revmap.h"

static void write_temp_csv(char *path, const char *content) {
  int fd = mkstemp(path);
  FILE *f;
  ck_assert_int_ge(fd, 0);
  f = fdopen(fd, "w");
  fputs(content, f);
  fclose(f);
}

START_TEST(revmap_load_and_lookup_round_trips) {
  char path[] = "/tmp/dvbipitools_test_revmap_XXXXXX";
  revmap_t m;
  write_temp_csv(path, "# comment\n\n"
                  "rtp://239.1.1.1:5000,orf1\n"
                  "rtp://239.1.1.2:5000,orf2\n");

  ck_assert_int_eq(revmap_load(path, &m), 0);
  ck_assert_int_eq(m.count, 2);
  ck_assert_str_eq(revmap_lookup(&m, "rtp://239.1.1.1:5000"), "orf1");
  ck_assert_str_eq(revmap_lookup(&m, "rtp://239.1.1.2:5000"), "orf2");
  ck_assert_ptr_null(revmap_lookup(&m, "rtp://nope:5000"));

  revmap_free(&m);
  unlink(path);
}
END_TEST

START_TEST(revmap_load_id_may_contain_commas) {
  char path[] = "/tmp/dvbipitools_test_revmap_XXXXXX";
  revmap_t m;
  write_temp_csv(path, "rtp://239.1.1.1:5000,orf1,at,extra\n");

  ck_assert_int_eq(revmap_load(path, &m), 0);
  ck_assert_str_eq(revmap_lookup(&m, "rtp://239.1.1.1:5000"), "orf1,at,extra");

  revmap_free(&m);
  unlink(path);
}
END_TEST

START_TEST(revmap_load_rejects_malformed_line) {
  char path[] = "/tmp/dvbipitools_test_revmap_XXXXXX";
  revmap_t m;
  write_temp_csv(path, "no-comma-here\n");
  ck_assert_int_eq(revmap_load(path, &m), -1);
  unlink(path);
}
END_TEST

START_TEST(revmap_load_rejects_missing_file) {
  revmap_t m;
  ck_assert_int_eq(revmap_load("/nonexistent/dvbipitools_test_revmap.csv", &m), -1);
}
END_TEST

static Suite *revmap_suite(void) {
  Suite *s = suite_create("revmap");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, revmap_load_and_lookup_round_trips);
  tcase_add_test(tc, revmap_load_id_may_contain_commas);
  tcase_add_test(tc, revmap_load_rejects_malformed_line);
  tcase_add_test(tc, revmap_load_rejects_missing_file);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(revmap_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
