/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_BITREADER_H
#define DVBIPITOOLS_LIB_DEMUX_BITREADER_H

#include <stddef.h>

typedef struct {
  const unsigned char *d;
  size_t len, bit; /* len, bit: bytes, bits */
  int err;
} br_t;

unsigned br_u(br_t *b, int n);   /* n<=32 bits, MSB first */
unsigned br_ue(br_t *b);         /* exp-golomb unsigned */
int      br_se(br_t *b);         /* exp-golomb signed */

/* strip H.264/HEVC emulation prevention (00 00 03 -> 00 00) */
size_t rbsp_unescape(const unsigned char *s, size_t len, unsigned char *d, size_t cap);

/* next Annex-B start code (00 00 01 or 00 00 00 01) at/after 'from'. *sclen = 3 or 4 */
size_t find_startcode(const unsigned char *d, size_t len, size_t from, size_t *sclen);

#endif
