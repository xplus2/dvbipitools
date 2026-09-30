/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "box.h"
#include <string.h>

uint16_t fmp4_rb_u16(const unsigned char *p) { return (uint16_t)((p[0] << 8) | p[1]); }
uint32_t fmp4_rb_u24(const unsigned char *p) { return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]; }
uint32_t fmp4_rb_u32(const unsigned char *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
uint64_t fmp4_rb_u64(const unsigned char *p) { return ((uint64_t)fmp4_rb_u32(p) << 32) | fmp4_rb_u32(p + 4); }

int fmp4_box_read(const unsigned char *p, const unsigned char *end, fmp4_box_t *out) {
  uint64_t size64;
  size_t hdrlen;
  if ((size_t)(end - p) < 8) return 0;
  size64 = fmp4_rb_u32(p);
  memcpy(out->fourcc, p + 4, 4);
  out->fourcc[4] = '\0';
  if (size64 == 1) {
    if ((size_t)(end - p) < 16) return 0;
    size64 = fmp4_rb_u64(p + 8);
    hdrlen = 16;
  } else if (size64 == 0) {
    size64 = (uint64_t)(end - p);
    hdrlen = 8;
  } else {
    hdrlen = 8;
  }
  if (size64 < hdrlen || (size_t)(end - p) < size64) return 0;
  out->start = p;
  out->body = p + hdrlen;
  out->body_len = (size_t)size64 - hdrlen;
  out->next = p + size64;
  return 1;
}

int fmp4_box_find(const unsigned char *body, size_t len, const char fourcc[4], fmp4_box_t *out) {
  const unsigned char *p = body;
  const unsigned char *end = body + len;
  while (p < end) {
    fmp4_box_t b;
    if (!fmp4_box_read(p, end, &b)) return 0;
    if (!memcmp(b.fourcc, fourcc, 4)) {
      *out = b;
      return 1;
    }
    p = b.next;
  }
  return 0;
}
