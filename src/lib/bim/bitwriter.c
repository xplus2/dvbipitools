/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>

#include "bitwriter.h"

void bitwriter_init(bitwriter_t *bw) {
  bw->buf = NULL;
  bw->cap = 0;
  bw->len = 0;
  bw->cur = 0;
  bw->cur_bits = 0;
}

void bitwriter_free(bitwriter_t *bw) {
  free(bw->buf);
  bw->buf = NULL;
  bw->cap = 0;
  bw->len = 0;
}

static int push_byte(bitwriter_t *bw, unsigned char b) {
  if (bw->len >= bw->cap) {
    size_t newcap = bw->cap ? bw->cap * 2 : 64;
    unsigned char *np = realloc(bw->buf, newcap);
    if (!np)
      return -1;
    bw->buf = np;
    bw->cap = newcap;
  }
  bw->buf[bw->len++] = b;
  return 0;
}

int bitwriter_put(bitwriter_t *bw, uint64_t value, int nbits) {
  int i;
  for (i = nbits - 1; i >= 0; i--) {
    unsigned bit = (unsigned)((value >> i) & 1);
    bw->cur |= bit << (7 - bw->cur_bits);
    bw->cur_bits++;
    if (bw->cur_bits == 8) {
      if (push_byte(bw, (unsigned char)bw->cur))
        return -1;
      bw->cur = 0;
      bw->cur_bits = 0;
    }
  }
  return 0;
}

int bitwriter_put_bytes(bitwriter_t *bw, const unsigned char *data, size_t len) {
  size_t i;
  for (i = 0; i < len; i++)
    if (bitwriter_put(bw, data[i], 8))
      return -1;
  return 0;
}

void bitwriter_flush(bitwriter_t *bw) {
  if (bw->cur_bits > 0) {
    push_byte(bw, (unsigned char)bw->cur);
    bw->cur = 0;
    bw->cur_bits = 0;
  }
}

const unsigned char *bitwriter_data(bitwriter_t *bw, size_t *out_len) {
  bitwriter_flush(bw);
  *out_len = bw->len;
  return bw->buf;
}

int bitwriter_put_vluimsbf8(bitwriter_t *bw, uint64_t value) {
  int ngroups = 1, i;
  while ((value >> (7 * ngroups)) != 0)
    ngroups++;
  for (i = ngroups - 1; i >= 0; i--) {
    if (bitwriter_put(bw, i != 0, 1))
      return -1;
    if (bitwriter_put(bw, (value >> (7 * i)) & 0x7F, 7))
      return -1;
  }
  return 0;
}

int bitwriter_put_vluimsbf4(bitwriter_t *bw, uint64_t value) {
  int n = 1, i;
  while ((value >> (4 * n)) != 0)
    n++;
  for (i = 0; i < n - 1; i++)
    if (bitwriter_put(bw, 1, 1))
      return -1;
  if (bitwriter_put(bw, 0, 1))
    return -1;
  return bitwriter_put(bw, value, n * 4);
}
