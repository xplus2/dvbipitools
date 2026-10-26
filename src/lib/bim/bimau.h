/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_BIMAU_H
#define DVBIPITOOLS_LIB_BIM_BIMAU_H

#include <stddef.h>
#include <stdint.h>

#include "decoderinit.h"

/* ISO 23001-1 6.3, 6.7 (Cor 2), ISO 15938-1:2002 7.3 */

/* partial: codes[i] with counts[i], else counts per code. attributes: codes only */
typedef struct {
  uint64_t nglobal;
  bim_ext_t ext;
  int partial;
  uint64_t *codes;
  uint64_t *counts;
  size_t n;
} bim_suu_table_t;

typedef struct {
  uint64_t schema; /* SchemaToUpdate */
  int first;
  char *ns;
  char **imported;
  size_t nimported;
  bim_suu_table_t types;
  bim_suu_table_t elems;
  bim_suu_table_t attrs;
  bim_bits_t encoded; /* BinaryEncodedSchema (6.7.8.2) */
} bim_suu_t;

typedef struct {
  bim_suu_t *suus;
  size_t nsuus;
  bim_bits_t *fuus; /* after FUU_Length */
  size_t nfuus;
} bim_au_t;

typedef struct {
  char *ns;
  uint64_t ntypes;
  uint64_t nelems;
  uint64_t nattrs;
} bim_au_schema_t;

/* set by first SUU */
typedef struct {
  const bim_decoderinit_t *di;
  bim_au_schema_t *add;
  size_t nadd;
} bim_au_state_t;

int bim_au_state_init(bim_au_state_t *st, const bim_decoderinit_t *di);
void bim_au_state_free(bim_au_state_t *st);

/* *used: bytes. 0 ok, -1 error (err), au freed */
int bim_au_parse(bim_au_state_t *st, const unsigned char *buf, size_t len, bim_au_t *au, size_t *used, bim_err_t *err);
int bim_au_write(bim_au_state_t *st, const bim_au_t *au, bitwriter_t *bw, bim_err_t *err);
void bim_au_free(bim_au_t *au);

#endif
