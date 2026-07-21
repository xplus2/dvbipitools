/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISDS_SDS_XML_H
#define DIPISDS_SDS_XML_H

#include <stddef.h>

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

/* payload 0x02 doc. 0 = didn't fit cap */
size_t sds_build_broadcast(const char *domain, unsigned version, const sds_service_t *svcs, int count, unsigned char *buf, size_t cap);

/* payload 0x01 doc, self-pointing Push at push_addr:push_port. lang is the 3-letter ISO 639-2 for display_name. 0 = didn't fit cap */
size_t sds_build_sp(const char *domain, const char *display_name, const char *lang, unsigned version, const char *push_addr, unsigned push_port, unsigned char *buf, size_t cap);

/* xml must be null-terminated. fills out[0..return), tsid/onid default 1, sid defaults to 1-based index if absent */
int sds_parse_broadcast(const char *xml, sds_service_t *out, int max);

/* name="..." bounded to [s,end). 0 ok, -1 not found */
int sds_xml_attr(const char *s, const char *end, const char *name, char *out, size_t outcap);

#endif
