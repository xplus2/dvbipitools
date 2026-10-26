/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_BIMREADER_H
#define DVBIPITOOLS_LIB_BIM_BIMREADER_H

#include <stddef.h>
#include <stdint.h>

#define VLUIMSBF8_MAX_GROUPS 10
#define VLUIMSBF5_MAX_GROUPS 16

typedef struct {
  const unsigned char *buf;
  size_t len; /* total bytes */
  size_t byte_pos;
  int bit_pos; /* bits already consumed from buf[byte_pos], MSB-first, 0..7 */
} bitreader_t;

void bitreader_init(bitreader_t *br, const unsigned char *buf, size_t len);
/* MSB-first, nbits 0..64. returns 0 ok, -1 if fewer than nbits remain */
int bitreader_get(bitreader_t *br, int nbits, uint64_t *out);
size_t bitreader_bits_left(const bitreader_t *br);
/* 0 ok, -1 if <nbits remain */
int bitreader_skip(bitreader_t *br, size_t nbits);
/* TS 102 822-3-2 3.3: byte-continuation varint, 7 payload bits/byte */
int bitreader_get_vluimsbf8(bitreader_t *br, uint64_t *out);
/* ISO 15938-1 clause 4: unary continuation prefix, n*4 payload bits */
int bitreader_get_vluimsbf5(bitreader_t *br, uint64_t *out);
int bitreader_get_bytes(bitreader_t *br, unsigned char *out, size_t n);
int bitreader_fuu_header(bitreader_t *br, uint64_t *flen, uint64_t *ctxpath);

#endif
