/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_BYTE_RING_H
#define LIB_BYTE_RING_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint8_t *buf;
  uint32_t cap; /* power of 2 */
  _Atomic uint32_t wpos;
  _Atomic uint32_t rpos;
} byte_ring_t;

/* mallocs buf if not allocated (cap fixed at first call), resets wpos/rpos to 0. cap must be a ^2. buf stays NULL on OOM */
void byte_ring_reset(byte_ring_t *r, uint32_t cap);

void byte_ring_free(byte_ring_t *r);

/* single producer. 1 ok (or len 0, no-op), 0 buf NULL/would overflow (nothing written) */
int byte_ring_write(byte_ring_t *r, const uint8_t *data, size_t len);

/* single consumer. copies up to maxlen contiguous bytes, 0 if empty/buf NULL */
size_t byte_ring_read(byte_ring_t *r, uint8_t *dst, size_t maxlen);

/* zero-copy: ptr to the next contiguous run, *len its size (0/NULL if empty) */
const uint8_t *byte_ring_peek(byte_ring_t *r, size_t *len);

/* consumer-side advance after byte_ring_peek() */
void byte_ring_advance(byte_ring_t *r, size_t n);

#endif
