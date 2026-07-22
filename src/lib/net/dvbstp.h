/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISDS_NET_DVBSTP_H
#define DIPISDS_NET_DVBSTP_H

#include <stddef.h>

#include "multicast.h"

/* ETSI TS 102 034 clause 5.4.1. recommended max to stay under ethernet MTU */
#define DVBSTP_MAX_SECTION 1452

/* payload ids used by SD&S, table 1 */
#define DVBSTP_PAYLOAD_SP_DISCOVERY 0x01
#define DVBSTP_PAYLOAD_BROADCAST_DISCOVERY 0x02

/* BCG over IP payload ids, TS 102 539 table 2 */
#define DVBSTP_PAYLOAD_BCG_TVA_INIT 0xA1
#define DVBSTP_PAYLOAD_BCG_TVAMAIN 0xA2
#define DVBSTP_PAYLOAD_BCG_DATA_CONTAINER 0xA3
#define DVBSTP_PAYLOAD_BCG_INDEX_CONTAINER 0xA4
#define DVBSTP_PAYLOAD_BCG_DATA_AND_INDEX 0xA5

typedef struct {
  unsigned payload_id;
  unsigned segment_id;
  unsigned segment_version;
  unsigned section_number;
  unsigned last_section_number;
  unsigned total_segment_size;
  int crc_present;
  int has_provider_id;
  unsigned provider_id;
} dvbstp_header_t;

/* one packet's header. returns header len, 0 if malformed */
size_t dvbstp_parse_header(const unsigned char *buf, size_t len, dvbstp_header_t *h);

/* splits data into sections, sends each. no compression. crc on last section if want_crc */
int dvbstp_send_segment(mcast_t *m, unsigned payload_id, unsigned segment_id, unsigned segment_version, int has_provider_id, unsigned provider_id, int want_crc, const unsigned char *data, size_t len);

typedef struct dvbstp_reasm dvbstp_reasm_t;

dvbstp_reasm_t *dvbstp_reasm_new(void);
void dvbstp_reasm_free(dvbstp_reasm_t *r);

/* one datagram in. 1 = segment complete, out_header/out_data/out_len valid (out_data owned by r). 0 = incomplete or rejected */
int dvbstp_reasm_feed(dvbstp_reasm_t *r, const unsigned char *pkt, size_t len, dvbstp_header_t *out_header, const unsigned char **out_data, size_t *out_len);

#endif
