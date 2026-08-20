/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "../beutil.h"
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
  be16_put(out + 2, (uint16_t)words);
  be32_put(out + 4, sender_ssrc);
  be32_put(out + 8, media_ssrc);
  for (i = 0; i < entry_count; i++) {
    be16_put(out + 12 + i * 4, entries[i].pid);
    be16_put(out + 12 + i * 4 + 2, entries[i].blp);
  }
  return total;
}

size_t rtcp_build_rsi_header(uint32_t ssrc, uint32_t summarized_ssrc, uint32_t ntp_sec, uint32_t ntp_frac, size_t total_len, unsigned char *out, size_t cap) {
  size_t words;

  if (cap < 20 || total_len < 20 || total_len % 4 != 0)
    return 0;

  out[0] = 0x80; /* V=2, P=0, reserved=0 */
  out[1] = RTCP_PT_RSI;
  words = total_len / 4 - 1;
  be16_put(out + 2, (uint16_t)words);
  be32_put(out + 4, ssrc);
  be32_put(out + 8, summarized_ssrc);
  be32_put(out + 12, ntp_sec);
  be32_put(out + 16, ntp_frac);

  return 20;
}

size_t rtcp_build_rsi_srbt_addr(const unsigned char *addr, size_t addr_len, uint16_t port, unsigned char *out, size_t cap) {
  unsigned srbt;
  size_t sub_len;

  if (addr_len == 4)
    srbt = 0; /* SRBT 0 = IPv4 unicast feedback address, F.5.3 Figure F.8 */
  else if (addr_len == 16)
    srbt = 1; /* SRBT 1 = IPv6, Figure F.8, "not supported in DVB", caller policy to withhold */
  else
    return 0;

  sub_len = 4 + addr_len; /* SRBT(1)+Length(1)+Port(2)+address */
  if (cap < sub_len)
    return 0;

  out[0] = (unsigned char)srbt;
  out[1] = (unsigned char)(sub_len / 4);
  be16_put(out + 2, port);
  memcpy(out + 4, addr, addr_len);

  return sub_len;
}

size_t rtcp_build_rsi_srbt_dns(const char *name, size_t name_len, uint16_t port, unsigned char *out, size_t cap) {
  size_t data_len, padded, total;

  if (name_len == 0 || name_len > 250)
    return 0;

  data_len = name_len + 1; /* NUL terminator, Figure F.8 DNS-name padding rule */
  padded = (data_len + 3) & ~(size_t)3;
  total = 4 + padded;
  if (cap < total)
    return 0;

  out[0] = 2; /* SRBT 2 = DNS name unicast feedback, Figure F.8 */
  out[1] = (unsigned char)(total / 4);
  be16_put(out + 2, port);
  memset(out + 4, 0, padded);
  memcpy(out + 4, name, name_len);

  return total;
}

size_t rtcp_build_rsi_srbt_bandwidth(double kbps, unsigned char *out, size_t cap) {
  uint32_t fixed_point;

  if (cap < 8 || kbps < 0.0 || kbps >= 65536.0)
    return 0;

  out[0] = 11; /* SRBT 11 = "Receiver Bandwidth", Figure F.10 */
  out[1] = 2;  /* sub-report length: 2 32-bit words */
  be16_put(out + 2, 0x4000); /* S=0, R=1 (applies per RET-enabled HNED), Reserved=0 */
  fixed_point = (uint32_t)(kbps * 65536.0); /* Q16.16, binary point between byte 2 and 3 */
  be32_put(out + 4, fixed_point);

  return 8;
}

size_t rtcp_build_rsi_srbt_collision(const uint32_t *ssrcs, size_t count, unsigned char *out, size_t cap) {
  size_t total, i;

  if (count == 0 || count > 255)
    return 0;

  total = 4 + count * 4; /* SRBT(1)+Length(1)+Reserved(2), then n x 32-bit Collision SSRC, Figure F.9 */
  if (cap < total)
    return 0;

  out[0] = 8; /* SRBT 8 = SSRC collision list */
  out[1] = (unsigned char)(total / 4);
  be16_put(out + 2, 0); /* Reserved */
  for (i = 0; i < count; i++)
    be32_put(out + 4 + i * 4, ssrcs[i]);

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
  be16_put(out + 2, (uint16_t)words);
  be32_put(out + 4, sender_ssrc);
  be32_put(out + 8, media_ssrc);
  out[12] = RTCP_SFMT_RAMS_I;
  out[13] = msn;
  be16_put(out + 14, response);

  off = 16;
  if (tlvs && tlvs->has_media_ssrc_tlv) {
    out[off] = RTCP_RAMS_TLV_MEDIA_SSRC;
    out[off + 1] = 0;
    be16_put(out + off + 2, 4);
    be32_put(out + off + 4, tlvs->media_ssrc_tlv);
    off += 8;
  }
  if (tlvs && tlvs->has_first_packet_seqnum) {
    out[off] = RTCP_RAMS_TLV_FIRST_PACKET_SEQNUM;
    out[off + 1] = 0;
    be16_put(out + off + 2, 2);
    be16_put(out + off + 4, tlvs->first_packet_seqnum);
    out[off + 6] = 0; /* padding to 4-byte boundary */
    out[off + 7] = 0;
    off += 8;
  }
  if (tlvs && tlvs->has_earliest_join_time) {
    out[off] = RTCP_RAMS_TLV_EARLIEST_JOIN_TIME;
    out[off + 1] = 0;
    be16_put(out + off + 2, 4);
    be32_put(out + off + 4, tlvs->earliest_join_time_ms);
    off += 8;
  }
  if (tlvs && tlvs->has_burst_duration) {
    out[off] = RTCP_RAMS_TLV_BURST_DURATION;
    out[off + 1] = 0;
    be16_put(out + off + 2, 4);
    be32_put(out + off + 4, tlvs->burst_duration_ms);
    off += 8;
  }
  if (tlvs && tlvs->has_max_transmit_bitrate) {
    out[off] = RTCP_RAMS_TLV_MAX_TRANSMIT_BITRATE;
    out[off + 1] = 0;
    be16_put(out + off + 2, 8);
    be64_put(out + off + 4, tlvs->max_transmit_bitrate_bps);
  }

  return total;
}
