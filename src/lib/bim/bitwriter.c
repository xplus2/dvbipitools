/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/sys/ioutil.h"
#include "bimreader.h"
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

void bitwriter_reset(bitwriter_t *bw) {
  bw->len = 0;
  bw->cur = 0;
  bw->cur_bits = 0;
}

static int reserve(bitwriter_t *bw, size_t more) {
  if (more > SIZE_MAX - bw->len)
    return -1;
  return growbuf_reserve((void **)&bw->buf, &bw->cap, 1, bw->len + more, 64);
}

static int push_byte(bitwriter_t *bw, unsigned char b) {
  if (reserve(bw, 1))
    return -1;
  bw->buf[bw->len++] = b;
  return 0;
}

int bitwriter_put(bitwriter_t *bw, uint64_t value, int nbits) {
  if (nbits <= 0)
    return 0;
  /* reserve first: failure leaves state untouched */
  if (reserve(bw, ((size_t)bw->cur_bits + (size_t)nbits) / 8))
    return -1;
  while (nbits > 0) {
    int space = 8 - bw->cur_bits;
    int take = nbits < space ? nbits : space;
    unsigned chunk = (unsigned)(value >> (nbits - take)) & ((1u << take) - 1);
    bw->cur |= chunk << (space - take);
    bw->cur_bits += take;
    nbits -= take;
    if (bw->cur_bits == 8) {
      bw->buf[bw->len++] = (unsigned char)bw->cur;
      bw->cur = 0;
      bw->cur_bits = 0;
    }
  }
  return 0;
}

static int push_bytes(bitwriter_t *bw, const unsigned char *data, size_t len) {
  if (reserve(bw, len))
    return -1;
  memcpy(bw->buf + bw->len, data, len);
  bw->len += len;
  return 0;
}

int bitwriter_put_bytes(bitwriter_t *bw, const unsigned char *data, size_t len) {
  if (bw->cur_bits == 0)
    return push_bytes(bw, data, len);
  for (size_t i = 0; i < len; i++)
    if (bitwriter_put(bw, data[i], 8))
      return -1;
  return 0;
}

int bitwriter_flush(bitwriter_t *bw) {
  if (bw->cur_bits > 0) {
    if (push_byte(bw, (unsigned char)bw->cur))
      return -1;
    bw->cur = 0;
    bw->cur_bits = 0;
  }
  return 0;
}

const unsigned char *bitwriter_data(bitwriter_t *bw, size_t *out_len) {
  if (bitwriter_flush(bw)) {
    *out_len = 0;
    return NULL;
  }
  *out_len = bw->len;
  return bw->buf;
}

int bitwriter_vluimsbf8_len(uint64_t value) {
  int n = 1;
  while (n < VLUIMSBF8_MAX_GROUPS && (value >> (7 * n)) != 0)
    n++;
  return n;
}

int bitwriter_put_vluimsbf8(bitwriter_t *bw, uint64_t value) {
  for (int i = bitwriter_vluimsbf8_len(value) - 1; i >= 0; i--) {
    if (bitwriter_put(bw, i != 0, 1))
      return -1;
    if (bitwriter_put(bw, (value >> (7 * i)) & 0x7F, 7))
      return -1;
  }
  return 0;
}

int bitwriter_put_vluimsbf5(bitwriter_t *bw, uint64_t value) {
  int n = 1;
  while (n < VLUIMSBF5_MAX_GROUPS && (value >> (4 * n)) != 0)
    n++;
  for (int i = 0; i < n - 1; i++)
    if (bitwriter_put(bw, 1, 1))
      return -1;
  if (bitwriter_put(bw, 0, 1))
    return -1;
  return bitwriter_put(bw, value, n * 4);
}

int bitwriter_append(bitwriter_t *dst, const bitwriter_t *src) {
  if (src->len && bitwriter_put_bytes(dst, src->buf, src->len)) return -1;
  return src->cur_bits ? bitwriter_put(dst, src->cur >> (8 - src->cur_bits), src->cur_bits) : 0;
}
