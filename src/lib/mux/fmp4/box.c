/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "box.h"
#include <stdlib.h>
#include <string.h>
#include "lib/helper/ioutil.h"

static void mb_reserve(mp4buf_t *b, size_t need) {
  if (b->err) return;
  if (growbuf_reserve((void **)&b->p, &b->cap, 1, need, 256)) b->err = 1;
}

void mp4buf_free(mp4buf_t *b) {
  free(b->p);
  b->p = NULL;
  b->len = b->cap = 0;
}

void mb_bytes(mp4buf_t *b, const void *data, size_t n) {
  if (!n) return;
  mb_reserve(b, b->len + n);
  if (b->err) return;
  memcpy(b->p + b->len, data, n);
  b->len += n;
}

void mb_u8(mp4buf_t *b, unsigned v) {
  unsigned char c = (unsigned char)v;
  mb_bytes(b, &c, 1);
}

void mb_u16(mp4buf_t *b, unsigned v) {
  unsigned char c[2];
  c[0] = (unsigned char)(v >> 8);
  c[1] = (unsigned char)v;
  mb_bytes(b, c, 2);
}

void mb_u24(mp4buf_t *b, unsigned v) {
  unsigned char c[3];
  c[0] = (unsigned char)(v >> 16);
  c[1] = (unsigned char)(v >> 8);
  c[2] = (unsigned char)v;
  mb_bytes(b, c, 3);
}

void mb_u32(mp4buf_t *b, uint32_t v) {
  unsigned char c[4];
  c[0] = (unsigned char)(v >> 24);
  c[1] = (unsigned char)(v >> 16);
  c[2] = (unsigned char)(v >> 8);
  c[3] = (unsigned char)v;
  mb_bytes(b, c, 4);
}

void mb_u64(mp4buf_t *b, uint64_t v) {
  mb_u32(b, (uint32_t)(v >> 32));
  mb_u32(b, (uint32_t)v);
}

void mb_fourcc(mp4buf_t *b, const char fourcc[4]) {
  mb_bytes(b, fourcc, 4);
}

void mb_patch_u32(mp4buf_t *b, size_t pos, uint32_t v) {
  if (b->err || pos + 4 > b->len) return;
  b->p[pos] = (unsigned char)(v >> 24);
  b->p[pos + 1] = (unsigned char)(v >> 16);
  b->p[pos + 2] = (unsigned char)(v >> 8);
  b->p[pos + 3] = (unsigned char)v;
}

void mb_box(mp4buf_t *parent, const char fourcc[4], mp4buf_t *child) {
  if (child->err) parent->err = 1;
  mb_u32(parent, (uint32_t)(8 + child->len));
  mb_fourcc(parent, fourcc);
  mb_bytes(parent, child->p, child->len);
  mp4buf_free(child);
}
