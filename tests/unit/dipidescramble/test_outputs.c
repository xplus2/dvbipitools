/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../log_capture.h"
#include "dipidescramble/outputs.h"

typedef struct {
  char dir[64];
  char a[128];
  char b[128];
  config_t cfg;
  loop_ctx_t lc;
} fx_t;

static void fx_open(fx_t *fx) {
  memset(fx, 0, sizeof *fx);
  snprintf(fx->dir, sizeof fx->dir, "/tmp/dscr_outputs_XXXXXX");
  ck_assert_ptr_nonnull(mkdtemp(fx->dir));
  snprintf(fx->a, sizeof fx->a, "%s/a.ts", fx->dir);
  snprintf(fx->b, sizeof fx->b, "%s/b.ts", fx->dir);
}

static void fx_add_file(fx_t *fx, const char *path) {
  out_target_t *o = &fx->cfg.out[fx->cfg.n_out++];

  memset(o, 0, sizeof *o);
  o->kind = OUT_FILE;
  snprintf(o->file_path, sizeof o->file_path, "%s", path);
}

static void fx_close(fx_t *fx) {
  unlink(fx->a);
  unlink(fx->b);
  rmdir(fx->dir);
}

static int fd_is_open(int fd) {
  return fcntl(fd, F_GETFD) != -1;
}

START_TEST(file_targets_are_created_in_order) {
  fx_t fx;
  int mkv_fd = 5;

  fx_open(&fx);
  fx_add_file(&fx, fx.a);
  fx_add_file(&fx, fx.b);
  ck_assert_int_eq(dscr_open_outputs(&fx.cfg, &fx.lc, &mkv_fd), 0);
  ck_assert_int_eq(fx.lc.n_outfd, 2);
  ck_assert_int_eq(mkv_fd, -1);
  ck_assert_int_eq(write(fx.lc.outfd[0], "A", 1), 1);
  ck_assert_int_eq(write(fx.lc.outfd[1], "B", 1), 1);
  dscr_close_outputs(&fx.lc, mkv_fd);
  ck_assert_int_eq(access(fx.a, R_OK), 0);
  ck_assert_int_eq(access(fx.b, R_OK), 0);
  fx_close(&fx);
}
END_TEST

START_TEST(close_releases_every_fd_but_never_stdout) {
  fx_t fx;
  int mkv_fd = -1;
  int opened;

  fx_open(&fx);
  fx_add_file(&fx, fx.a);
  fx_add_file(&fx, "-");
  ck_assert_int_eq(dscr_open_outputs(&fx.cfg, &fx.lc, &mkv_fd), 0);
  ck_assert_int_eq(fx.lc.outfd[1], STDOUT_FILENO);
  opened = fx.lc.outfd[0];
  dscr_close_outputs(&fx.lc, mkv_fd);
  ck_assert_int_eq(fd_is_open(opened), 0);
  ck_assert_int_eq(fd_is_open(STDOUT_FILENO), 1);
  fx_close(&fx);
}
END_TEST

START_TEST(mkv_formats_hand_the_single_file_fd_to_the_caller) {
  fx_t fx;
  int mkv_fd = -1;

  fx_open(&fx);
  fx.cfg.format = FMT_MKV;
  fx_add_file(&fx, fx.a);
  ck_assert_int_eq(dscr_open_outputs(&fx.cfg, &fx.lc, &mkv_fd), 0);
  ck_assert_int_eq(fx.lc.n_outfd, 0);
  ck_assert_int_ge(mkv_fd, 0);
  dscr_close_outputs(&fx.lc, mkv_fd);
  ck_assert_int_eq(fd_is_open(mkv_fd), 0);
  fx_close(&fx);

  fx_open(&fx);
  fx.cfg.format = FMT_MKA;
  fx_add_file(&fx, "-");
  mkv_fd = -1;
  ck_assert_int_eq(dscr_open_outputs(&fx.cfg, &fx.lc, &mkv_fd), 0);
  ck_assert_int_eq(mkv_fd, STDOUT_FILENO);
  dscr_close_outputs(&fx.lc, mkv_fd);
  ck_assert_int_eq(fd_is_open(STDOUT_FILENO), 1);
  fx_close(&fx);
}
END_TEST

