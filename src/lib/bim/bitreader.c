/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "bitreader.h"

void bitreader_init(bitreader_t *br, const unsigned char *buf, size_t len) {
  br->buf = buf;
  br->len = len;
  br->byte_pos = 0;
  br->bit_pos = 0;
}

size_t bitreader_bits_left(const bitreader_t *br) {
  return (br->len - br->byte_pos) * 8 - (size_t)br->bit_pos;
}

int bitreader_get(bitreader_t *br, int nbits, uint64_t *out) {
  uint64_t v = 0;
  int i;
  if ((size_t)nbits > bitreader_bits_left(br))
    return -1;
  for (i = 0; i < nbits; i++) {
    unsigned bit = (br->buf[br->byte_pos] >> (7 - br->bit_pos)) & 1;
    v = (v << 1) | bit;
    br->bit_pos++;
    if (br->bit_pos == 8) {
      br->bit_pos = 0;
      br->byte_pos++;
    }
  }
  *out = v;
  return 0;
}

int bitreader_get_vluimsbf8(bitreader_t *br, uint64_t *out) {
  uint64_t v = 0, cont, group;
  for (;;) {
    if (bitreader_get(br, 1, &cont))
      return -1;
    if (bitreader_get(br, 7, &group))
      return -1;
    v = (v << 7) | group;
    if (!cont)
      break;
  }
  *out = v;
  return 0;
}

int bitreader_get_vluimsbf4(bitreader_t *br, uint64_t *out) {
  int n = 0;
  uint64_t bit, v;
  do {
    if (bitreader_get(br, 1, &bit))
      return -1;
    n++;
  } while (bit);
  if (bitreader_get(br, n * 4, &v))
    return -1;
  *out = v;
  return 0;
}
