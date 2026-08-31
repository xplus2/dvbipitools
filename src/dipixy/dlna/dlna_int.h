/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_DLNA_INT_H
#define DIPIXY_DLNA_INT_H

#include <stdint.h>
#include <stdio.h>

#include "dlna.h"

#define CD_URN "urn:schemas-upnp-org:service:ContentDirectory:1"
#define CM_URN "urn:schemas-upnp-org:service:ConnectionManager:1"

/* OP=00: no byte/time seek, matches lack of Range support. FLAGS: streaming + sN-increasing + dlna v1.5, no seek bits */
#define TS_PROTOCOL_INFO "http-get:*:video/mpeg:DLNA.ORG_OP=00;DLNA.ORG_FLAGS=05100000000000000000000000000000"
#define MCAST_IGMP_PROTOCOL_INFO "dvb-igmp:*:33:*"
#define MCAST_MLD_PROTOCOL_INFO "dvb-mld:*:33:*"

typedef enum { OID_ROOT, OID_STDIN, OID_RIST, OID_HTTP, OID_LIST, OID_ITEM } oid_kind_t;
typedef struct {
  oid_kind_t kind;
  unsigned ord, item_num;
} oid_t;

typedef struct {
  char *buf;
  size_t cap;
  size_t len;
} strbuf_t;

typedef struct {
  char *buf;
  size_t len, cap;
} gbuf_t;

/* dlna_oid.c */
int parse_object_id(const char *s, oid_t *out);
const source_def_t *find_source(const config_t *cfg, unsigned ord);
const char *source_kind_str(source_kind_t k);
const char *strip_scheme_at(const char *uri);
void sb_init(strbuf_t *b, char *buf, size_t cap);
void sb_add_n(strbuf_t *b, const char *s, size_t maxn);
void sb_add(strbuf_t *b, const char *s);
void sb_add_u64(strbuf_t *b, uint64_t v);
void build_play_path(const config_t *cfg, oid_kind_t kind, unsigned ord, unsigned item_num, media_type_t media_type, char *out, size_t outsz);

/* dlna_soap.c */
FILE *gbuf_open(gbuf_t *g);
typedef struct {
  const char *name, *value;
} soap_field_t;
int soap_action_response(char **out, size_t *out_len, const char *service_urn, const char *action_response_tag, const soap_field_t *fields, int nfields);
int soap_fault(char **out, size_t *out_len, int upnp_error_code, const char *desc);

/* dlna_didl.c */
int build_didl(const config_t *cfg, const channels_t *channels, const oid_t *oid, int metadata, unsigned starting_index, unsigned requested_count, char **out_didl, unsigned *number_returned, unsigned *total_matches);

#endif
