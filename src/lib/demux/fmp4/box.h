/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_FMP4_BOX_H
#define DVBIPITOOLS_LIB_DEMUX_FMP4_BOX_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  char fourcc[5];
  const unsigned char *start;
  const unsigned char *body;
  size_t body_len;
  const unsigned char *next;
} fmp4_box_t;

int fmp4_box_read(const unsigned char *p, const unsigned char *end, fmp4_box_t *out);

int fmp4_box_find(const unsigned char *body, size_t len, const char fourcc[4], fmp4_box_t *out);

uint16_t fmp4_rb_u16(const unsigned char *p);
uint32_t fmp4_rb_u24(const unsigned char *p);
uint32_t fmp4_rb_u32(const unsigned char *p);
uint64_t fmp4_rb_u64(const unsigned char *p);

#endif
