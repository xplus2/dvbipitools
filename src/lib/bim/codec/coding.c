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

const bim_subtypes_t *bim_subtypes_find(const bim_schema_t *sc, const bim_type_t *t) {
  for (int i = 0; i < sc->nsubs; i++)
    if (sc->subs[i].base == t) return &sc->subs[i];
  return NULL;
}

const bim_subtypes_t *bim_subtypes_find_x(const bim_schema_t *sc, const bim_type_t *t) {
  const bim_subtypes_t *s = bim_subtypes_find(sc, t);
  if (s || !t || !t->name) return s;
  for (int i = 0; i < sc->nsubs; i++) {
    const bim_type_t *b = sc->subs[i].base;
    if (b->name && !strcmp(b->name, t->name) && !(b->ns && t->ns ? strcmp(b->ns, t->ns) : b->ns != t->ns)) return &sc->subs[i];
  }
  return NULL;
}
