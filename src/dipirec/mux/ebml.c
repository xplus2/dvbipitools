/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>
#include "ebml.h"

void ebuf_free(ebuf_t *b) {
  free(b->p);
  b->p = NULL;
  b->len = b->cap = 0;
  b->err = 0;
}

void eb_bytes(ebuf_t *b, const void *data, size_t n) {
  if (b->err)
    return;
  if (b->len + n > b->cap) {
    size_t nc = b->cap ? b->cap * 2 : 4096;
    unsigned char *np;
    while (nc < b->len + n)
      nc *= 2;
    np = realloc(b->p, nc);
    if (!np) {
      b->err = 1;
      return;
    }
    b->p = np;
    b->cap = nc;
  }
  memcpy(b->p + b->len, data, n);
  b->len += n;
}

static void eb_be(ebuf_t *b, uint64_t v, int n) {
  unsigned char t[8];
  int i;
  for (i = 0; i < n; i++)
    t[n - 1 - i] = (unsigned char)(v >> (8 * i));
  eb_bytes(b, t, (size_t)n);
}

static int id_len(uint32_t id) {
  if (id & 0xFF000000) return 4;
  if (id & 0x00FF0000) return 3;
  if (id & 0x0000FF00) return 2;
  return 1;
}

void eb_id(ebuf_t *b, uint32_t id) { eb_be(b, id, id_len(id)); }

void eb_size(ebuf_t *b, uint64_t size) {
  int n = 1;
  uint64_t max = 0x7F;
  while (size >= max && n < 8) { /* all-ones = unknown */
    n++;
    max = (max << 7) | 0x7F;
  }
  eb_be(b, size | ((uint64_t)1 << (7 * n)), n);
}

static int uint_len(uint64_t v) {
  int n = 1;
  while ((v >>= 8) && n < 8)
    n++;
  return n;
}

void eb_uint(ebuf_t *b, uint32_t id, uint64_t val) {
  int n = uint_len(val);
  eb_id(b, id);
  eb_size(b, (uint64_t)n);
  eb_be(b, val, n);
}

void eb_bin(ebuf_t *b, uint32_t id, const void *data, size_t n) {
  eb_id(b, id);
  eb_size(b, n);
  eb_bytes(b, data, n);
}

void eb_str(ebuf_t *b, uint32_t id, const char *s) {
  eb_bin(b, id, s, strlen(s));
}

void eb_float(ebuf_t *b, uint32_t id, double val) {
  union {
    double d;
    uint64_t u;
  } c;
  c.d = val;
  eb_id(b, id);
  eb_size(b, 8);
  eb_be(b, c.u, 8);
}

void eb_master(ebuf_t *parent, uint32_t id, ebuf_t *child) {
  if (child->err)
    parent->err = 1;
  eb_id(parent, id);
  eb_size(parent, child->len);
  eb_bytes(parent, child->p, child->len);
  ebuf_free(child);
}
