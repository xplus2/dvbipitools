/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_DVBSTP_H
#define DVBIPITOOLS_LIB_NET_DVBSTP_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "multicast.h"

/* ETSI TS 102 034 clause 5.4.1. recommended max section payload, IPv4 send limit */
#define DVBSTP_MAX_SECTION 1452

/* receive cap: spec size is a recommendation, tolerate jumbo frames / other MTUs */
#define DVBSTP_MAX_RX_SECTION 65535u

/* IPv6 send limit: 1500 mtu, 40 ip6, 8 udp, 28 hdr w/ 128 bit provider id, 4 crc */
#define DVBSTP_MAX_SECTION_V6 1420

/* byte cap per reassembled segment, enforced on send and receive */
#define DVBSTP_MAX_SEGMENT (4u * 1024u * 1024u)

/* payload ids, TS 101 162 V1.9.1 table 52 (TS 102 034 V2.1.1 table 12a informative) */
#define DVBSTP_PAYLOAD_SP_DISCOVERY 0x01
#define DVBSTP_PAYLOAD_BROADCAST_DISCOVERY 0x02
#define DVBSTP_PAYLOAD_COD_DISCOVERY 0x03
#define DVBSTP_PAYLOAD_OTHER_SP_SERVICES 0x04
#define DVBSTP_PAYLOAD_PACKAGE_DISCOVERY 0x05
#define DVBSTP_PAYLOAD_BCG_DISCOVERY 0x06
#define DVBSTP_PAYLOAD_REGIONALISATION_DISCOVERY 0x07
#define DVBSTP_PAYLOAD_RMSFUS_DISCOVERY 0x08
#define DVBSTP_PAYLOAD_SRM 0x09
#define DVBSTP_PAYLOAD_CDS_SESSION 0xB1
#define DVBSTP_PAYLOAD_RMSFUS_FIRMWARE 0xB2
#define DVBSTP_PAYLOAD_APP_DISCOVERY 0xC1

/* BCG over IP payload ids, TS 102 539 table 2 */
#define DVBSTP_PAYLOAD_BCG_TVA_INIT 0xA1
#define DVBSTP_PAYLOAD_BCG_TVAMAIN 0xA2
#define DVBSTP_PAYLOAD_BCG_DATA_CONTAINER 0xA3
#define DVBSTP_PAYLOAD_BCG_INDEX_CONTAINER 0xA4
#define DVBSTP_PAYLOAD_BCG_DATA_AND_INDEX 0xA5
#define DVBSTP_PAYLOAD_BCG_RNT 0xA6
#define DVBSTP_PAYLOAD_BCG_CRI 0xA7

typedef struct {
  unsigned payload_id;
  unsigned segment_id;
  unsigned segment_version;
  unsigned section_number;
  unsigned last_section_number;
  unsigned total_segment_size;
  int crc_present;
  unsigned compr; /* 3 bit compression field, clause 5.4.1.3.4 table 12 */
  int ipv6; /* Ver 01 */
  int has_provider_id;
  unsigned char provider_id[16]; /* IPv4: first 4 bytes */
} dvbstp_header_t;

typedef struct {
  unsigned payload_id;
  unsigned segment_id;
  int has_provider_id;
  unsigned char provider_id[16];
} dvbstp_key_t;

/* TS 102 034 5.4.1.2: segment version mod 256 */
static inline unsigned dvbstp_version_next(unsigned v) { return (v + 1) & 0xFF; }

static inline void dvbstp_key_set(dvbstp_key_t *k, const dvbstp_header_t *h) {
  k->payload_id = h->payload_id;
  k->segment_id = h->segment_id;
  k->has_provider_id = h->has_provider_id;
  memcpy(k->provider_id, h->provider_id, sizeof k->provider_id);
}

static inline int dvbstp_key_match(const dvbstp_key_t *k, const dvbstp_header_t *h) {
  return k->payload_id == h->payload_id && k->segment_id == h->segment_id && k->has_provider_id == h->has_provider_id &&
         !memcmp(k->provider_id, h->provider_id, sizeof k->provider_id);
}

/* one packet's header. ret hdr len, 0 malformed, ver not 00/01, enc not 00. reject !0 payload ids 0x01/0x02 only (table 12).
 * payload-specific compression semantics (e.g. TS 102 539 table 3 for BCG ids) are caller's job */
size_t dvbstp_parse_header(const unsigned char *buf, size_t len, dvbstp_header_t *h);

typedef struct {
  unsigned payload_id;
  unsigned segment_id;
  unsigned segment_version;
  unsigned compr;
  int has_provider_id;
  unsigned char provider_id[16]; /* 4 bytes on IPv4, 16 on IPv6 */
  int want_crc;
  int64_t pacing_budget_ns; /* max total inter-section delay, 0 = fixed 1 ms gap */
} dvbstp_send_t;

/* splits data into sections, sends each. compr written into header as-is,
   caller must pass value legal for payload_id. crc on last section if want_crc */
int dvbstp_send_segment(mcast_t *m, const dvbstp_send_t *seg, const unsigned char *data, size_t len);

typedef struct dvbstp_reasm dvbstp_reasm_t;

dvbstp_reasm_t *dvbstp_reasm_new(void);
void dvbstp_reasm_free(dvbstp_reasm_t *r);

/* one datagram in. 1 = segment complete, out_header/out_data/out_len valid (out_data owned by r). 0 = incomplete or rejected */
int dvbstp_reasm_feed(dvbstp_reasm_t *r, const unsigned char *pkt, size_t len, dvbstp_header_t *out_header, const unsigned char **out_data, size_t *out_len);

typedef int (*dvbstp_segment_fn)(void *ctx, const dvbstp_header_t *h, const unsigned char *data, size_t len);
int dvbstp_listen(mcast_t *m, double timeout_s, dvbstp_segment_fn fn, void *ctx);

#endif
