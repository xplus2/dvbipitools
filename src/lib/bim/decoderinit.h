/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_DECODERINIT_H
#define DVBIPITOOLS_LIB_BIM_DECODERINIT_H

#include <stddef.h>
#include <stdint.h>

#include "bimio.h"

/* ISO 15938-1:2002 7.2, ISO 23001-1 6.2 (Cor 1, Cor 2, Amd 2) */

typedef struct {
  char *uri;
  uint64_t *types; /* TypeIdentificationCode */
  size_t ntypes;
} bim_di_codec_t;

/* hint: BinaryLocationHint for known additional schema */
typedef struct {
  uint64_t id;
  char *uri;
  char *hint;
  bim_di_codec_t *codecs;
  size_t ncodecs;
  bim_ext_t castable;
  bim_ext_t substitutable;
} bim_di_schema_t;

typedef struct {
  unsigned char *bits;
  uint64_t nbits;
  uint64_t code;
} bim_di_cpath_t;

/* AdvancedOptimisedDecoderInstantiation: raw = Type, Params(), padding */
typedef struct {
  uint64_t type;
  unsigned char *raw;
  size_t nraw;
} bim_di_instance_t;

typedef struct {
  uint64_t schema;
  uint64_t type;
} bim_di_typeref_t;

typedef struct {
  int preserve_default;
  unsigned reserved;
  uint64_t *instances;
  size_t ninstances;
  bim_di_typeref_t *types;
  size_t ntypes;
} bim_di_mapping_t;

typedef struct {
  char *uri;
  char *prefix;
} bim_di_prefix_t;

typedef struct {
  int mpegb; /* 23001-1 syntax, 0: 15938-1:2002 */
  uint64_t profile; /* SystemsProfileLevelIndication */
  unsigned unit_size_code;
  unsigned reserved; /* ReservedBits: 5 (15938-1:2002) or 4 bits */
  int no_advanced;
  int insert;
  int aod; /* AdvancedOptimisedDecodersFlag */
  int add_schema;
  int add_schema_only;
  int frag_ref;
  int mpc_only;
  int hier;
  int cpt; /* ContextPathTableFlag */
  int prefix_table;
  bim_di_schema_t *schemas;
  size_t nschemas;
  uint64_t cpt_code_len;
  int cpt_complete;
  bim_di_cpath_t *cpaths;
  size_t ncpaths;
  char **aod_types;
  size_t naod_types;
  bim_di_instance_t *instances;
  size_t ninstances;
  bim_di_mapping_t *mappings;
  size_t nmappings;
  unsigned char *frag_formats;
  size_t nfrag_formats;
  uint64_t nadditional; /* NumberOfAdditionalSchemas */
  bim_di_schema_t *known;
  size_t nknown;
  unsigned schema_encoding;
  bim_ext_t castable; /* InitialSchema */
  bim_ext_t substitutable;
  bim_di_prefix_t *prefixes;
  size_t nprefixes;
  unsigned char *initial; /* InitialDocument, access unit syntax */
  size_t ninitial;
} bim_decoderinit_t;

/* global type and element counts (6.7.5.3, 6.7.6.3) of known additional schema uri,
   NULL: initial schema (6.2.2). 0 known, -1 unknown */
typedef int (*bim_di_counts_fn)(void *ctx, const char *uri, uint64_t *ntypes, uint64_t *nelems);

/* *used: DecoderInit bytes. counts NULL: code lists refused. 0 ok, -1 error (err), di freed */
int bim_decoderinit_parse(const unsigned char *buf, size_t len, int mpegb, bim_di_counts_fn counts, void *ctx, bim_decoderinit_t *di,
  size_t *used, bim_err_t *err);
/* lengths computed. 0 ok, -1 invalid (err) or oom */
int bim_decoderinit_write(const bim_decoderinit_t *di, bim_di_counts_fn counts, void *ctx, bitwriter_t *bw, bim_err_t *err);
void bim_decoderinit_free(bim_decoderinit_t *di);
/* AdvancedOptimisedDecodersConfig (8.3.1) of a FUU (6.4.2) into out->instances, out->mappings */
int bim_di_aod_config_parse(bitreader_t *br, const bim_decoderinit_t *di, bim_decoderinit_t *out, bim_err_t *err);
/* inverse of bim_di_aod_config_parse, di: the DecoderInit it belongs to */
int bim_di_aod_config_write(bitwriter_t *bw, const bim_decoderinit_t *di, const bim_decoderinit_t *cfg, bim_err_t *err);
/* NumberOfSchemas + NumberOfAdditionalSchemas, saturated */
uint64_t bim_di_nall(const bim_decoderinit_t *di);

#endif
