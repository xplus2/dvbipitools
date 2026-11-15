/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/bim/codec/codec_int.h"

int occ_coding(const bim_part_t *p, int *shunt) {
  *shunt = p->min == 0 && p->max >= 1;
  if (p->max <= 1) return 0;
  if (p->max == BIM_UNBOUNDED || p->max - p->min > 65535) return -1;
  return bim_width((uint64_t)(int)(p->max - p->min + 1));
}

int range_bits(const bim_range_t *r) { return r->span > 65535 ? -1 : bim_width((uint64_t)(int)r->span + 1); }

int bim_schema_elem(const bim_schema_t *sc, const char *ns, const char *name) {
  if (sc->ea_cap && sc->ix_elems == sc->elems) {
    for (size_t k = bim_qhash(ns, name) & (size_t)(sc->ea_cap - 1); sc->elem_nix[k]; k = (k + 1) & (size_t)(sc->ea_cap - 1)) {
      const struct bim_elem *e = sc->elems[sc->elem_nix[k] - 1];
      if (!strcmp(e->name, name) && bim_ns_same(e->ns, ns)) return sc->elem_nix[k] - 1;
    }
    return -1;
  }
  for (int c = 0; c < sc->nelems; c++)
    if (!strcmp(sc->elems[c]->name, name) && bim_ns_same(sc->elems[c]->ns, ns)) return c;
  return -1;
}

int bim_schema_attr(const bim_schema_t *sc, const char *ns, const char *name) {
  if (sc->ea_cap && sc->ix_attrs == sc->attrs) {
    for (size_t k = bim_qhash(ns, name) & (size_t)(sc->ea_cap - 1); sc->attr_nix[k]; k = (k + 1) & (size_t)(sc->ea_cap - 1)) {
      const bim_attr_t *a = &sc->attrs[sc->attr_nix[k] - 1];
      if (!strcmp(a->name, name) && bim_ns_same(a->ns, ns)) return sc->attr_nix[k] - 1;
    }
    return -1;
  }
  for (int k = 0; k < sc->nattrs; k++)
    if (!strcmp(sc->attrs[k].name, name) && bim_ns_same(sc->attrs[k].ns, ns)) return k;
  return -1;
}

const bim_subtypes_t *bim_subtypes_find(const bim_schema_t *sc, const bim_type_t *t) {
  if (sc->sub_cap && sc->ix_subs == sc->subs) {
    for (size_t k = bim_phash(t) & (size_t)(sc->sub_cap - 1); sc->sub_ix[k]; k = (k + 1) & (size_t)(sc->sub_cap - 1))
      if (sc->subs[sc->sub_ix[k] - 1].base == t) return &sc->subs[sc->sub_ix[k] - 1];
    return NULL;
  }
  for (int i = 0; i < sc->nsubs; i++)
    if (sc->subs[i].base == t) return &sc->subs[i];
  return NULL;
}

const bim_subtypes_t *bim_subtypes_find_x(const bim_schema_t *sc, const bim_type_t *t) {
  const bim_subtypes_t *s = bim_subtypes_find(sc, t);
  if (s || !t || !t->name) return s;
  if (sc->sub_cap && sc->ix_subs == sc->subs) {
    for (size_t k = bim_qhash(t->ns, t->name) & (size_t)(sc->sub_cap - 1); sc->sub_nix[k]; k = (k + 1) & (size_t)(sc->sub_cap - 1)) {
      const bim_type_t *b = sc->subs[sc->sub_nix[k] - 1].base;
      if (!strcmp(b->name, t->name) && bim_ns_eq(b->ns, t->ns)) return &sc->subs[sc->sub_nix[k] - 1];
    }
    return NULL;
  }
  for (int i = 0; i < sc->nsubs; i++) {
    const bim_type_t *b = sc->subs[i].base;
    if (b->name && !strcmp(b->name, t->name) && bim_ns_eq(b->ns, t->ns)) return &sc->subs[i];
  }
  return NULL;
}
