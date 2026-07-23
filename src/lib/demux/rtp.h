/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_DEMUX_RTP_H
#define DIPIREC_DEMUX_RTP_H

#include <stddef.h>
#include <stdint.h>

/* TS payload offset in RTP datagram, 0 if not RTP. checks v2 + sync */
size_t rtp_payload_offset(const unsigned char *p, size_t len);

typedef struct {
  uint8_t pt;         /* payload type, marker bit masked off */
  uint16_t seq;
  uint32_t timestamp;
  uint32_t ssrc;
  size_t payload_off;
} rtp_hdr_t;

/* fills h, no payload-content check (unlike rtp_payload_offset); 1 on v2 header, 0 too short/bad version */
int rtp_parse_header(const unsigned char *p, size_t len, rtp_hdr_t *h);

#endif
