/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBIM_BITWRITER_H
#define DIPIBIM_BITWRITER_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  unsigned char *buf;
  size_t cap;
  size_t len;   /* committed full bytes */
  unsigned cur; /* partial byte, MSB-first */
  int cur_bits; /* bits already placed in cur, 0..7 */
} bitwriter_t;

void bitwriter_init(bitwriter_t *bw);
void bitwriter_free(bitwriter_t *bw);
/* MSB-first, nbits 0..64. returns 0 ok, -1 oom */
int bitwriter_put(bitwriter_t *bw, uint64_t value, int nbits);
/* caller must be byte-aligned already (e.g. right after bitwriter_flush) */
int bitwriter_put_bytes(bitwriter_t *bw, const unsigned char *data, size_t len);
/* zero-pads and commits any pending partial byte */
void bitwriter_flush(bitwriter_t *bw);
/* TS 102 822-3-2 3.3: byte-continuation varint, 7 payload bits/byte */
int bitwriter_put_vluimsbf8(bitwriter_t *bw, uint64_t value);
/* TS 102 822-3-2 3.3 (vluimsbf5-style): unary continuation prefix, n*4 payload bits */
int bitwriter_put_vluimsbf4(bitwriter_t *bw, uint64_t value);
/* implies flush */
const unsigned char *bitwriter_data(bitwriter_t *bw, size_t *out_len);

#endif
