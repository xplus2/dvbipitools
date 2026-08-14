/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "rtcp.h"

#define RTCP_PT_RTPFB 205 /* RFC 4585 transport layer feedback */
#define RTCP_FMT_NACK 1 /* Generic NACK, RFC 4585 6.2.1 */
#define RTCP_FMT_RAMS 6 /* RAMS, RFC 6285 sec 7 */

#define RTCP_SFMT_RAMS_I 2 /* RFC 6285 7.3, server->client only, not parsed here */

#define RTCP_RAMS_TLV_MEDIA_SSRC 1
#define RTCP_RAMS_TLV_MIN_BUFFER_FILL 2
#define RTCP_RAMS_TLV_MAX_BUFFER_FILL 3
#define RTCP_RAMS_TLV_MAX_BITRATE 4

#define RTCP_RAMS_TLV_FIRST_MC_SEQNUM 61

#define RTCP_PT_SDES 202 /* RFC 3550 6.5 */
#define RTCP_SDES_ITEM_END 0
#define RTCP_SDES_ITEM_CNAME 1

static uint32_t rd32(const unsigned char *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t rd16(const unsigned char *p) {
  return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint64_t rd64(const unsigned char *p) {
  return ((uint64_t)rd32(p) << 32) | rd32(p + 4);
}

static void parse_nack(const unsigned char *p, size_t len, rtcp_nack_cb cb, void *user) {
  rtcp_nack_t nack;
  size_t fci_off, fci_len, i;

  if (len < 12 || !cb)
    return;

  nack.sender_ssrc = rd32(p + 4);
  nack.media_ssrc = rd32(p + 8);
  nack.entry_count = 0;
  nack.truncated = 0;

  fci_off = 12;
  fci_len = len - fci_off; /* may include RFC 3550 padding, worst case one bogus trailing entry */
  for (i = 0; i + 4 <= fci_len && nack.entry_count < RTCP_NACK_MAX_ENTRIES; i += 4) {
    nack.entry[nack.entry_count].pid = rd16(p + fci_off + i);
    nack.entry[nack.entry_count].blp = rd16(p + fci_off + i + 2);
    nack.entry_count++;
  }
  if (nack.entry_count == RTCP_NACK_MAX_ENTRIES && i + 4 <= fci_len)
    nack.truncated = 1;
  if (nack.entry_count > 0)
    cb(&nack, user);
}

/* TLV area per Figure I.5: type(8) + reserved(8) + length(16), value padded to 32-bit boundary */
static void parse_rams_r_tlvs(const unsigned char *p, size_t len, rtcp_rams_r_t *req) {
  size_t off = 0;

  while (off + 4 <= len) {
    unsigned type = p[off];
    uint16_t vlen = rd16(p + off + 2);
    size_t total = 4 + (size_t)vlen;
    size_t padded = (total + 3) & ~(size_t)3;

    if (off + total > len)
      break; /* malformed, stop rather than misparse rest */

    switch (type) {
      case RTCP_RAMS_TLV_MEDIA_SSRC: /* DVB profile: 0-length flag, no value read */
        req->ignore_media_ssrc = 1;
        break;
      case RTCP_RAMS_TLV_MIN_BUFFER_FILL:
        if (vlen >= 4) {
          req->has_min_buffer_fill = 1;
          req->min_buffer_fill_ms = rd32(p + off + 4);
        }
        break;
      case RTCP_RAMS_TLV_MAX_BUFFER_FILL:
        if (vlen >= 4) {
          req->has_max_buffer_fill = 1;
          req->max_buffer_fill_ms = rd32(p + off + 4);
        }
        break;
      case RTCP_RAMS_TLV_MAX_BITRATE:
        if (vlen >= 8) {
          req->has_max_bitrate = 1;
          req->max_bitrate_bps = rd64(p + off + 4);
        }
        break;
      default:
        break; /* preamble-only/enterprise-number/private TLVs, tolerate-and-ignore */
    }
    off += padded;
  }
}

static void parse_rams_r(const unsigned char *p, size_t len, uint32_t sender_ssrc, uint32_t media_ssrc, rtcp_rams_r_cb cb, void *user) {
  rtcp_rams_r_t req;

  if (!cb)
    return;

  req.sender_ssrc = sender_ssrc;
  req.media_ssrc = media_ssrc;
  req.ignore_media_ssrc = 0;
  req.has_min_buffer_fill = 0;
  req.min_buffer_fill_ms = 0;
  req.has_max_buffer_fill = 0;
  req.max_buffer_fill_ms = 0;
  req.has_max_bitrate = 0;
  req.max_bitrate_bps = 0;

  parse_rams_r_tlvs(p, len, &req);
  cb(&req, user);
}

/* only TLV defined for RAMS-T, same generic type/reserved/length shape as RAMS-R's.
   returns 1 if a malformed TLV was hit (parsing stopped early, term has partial data) */
static int parse_rams_t_tlvs(const unsigned char *p, size_t len, rtcp_rams_t_t *term) {
  size_t off = 0;

  while (off + 4 <= len) {
    unsigned type = p[off];
    uint16_t vlen = rd16(p + off + 2);
    size_t total = 4 + (size_t)vlen;
    size_t padded = (total + 3) & ~(size_t)3;

    if (off + total > len)
      return 1; /* malformed, stop rather than misparse rest */

    if (type == RTCP_RAMS_TLV_FIRST_MC_SEQNUM && vlen >= 4) {
      term->has_first_mc_seqnum = 1;
      term->first_mc_seqnum = rd32(p + off + 4);
    }
    /* any other type: not defined for RAMS-T, tolerate-and-ignore */
    off += padded;
  }
  return 0;
}

static void parse_rams_t(const unsigned char *p, size_t len, uint32_t sender_ssrc, uint32_t media_ssrc, rtcp_rams_t_cb cb, rtcp_malformed_cb malformed_cb, void *user) {
  rtcp_rams_t_t term;
  int malformed;

  if (!cb && !malformed_cb)
    return;

  term.sender_ssrc = sender_ssrc;
  term.media_ssrc = media_ssrc;
  term.has_first_mc_seqnum = 0;
  term.first_mc_seqnum = 0;

  malformed = parse_rams_t_tlvs(p, len, &term);
  if (cb)
    cb(&term, user); /* tolerated: partial parse delivered same as always */
  if (malformed && malformed_cb)
    malformed_cb(RTCP_SFMT_RAMS_T, sender_ssrc, media_ssrc, user);
}

static void parse_rams(const unsigned char *p, size_t len, rtcp_rams_r_cb rams_r_cb, rtcp_rams_t_cb rams_t_cb, rtcp_malformed_cb malformed_cb, void *user) {
  uint32_t sender_ssrc, media_ssrc;
  unsigned sfmt;

  if (len < 16) /* SSRC pair (8) + SFMT/reserved (4), at minimum */
    return;

  sender_ssrc = rd32(p + 4);
  media_ssrc = rd32(p + 8);
  sfmt = p[12];

  if (sfmt == RTCP_SFMT_RAMS_R)
    parse_rams_r(p + 16, len - 16, sender_ssrc, media_ssrc, rams_r_cb, user);
  else if (sfmt == RTCP_SFMT_RAMS_T)
    parse_rams_t(p + 16, len - 16, sender_ssrc, media_ssrc, rams_t_cb, malformed_cb, user);
  /* RAMS-I (server->client): not parsed here, intentionally skipped */
}

static void parse_rtpfb(const unsigned char *p, size_t len, unsigned fmt, rtcp_nack_cb nack_cb, rtcp_rams_r_cb rams_r_cb, rtcp_rams_t_cb rams_t_cb, rtcp_malformed_cb malformed_cb, void *user) {
  if (fmt == RTCP_FMT_NACK)
    parse_nack(p, len, nack_cb, user);
  else if (fmt == RTCP_FMT_RAMS)
    parse_rams(p, len, rams_r_cb, rams_t_cb, malformed_cb, user);
  /* other FMT values: valid, intentionally skipped */
}

/* one SDES chunk: SSRC(4) + items (type(8)+len(8)+text) terminated by a type-0 byte, whole
   chunk padded to a 32-bit boundary. RFC 3550 6.5. */
static void parse_sdes(const unsigned char *p, size_t len, unsigned sc, rtcp_sdes_cb cb, void *user) {
  size_t off = 4; /* skip RTCP header (V/P/SC, PT, length) */
  unsigned chunk;

  if (!cb)
    return;

  for (chunk = 0; chunk < sc && off + 4 <= len; chunk++) {
    uint32_t ssrc = rd32(p + off);
    size_t item_off = off + 4;
    rtcp_sdes_t sdes;
    int found_cname = 0;
    size_t chunk_len;

    while (item_off < len && p[item_off] != RTCP_SDES_ITEM_END) {
      unsigned type = p[item_off];
      unsigned ilen;
      if (item_off + 2 > len)
        break; /* malformed, stop this chunk */
      ilen = p[item_off + 1];
      if (item_off + 2 + ilen > len)
        break; /* malformed, stop this chunk */
      if (type == RTCP_SDES_ITEM_CNAME && !found_cname) {
        size_t n = ilen < RTCP_CNAME_MAX - 1 ? ilen : RTCP_CNAME_MAX - 1;
        memcpy(sdes.cname, p + item_off + 2, n);
        sdes.cname[n] = '\0';
        sdes.cname_len = n;
        found_cname = 1;
      }
      item_off += 2 + ilen;
    }
    if (item_off < len)
      item_off++; /* consume terminating type-0 byte, if present */

    chunk_len = ((item_off - off) + 3) & ~(size_t)3; /* pad whole chunk to 32-bit */
    if (chunk_len == 0)
      break; /* can't happen, min chunk is 4 bytes. defensive against infinite loop */

    if (found_cname) {
      sdes.ssrc = ssrc;
      cb(&sdes, user);
    }
    off += chunk_len;
  }
}

void rtcp_parse(const unsigned char *p, size_t len, rtcp_nack_cb nack_cb, rtcp_rams_r_cb rams_r_cb, rtcp_rams_t_cb rams_t_cb, rtcp_sdes_cb sdes_cb, rtcp_malformed_cb malformed_cb, void *user) {
  size_t off = 0;

  while (off + 4 <= len) {
    unsigned version = p[off] >> 6;
    unsigned pt = p[off + 1];
    unsigned fmt = p[off] & 0x1F; /* FMT (RTPFB/PSFB), RC (SR/RR) or SC (SDES), same bit field */
    size_t pkt_len = ((size_t)rd16(p + off + 2) + 1) * 4; /* RFC 3550 6.4.1 */

    if (version != 2 || off + pkt_len > len)
      break; /* malformed, stop rather than misparse the rest */

    if (pt == RTCP_PT_RTPFB)
      parse_rtpfb(p + off, pkt_len, fmt, nack_cb, rams_r_cb, rams_t_cb, malformed_cb, user);
    else if (pt == RTCP_PT_SDES)
      parse_sdes(p + off, pkt_len, fmt, sdes_cb, user);
    /* other types (SR/RR/BYE/PSFB): valid, intentionally skipped */

    off += pkt_len;
  }
}
