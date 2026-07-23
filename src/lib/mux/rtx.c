/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#include "rtx.h"

struct rtx_ctx {
  _Atomic uint16_t seq; /* starts at 0, no random-start requirement for RTX */
};

rtx_ctx_t *rtx_ctx_new(void) {
  return calloc(1, sizeof(rtx_ctx_t));
}

void rtx_ctx_free(rtx_ctx_t *ctx) { free(ctx); }

size_t rtx_build(rtx_ctx_t *ctx, uint32_t ssrc, unsigned char pt, uint32_t timestamp, uint16_t orig_seq, const unsigned char *orig_payload, size_t orig_payload_len, unsigned char *out, size_t cap) {
  size_t total = 12 + 2 + orig_payload_len;
  uint16_t my_seq;

  if (cap < total)
    return 0;

  my_seq = atomic_fetch_add_explicit(&ctx->seq, 1, memory_order_relaxed); /* single atomic op: no two callers can get the same seq */
  out[0] = 0x80; /* V=2, P=0, X=0, CC=0 */
  out[1] = (unsigned char)(pt & 0x7F); /* M=0 */
  out[2] = (unsigned char)(my_seq >> 8);
  out[3] = (unsigned char)my_seq;
  out[4] = (unsigned char)(timestamp >> 24);
  out[5] = (unsigned char)(timestamp >> 16);
  out[6] = (unsigned char)(timestamp >> 8);
  out[7] = (unsigned char)timestamp;
  out[8] = (unsigned char)(ssrc >> 24);
  out[9] = (unsigned char)(ssrc >> 16);
  out[10] = (unsigned char)(ssrc >> 8);
  out[11] = (unsigned char)ssrc;
  out[12] = (unsigned char)(orig_seq >> 8); /* OSN, RFC 4588 */
  out[13] = (unsigned char)orig_seq;
  if (orig_payload_len)
    memcpy(out + 14, orig_payload, orig_payload_len);
  return total;
}
