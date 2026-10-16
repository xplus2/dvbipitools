/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_ESBUILD_TSPACKETIZE_H
#define DVBIPITOOLS_LIB_MUX_ESBUILD_TSPACKETIZE_H

#include <stddef.h>
#include <stdint.h>

#include "lib/mux/tspacket_write.h"

/* PCR leads ref time (DTS) by this: decoder buffering for T-STD */
#define ESBUILD_PCR_LEAD_90K 63000ULL

/* pcr_90k: ref time, ESBUILD_PCR_LEAD_90K is subtracted (mod 2^33) */
size_t esbuild_ts_packetize(unsigned pid, unsigned char *cc, unsigned char stream_id, uint64_t pts_90k, int has_dts, uint64_t dts_90k, const unsigned char *es, size_t es_len, int pcr_first, uint64_t pcr_90k, unsigned char *pesbuf, size_t pesbuf_cap, ts_packet_cb cb, void *ctx);

#endif