START_TEST(an_unopenable_file_fails_and_is_logged) {
  fx_t fx;
  int mkv_fd = -1;
  char log[LOG_CAPTURE_BUF];

  fx_open(&fx);
  fx_add_file(&fx, "/nonexistent-dir-dipidescramble/out.ts");
  log_capture_begin();
  ck_assert_int_eq(dscr_open_outputs(&fx.cfg, &fx.lc, &mkv_fd), -1);
  log_capture_end(log, sizeof log);
  ck_assert_int_eq(log_count_of(log, "cannot open -o"), 1);
  fx_close(&fx);

  fx_open(&fx);
  fx.cfg.format = FMT_MKV;
  fx_add_file(&fx, "/nonexistent-dir-dipidescramble/out.mkv");
  ck_assert_int_eq(dscr_open_outputs(&fx.cfg, &fx.lc, &mkv_fd), -1);
  fx_close(&fx);
}
END_TEST

START_TEST(a_failing_later_target_leaves_earlier_fds_for_the_caller_to_close) {
  fx_t fx;
  int mkv_fd = -1;

  fx_open(&fx);
  fx_add_file(&fx, fx.a);
  fx_add_file(&fx, "/nonexistent-dir-dipidescramble/out.ts");
  ck_assert_int_eq(dscr_open_outputs(&fx.cfg, &fx.lc, &mkv_fd), -1);
  ck_assert_int_eq(fx.lc.n_outfd, 1);
  dscr_close_outputs(&fx.lc, mkv_fd);
  fx_close(&fx);
}
END_TEST

START_TEST(an_rtmp_target_opens_lazily_and_a_malformed_url_fails) {
  fx_t fx;
  int mkv_fd = -1;
  out_target_t *o;

  fx_open(&fx);
  o = &fx.cfg.out[fx.cfg.n_out++];
  o->kind = OUT_RTMP;
  snprintf(o->rtmp_url, sizeof o->rtmp_url, "rtmp://127.0.0.1:1/app/key");
  ck_assert_int_eq(dscr_open_outputs(&fx.cfg, &fx.lc, &mkv_fd), 0);
  ck_assert_int_eq(fx.lc.n_rtmp, 1);
  ck_assert_int_eq(fx.lc.rtmp_had_error[0], 0);
  dscr_close_outputs(&fx.lc, mkv_fd);
  fx_close(&fx);

  fx_open(&fx);
  o = &fx.cfg.out[fx.cfg.n_out++];
  o->kind = OUT_RTMP;
  snprintf(o->rtmp_url, sizeof o->rtmp_url, "not-a-url");
  ck_assert_int_eq(dscr_open_outputs(&fx.cfg, &fx.lc, &mkv_fd), -1);
  ck_assert_int_eq(fx.lc.n_rtmp, 0);
  fx_close(&fx);
}
END_TEST

START_TEST(an_srt_target_fails_cleanly_without_libsrt) {
  fx_t fx;
  int mkv_fd = -1;
  out_target_t *o;

  fx_open(&fx);
  o = &fx.cfg.out[fx.cfg.n_out++];
  o->kind = OUT_SRT;
  snprintf(o->srt_host, sizeof o->srt_host, "127.0.0.1");
  o->srt_port = 9001;
  ck_assert_int_eq(dscr_open_outputs(&fx.cfg, &fx.lc, &mkv_fd), -1);
  ck_assert_int_eq(fx.lc.n_srt, 0);
  fx_close(&fx);
}
END_TEST

static Suite *outputs_suite(void) {
  Suite *s = suite_create("dipidescramble_outputs");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, file_targets_are_created_in_order);
  tcase_add_test(tc, close_releases_every_fd_but_never_stdout);
  tcase_add_test(tc, mkv_formats_hand_the_single_file_fd_to_the_caller);
  tcase_add_test(tc, an_unopenable_file_fails_and_is_logged);
  tcase_add_test(tc, a_failing_later_target_leaves_earlier_fds_for_the_caller_to_close);
  tcase_add_test(tc, an_rtmp_target_opens_lazily_and_a_malformed_url_fails);
  tcase_add_test(tc, an_srt_target_fails_cleanly_without_libsrt);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(outputs_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
