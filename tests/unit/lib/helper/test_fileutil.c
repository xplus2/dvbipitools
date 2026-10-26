/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/helper/fileutil.h"

START_TEST(open_std_maps_dash_to_the_standard_streams) {
  FILE *f;
  ck_assert_ptr_eq(fileutil_open_std("-", "r"), stdin);
  ck_assert_ptr_eq(fileutil_open_std("-", "w"), stdout);
  ck_assert_ptr_null(fileutil_open_std("/nonexistent/dir/file", "r"));
  f = fileutil_open_std("/dev/null", "w");
  ck_assert_ptr_nonnull(f);
  fclose(f);
}
END_TEST

START_TEST(copy_out_writes_small_and_large_content) {
  char path[] = "/tmp/dvbipitools_test_fileutil_XXXXXX";
  static char big[40000];
  char back[40001];
  FILE *tmp = tmpfile();
  FILE *in;
  size_t n;
  int fd = mkstemp(path);
  ck_assert_int_ge(fd, 0);
  close(fd);
  ck_assert_ptr_nonnull(tmp);
  memset(big, 'q', sizeof big);
  ck_assert_uint_eq(fwrite(big, 1, sizeof big, tmp), sizeof big);
  ck_assert_int_eq(fileutil_copy_out(tmp, path), 0);
  in = fopen(path, "r");
  ck_assert_ptr_nonnull(in);
  n = fread(back, 1, sizeof back, in);
  fclose(in);
  ck_assert_uint_eq(n, sizeof big);
  ck_assert_mem_eq(back, big, sizeof big);
  fclose(tmp);
  unlink(path);
}
END_TEST

START_TEST(copy_out_reports_each_failure) {
  FILE *tmp = tmpfile();
  FILE *wo;
  int fds[2];
  FILE *pipef;
  ck_assert_ptr_nonnull(tmp);
  fputs("data", tmp);
  ck_assert_int_eq(fileutil_copy_out(tmp, "/nonexistent-dir/out"), -1);
  tmp = freopen(NULL, "w+", tmp);
  ck_assert_ptr_nonnull(tmp);
  fputs("data", tmp);
  ck_assert_int_eq(fileutil_copy_out(tmp, "/dev/full"), -2);
  fclose(tmp);

  wo = fopen("/dev/null", "w");
  ck_assert_ptr_nonnull(wo);
  ck_assert_int_eq(fileutil_copy_out(wo, "/dev/null"), -2);
  fclose(wo);

  ck_assert_int_eq(pipe(fds), 0);
  pipef = fdopen(fds[1], "w");
  ck_assert_ptr_nonnull(pipef);
  fputs("x", pipef);
  ck_assert_int_eq(fileutil_copy_out(pipef, "/dev/null"), -2);
  fclose(pipef);
  close(fds[0]);
}
END_TEST

static Suite *fileutil_suite(void) {
  Suite *s = suite_create("fileutil");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, open_std_maps_dash_to_the_standard_streams);
  tcase_add_test(tc, copy_out_writes_small_and_large_content);
  tcase_add_test(tc, copy_out_reports_each_failure);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(fileutil_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
