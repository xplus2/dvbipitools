/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "rtcp_build.h"

#define RTCP_PT_RTPFB 205 /* RFC 4585 transport layer feedback */
#define RTCP_FMT_NACK 1 /* Generic NACK, RFC 4585 6.2.1 */
#define RTCP_FMT_RAMS 6 /* RAMS, RFC 6285 sec 7 */
#define RTCP_PT_RSI 208 /* Receiver Summary Information, Annex F.5.3 */

#define RTCP_SFMT_RAMS_I 2 /* RFC 6285 7.3 */

#define RTCP_RAMS_TLV_MEDIA_SSRC 31
#define RTCP_RAMS_TLV_FIRST_PACKET_SEQNUM 32
#define RTCP_RAMS_TLV_EARLIEST_JOIN_TIME 33
#define RTCP_RAMS_TLV_BURST_DURATION 34
#define RTCP_RAMS_TLV_MAX_TRANSMIT_BITRATE 35

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

static void wr64(unsigned char *p, uint64_t v) {
  wr32(p, (uint32_t)(v >> 32));
  wr32(p + 4, (uint32_t)v);
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

size_t rtcp_build_rams_i(uint32_t sender_ssrc, uint32_t media_ssrc, uint8_t msn, uint16_t response, const rtcp_rams_i_tlvs_t *tlvs, unsigned char *out, size_t cap) {
  size_t total, words, off;

  total = 16; /* header(4) + sender_ssrc(4) + media_ssrc(4) + SFMT/MSN/Response(4) */
  if (tlvs) {
    if (tlvs->has_media_ssrc_tlv)
      total += 8; /* type 31, 4-byte value */
    if (tlvs->has_first_packet_seqnum)
      total += 8; /* type 32, 2-byte value padded to 4 */
    if (tlvs->has_earliest_join_time)
      total += 8; /* type 33, 4-byte value */
    if (tlvs->has_burst_duration)
      total += 8; /* type 34, 4-byte value */
    if (tlvs->has_max_transmit_bitrate)
      total += 12; /* type 35, 8-byte value */
  }
  if (cap < total)
    return 0;

  out[0] = 0x80 | RTCP_FMT_RAMS; /* V=2, P=0, FMT=6 */
  out[1] = RTCP_PT_RTPFB;
  words = total / 4 - 1;
  wr16(out + 2, (uint16_t)words);
  wr32(out + 4, sender_ssrc);
  wr32(out + 8, media_ssrc);
  out[12] = RTCP_SFMT_RAMS_I;
  out[13] = msn;
  wr16(out + 14, response);

  off = 16;
  if (tlvs && tlvs->has_media_ssrc_tlv) {
    out[off] = RTCP_RAMS_TLV_MEDIA_SSRC;
    out[off + 1] = 0;
    wr16(out + off + 2, 4);
    wr32(out + off + 4, tlvs->media_ssrc_tlv);
    off += 8;
  }
  if (tlvs && tlvs->has_first_packet_seqnum) {
    out[off] = RTCP_RAMS_TLV_FIRST_PACKET_SEQNUM;
    out[off + 1] = 0;
    wr16(out + off + 2, 2);
    wr16(out + off + 4, tlvs->first_packet_seqnum);
    out[off + 6] = 0; /* padding to 4-byte boundary */
    out[off + 7] = 0;
    off += 8;
  }
  if (tlvs && tlvs->has_earliest_join_time) {
    out[off] = RTCP_RAMS_TLV_EARLIEST_JOIN_TIME;
    out[off + 1] = 0;
    wr16(out + off + 2, 4);
    wr32(out + off + 4, tlvs->earliest_join_time_ms);
    off += 8;
  }
  if (tlvs && tlvs->has_burst_duration) {
    out[off] = RTCP_RAMS_TLV_BURST_DURATION;
    out[off + 1] = 0;
    wr16(out + off + 2, 4);
    wr32(out + off + 4, tlvs->burst_duration_ms);
    off += 8;
  }
  if (tlvs && tlvs->has_max_transmit_bitrate) {
    out[off] = RTCP_RAMS_TLV_MAX_TRANSMIT_BITRATE;
    out[off + 1] = 0;
    wr16(out + off + 2, 8);
    wr64(out + off + 4, tlvs->max_transmit_bitrate_bps);
    off += 12;
  }

  return total;
}
