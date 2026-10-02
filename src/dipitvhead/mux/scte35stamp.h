/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_SCTE35STAMP_H
#define DIPITVHEAD_SCTE35STAMP_H

#include <stdint.h>

#define SCTE35_MAX_PACKETS 24

typedef void (*scte35_emit_fn)(void *ctx, unsigned char *pkt188);

typedef struct {
  unsigned char pk[SCTE35_MAX_PACKETS][188];
  int n;
  unsigned total;
  unsigned got;
  unsigned long long patched;
} scte35stamp_t;

void scte35stamp_init(scte35stamp_t *s);
void scte35stamp_feed(scte35stamp_t *s, unsigned char *pkt188, int64_t delta90k, scte35_emit_fn emit, void *ctx);
void scte35stamp_flush(scte35stamp_t *s, scte35_emit_fn emit, void *ctx);

#endif
