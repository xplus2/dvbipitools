/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/fuu/priv.h"

static int multi_group(const bim_part_t *p) {
  if (!p || p->kind == BIM_PART_ELEM || p->kind == BIM_PART_ANY) return 0;
  if (p->kind == BIM_PART_ALL || p->max > 1) return 1;
  for (int i = 0; i < p->nkids; i++)
    if (multi_group(&p->kids[i])) return 1;
  return 0;
}

int bim_fuu_uses_mpc(const bim_fuu_state_t *st, const bim_type_t *t) { return st->di->mpc_only || t->mixed || multi_group(t->top); }

/* 6.6.5.5.3 */
static uint64_t mpa(const bim_part_t *p) {
  uint64_t acc = 0;
  if (p->kind == BIM_PART_ELEM || p->kind == BIM_PART_ANY || p->max == UNB) return p->max;
  if (p->kind == BIM_PART_ALL) return (uint64_t)p->nkids > UNB / (p->max ? p->max : 1) ? UNB : p->max * (uint64_t)p->nkids;
  for (int i = 0; i < p->nkids; i++) {
    uint64_t k = mpa(&p->kids[i]);
    if (k == UNB) return UNB;
    if (p->kind == BIM_PART_SEQ) acc = acc + k < acc ? UNB : acc + k;
    else if (k > acc) acc = k;
  }
  return acc == UNB || (p->max && acc > UNB / p->max) ? UNB : p->max * acc;
}

int bim_fuu_pc_coding(const bim_fuu_state_t *st, const bim_tbc_t *t, const bim_type_t *pt, uint64_t pmax, int sel, int *w, int *mpc) {
  *mpc = 0;
  *w = 0;
  if (sel || (t->kind != BIM_TBC_ELEM && t->kind != BIM_TBC_CHARDATA)) return BIM_PC_NONE;
  if (t->kind == BIM_TBC_CHARDATA || bim_fuu_uses_mpc(st, pt)) {
    uint64_t m = pt->top ? mpa(pt->top) : 1;
    *mpc = 1;
    if (pt->mixed && m != UNB) m = m > (UNB - 1) / 2 ? UNB : 2 * m + 1;
    if (st->di->insert) return BIM_PC_RATIONAL;
    if (m == UNB || m > 65535) return BIM_PC_V5;
    *w = bim_width(m);
    return BIM_PC_BITS;
  }
  if (pmax <= 1) return BIM_PC_NONE;
  if (st->di->insert) return BIM_PC_RATIONAL;
  *w = pmax == UNB ? 65 : bim_width(pmax);
  if (*w > 4) return BIM_PC_V5;
  return BIM_PC_BITS;
}

/* 6.6.5.5. -1 error, 0 absent, 1 present */
static int rd_pc(fr_t *r, size_t i, uint64_t *pos) {
  int w;
  int mpc;
  switch (bim_fuu_pc_coding(r->st, &r->f->tbcs[i], r->ptype[i], r->pmax[i], r->sel[i], &w, &mpc)) {
    case BIM_PC_NONE: return 0;
    case BIM_PC_RATIONAL:
      if (bim_rd_rational(&r->r, pos, BIM_FUU_RANGE)) return -1;
      return mpc && !*pos ? range(r, "rational position code 0") : 1;
    case BIM_PC_V5: return bim_rd_v5(&r->r, pos) ? -1 : 1;
    default: return bim_rd_bits(&r->r, w, pos) ? -1 : 1;
  }
}

/* 6.6.5.6 rational increment order r[i] = (2(i+1) + 1 - 2^j) / 2^j, j = 1 + floor(log2(i+1)) */
uint64_t fuu_rat_order(uint64_t i) {
  int j = 1;
  while (j < 63 && (1ull << j) <= i + 1) j++;
  return (2 * (i + 1) + 1 - (1ull << j)) << (64 - j);
}

uint64_t fuu_rat_index(uint64_t v) {
  int tz = 0;
  int j;
  uint64_t i1;
  if (!v) return UNB;
  while (!(v >> tz & 1)) tz++;
  j = 64 - tz;
  if (j > 21) return UNB;
  i1 = ((v >> tz) - 1 + (1ull << j)) / 2;
  return i1 >= 1 && i1 <= (1u << 20) && fuu_rat_order(i1 - 1) == v ? i1 - 1 : UNB;
}

static int rat_next(uint64_t *v) {
  uint64_t i = fuu_rat_index(*v);
  if (i == UNB) return -1;
  *v = fuu_rat_order(i + 1);
  return 0;
}

/* 6.6.5.6 */
static int rd_multi(fr_t *r, const int *has) {
  bim_fuu_t *f = r->f;
  size_t n = f->ntbcs;
  int layers[TBC_MAX];
  int nl = 0;
  int rational = r->st->di->insert;
  uint64_t cur[TBC_MAX];
  size_t cap = 1;
  int w;
  memcpy(cur, f->pos, n * sizeof *cur);
  for (size_t i = 0; i < n; i++)
    if (has[i]) layers[nl++] = (int)i;
  w = bim_width((uint64_t)nl + 2);
  for (;;) {
    uint64_t c;
    int skip = 0;
    if (bim_rd_bits(&r->r, w, &c)) return -1;
    if (!c) {
      skip = 1;
      if (bim_rd_bits(&r->r, w, &c)) return -1;
    }
    if (c == (1ull << w) - 1) {
      if (skip) return range(r, "Skip_Indication");
      break;
    }
    if (!c || c > (uint64_t)nl) return range(r, "IncrementalPositionCode");
    if (rational ? rat_next(&cur[layers[c - 1]]) : (cur[layers[c - 1]]++, 0)) return range(r, "rational position increment");
    for (int l = (int)c; l < nl; l++) cur[layers[l]] = rational ? fuu_rat_order(0) : 0;
    if (skip) continue;
    if (f->npayloads == cap) {
      uint64_t *np = realloc(f->pos, cap * 2 * n * sizeof *np);
      if (!np) return bim_oom(r->r.err);
      f->pos = np;
      cap *= 2;
    }
    memcpy(&f->pos[f->npayloads++ * n], cur, n * sizeof *cur);
  }
  return 0;
}

int bim_fuu_rd_positions(fr_t *r) {
  bim_fuu_t *f = r->f;
  int has[TBC_MAX];
  f->npayloads = 1;
  if (!(f->pos = calloc(f->ntbcs ? f->ntbcs : 1, sizeof *f->pos))) return bim_oom(r->r.err);
  for (size_t i = 0; i < f->ntbcs; i++) {
    if ((has[i] = rd_pc(r, i, &f->pos[i])) < 0) return -1;
    f->tbcs[i].has_pos = has[i];
    f->tbcs[i].pos = f->pos[i];
  }
  return f->mode == 3 || f->mode == 4 ? rd_multi(r, has) : 0;
}
