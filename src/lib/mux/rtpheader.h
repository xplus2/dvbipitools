/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_RTPHEADER_H
#define DVBIPITOOLS_LIB_MUX_RTPHEADER_H

#include <stddef.h>
#include <stdint.h>

#include "../beutil.h"

typedef struct rtpheader rtpheader_t;

rtpheader_t *rtpheader_new(void);
void rtpheader_free(rtpheader_t *r);

/* 12-byte RTP/MP2T header (RFC 2250, PT 33), seq auto-increments, SSRC fixed per session. 0 on overflow, else 12 */
size_t rtpheader_build(rtpheader_t *r, uint32_t pts_90k, unsigned char *out, size_t cap);

/* 12-byte fixed RTP header (RFC 3550 5.1): V=2,P=0,X=0,CC=0,M=0, then PT/seq/timestamp/ssrc */
static inline void rtp_write_header(unsigned char *out, unsigned char pt, uint16_t seq, uint32_t timestamp, uint32_t ssrc) {
  out[0] = 0x80;
  out[1] = (unsigned char)(pt & 0x7F);
  be16_put(out + 2, seq);
  be32_put(out + 4, timestamp);
  be32_put(out + 8, ssrc);
}

#endif
