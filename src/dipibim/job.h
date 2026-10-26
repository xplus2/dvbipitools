/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBIM_JOB_H
#define DIPIBIM_JOB_H

#include <stddef.h>
#include <stdio.h>

#include "cli/args.h"
#include "lib/bim/codec.h"
#include "lib/bim/decoderinit.h"
#include "lib/bim/dvbstream.h"
#include "lib/bim/fuu.h"
#include "lib/bim/tvafrag.h"
#include "lib/helper/xml_util.h"

#ifdef DIPIBIM_ZLIB
#include "lib/helper/zutil.h"
#define Z_DEFLATE zdeflate
#define Z_INFLATE zinflate
#define ZLIB_BUILT 1
#else
#define Z_DEFLATE NULL
#define Z_INFLATE NULL
#define ZLIB_BUILT 0
#endif

#define FRAG_CODES 0x40

typedef struct {
  const config_t *cfg;
  xsd_set_t *set;
  xsd_index_t *idx; /* schema files below -s, all loads */
  const xsd_compiled_t *c;
  bim_tvans_t ns;
  const char *root_ns; /* SchemaURI[0] */
  bim_encode_opts_t eo;
  bim_decode_opts_t dop;
  bim_decoderinit_t di;
  int di_parsed; /* di from stream or built from root_ns */
  bim_fuu_state_t st;
  xml_doc_t *xml;
  char *buf;
  bim_err_t err;
  char diag[96]; /* encoder failure detail */
  size_t nfrag[FRAG_CODES]; /* fragments per type code, verbose */
  dvbstream_head_t head; /* dvb-tva: stream SchemaURIs */
  size_t nschemas; /* DecoderInit, 0: one */
  const char *uris[DVBTVA_SCHEMAS];
  xsd_set_t *xsets[DVBTVA_SCHEMAS]; /* schemas 1.. */
  const xsd_compiled_t *xc[DVBTVA_SCHEMAS]; /* compiled, NULL unknown */
  dvbtva_schemas_t sch;
} job_t;

/* 23001-1 or 15938-1:2002 payload syntax (generic only) */
int job_mpegb(const config_t *cfg);
/* profiles with Zlib strings */
int job_uses_zlib(const config_t *cfg);
/* main schema: -s file or file declaring ns in -s dirs. 0 ok, -1 error (logged) */
int job_schema_load(job_t *j, const char *ns);
/* profile codecs and compile options, then eo and dop. 0 ok, -1 error (logged) */
int job_schema_compile(job_t *j);
/* SchemaURI[1..] of DecoderInit from -s paths. Unknown: warn, chunks skipped. 0 ok, -1 error (logged) */
int job_schemas_open(job_t *j);
/* extra schemas for foreign namespaces of doc, from -s. After job_schema_compile. 0 ok, -1 error (logged) */
int job_schemas_encode(job_t *j, const bim_node_t *root);
/* binds xsi:type of doc to schema types. 0 ok, -1 error (logged) */
int job_cast_bind(const job_t *j, bim_node_t *root);
void job_free(job_t *j);
/* verbose: one line per fragment type seen */
void job_count_fragment(job_t *j, unsigned code);
void job_log_fragments(const job_t *j);
int job_put_all(FILE *out, const unsigned char *p, size_t n);

/* XML to profile BiM stream. 0 ok, -1 error (logged) */
int job_encode(job_t *j, FILE *in, FILE *out);
/* inverse */
int job_decode(job_t *j, FILE *in, FILE *out);

#endif
