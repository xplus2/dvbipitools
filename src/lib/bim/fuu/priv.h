/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_BIM_FUU_PRIV_H
#define DVBIPITOOLS_LIB_BIM_FUU_PRIV_H

#include "lib/bim/fuu.h"

#define BIM_FUU_TRUNC "LE400303"
#define BIM_FUU_RANGE "LE400304"
#define BIM_FUU_UNSUP "LE400305"
#define BIM_FUU_STATE "LE400306"

#define UNB UINT64_MAX
#define TBC_MAX 256

/* per TBC: parent type, element maxOccurs, selector TBC */
typedef struct {
  bim_rd_t r;
  bim_fuu_state_t *st;
  bim_fuu_t *f;
  const bim_type_t *ptype[TBC_MAX];
  uint64_t pmax[TBC_MAX];
  int sel[TBC_MAX];
  size_t cap;
} fr_t;

static inline int range(fr_t *r, const char *what) { return bim_fail(r->r.err, BIM_FUU_RANGE, "bim: FUU %s out of range", what); }

uint64_t fuu_occ_max(const bim_part_t *p);
int fuu_is_complex(const bim_type_t *t);
int fuu_is_elem_sbc(const bim_sbc_t *s);
/* part of SBC entry: ELEM/ANY parts in SBC order */
const bim_part_t *fuu_part_of(const bim_type_t *t, int sbc_index);
/* schema and code of global element or type x */
int fuu_find_code(const bim_fuu_state_t *st, const void *x, int elems, size_t *sid, uint64_t *code);
/* element or type of an external substitution or cast table, 6.7.5.4, 6.7.6.4 */
int fuu_external(const bim_fuu_state_t *st, const void *x, int elems);

typedef enum { BIM_PC_NONE, BIM_PC_RATIONAL, BIM_PC_V5, BIM_PC_BITS } bim_pc_t;

/* 6.6.5.5: position code coding of t. *w bits, *mpc: multiple position code */
int bim_fuu_pc_coding(const bim_fuu_state_t *st, const bim_tbc_t *t, const bim_type_t *pt, uint64_t pmax, int sel, int *w, int *mpc);
/* 6.6.5.6: i-th rational position in increment order */
uint64_t fuu_rat_order(uint64_t i);
/* inverse of fuu_rat_order for i below 2^20, UNB none */
uint64_t fuu_rat_index(uint64_t v);

int bim_fuu_rd_positions(fr_t *r);

int bim_fuu_uses_mpc(const bim_fuu_state_t *st, const bim_type_t *t);

#endif
