/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_RTCP_H
#define DVBIPITOOLS_LIB_DEMUX_RTCP_H

#include <stddef.h>
#include <stdint.h>

#define RTCP_NACK_MAX_ENTRIES 32 /* FCI entries per RTPFB packet, defensive cap */

#define RTCP_SFMT_RAMS_R 1 /* RFC 6285 7.2 */
#define RTCP_SFMT_RAMS_T 3 /* RFC 6285 7.4 */

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
  int has_first_mc_seqnum; /* type 61: client already on primary MC */
  uint32_t first_mc_seqnum; /* 32-bit extended, not raw 16-bit RTP seq */
} rtcp_rams_t_t;

/* called per RAMS-T termination found */
typedef void (*rtcp_rams_t_cb)(const rtcp_rams_t_t *term, void *user);

/* RAMS-I (Annex I.2.7.3 / RFC 6285 7.3), server->client */
typedef struct {
  uint32_t sender_ssrc;
  uint32_t media_ssrc;
  uint8_t msn;
  uint16_t response;
  int has_media_ssrc_tlv;
  uint32_t media_ssrc_tlv;
  int has_first_packet_seqnum;
  uint16_t first_packet_seqnum;
  int has_earliest_join_time;
  uint32_t earliest_join_time_ms;
  int has_burst_duration;
  uint32_t burst_duration_ms;
  int has_max_transmit_bitrate;
  uint64_t max_transmit_bitrate_bps;
} rtcp_rams_i_t;

/* called per RAMS-I response found */
typedef void (*rtcp_rams_i_cb)(const rtcp_rams_i_t *info, void *user);

#define RTCP_CNAME_MAX 64 /* SDES CNAME, truncated if longer. only uniqueness matters here */

/* RFC 3550 6.5, one SDES chunk with a CNAME item */
typedef struct {
  uint32_t ssrc;
  char cname[RTCP_CNAME_MAX];
  size_t cname_len;
} rtcp_sdes_t;

/* called per SDES chunk that carries a CNAME item (chunks without one are skipped) */
typedef void (*rtcp_sdes_cb)(const rtcp_sdes_t *sdes, void *user);

/* sfmt: RFC 6285 7.1 (1=RAMS-R, 3=RAMS-T). fires alongside normal callback, doesn't
   suppress it. malformed TLV region still yields whatever parsed before corruption hit */
typedef void (*rtcp_malformed_cb)(unsigned sfmt, uint32_t sender_ssrc, uint32_t media_ssrc, void *user);

/* skips SR/RR/BYE; parses SDES CNAME items if sdes_cb given. stops on malformed length, no misparse. any cb may be NULL to ignore that mtype. */
void rtcp_parse(const unsigned char *p, size_t len, rtcp_nack_cb nack_cb, rtcp_rams_r_cb rams_r_cb, rtcp_rams_i_cb rams_i_cb, rtcp_rams_t_cb rams_t_cb, rtcp_sdes_cb sdes_cb, rtcp_malformed_cb malformed_cb, void *user);

#endif
