/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_TSPACK_H
#define DVBIPITOOLS_LIB_DEMUX_TSPACK_H

#include <stddef.h>

typedef struct {
  unsigned char acc[188];
  size_t acclen;
} tspack_t;

/* feed bytes, packetize to 188 B, resync on 0x47. cb !=0 stops */
int tspack_feed(tspack_t *pz, const unsigned char *d, size_t len, int (*cb)(void *, const unsigned char *), void *ctx);

#endif
