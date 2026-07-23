/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_RTX_H
#define DVBIPITOOLS_LIB_DEMUX_RTX_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t ssrc; /* original stream's, Annex F.6.2.2 */
  uint16_t osn; /* original seq, RFC 4588 */
  const unsigned char *payload; /* points into the input buffer */
  size_t payload_len;
} rtx_pkt_t;

/* demux counterpart to lib/mux/rtx.c's rtx_build; 1 on ok (v2 header, pt matches, len), 0 otherwise */
int rtx_parse(const unsigned char *p, size_t len, unsigned char rtx_pt, rtx_pkt_t *out);

#endif
