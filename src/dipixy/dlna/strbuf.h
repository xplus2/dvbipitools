/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_DLNA_STRBUF_H
#define DIPIXY_DLNA_STRBUF_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  char *buf;
  size_t cap;
  size_t len;
  int truncated;
} strbuf_t;

void sb_init(strbuf_t *b, char *buf, size_t cap);
void sb_add_n(strbuf_t *b, const char *s, size_t maxn);
void sb_add(strbuf_t *b, const char *s);
void sb_add_u64(strbuf_t *b, uint64_t v);
void sb_add_uint(strbuf_t *b, unsigned v);

#endif
