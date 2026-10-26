/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE
#include <check.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/helper/argutil.h"
#include "lib/helper/log.h"
#include "../../log_capture.h"

#define OUT_MAX 1024

static const char *body(const char *s) {
  static const char pat[] = "dddddddd dddddd ";
  char pid[32];
  size_t n;

  for (size_t i = 0; pat[i]; i++) {
    if (pat[i] == 'd' ? (s[i] < '0' || s[i] > '9') : s[i] != pat[i]) return NULL;
  }
  n = (size_t)snprintf(pid, sizeof pid, "%ld ", (long)getpid());
  if (strncmp(s + 16, pid, n) != 0) return NULL;
  return s + 16 + n;
}

START_TEST(log_code_puts_code_after_timestamp) {
  char out[OUT_MAX];

  log_capture_begin();
  log_code("TE500001", "bind %s: %d", "x", 7);
  log_capture_end(out, sizeof out);
  ck_assert_ptr_nonnull(body(out));
  ck_assert_str_eq(body(out), "[TE500001] bind x: 7\n");
}
END_TEST

START_TEST(log_line_stays_uncoded) {
  char out[OUT_MAX];

  log_capture_begin();
  log_line("plain %d", 1);
  log_capture_end(out, sizeof out);
  ck_assert_ptr_nonnull(body(out));
  ck_assert_str_eq(body(out), "plain 1\n");
}
END_TEST

START_TEST(log_code_ansi_strips_escapes_when_colors_are_off) {
  char out[OUT_MAX];

  log_set_color(LOG_COLOR_NEVER);
  log_capture_begin();
  log_code_ansi("LW400001", LOG_RED "bad" LOG_RESET " pid %d", 5);
  log_capture_end(out, sizeof out);
  ck_assert_str_eq(body(out), "[LW400001] bad pid 5\n");
}
END_TEST

START_TEST(log_code_ansi_keeps_escapes_when_colors_are_forced) {
  char out[OUT_MAX];

  log_set_color(LOG_COLOR_ALWAYS);
  log_capture_begin();
  log_code_ansi("LW400001", LOG_RED "bad" LOG_RESET);
  log_capture_end(out, sizeof out);
  log_set_color(LOG_COLOR_AUTO);
  ck_assert_ptr_nonnull(strstr(out, "[LW400001] \033[31mbad\033[0m\n"));
}
END_TEST

START_TEST(log_code_null_code_prints_no_brackets) {
  char out[OUT_MAX];

  log_capture_begin();
  log_code_ansi(NULL, "info");
  log_capture_end(out, sizeof out);
  ck_assert_str_eq(body(out), "info\n");
}
END_TEST

START_TEST(throttled_logs_first_and_suppresses_rest_in_window) {
  char out[OUT_MAX];
  log_throttle_t t;

  memset(&t, 0, sizeof t);
  log_capture_begin();
  log_throttled(&t, 60, "LW500001", "drop %d", 1);
  log_throttled(&t, 60, "LW500001", "drop %d", 2);
  log_throttled(&t, 60, "LW500001", "drop %d", 3);
  log_capture_end(out, sizeof out);
  ck_assert_int_eq(log_count_of(out, "[LW500001] drop 1\n"), 1);
  ck_assert_ptr_null(strstr(out, "drop 2"));
  ck_assert_uint_eq(atomic_load(&t.suppressed), 2u);
}
END_TEST

START_TEST(throttled_reports_suppressed_count_on_next_window) {
  char out[OUT_MAX];
  log_throttle_t t;

  memset(&t, 0, sizeof t);
  atomic_store(&t.suppressed, 4u);
  log_capture_begin();
  log_throttled(&t, 60, "LW500002", "drop");
  log_capture_end(out, sizeof out);
  ck_assert_ptr_nonnull(strstr(out, "[LW500002] drop (4 more in 60s)\n"));
}
END_TEST

START_TEST(argutil_err_puts_code_first) {
  char out[OUT_MAX];

  log_capture_begin();
  argutil_err("tool", "TS000001", "bad -x: %s", "y");
  log_capture_end(out, sizeof out);
  ck_assert_str_eq(out, "[TS000001] tool: bad -x: y\n");
}
END_TEST

START_TEST(argutil_err_null_code_keeps_plain_form) {
  char out[OUT_MAX];

  log_capture_begin();
  argutil_err("tool", NULL, "ok");
  log_capture_end(out, sizeof out);
  ck_assert_str_eq(out, "tool: ok\n");
}
END_TEST

