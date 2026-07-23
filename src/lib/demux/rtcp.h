/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_RTCP_H
#define DVBIPITOOLS_LIB_DEMUX_RTCP_H

#include <stddef.h>
#include <stdint.h>

#define RTCP_NACK_MAX_ENTRIES 32 /* FCI entries per RTPFB packet, defensive cap */

typedef struct {
  uint16_t pid; /* seq of first lost packet */
  uint16_t blp; /* bit n set = seq pid+n+1 also lost */
} rtcp_nack_entry_t;

typedef struct {
  uint32_t sender_ssrc;
  uint32_t media_ssrc;
  rtcp_nack_entry_t entry[RTCP_NACK_MAX_ENTRIES];
  size_t entry_count;
} rtcp_nack_t;

/* called per Generic NACK found */
typedef void (*rtcp_nack_cb)(const rtcp_nack_t *nack, void *user);

/* skips SR/RR/SDES/BYE; stops on malformed length, no misparse */
void rtcp_parse(const unsigned char *p, size_t len, rtcp_nack_cb cb, void *user);

#endif
