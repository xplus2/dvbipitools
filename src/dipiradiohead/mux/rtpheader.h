/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_MUX_RTPHEADER_H
#define DIPIRADIOHEAD_MUX_RTPHEADER_H

#include <stddef.h>
#include <stdint.h>

typedef struct rtpheader rtpheader_t;

rtpheader_t *rtpheader_new(void);
void rtpheader_free(rtpheader_t *r);

/* 12-byte RTP/MP2T header (RFC 2250, PT 33); seq auto-increments, SSRC fixed per session. 0 on overflow, else 12 */
size_t rtpheader_build(rtpheader_t *r, uint32_t pts_90k, unsigned char *out, size_t cap);

#endif