START_TEST(log_ansi_strip_handles_every_escape_shape) {
  static const struct {
    const char *in;
    const char *want;
  } cases[] = {
    {"a\033[31mred\033[0mb", "aredb"}, {"a\033Mb", "ab"}, {"a\033", "a"}, {"a\033[12", "a"}, {"a\033[", "a"}, {"\033[1;2;3m", ""}, {"plain", "plain"},
  };
  char out[OUT_MAX];
  log_set_color(LOG_COLOR_NEVER);
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    log_capture_begin();
    log_line_ansi("%s", cases[i].in);
    log_capture_end(out, sizeof out);
    ck_assert_ptr_nonnull(body(out));
    ck_assert_msg(!strncmp(body(out), cases[i].want, strlen(cases[i].want)) && body(out)[strlen(cases[i].want)] == '\n', "case %zu: %s", i, body(out));
  }
}
END_TEST

START_TEST(log_color_prescan_reads_both_spellings) {
  char *a1[] = {"t", "--color", "always"};
  char *a2[] = {"t", "--color=never"};
  char *a3[] = {"t", "--color=auto"};
  char *a4[] = {"t", "--color"};
  char *a5[] = {"t", "x", "--color=always", "--color=never"};
  char *a6[] = {"t", "--color", "bogus"};
  ck_assert_int_eq(log_color_prescan(3, a1), LOG_COLOR_ALWAYS);
  ck_assert_int_eq(log_color_prescan(2, a2), LOG_COLOR_NEVER);
  ck_assert_int_eq(log_color_prescan(2, a3), LOG_COLOR_AUTO);
  ck_assert_int_eq(log_color_prescan(2, a4), LOG_COLOR_AUTO);
  ck_assert_int_eq(log_color_prescan(4, a5), LOG_COLOR_ALWAYS);
  ck_assert_int_eq(log_color_prescan(3, a6), LOG_COLOR_AUTO);
  ck_assert_int_eq(log_color_prescan(1, a6), LOG_COLOR_AUTO);
}
END_TEST

START_TEST(log_colors_follow_terminal_and_environment) {
  static const struct {
    const char *term;
    int no_color;
    int want;
  } cases[] = {{"xterm", 0, 1}, {"xterm", 1, 0}, {"dumb", 0, 0}, {NULL, 0, 1}};
  int master = posix_openpt(O_RDWR | O_NOCTTY);
  int slave;
  ck_assert_int_ge(master, 0);
  ck_assert_int_eq(grantpt(master), 0);
  ck_assert_int_eq(unlockpt(master), 0);
  slave = open(ptsname(master), O_RDWR | O_NOCTTY);
  ck_assert_int_ge(slave, 0);
  ck_assert_int_ge(dup2(slave, STDERR_FILENO), 0);
  unsetenv("NO_COLOR");
  if (cases[_i].term) setenv("TERM", cases[_i].term, 1);
  else unsetenv("TERM");
  if (cases[_i].no_color) setenv("NO_COLOR", "1", 1);
  log_set_color(LOG_COLOR_AUTO);
  ck_assert_int_eq(log_stderr_is_tty(), 1);
  ck_assert_int_eq(log_colors_enabled(), cases[_i].want);
  ck_assert_int_eq(log_colors_enabled(), cases[_i].want);
  log_set_color(LOG_COLOR_ALWAYS);
  ck_assert_int_eq(log_colors_enabled(), 1);
  log_set_color(LOG_COLOR_NEVER);
  ck_assert_int_eq(log_colors_enabled(), 0);
}
END_TEST

static Suite *log_suite(void) {
  Suite *s = suite_create("log");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, log_code_puts_code_after_timestamp);
  tcase_add_test(tc, log_line_stays_uncoded);
  tcase_add_test(tc, log_code_ansi_strips_escapes_when_colors_are_off);
  tcase_add_test(tc, log_code_ansi_keeps_escapes_when_colors_are_forced);
  tcase_add_test(tc, log_code_null_code_prints_no_brackets);
  tcase_add_test(tc, throttled_logs_first_and_suppresses_rest_in_window);
  tcase_add_test(tc, throttled_reports_suppressed_count_on_next_window);
  tcase_add_test(tc, argutil_err_puts_code_first);
  tcase_add_test(tc, argutil_err_null_code_keeps_plain_form);
  tcase_add_test(tc, log_ansi_strip_handles_every_escape_shape);
  tcase_add_test(tc, log_color_prescan_reads_both_spellings);
  tcase_add_loop_test(tc, log_colors_follow_terminal_and_environment, 0, 4);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(log_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
