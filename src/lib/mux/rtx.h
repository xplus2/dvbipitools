/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_RTX_H
#define DVBIPITOOLS_LIB_MUX_RTX_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* seq: caller-owned counter, one per RTX session (per client, or shared MC RET session),
   F.3.2.1. seq independent of orig_seq, sent via OSN prefix. atomic: safe to share across threads.
   ssrc = original stream's, Annex F.6.2.2. returns 0 if cap too small. */
size_t rtx_build(_Atomic uint16_t *seq, uint32_t ssrc, unsigned char pt, uint32_t timestamp, uint16_t orig_seq, const unsigned char *orig_payload, size_t orig_payload_len, unsigned char *out, size_t cap);

#endif
