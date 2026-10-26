/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_FUU_H
#define DVBIPITOOLS_LIB_BIM_FUU_H

#include <stddef.h>
#include <stdint.h>

#include "aod.h"
#include "bimau.h"
#include "codec.h"

/* ISO 23001-1 6.4 .. 6.6, ISO 15938-1:2002 7.4 .. 7.6 */

typedef enum { BIM_TBC_ELEM, BIM_TBC_ATTR, BIM_TBC_SIMPLE, BIM_TBC_CHARDATA, BIM_TBC_PARENT, BIM_TBC_END } bim_tbc_kind_t;

/* pos: integer or binary fraction (bit 63 = 1/2). type: ELEM effective, parent for others */
typedef struct {
  bim_tbc_kind_t kind;
  const char *name;
  const char *ns;
  const bim_type_t *type;
  const bim_simple_t *simple;
  int has_pos;
  uint64_t pos;
  int owned; /* name heap (lax attribute) */
} bim_tbc_t;

#define BIM_CMD_ADD 1
#define BIM_CMD_REPLACE 2
#define BIM_CMD_DELETE 3
#define BIM_CMD_RESET 4

typedef struct {
  unsigned command;
  uint64_t schema;
  unsigned mode; /* ContextModeCode */
  int user_data; /* User Data Extension Code: rest skipped */
  int reparam; /* OptimisedDecodersReparameterization: 1 keep, 2 DecoderInit's, 0 new config */
  bim_decoderinit_t *config; /* reparam 0: instances and mappings, heap */
  uint64_t path_code; /* ContextPathCode of mode 101 */
  bim_tbc_t *tbcs;
  size_t ntbcs;
  size_t npayloads;
  uint64_t *pos; /* npayloads x ntbcs */
  bim_node_t **payloads; /* ELEM operand */
  char **values; /* ATTR, SIMPLE, CHARDATA operand */
} bim_fuu_t;

#define BIM_CTX_MAX 64

/* schemas: NumberOfSchemas + NumberOfAdditionalSchemas, castable/substitutable per schema or NULL */
typedef struct {
  const bim_decoderinit_t *di;
  const bim_schema_t *schemas;
  const bim_ext_t *const *castable;
  const bim_ext_t *const *substitutable;
  size_t nschemas;
  bim_decode_opts_t opts; /* payloads, schemas filled from above */
  bim_aod_t *aod_default; /* DecoderInit mappings */
  bim_aod_t *aod_cur; /* after OptimisedDecodersReparameterization */
  bim_node_t *doc; /* topmost node */
  bim_node_t *ctx[BIM_CTX_MAX]; /* context node chain from topmost */
  const bim_type_t *ctx_type[BIM_CTX_MAX];
  int nctx;
  uint64_t last_schema;
} bim_fuu_state_t;

/* 0 ok, -1 error (err), f freed */
int bim_fuu_parse(bim_fuu_state_t *st, const bim_bits_t *fuu, bim_fuu_t *f, bim_err_t *err);
/* inverse of bim_fuu_parse: f as FUU bits after FUU_Length, st as before applying f. payloads are written in
   position increment order. 0 ok, -1 error (err) */
int bim_fuu_write(bim_fuu_state_t *st, const bim_fuu_t *f, const bim_encode_opts_t *eo, strrepo_writer_t *sw, bitwriter_t *bw, bim_err_t *err);
/* 6.5 commands on st->doc, payloads moved into doc. reset: caller decodes InitialDocument afterwards */
int bim_fuu_apply(bim_fuu_state_t *st, bim_fuu_t *f, bim_err_t *err);
void bim_fuu_free(bim_fuu_t *f);
void bim_fuu_state_free(bim_fuu_state_t *st);
/* aod_default from di mappings, opts.aod set. 0 ok, -1 error (err) */
int bim_fuu_state_aod_init(bim_fuu_state_t *st, bim_err_t *err);
/* frees document + decoders */
void bim_fuu_state_close(bim_fuu_state_t *st);

#endif
