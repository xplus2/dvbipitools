/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_TESTS_UNIT_LIB_DEMUX_BITSTREAM_TEST_H
#define DVBIPITOOLS_TESTS_UNIT_LIB_DEMUX_BITSTREAM_TEST_H

#include <check.h>

#include "lib/bim/bitwriter.h"
#include "lib/demux/bitreader.h"

typedef struct {
  bitwriter_t bw;
  br_t b;
} stream_t;

static inline void put_bits(stream_t *s, unsigned long long value, int nbits) {
  ck_assert_int_eq(bitwriter_put(&s->bw, value, nbits), 0);
}

static inline void put_repeat(stream_t *s, unsigned bit, unsigned count) {
  for (unsigned i = 0; i < count; i++) put_bits(s, bit, 1);
}

static inline void put_ue(stream_t *s, unsigned value) {
  unsigned long long n = (unsigned long long)value + 1;
  int width = 1;

  while ((n >> width) != 0) width++;
  put_bits(s, n, 2 * width - 1);
}

static inline void put_se(stream_t *s, int value) {
  put_ue(s, value > 0 ? (unsigned)(2 * value - 1) : (unsigned)(-2 * value));
}

static inline void pad_to_byte(stream_t *s) {
  put_repeat(s, 0, (unsigned)((8 - s->bw.cur_bits) & 7));
}

static inline void stream_open(stream_t *s) {
  bitwriter_init(&s->bw);
}

static inline void stream_read(stream_t *s) {
  size_t len = 0;

  s->b.d = bitwriter_data(&s->bw, &len);
  s->b.len = len;
  s->b.bit = 0;
  s->b.err = 0;
}

static inline void stream_close(stream_t *s) {
  bitwriter_free(&s->bw);
}

#endif
