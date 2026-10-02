/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_TESTS_UNIT_LOG_CAPTURE_H
#define DVBIPITOOLS_TESTS_UNIT_LOG_CAPTURE_H

#include <check.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define LOG_CAPTURE_BUF 8192

static int g_log_pipe[2];
static int g_log_saved;

static inline void log_capture_begin(void) {
  ck_assert_int_eq(pipe(g_log_pipe), 0);
  ck_assert_int_eq(fcntl(g_log_pipe[0], F_SETFL, O_NONBLOCK), 0);
  g_log_saved = dup(STDERR_FILENO);
  ck_assert_int_ge(g_log_saved, 0);
  ck_assert_int_ge(dup2(g_log_pipe[1], STDERR_FILENO), 0);
}

static inline void log_capture_end(char *out, size_t cap) {
  ssize_t n;

  ck_assert_int_ge(dup2(g_log_saved, STDERR_FILENO), 0);
  close(g_log_saved);
  close(g_log_pipe[1]);
  n = read(g_log_pipe[0], out, cap - 1);
  close(g_log_pipe[0]);
  out[n < 0 ? 0 : n] = '\0';
}

static inline int log_count_of(const char *hay, const char *needle) {
  int n = 0;
  const char *p = hay;

  while ((p = strstr(p, needle)) != NULL) {
    n++;
    p += strlen(needle);
  }
  return n;
}

#endif
