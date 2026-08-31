/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_SDS_XML_H
#define DIPIREC_SDS_XML_H

#include <stddef.h>
#include <stdio.h>

#define SDS_MAX_SERVICES 256
#define SDS_MAX_NAME 128
#define SDS_MAX_ADDR 64
#define SDS_MAX_PACKAGES 64
#define SDS_MAX_PKG_SERVICES 64
#define SDS_MAX_CELLS 64
#define SDS_MAX_CA_DEPTH 8

typedef struct {
  char addr[SDS_MAX_ADDR];
  unsigned port;
  unsigned rtx_time_ms;
  unsigned char rtx_pt;
  int mc;
  unsigned mc_port;
  int rsi_mc_ret; /* RSI (Annex F.5.3) */
} sds_ret_t;

typedef struct {
  char addr[SDS_MAX_ADDR];
  unsigned port;
  unsigned rtx_time_ms;
  unsigned char rtx_pt;
  int resolve_by_port;
  unsigned resolve_base_port;
  size_t resolve_max_channels;
} sds_fcc_t;

typedef struct {
  char name[SDS_MAX_NAME];
  char address[SDS_MAX_ADDR];
  int family;
  unsigned port;
  int rtp;
  unsigned tsid, onid, sid;
  unsigned max_bitrate_kbps;
  int has_bitrate;
  unsigned content_nibble;
  int has_content_nibble;
  int has_ret;
  sds_ret_t ret;
  int has_fcc;
  sds_fcc_t fcc;
} sds_service_t;

/* Package (PackagedServices/Package, TS 102 034 clause 5.2.13.4) */
typedef struct {
  unsigned id;
  char name[SDS_MAX_NAME];
  char lang[4]; /* ISO 639-2 */
  int visible;  /* default true */
  char service_names[SDS_MAX_PKG_SERVICES][SDS_MAX_NAME]; /* matched into announced service list by name */
  int service_count;
} sds_package_t;

/* nested civic address, RFC 4676. regionalisation Cell: single outer-to-inner chain, not general tree */
typedef struct {
  unsigned type;
  char value[SDS_MAX_NAME];
} sds_civic_addr_t;

/* Cell (RegionalisationOffering/Cell, clause 5.2.13.8) */
typedef struct {
  char id[SDS_MAX_NAME];
  char country[4]; /* ISO 3166 2-letter */
  sds_civic_addr_t ca[SDS_MAX_CA_DEPTH]; /* ca[0] outermost, nested inward */
  int ca_depth;
} sds_cell_t;

/* RMSType (clause 5.2.12.28) */
typedef struct {
  char name[SDS_MAX_NAME];
  char lang[4];
  const char *location; /* required */
  const char *logo_uri; /* NULL = omit */
} sds_rms_t;

/* FUSType (clause 5.2.12.12), minus Description. FUSAnnouncement reduced to MulticastAnnouncementAddress */
typedef struct {
  char name[SDS_MAX_NAME];
  char lang[4];
  unsigned long fus_id; /* decimal */
  const char *announce_addr; /* NULL = omit */
  unsigned announce_port;
  const char *logo_uri; /* NULL = omit */
} sds_fus_t;

/* streaming BroadcastDiscovery (payload 0x02), one <SingleService> per item call. ret/fcc NULL = no such record */
void sds_broadcast_open(FILE *f, const char *domain, unsigned version);
void sds_broadcast_item(FILE *f, const sds_service_t *s, const sds_ret_t *ret, const sds_fcc_t *fcc);
void sds_broadcast_close(FILE *f);

/* same document, single-shot into a memory buffer. 0 = didn't fit cap */
size_t sds_build_broadcast(const char *domain, unsigned version, const sds_service_t *svcs, int count, const sds_ret_t *ret, const sds_fcc_t *fcc, unsigned char *buf, size_t cap);

/* payload 0x01. push_addr/push_port: this announcer's own delivery point. lang: ISO 639-2 for display_name.
   extra_payload_ids: other PayloadIds sharing this push socket. 0 = didn't fit cap */
size_t sds_build_sp(const char *domain, const char *display_name, const char *lang, unsigned version, const char *push_addr, unsigned push_port, const unsigned *extra_payload_ids, int extra_count, unsigned char *buf, size_t cap);

/* streaming PackageDiscovery (payload 0x05), one <Package> per item call. svcs/svc_count for DVBTriplet lookup */
void sds_package_open(FILE *f, const char *domain, unsigned version);
void sds_package_item(FILE *f, const sds_package_t *pkg, const sds_service_t *svcs, int svc_count);
void sds_package_close(FILE *f);
size_t sds_build_package(const char *domain, unsigned version, const sds_package_t *pkgs, int pkg_count, const sds_service_t *svcs, int svc_count, unsigned char *buf, size_t cap);

/* streaming RegionalisationDiscovery (payload 0x07), one <Cell> per item call */
void sds_regionalisation_open(FILE *f, const char *domain, unsigned version);
void sds_regionalisation_item(FILE *f, const sds_cell_t *cell);
void sds_regionalisation_close(FILE *f);
size_t sds_build_regionalisation(const char *domain, unsigned version, const sds_cell_t *cells, int count, unsigned char *buf, size_t cap);

/* single-shot RMSFUSDiscovery (payload 0x08). exactly one of rms_count/fus_count nonzero: XSD choice,
   FUSProvider+ or RMSProvider+, never both. 0 = didn't fit cap */
size_t sds_build_rms_fus(const char *domain, unsigned version, const sds_rms_t *rms, int rms_count, const sds_fus_t *fus, int fus_count, unsigned char *buf, size_t cap);

/* xml null-terminated. out[0..return) filled. tsid/onid default 1, sid defaults to 1-based index if absent.
   truncated NULL ok, else 1 if more entries existed past max */
int sds_parse_broadcast(const char *xml, sds_service_t *out, int max, int *truncated);

#endif
