/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/bim/codec/laxsym.h"

static int eq(const lax_sym_t *a, const uint32_t *ev, int n) { return a->n == n && !memcmp(a->ev, ev, (size_t)n * sizeof *ev); }

static unsigned bucket_of(const uint32_t *ev, int n) {
  uint32_t h = 2166136261u ^ (uint32_t)n;
  for (int i = 0; i < n; i++) h = (h ^ ev[i]) * 16777619u;
  return (h ^ h >> 16) & (LAX_BUCKETS - 1);
}

static int find(const lax_syms_t *l, const uint32_t *ev, int n) {
  int i = l->bucket[bucket_of(ev, n)];
  while (i >= 0 && !eq(&l->sym[i], ev, n)) i = l->hnext[i];
  return i;
}

static void hash_link(lax_syms_t *l, int i) {
  unsigned b = bucket_of(l->sym[i].ev, l->sym[i].n);
  l->hnext[i] = l->bucket[b];
  l->bucket[b] = (int16_t)i;
}

static void hash_unlink(lax_syms_t *l, int i) {
  int16_t *p = &l->bucket[bucket_of(l->sym[i].ev, l->sym[i].n)];
  while (*p != i) p = &l->hnext[*p];
  *p = l->hnext[i];
}

static void to_front(lax_syms_t *l, int i) {
  if (i == l->head) return;
  l->next[l->prev[i]] = l->next[i];
  if (i == l->tail) l->tail = l->prev[i];
  else l->prev[l->next[i]] = l->prev[i];
  l->prev[i] = -1;
  l->next[i] = (int16_t)l->head;
  l->prev[l->head] = (int16_t)i;
  l->head = i;
}

static void update_symbol(lax_syms_t *l, const uint32_t *ev, int n) {
  int i = find(l, ev, n);
  if (i < 0) {
    i = l->tail;
    hash_unlink(l, i);
    memcpy(l->sym[i].ev, ev, (size_t)n * sizeof *ev);
    l->sym[i].n = n;
    hash_link(l, i);
  }
  to_front(l, i);
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
  memset(s->bucket, 0xFF, sizeof s->bucket);
  for (int i = 0; i < LAX_SYMS; i++) {
    s->sym[i].ev[0] = (uint32_t)i;
    s->sym[i].n = 1;
    s->sym[i].code = i;
    s->prev[i] = (int16_t)(i - 1);
    s->next[i] = (int16_t)(i + 1 < LAX_SYMS ? i + 1 : -1);
    hash_link(s, i);
  }
  s->head = 0;
  s->tail = LAX_SYMS - 1;
  s->agg = agg;
}

void lax_syms_event(lax_syms_t *s, uint32_t e) {
  if (s->agg) update_one(s, e);
  else update_symbol(s, &e, 1);
}

const lax_sym_t *lax_syms_by_code(const lax_syms_t *s, int code) { return code >= 0 && code < LAX_SYMS ? &s->sym[code] : NULL; }

int lax_syms_code(const lax_syms_t *s, uint32_t e) {
  int i = find(s, &e, 1);
  return i < 0 ? -1 : s->sym[i].code;
}
