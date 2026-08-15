/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <netinet/in.h>
#include <string.h>

#include "lib/demux/rtp.h"

#include "priv.h"

/* walks IPv6 extension headers from *hdr_off to find the L4 header, updating *next_header
   as it goes. 1: found UDP (next_header==17). 0: unsupported header or truncated packet */
static int walk_ipv6_ext_headers(const unsigned char *pkt, size_t len, size_t *hdr_off, unsigned *next_header) {
  for (;;) {
    if (*next_header == 17) /* UDP */
      return 1;
    if (*next_header == 0 || *next_header == 60 || *next_header == 43) {
      /* Hop-by-Hop / Destination Options / Routing: next-header(1) + len-in-8-octet-units-minus-1(1) + data */
      unsigned ext_len;
      if (len < *hdr_off + 2)
        return 0;
      ext_len = pkt[*hdr_off + 1];
      *next_header = pkt[*hdr_off];
      *hdr_off += ((size_t)ext_len + 1) * 8;
      continue;
    }
    if (*next_header == 44) { /* Fragment header: fixed 8 bytes */
      if (len < *hdr_off + 8)
        return 0;
      *next_header = pkt[*hdr_off];
      *hdr_off += 8;
      continue;
    }
    return 0; /* AH/ESP or anything else unsupported: documented scope limit */
  }
}

void capture_handle_frame(const unsigned char *pkt, size_t len, const cidr_t *ranges, size_t range_count, capture_frame_cb cb, void *user) {
  size_t off, ip_off, udp_off, rtp_off, addr_len;
  unsigned ethertype, dport;
  int family;
  struct in_addr dst4;
  struct in6_addr dst6;
  const void *dst_bytes;
  unsigned char dscp;
  rtp_hdr_t rtp;

  if (len < 14)
    return;
  ethertype = ((unsigned)pkt[12] << 8) | pkt[13];
  off = 14;
  if (ethertype == 0x8100) { /* single VLAN tag */
    if (len < off + 4)
      return;
    ethertype = ((unsigned)pkt[off + 2] << 8) | pkt[off + 3];
    off += 4;
  }

  if (ethertype == 0x0800) {
    unsigned ihl, proto;

    ip_off = off;
    if (len < ip_off + 20 || (pkt[ip_off] >> 4) != 4)
      return;
    ihl = (unsigned)(pkt[ip_off] & 0x0F) * 4;
    if (ihl < 20) /* RFC 791 min IHL is 5 (20 bytes) */
      return;
    dscp = pkt[ip_off + 1] & 0xFC; /* TOS byte, top 6 bits (DSCP), ECN masked off */
    proto = pkt[ip_off + 9];
    if (proto != 17 || len < ip_off + ihl + 8) /* UDP only */
      return;
    memcpy(&dst4, pkt + ip_off + 16, 4);
    family = AF_INET;
    dst_bytes = &dst4;
    addr_len = sizeof dst4;
    udp_off = ip_off + ihl;
  } else if (ethertype == 0x86DD) {
    unsigned next_header;
    size_t hdr_off;

    ip_off = off;
    if (len < ip_off + 40 || (pkt[ip_off] >> 4) != 6)
      return;
    dscp = (unsigned char)(((pkt[ip_off] & 0x0F) << 4) | (pkt[ip_off + 1] >> 4)); /* Traffic Class split across both bytes */
    dscp &= 0xFC; /* top 6 bits (DSCP), ECN masked off */
    next_header = pkt[ip_off + 6];
    memcpy(&dst6, pkt + ip_off + 24, 16);
    hdr_off = ip_off + 40;

    if (!walk_ipv6_ext_headers(pkt, len, &hdr_off, &next_header))
      return;
    if (len < hdr_off + 8)
      return;
    family = AF_INET6;
    dst_bytes = &dst6;
    addr_len = sizeof dst6;
    udp_off = hdr_off;
  } else {
    return; /* not IPv4 or IPv6 */
  }

  if (!in_ranges(family, dst_bytes, ranges, range_count)) /* userspace whitelist, authoritative regardless of installed kernel filter */
    return;

  dport = ((unsigned)pkt[udp_off + 2] << 8) | pkt[udp_off + 3];
  rtp_off = udp_off + 8;
  if (rtp_off > len)
    return;

  if (rtp_payload_offset(pkt + rtp_off, len - rtp_off) == 0) /* not RTP-wrapped TS */
    return;
  if (!rtp_parse_header(pkt + rtp_off, len - rtp_off, &rtp))
    return;

  if (!cb)
    return;
  cb(family, dst_bytes, addr_len, dport, dscp, rtp.ssrc, rtp.seq, rtp.timestamp, pkt + rtp_off + rtp.payload_off, len - rtp_off - rtp.payload_off, user);
}
