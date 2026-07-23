/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_RTX_H
#define DVBIPITOOLS_LIB_MUX_RTX_H

#include <stddef.h>
#include <stdint.h>

/* one per RTX session (per client, or shared MC RET session); seq independent of orig_seq, sent via OSN prefix; safe to share across threads, seq is atomic */
typedef struct rtx_ctx rtx_ctx_t;

rtx_ctx_t *rtx_ctx_new(void);
void rtx_ctx_free(rtx_ctx_t *ctx);

/* ssrc = original stream's, Annex F.6.2.2; 0 if cap too small */
size_t rtx_build(rtx_ctx_t *ctx, uint32_t ssrc, unsigned char pt, uint32_t timestamp, uint16_t orig_seq, const unsigned char *orig_payload, size_t orig_payload_len, unsigned char *out, size_t cap);

#endif
