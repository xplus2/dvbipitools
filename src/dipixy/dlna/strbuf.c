/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "strbuf.h"

#include "lib/helper/ioutil.h"

#include <string.h>

void sb_init(strbuf_t *b, char *buf, size_t cap) {
  b->buf = buf;
  b->cap = cap;
  b->len = 0;
  b->truncated = 0;
  if (cap) buf[0] = '\0';
}

void sb_add_n(strbuf_t *b, const char *s, size_t maxn) {
  size_t n = strlen(s);
  size_t room = b->cap > b->len ? b->cap - b->len - 1 : 0;
  if (n > maxn) n = maxn;
  if (n > room) {
    n = room;
    b->truncated = 1;
  }
  memcpy(b->buf + b->len, s, n);
  b->len += n;
  b->buf[b->len] = '\0';
}

void sb_add(strbuf_t *b, const char *s) { sb_add_n(b, s, strlen(s)); }

void sb_add_u64(strbuf_t *b, uint64_t v) {
  char tmp[21];
  u64_to_dec(tmp, v);
  sb_add(b, tmp);
}

void sb_add_uint(strbuf_t *b, unsigned v) {
  char buf[16];
  uint_to_str(buf, v);
  sb_add(b, buf);
}
