/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_TESTS_UNIT_ALLOC_FAIL_H
#define DVBIPITOOLS_TESTS_UNIT_ALLOC_FAIL_H

#include <stddef.h>
#include <string.h>

void *__real_malloc(size_t n);
void *__real_calloc(size_t n, size_t sz);
void *__real_realloc(void *p, size_t n);
char *__real_strdup(const char *s);

static long g_alloc_left = -1;
static int g_alloc_hit;

static inline int alloc_gate(void) {
  if (g_alloc_left < 0) return 1;
  if (g_alloc_left == 0) {
    g_alloc_hit = 1;
    return 0;
  }
  g_alloc_left--;
  return 1;
}

void *__wrap_malloc(size_t n) { return alloc_gate() ? __real_malloc(n) : NULL; }
void *__wrap_calloc(size_t n, size_t sz) { return alloc_gate() ? __real_calloc(n, sz) : NULL; }
void *__wrap_realloc(void *p, size_t n) { return alloc_gate() ? __real_realloc(p, n) : NULL; }
char *__wrap_strdup(const char *s) { return alloc_gate() ? __real_strdup(s) : NULL; }

static inline void alloc_fail_at(long n) {
  g_alloc_left = n;
  g_alloc_hit = 0;
}

static inline int alloc_fail_end(void) {
  int hit = g_alloc_hit;
  g_alloc_left = -1;
  g_alloc_hit = 0;
  return hit;
}

#endif
