/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_RTCP_BUILD_H
#define DVBIPITOOLS_LIB_MUX_RTCP_BUILD_H

#include <stddef.h>
#include <stdint.h>

#include "lib/demux/rtcp.h"

/* F.5.2, same shape as Generic NACK; sender_ssrc = triggering client, not us; 0 if entry_count 0 or cap small */
size_t rtcp_build_ff(uint32_t sender_ssrc, uint32_t media_ssrc, const rtcp_nack_entry_t *entries, size_t entry_count, unsigned char *out, size_t cap);

/* F.5.3 unicast-feedback-address sub-report; addr_len 4=IPv4, 16=IPv6; ntp_sec/frac = NTP epoch 1900, not Unix; 0 on bad addr_len/cap */
size_t rtcp_build_rsi_addr(uint32_t ssrc, uint32_t summarized_ssrc, uint32_t ntp_sec, uint32_t ntp_frac, uint16_t port, const unsigned char *addr, size_t addr_len, unsigned char *out, size_t cap);

#endif
