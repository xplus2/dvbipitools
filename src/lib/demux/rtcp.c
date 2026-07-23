/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "rtcp.h"

#define RTCP_PT_RTPFB 205 /* RFC 4585 transport layer feedback */
#define RTCP_FMT_NACK 1 /* Generic NACK, RFC 4585 6.2.1 */

static uint32_t rd32(const unsigned char *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t rd16(const unsigned char *p) {
  return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static void parse_rtpfb(const unsigned char *p, size_t len, unsigned fmt, rtcp_nack_cb cb, void *user) {
  rtcp_nack_t nack;
  size_t fci_off, fci_len, i;

  if (fmt != RTCP_FMT_NACK || len < 12 || !cb)
    return;

  nack.sender_ssrc = rd32(p + 4);
  nack.media_ssrc = rd32(p + 8);
  nack.entry_count = 0;

  fci_off = 12;
  fci_len = len - fci_off; /* may include RFC 3550 padding, worst case one bogus trailing entry */
  for (i = 0; i + 4 <= fci_len && nack.entry_count < RTCP_NACK_MAX_ENTRIES; i += 4) {
    nack.entry[nack.entry_count].pid = rd16(p + fci_off + i);
    nack.entry[nack.entry_count].blp = rd16(p + fci_off + i + 2);
    nack.entry_count++;
  }
  if (nack.entry_count > 0)
    cb(&nack, user);
}

void rtcp_parse(const unsigned char *p, size_t len, rtcp_nack_cb cb, void *user) {
  size_t off = 0;

  while (off + 4 <= len) {
    unsigned version = p[off] >> 6;
    unsigned pt = p[off + 1];
    unsigned fmt = p[off] & 0x1F; /* FMT (RTPFB/PSFB) or RC (SR/RR), same bit field */
    size_t pkt_len = ((size_t)rd16(p + off + 2) + 1) * 4; /* RFC 3550 6.4.1 */

    if (version != 2 || off + pkt_len > len)
      break; /* malformed, stop rather than misparse the rest */

    if (pt == RTCP_PT_RTPFB)
      parse_rtpfb(p + off, pkt_len, fmt, cb, user);
    /* other types (SR/RR/SDES/BYE/PSFB): valid, intentionally skipped */

    off += pkt_len;
  }
}
