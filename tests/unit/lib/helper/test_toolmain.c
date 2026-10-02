/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "lib/helper/log.h"
#include "lib/helper/toolmain.h"

#define CAPTURE_MAX 512

static size_t capture_banner(log_color_t color, char *out, size_t cap) {
  int fds[2];
  int saved;
  ssize_t n;

  ck_assert_int_eq(pipe(fds), 0);
  saved = dup(STDERR_FILENO);
  ck_assert_int_ge(saved, 0);
  log_set_color(color);
  fflush(stderr);
  ck_assert_int_ge(dup2(fds[1], STDERR_FILENO), 0);
  toolmain_print_banner("tool", "1.2.3", "arch", "Debug", "static");
  fflush(stderr);
  ck_assert_int_ge(dup2(saved, STDERR_FILENO), 0);
  close(saved);
  close(fds[1]);
  n = read(fds[0], out, cap - 1);
  close(fds[0]);
  ck_assert_int_gt((int)n, 0);
  out[n] = '\0';
  return (size_t)n;
}

START_TEST(banner_prints_all_fields_plain_when_colors_are_off) {
  char out[CAPTURE_MAX];

  capture_banner(LOG_COLOR_NEVER, out, sizeof out);
  ck_assert_ptr_nonnull(strstr(out, "tool v1.2.3 arch Debug static\n"));
  ck_assert_ptr_null(strchr(out, '\033'));
}
END_TEST

START_TEST(banner_keeps_escape_sequences_when_colors_are_forced) {
  char out[CAPTURE_MAX];

  capture_banner(LOG_COLOR_ALWAYS, out, sizeof out);
  ck_assert_ptr_nonnull(strstr(out, "\033[1mtool\033[0m"));
  ck_assert_ptr_nonnull(strstr(out, "v1.2.3"));
  ck_assert_ptr_nonnull(strstr(out, "static"));
}
END_TEST

START_TEST(daemonize_not_requested_does_nothing) {
  pid_t before = getpid();

  ck_assert_int_eq(toolmain_daemonize(0, "tool"), 0);
  ck_assert_int_eq(getpid(), before);
}
END_TEST

START_TEST(daemonize_detaches_into_a_running_grandchild) {
  int fds[2];
  pid_t pid;
  int status = -1;
  struct pollfd pfd;
  char marker = 0;

  ck_assert_int_eq(pipe(fds), 0);
  pid = fork();
  ck_assert_int_ge(pid, 0);
  if (pid == 0) {
    close(fds[0]);
    if (toolmain_daemonize(1, "tool") != 0) _exit(3);
    if (write(fds[1], "k", 1) != 1) _exit(4);
    _exit(0);
  }
  close(fds[1]);
  ck_assert_int_eq(waitpid(pid, &status, 0), pid);
  ck_assert_int_eq(WIFEXITED(status), 1);
  ck_assert_int_eq(WEXITSTATUS(status), 0);

  pfd.fd = fds[0];
  pfd.events = POLLIN;
  pfd.revents = 0;
  ck_assert_int_eq(poll(&pfd, 1, 3000), 1);
  ck_assert_int_eq((int)read(fds[0], &marker, 1), 1);
  ck_assert_int_eq(marker, 'k');
  close(fds[0]);
}
END_TEST

static Suite *toolmain_suite(void) {
  Suite *s = suite_create("toolmain");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, banner_prints_all_fields_plain_when_colors_are_off);
  tcase_add_test(tc, banner_keeps_escape_sequences_when_colors_are_forced);
  tcase_add_test(tc, daemonize_not_requested_does_nothing);
  tcase_add_test(tc, daemonize_detaches_into_a_running_grandchild);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(toolmain_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
