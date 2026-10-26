/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/bim/fuu/priv.h"

uint64_t fuu_occ_max(const bim_part_t *p) { return !p ? 1 : p->max; }

int fuu_is_complex(const bim_type_t *t) { return t && !(t->content == BIM_CONTENT_SIMPLE && !t->nsbc); }

int fuu_is_elem_sbc(const bim_sbc_t *s) { return s->kind == BIM_SBC_ELEM || s->kind == BIM_SBC_ANY; }

static void elem_parts(const bim_part_t *p, const bim_part_t **out, int *n) {
  if (!p) return;
  if (p->kind == BIM_PART_ELEM || p->kind == BIM_PART_ANY) {
    if (*n < TBC_MAX) out[*n] = p;
    (*n)++;
    return;
  }
  for (int i = 0; i < p->nkids; i++) elem_parts(&p->kids[i], out, n);
}

/* part of SBC entry: ELEM/ANY parts in SBC order */
const bim_part_t *fuu_part_of(const bim_type_t *t, int sbc_index) {
  const bim_part_t *parts[TBC_MAX] = {NULL};
  int n = 0;
  int k = 0;
  elem_parts(t->top, parts, &n);
  for (int i = 0; i < sbc_index; i++) k += fuu_is_elem_sbc(&t->sbc[i]);
  return k < n && k < TBC_MAX ? parts[k] : NULL;
}

int fuu_find_code(const bim_fuu_state_t *st, const void *x, int elems, size_t *sid, uint64_t *code) {
  for (size_t i = 0; i < st->nschemas; i++) {
    int n = elems ? st->schemas[i].nelems : st->schemas[i].ntypes;
    for (int k = 0; k < n; k++)
      if ((elems ? (const void *)st->schemas[i].elems[k] : (const void *)st->schemas[i].types[k]) == x) {
        *sid = i;
        *code = (uint64_t)k;
        return 0;
      }
  }
  return -1;
}

int fuu_external(const bim_fuu_state_t *st, const void *x, int elems) {
  const bim_ext_t *const *tab = elems ? st->substitutable : st->castable;
  size_t sid;
  uint64_t code;
  const bim_ext_t *e;
  if (!tab) return 0;
  if (fuu_find_code(st, x, elems, &sid, &code)) return 0;
  if (!(e = tab[sid])) return 0;
  if (!e->present) return 0;
  if (e->all) return 1;
  for (size_t i = 0; i < e->ncodes; i++)
    if (e->codes[i] == code) return 1;
  return 0;
}
