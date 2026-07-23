/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_SDS_XML_H
#define DIPIREC_SDS_XML_H

#include <stddef.h>
#include <stdio.h>

#define SDS_MAX_SERVICES 256
#define SDS_MAX_NAME 128
#define SDS_MAX_ADDR 64

typedef struct {
  char name[SDS_MAX_NAME];
  char address[SDS_MAX_ADDR];
  int family; /* AF_INET or AF_INET6 */
  unsigned port;
  int rtp; /* Streaming="rtp" vs "udp" */
  unsigned tsid, onid, sid;
} sds_service_t;

/* McastType/RTPRetransmission (RETInfoType), same for every service in one announce run */
typedef struct {
  char addr[SDS_MAX_ADDR]; /* RTCPReporting DestinationAddress - RET server's NACK listener */
  unsigned port;           /* RTCPReporting DestinationPort */
  unsigned rtx_time_ms;    /* UnicastRET/MulticastRET rtx-time */
  unsigned char rtx_pt;    /* UnicastRET/MulticastRET RTPPayloadTypeNumber */
  int mc;                  /* also emit MulticastRET */
  unsigned mc_port;        /* MulticastRET DestinationPort, 0 = reuse the service's own port */
} sds_ret_t;

/* streaming BroadcastDiscovery (payload 0x02), one <SingleService> per item call. ret NULL = no RET record */
void sds_broadcast_open(FILE *f, const char *domain, unsigned version);
void sds_broadcast_item(FILE *f, const sds_service_t *s, const sds_ret_t *ret);
void sds_broadcast_close(FILE *f);

/* same document, single-shot into a memory buffer (e.g. for DVBSTP transmission). 0 = didn't fit cap */
size_t sds_build_broadcast(const char *domain, unsigned version, const sds_service_t *svcs, int count, const sds_ret_t *ret, unsigned char *buf, size_t cap);

/* payload 0x01 doc, self-pointing Push at push_addr:push_port. lang is the 3-letter ISO 639-2 for display_name. 0 = didn't fit cap */
size_t sds_build_sp(const char *domain, const char *display_name, const char *lang, unsigned version, const char *push_addr, unsigned push_port, unsigned char *buf, size_t cap);

/* xml must be null-terminated. fills out[0..return), tsid/onid default 1, sid defaults to 1-based index if absent */
int sds_parse_broadcast(const char *xml, sds_service_t *out, int max);

#endif
