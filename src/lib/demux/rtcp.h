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
  int truncated; /* more FCI entries were present than RTCP_NACK_MAX_ENTRIES could hold */
} rtcp_nack_t;

/* called per Generic NACK found */
typedef void (*rtcp_nack_cb)(const rtcp_nack_t *nack, void *user);

/* RAMS-R (Annex I.2.7.2 / RFC 6285 7.2), FCI carries exactly one request */
typedef struct {
  uint32_t sender_ssrc;
  uint32_t media_ssrc;
  int ignore_media_ssrc;    /* type 1 present (DVB profile: 0-length flag) */
  int has_min_buffer_fill;
  uint32_t min_buffer_fill_ms; /* type 2 */
  int has_max_buffer_fill;
  uint32_t max_buffer_fill_ms; /* type 3 */
  int has_max_bitrate;
  uint64_t max_bitrate_bps; /* type 4 */
} rtcp_rams_r_t;

/* called per RAMS-R request found */
typedef void (*rtcp_rams_r_cb)(const rtcp_rams_r_t *req, void *user);

/* RAMS-T (Annex I.2.7.4 / RFC 6285 7.4), FCI carries exactly one termination */
typedef struct {
  uint32_t sender_ssrc;
  uint32_t media_ssrc;
  int has_first_mc_seqnum; /* type 61 present - client already joined, got primary MC packet */
  uint32_t first_mc_seqnum; /* extended RTP seqnum of first multicast packet received */
} rtcp_rams_t_t;

/* called per RAMS-T termination found */
typedef void (*rtcp_rams_t_cb)(const rtcp_rams_t_t *term, void *user);

/* skips SR/RR/SDES/BYE/RAMS-I; stops on malformed length, no misparse. any cb may be NULL to ignore that mtype. */
void rtcp_parse(const unsigned char *p, size_t len, rtcp_nack_cb nack_cb, rtcp_rams_r_cb rams_r_cb, rtcp_rams_t_cb rams_t_cb, void *user);

#endif
