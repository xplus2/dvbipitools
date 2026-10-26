/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBCG_SCHEMA_H
#define DIPIBCG_SCHEMA_H

#include <stddef.h>

#include "cli/args.h"
#include "lib/bim/bcgau.h"
#include "lib/bim/xsd/xsd.h"

#ifdef DIPIBCG_ZLIB
#include "lib/helper/zutil.h"
#define BCG_DEFLATE zdeflate
#define BCG_INFLATE zinflate
#else
#define BCG_DEFLATE NULL
#define BCG_INFLATE NULL
#endif

/* TV-Anytime schema of stream, derived data */
typedef struct {
  xsd_set_t *set;
  xsd_index_t *idx; /* schema files below cfg->schemas, open to close */
  xsd_set_t *xsets[DVBTVA_SCHEMAS]; /* schemas 1.. */
  const xsd_compiled_t *xc[DVBTVA_SCHEMAS]; /* compiled schemas 1.., NULL unknown */
  dvbtva_schemas_t tab; /* decode: SchemaID order */
  dvbtva_schemas_t etab; /* encode: known ones packed */
  bcgau_t au;
  char ns[256]; /* SchemaURI[0], tns of schema */
  char *euris[DVBTVA_SCHEMAS]; /* encode SchemaURI[1..], owned, au.eo.schema_uris */
  unsigned char *init; /* BCG payload 0xA1: DVB-TVA-init and DecoderInit */
  size_t init_len;
} bcg_schema_t;

/* schema from cfg->schemas: file given or 1 declaring ns in dirs. startup: code type. 0 ok, -1 error (logged) */
int bcg_schema_open(bcg_schema_t *s, const config_t *cfg, const char *ns, int startup);
/* SchemaURI[1..n-1] from cfg->schemas for au.dop. Unknown: warn, chunks skipped. 0 ok, -1 error (logged) */
int bcg_schema_extras(bcg_schema_t *s, const config_t *cfg, const char *const *uris, size_t n, int startup);
/* encode: known schemas of uris[1..n-1] become chunk schemas and DecoderInit URIs (copied). 0 ok, -1 error */
int bcg_schema_wire(bcg_schema_t *s, const char *const *uris, size_t n);
/* encode: schemas for foreign namespaces of root, from cfg->schemas, then wired. no file: stays wildcard. 0 ok, -1 error (logged) */
int bcg_schema_encode(bcg_schema_t *s, const config_t *cfg, const bim_node_t *root, int startup);
/* binds xsi:type of root to schema types. 0 ok, -1 error (logged) */
int bcg_cast_bind(const bcg_schema_t *s, bim_node_t *root, int startup);
void bcg_schema_close(bcg_schema_t *s);

#endif
