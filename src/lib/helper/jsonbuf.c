/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "jsonbuf.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ioutil.h"

#define JBUF_SHRINK_RATIO 2

void jbuf_reset(jbuf_t *j) {
  size_t need = j->len + 1;
  if (j->buf && (size_t)j->cap > need * JBUF_SHRINK_RATIO) {
    char *p = realloc(j->buf, need);
    if (p) {
      j->buf = p;
      j->cap = (int)need;
    }
  }
  j->len = 0;
  j->failed = 0;
}

void jbuf_raw(jbuf_t *j, const char *s, size_t n) {
  char *p;
  if (j->failed)
    return;
  p = array_grow(j->buf, &j->cap, (int)(j->len + n + 1), 1);
  if (!p) {
    j->failed = 1;
    return;
  }
  j->buf = p;
  memcpy(j->buf + j->len, s, n);
  j->len += n;
  j->buf[j->len] = '\0';
}

void jbuf_str(jbuf_t *j, const char *s) { jbuf_raw(j, s, strlen(s)); }

void jbuf_fmt(jbuf_t *j, const char *fmt, ...) {
  char tmp[64];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tmp, sizeof tmp, fmt, ap);
  va_end(ap);
  jbuf_str(j, tmp);
}

void jbuf_json_string(jbuf_t *j, const char *s) {
  jbuf_str(j, "\"");
  for (; *s; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\')
      jbuf_fmt(j, "\\%c", c);
    else if (c == '\n')
      jbuf_str(j, "\\n");
    else if (c == '\r')
      jbuf_str(j, "\\r");
    else if (c == '\t')
      jbuf_str(j, "\\t");
    else if (c < 0x20)
      jbuf_fmt(j, "\\u%04x", c);
    else
      jbuf_raw(j, (const char *)&c, 1);
  }
  jbuf_str(j, "\"");
}

void jbuf_key(jbuf_t *j, const char *key) {
  jbuf_json_string(j, key);
  jbuf_str(j, ":");
}
