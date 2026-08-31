/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_FMP4_BOX_H
#define DVBIPITOOLS_LIB_MUX_FMP4_BOX_H

#include <stddef.h>
#include <stdint.h>

/* growable ISOBMFF box build buffer */
typedef struct {
  unsigned char *p;
  size_t len, cap;
  int err; /* alloc failed */
} mp4buf_t;

void mp4buf_free(mp4buf_t *b);
void mb_bytes(mp4buf_t *b, const void *data, size_t n);
void mb_u8(mp4buf_t *b, unsigned v);
void mb_u16(mp4buf_t *b, unsigned v);
void mb_u24(mp4buf_t *b, unsigned v);
void mb_u32(mp4buf_t *b, uint32_t v);
void mb_u64(mp4buf_t *b, uint64_t v);
void mb_fourcc(mp4buf_t *b, const char fourcc[4]);

/* overwrites 4 bytes already written at pos with v (big-endian) */
void mb_patch_u32(mp4buf_t *b, size_t pos, uint32_t v);

/* frees child */
void mb_box(mp4buf_t *parent, const char fourcc[4], mp4buf_t *child);

#endif
