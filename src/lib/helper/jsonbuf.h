/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_JSONBUF_H
#define LIB_JSONBUF_H

#include <stddef.h>

/* json output buffer. oom sets failed, check once at end */
typedef struct {
  char *buf;
  size_t len;
  int cap;
  int failed;
} jbuf_t;

/* clears for reuse, shrinks first if cap far exceeds last len */
void jbuf_reset(jbuf_t *j);

void jbuf_raw(jbuf_t *j, const char *s, size_t n);
void jbuf_str(jbuf_t *j, const char *s);
void jbuf_fmt(jbuf_t *j, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

/* escapes + quotes s for JSON */
void jbuf_json_string(jbuf_t *j, const char *s);

/* emits "key": */
void jbuf_key(jbuf_t *j, const char *key);

#endif
