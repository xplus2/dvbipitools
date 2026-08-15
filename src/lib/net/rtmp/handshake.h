/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_RTMP_HANDSHAKE_H
#define DVBIPITOOLS_LIB_NET_RTMP_HANDSHAKE_H

#include <stddef.h>
#include <stdint.h>

#define RTMP_VERSION 3
#define RTMP_HANDSHAKE_SIZE 1536

/* simple handshake only, not Adobe's digest/HMAC variant */

/* fills RTMP_HANDSHAKE_SIZE bytes: 4B timestamp + 4B zero + 1528B random */
void rtmp_handshake_c1(unsigned char *c1, uint32_t timestamp);

/* c2 = s1 verbatim (RTMP_HANDSHAKE_SIZE bytes), timestamp field patched to caller's own */
void rtmp_handshake_c2(unsigned char *c2, const unsigned char *s1, uint32_t timestamp);

#endif
