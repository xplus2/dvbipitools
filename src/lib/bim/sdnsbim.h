/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_SDNSBIM_H
#define DVBIPITOOLS_LIB_BIM_SDNSBIM_H

#include "decoderinit.h"
#include "dvbtva.h"
#include "nodexml.h"

/* SD&S segments as BiM, TS 102 034 5.5.2: DVB-TVA-init 0xF0, DecoderInit with SD&S schema, DVBBiMAccessUnit with
   DVBContextPath 0x0030, zlib strings */

#define BIM_SDNS_CONTEXT_PATH 0x0030
/* TS 102 822-3-2 4.4.2.4.1: ZlibCodec term of urn:tva:metadata:cs:CodecTypeCS:2004 */
#define BIM_SDNS_ZLIB_CODEC "urn:tva:metadata:cs:CodecTypeCS:2004:1"

/* dvbtva_root_fn of the profile. ctx: bim_sdnsroots_t */
typedef struct {
  const xsd_compiled_t *c;
  const char *ns; /* SchemaURI[0], urn:dvb:metadata:iptv:sdns:... */
} bim_sdnsroots_t;
int bim_sdnsbim_root(const void *ctx, unsigned code, bim_tvaroot_t *out);

/* 5.5.2.2, 5.5.2.4: SchemaURI[0] = ns, Zlib signalled for the types c codes with it, empty InitialDescription.
   ns borrowed, write with bim_decoderinit_write, free with bim_sdnsbim_decoderinit_free. 0 ok, -1 oom */
int bim_sdnsbim_decoderinit(const xsd_compiled_t *c, const char *ns, bim_decoderinit_t *di);
void bim_sdnsbim_decoderinit_free(bim_decoderinit_t *di);

/* SD&S segments decoded through a loaded schema, ISO/IEC 23001-1 syntax, Zlib strings */
#define BIM_SDNSDOC_PREFIXES 16

typedef struct {
  xsd_set_t *set;
  const xsd_compiled_t *c;
  char ns[256]; /* SchemaURI[0] */
  bim_inflate_fn inflate; /* caller sets. NULL: Zlib strings not decodable */
  const bim_nsdecl_t *prefixes; /* caller sets: preferred XML prefixes, may be NULL */
  int nprefixes;
  bim_nsdecl_t pfx[BIM_SDNSDOC_PREFIXES];
} bim_sdnsdoc_t;

/* paths as xsd_load_for_ns. ns NULL: file's own. 0 ok, XSD_LOAD_*, -1 error (xsd_set_error of d->set), -2 oom, -3 no namespace */
int bim_sdnsdoc_open(bim_sdnsdoc_t *d, const char *const *paths, int npaths, const char *ns, const char **culprit);
void bim_sdnsdoc_close(bim_sdnsdoc_t *d);
/* au: access unit of one segment (DVBBiMAccessUnit with context path 0x0030). *xml mallocd. 0 ok, -1 not decodable */
int bim_sdnsdoc_to_xml(const bim_sdnsdoc_t *d, const unsigned char *au, size_t len, char **xml, size_t *xml_len);
/* compr as DVBSTP: 0 plain text, 1 BiM. *xml is data or *owned. 0 ok, -1 not decodable, -2 compression unsupported */
int bim_sdnsdoc_segment_xml(const bim_sdnsdoc_t *d, unsigned compr, const unsigned char *data, size_t len, const char **xml, size_t *xml_len, char **owned);

#endif
