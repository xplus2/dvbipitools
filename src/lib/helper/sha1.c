/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "sha1.h"
#include <stdlib.h>
#include <string.h>

static uint32_t rol32(uint32_t v, int bits) { return (v << bits) | (v >> (32 - bits)); }

static void sha1_block(uint32_t h[5], const uint8_t block[64]) {
  uint32_t w[80];
  uint32_t a, b, c, d, e;
  int i;
  for (i = 0; i < 16; i++)
    w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16) | ((uint32_t)block[i * 4 + 2] << 8) |
           (uint32_t)block[i * 4 + 3];
  for (i = 16; i < 80; i++)
    w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

  a = h[0];
  b = h[1];
  c = h[2];
  d = h[3];
  e = h[4];

  for (i = 0; i < 80; i++) {
    uint32_t f, k, tmp;
    if (i < 20) {
      f = (b & c) | (~b & d);
      k = 0x5A827999u;
    } else if (i < 40) {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1u;
    } else if (i < 60) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDCu;
    } else {
      f = b ^ c ^ d;
      k = 0xCA62C1D6u;
    }
    tmp = rol32(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = rol32(b, 30);
    b = a;
    a = tmp;
  }
  h[0] += a;
  h[1] += b;
  h[2] += c;
  h[3] += d;
  h[4] += e;
}

void sha1(const void *data, size_t len, uint8_t out[20]) {
  uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
  const uint8_t *p = data;
  size_t full_blocks = len / 64;
  size_t rem = len % 64;
  uint8_t tail[128];
  size_t tail_len;
  uint64_t bitlen = (uint64_t)len * 8;
  size_t i;
  for (i = 0; i < full_blocks; i++)
    sha1_block(h, p + i * 64);

  tail_len = rem;
  memcpy(tail, p + full_blocks * 64, rem);
  tail[tail_len++] = 0x80;
  if (tail_len > 56) {
    while (tail_len < 128)
      tail[tail_len++] = 0;
    sha1_block(h, tail);
    tail_len = 0;
  }
  while (tail_len < 56)
    tail[tail_len++] = 0;
  for (i = 0; i < 8; i++)
    tail[tail_len++] = (uint8_t)(bitlen >> (56 - 8 * i));
  sha1_block(h, tail);
  for (i = 0; i < 5; i++) {
    out[i * 4] = (uint8_t)(h[i] >> 24);
    out[i * 4 + 1] = (uint8_t)(h[i] >> 16);
    out[i * 4 + 2] = (uint8_t)(h[i] >> 8);
    out[i * 4 + 3] = (uint8_t)h[i];
  }
}
