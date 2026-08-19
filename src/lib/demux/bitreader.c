/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "bitreader.h"

unsigned br_u(br_t *b, int n) {
  unsigned v = 0;
  while (n-- > 0) {
    size_t byi = b->bit >> 3;
    if (byi >= b->len) {
      b->err = 1;
      return v;
    }
    v = (v << 1) | ((b->d[byi] >> (7 - (b->bit & 7))) & 1);
    b->bit++;
  }
  return v;
}

unsigned br_ue(br_t *b) {
  int lz = 0;
  while (lz < 32 && !b->err && !br_u(b, 1))
    lz++;
  return lz ? (unsigned)((1ull << lz) - 1 + br_u(b, lz)) : 0;
}

int br_se(br_t *b) {
  unsigned v = br_ue(b);
  return (v & 1) ? (int)((v + 1) / 2) : -(int)(v / 2);
}

size_t br_slice(const br_t *b, size_t from, size_t to, unsigned char *out, size_t cap) {
  size_t nbits = to - from, nb = (nbits + 7) / 8;

  if (!nb || nb > cap)
    return 0;
  if (from % 8 == 0) {
    size_t full = nbits / 8, rem = nbits % 8;
    memcpy(out, b->d + from / 8, full);
    if (rem)
      out[full] = (unsigned char)(b->d[from / 8 + full] & (0xFF << (8 - rem)));
    return nb;
  }
  memset(out, 0, nb);
  for (size_t i = 0; i < nbits; i++) {
    size_t s = from + i;
    unsigned bit = (b->d[s >> 3] >> (7 - (s & 7))) & 1;
    out[i >> 3] |= (unsigned char)(bit << (7 - (i & 7)));
  }
  return nb;
}

size_t rbsp_unescape(const unsigned char *s, size_t len, unsigned char *d, size_t cap) {
  size_t o = 0, zeros = 0;
  for (size_t i = 0; i < len && o < cap; i++) {
    if (zeros >= 2 && s[i] == 0x03) {
      zeros = 0;
      continue;
    }
    d[o++] = s[i];
    zeros = (s[i] == 0) ? zeros + 1 : 0;
  }
  return o;
}

size_t find_startcode(const unsigned char *d, size_t len, size_t from, size_t *sclen) {
  for (size_t i = from; i + 3 <= len; i++) {
    if (d[i] || d[i + 1])
      continue;
    if (d[i + 2] == 1) {
      *sclen = 3;
      return i;
    }
    if (i + 4 <= len && d[i + 2] == 0 && d[i + 3] == 1) {
      *sclen = 4;
      return i;
    }
  }
  return len;
}
