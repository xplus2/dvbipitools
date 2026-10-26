/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/bim/codec/laxsym.h"

static int eq(const lax_sym_t *a, const uint32_t *ev, int n) { return a->n == n && !memcmp(a->ev, ev, (size_t)n * sizeof *ev); }

static void update_symbol(lax_syms_t *l, const uint32_t *ev, int n) {
  int at = LAX_SYMS - 1;
  lax_sym_t s;
  for (int i = 0; i < LAX_SYMS; i++)
    if (eq(&l->tab[i], ev, n)) {
      at = i;
      break;
    }
  s = l->tab[at];
  if (!eq(&s, ev, n)) {
    /* oldest removed, its code reused */
    memcpy(s.ev, ev, (size_t)n * sizeof *ev);
    s.n = n;
  }
  memmove(&l->tab[1], &l->tab[0], (size_t)at * sizeof l->tab[0]);
  l->tab[0] = s;
}

static void update_one(lax_syms_t *l, uint32_t e) {
  if (l->has_last) {
    if (l->has_last2) {
      uint32_t three[3] = {l->last2, l->last, e};
      update_symbol(l, three, 3);
    }
    {
      uint32_t two[2] = {l->last, e};
      update_symbol(l, two, 2);
    }
  }
  update_symbol(l, &e, 1);
  l->last2 = l->last;
  l->has_last2 = l->has_last;
  l->last = e;
  l->has_last = 1;
}

void lax_syms_init(lax_syms_t *s, int agg) {
  memset(s, 0, sizeof *s);
  for (int i = 0; i < LAX_SYMS; i++) {
    s->tab[i].ev[0] = (uint32_t)i;
    s->tab[i].n = 1;
    s->tab[i].code = i;
  }
  s->agg = agg;
}

void lax_syms_event(lax_syms_t *s, uint32_t e) {
  if (s->agg) update_one(s, e);
  else update_symbol(s, &e, 1);
}

const lax_sym_t *lax_syms_by_code(const lax_syms_t *s, int code) {
  for (int i = 0; i < LAX_SYMS; i++)
    if (s->tab[i].code == code) return &s->tab[i];
  return NULL;
}

int lax_syms_code(const lax_syms_t *s, uint32_t e) {
  for (int i = 0; i < LAX_SYMS; i++)
    if (s->tab[i].n == 1 && s->tab[i].ev[0] == e) return s->tab[i].code;
  return -1;
}
