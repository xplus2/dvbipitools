/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_TSPACKET_WRITE_H
#define DVBIPITOOLS_LIB_MUX_TSPACKET_WRITE_H

#include <stddef.h>
#include <stdint.h>

typedef void (*ts_packet_cb)(void *ctx, const unsigned char *pkt188);

/* splits data into 188B TS packets on pid, bumping *cc per packet.
 * pointer_byte: non-NULL prepends it on the first packet only (PSI section start), NULL for PES.
 * pcr_first: PCR adaptation field on the first packet only. returns packet count. */
size_t ts_packet_emit(unsigned pid, unsigned char *cc, const unsigned char *pointer_byte, const unsigned char *data, size_t len, int pcr_first, uint64_t pcr_90k, ts_packet_cb cb, void *ctx);

#endif
