/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "rtp.h"
#include "rtx.h"

int rtx_parse(const unsigned char *p, size_t len, unsigned char rtx_pt, rtx_pkt_t *out) {
  rtp_hdr_t h;

  if (!rtp_parse_header(p, len, &h))
    return 0;
  if (h.pt != (rtx_pt & 0x7F))
    return 0;
  if (len < h.payload_off + 2) /* OSN prefix */
    return 0;

  out->ssrc = h.ssrc;
  out->osn = (uint16_t)(((unsigned)p[h.payload_off] << 8) | p[h.payload_off + 1]);
  out->payload = p + h.payload_off + 2;
  out->payload_len = len - (h.payload_off + 2);
  return 1;
}
