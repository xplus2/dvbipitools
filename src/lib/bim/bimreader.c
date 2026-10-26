/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "bimreader.h"

void bitreader_init(bitreader_t *br, const unsigned char *buf, size_t len) {
  br->buf = buf;
  br->len = len;
  br->byte_pos = 0;
  br->bit_pos = 0;
}

size_t bitreader_bits_left(const bitreader_t *br) {
  return (br->len - br->byte_pos) * 8 - (size_t)br->bit_pos;
}

int bitreader_skip(bitreader_t *br, size_t nbits) {
  size_t bit;
  if (nbits > bitreader_bits_left(br)) return -1;
  bit = (size_t)br->bit_pos + nbits;
  br->byte_pos += bit / 8;
  br->bit_pos = (int)(bit % 8);
  return 0;
}

int bitreader_get(bitreader_t *br, int nbits, uint64_t *out) {
  uint64_t v = 0;
  if ((size_t)nbits > bitreader_bits_left(br)) return -1;
  if (br->bit_pos == 0) {
    while (nbits >= 8) {
      v = (v << 8) | br->buf[br->byte_pos];
      br->byte_pos++;
      nbits -= 8;
    }
  }
  while (nbits > 0) {
    int avail = 8 - br->bit_pos;
    int take = nbits < avail ? nbits : avail;
    unsigned bits = (br->buf[br->byte_pos] >> (avail - take)) & ((1u << take) - 1);
    v = (v << take) | bits;
    br->bit_pos += take;
    nbits -= take;
    if (br->bit_pos == 8) {
      br->bit_pos = 0;
      br->byte_pos++;
    }
  }
  *out = v;
  return 0;
}

int bitreader_get_vluimsbf8(bitreader_t *br, uint64_t *out) {
  uint64_t v = 0;
  uint64_t cont;
  uint64_t group;
  for (int i = 0; i < VLUIMSBF8_MAX_GROUPS; i++) {
    if (bitreader_get(br, 1, &cont)) return -1;
    if (bitreader_get(br, 7, &group)) return -1;
    if (v >> 57) return -1;
    v = (v << 7) | group;
    if (!cont) {
      *out = v;
      return 0;
    }
  }
  return -1;
}

int bitreader_get_vluimsbf5(bitreader_t *br, uint64_t *out) {
  int n = 0;
  uint64_t bit;
  uint64_t v;
  do {
    if (bitreader_get(br, 1, &bit)) return -1;
    if (++n > VLUIMSBF5_MAX_GROUPS) return -1;
  } while (bit);
  if (bitreader_get(br, n * 4, &v)) return -1;
  *out = v;
  return 0;
}

int bitreader_get_bytes(bitreader_t *br, unsigned char *out, size_t n) {
  uint64_t c;
  if (n > bitreader_bits_left(br) / 8) return -1;
  if (!br->bit_pos) {
    memcpy(out, br->buf + br->byte_pos, n);
    br->byte_pos += n;
    return 0;
  }
  for (size_t i = 0; i < n; i++) {
    if (bitreader_get(br, 8, &c)) return -1;
    out[i] = (unsigned char)c;
  }
  return 0;
}

int bitreader_fuu_header(bitreader_t *br, uint64_t *flen, uint64_t *ctxpath) {
  if (bitreader_get_vluimsbf8(br, flen) || bitreader_get(br, 16, ctxpath) || br->bit_pos != 0 || *flen > bitreader_bits_left(br) / 8) return -1;
  return 0;
}
