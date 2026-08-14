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

/* 13-bit PID from a 188 B TS packet's header (pkt[1],pkt[2]) */
unsigned tspack_pid(const unsigned char *pkt);

/* payload past adaptation field: pl, plen, pusi set on success.
   0 if afc indicates no payload (0 or 2) or adaptation field length malformed. */
int tspack_payload(const unsigned char *pkt, const unsigned char **pl, size_t *plen, int *pusi);

/* 12-bit length field from 2 adjacent bytes: (p[0]&0x0F)<<8 | p[1] */
size_t tspack_length12(const unsigned char *p);

#endif
