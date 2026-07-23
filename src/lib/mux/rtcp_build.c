/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "rtcp_build.h"

#define RTCP_PT_RTPFB 205 /* RFC 4585 transport layer feedback */
#define RTCP_FMT_NACK 1 /* Generic NACK, RFC 4585 6.2.1 */
#define RTCP_PT_RSI 208 /* Receiver Summary Information, Annex F.5.3 */

static void wr16(unsigned char *p, uint16_t v) {
  p[0] = (unsigned char)(v >> 8);
  p[1] = (unsigned char)v;
}

static void wr32(unsigned char *p, uint32_t v) {
  p[0] = (unsigned char)(v >> 24);
  p[1] = (unsigned char)(v >> 16);
  p[2] = (unsigned char)(v >> 8);
  p[3] = (unsigned char)v;
}

size_t rtcp_build_ff(uint32_t sender_ssrc, uint32_t media_ssrc, const rtcp_nack_entry_t *entries, size_t entry_count, unsigned char *out, size_t cap) {
  size_t total, words, i;

  if (entry_count == 0)
    return 0;

  total = 12 + entry_count * 4;
  if (cap < total)
    return 0;

  out[0] = 0x80 | RTCP_FMT_NACK; /* V=2, P=0, FMT=1 */
  out[1] = RTCP_PT_RTPFB;
  words = total / 4 - 1;
  wr16(out + 2, (uint16_t)words);
  wr32(out + 4, sender_ssrc);
  wr32(out + 8, media_ssrc);
  for (i = 0; i < entry_count; i++) {
    wr16(out + 12 + i * 4, entries[i].pid);
    wr16(out + 12 + i * 4 + 2, entries[i].blp);
  }
  return total;
}

size_t rtcp_build_rsi_addr(uint32_t ssrc, uint32_t summarized_ssrc, uint32_t ntp_sec, uint32_t ntp_frac, uint16_t port, const unsigned char *addr, size_t addr_len, unsigned char *out, size_t cap) {
  unsigned srbt;
  size_t sub_len, total, words;

  if (addr_len == 4)
    srbt = 0; /* SRBT 0 = IPv4 unicast feedback address, F.5.3 */
  else if (addr_len == 16)
    srbt = 1; /* SRBT 1 = IPv6 */
  else
    return 0;

  sub_len = 4 + addr_len; /* SRBT+Length+Port header, then the address */
  total = 20 + sub_len;
  if (cap < total)
    return 0;

  out[0] = 0x80; /* V=2, P=0, reserved=0 */
  out[1] = RTCP_PT_RSI;
  words = total / 4 - 1;
  wr16(out + 2, (uint16_t)words);
  wr32(out + 4, ssrc);
  wr32(out + 8, summarized_ssrc);
  wr32(out + 12, ntp_sec);
  wr32(out + 16, ntp_frac);

  out[20] = (unsigned char)srbt;
  out[21] = (unsigned char)(sub_len / 4);
  wr16(out + 22, port);
  memcpy(out + 24, addr, addr_len);

  return total;
}
