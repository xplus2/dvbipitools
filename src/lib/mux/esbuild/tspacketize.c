/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "tspacketize.h"

#include "pes.h"

size_t esbuild_ts_packetize(unsigned pid, unsigned char *cc, unsigned char stream_id, uint64_t pts_90k, int has_dts, uint64_t dts_90k, const unsigned char *es, size_t es_len, int pcr_first, uint64_t pcr_90k, unsigned char *pesbuf, size_t pesbuf_cap, ts_packet_cb cb, void *ctx) {
  size_t n = esbuild_pes_build(stream_id, pts_90k, has_dts, dts_90k, es, es_len, pesbuf, pesbuf_cap);
  if (!n) return 0;
  return ts_packet_emit(pid, cc, NULL, pesbuf, n, pcr_first, pcr_90k, cb, ctx);
}
