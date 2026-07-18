/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_DEMUX_RTP_H
#define DIPIREC_DEMUX_RTP_H

#include <stddef.h>

/* TS payload offset in RTP datagram, 0 if not RTP. checks v2 + sync */
size_t rtp_payload_offset(const unsigned char *p, size_t len);

#endif
