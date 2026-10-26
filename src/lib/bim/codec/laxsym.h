/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_CODEC_LAXSYM_H
#define DVBIPITOOLS_LIB_BIM_CODEC_LAXSYM_H

#include <stdint.h>

/* lax symbol table, ISO 23001-1 Amd 2 7.5.2.4.5.4.3: entries of up to 3 events, most recent first */
#define LAX_SYMS 128

typedef struct {
  uint32_t ev[3];
  int n;
  int code;
} lax_sym_t;

typedef struct {
  lax_sym_t tab[LAX_SYMS];
  int agg;
  int has_last;
  int has_last2;
  uint32_t last;
  uint32_t last2;
} lax_syms_t;

void lax_syms_init(lax_syms_t *s, int agg);
/* event decoded or encoded: table update */
void lax_syms_event(lax_syms_t *s, uint32_t e);
/* entry of 7 bit code, NULL none */
const lax_sym_t *lax_syms_by_code(const lax_syms_t *s, int code);
/* code of single event, -1 none */
int lax_syms_code(const lax_syms_t *s, uint32_t e);

#endif
