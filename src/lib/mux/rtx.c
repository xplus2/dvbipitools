/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdatomic.h>
#include <string.h>

#include "rtpheader.h"
#include "rtx.h"

size_t rtx_build(_Atomic uint16_t *seq, uint32_t ssrc, unsigned char pt, uint32_t timestamp, uint16_t orig_seq, const unsigned char *orig_payload, size_t orig_payload_len, unsigned char *out, size_t cap) {
  size_t total = 12 + 2 + orig_payload_len;
  uint16_t my_seq;

  if (cap < total)
    return 0;

  my_seq = atomic_fetch_add_explicit(seq, 1, memory_order_relaxed); /* single atomic op: no two callers get same seq */
  rtp_write_header(out, pt, my_seq, timestamp, ssrc);
  be16_put(out + 12, orig_seq); /* OSN, RFC 4588 */
  if (orig_payload_len)
    memcpy(out + 14, orig_payload, orig_payload_len);
  return total;
}
