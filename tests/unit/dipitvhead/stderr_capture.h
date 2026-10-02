/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_TESTS_STDERR_CAPTURE_H
#define DIPITVHEAD_TESTS_STDERR_CAPTURE_H

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  int saved;
  char path[64];
} capture_t;

static inline void capture_begin(capture_t *c) {
  int fd;

  strcpy(c->path, "/tmp/dipitvhead_stderr_XXXXXX");
  fd = mkstemp(c->path);
  ck_assert_int_ge(fd, 0);
  fflush(stderr);
  c->saved = dup(STDERR_FILENO);
  ck_assert_int_ge(c->saved, 0);
  ck_assert_int_ge(dup2(fd, STDERR_FILENO), 0);
  close(fd);
}

static inline size_t capture_end(const capture_t *c, char *out, size_t cap) {
  FILE *f;
  size_t n;

  fflush(stderr);
  ck_assert_int_ge(dup2(c->saved, STDERR_FILENO), 0);
  close(c->saved);
  f = fopen(c->path, "rb");
  ck_assert_ptr_nonnull(f);
  n = fread(out, 1, cap - 1, f);
  fclose(f);
  unlink(c->path);
  out[n] = '\0';
  return n;
}

#endif
