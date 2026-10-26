/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/bim/xsd/compile/priv.h"

int xc_oom(cc_t *c) {
  xsd_set_fail(c->s, "LS500101", "out of memory");
  return -2;
}

void *xc_alloc(cc_t *c, size_t n) { return xsd_set_alloc(c->s, n ? n : 1); }

const char *xc_label(const xsd_type_t *t) { return t->q.name ? t->q.name : "(anonymous type)"; }

int xc_is_any_type(const xsd_type_t *t) { return t->builtin && !strcmp(t->q.name, "anyType"); }

static size_t phash(const void *p) {
  uint64_t x = (uint64_t)(uintptr_t)p;
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  return (size_t)x;
}

void *xc_pmap_get(const pmap_t *m, const void *k) {
  if (!m->cap) return NULL;
  for (size_t i = phash(k) & (m->cap - 1);; i = (i + 1) & (m->cap - 1)) {
    if (!m->k[i]) return NULL;
    if (m->k[i] == k) return m->v[i];
  }
}

int xc_pmap_put(cc_t *c, pmap_t *m, const void *k, void *v) {
  size_t i;
  if ((m->n + 1) * 2 > m->cap) {
    pmap_t g = {NULL, NULL, m->cap ? m->cap * 2 : 256, 0};
    if (!(g.k = xc_alloc(c, sizeof *g.k * g.cap)) || !(g.v = xc_alloc(c, sizeof *g.v * g.cap))) return xc_oom(c);
    for (size_t j = 0; j < m->cap; j++)
      if (m->k[j]) xc_pmap_put(c, &g, m->k[j], m->v[j]);
    *m = g;
  }
  for (i = phash(k) & (m->cap - 1); m->k[i] && m->k[i] != k; i = (i + 1) & (m->cap - 1)) {
  }
  if (!m->k[i]) m->n++;
  m->k[i] = k;
  m->v[i] = v;
  return 0;
}

int xc_ename_cmp(const char *ans, const char *a, const char *bns, const char *b) {
  const char *pa[3] = {ans ? ans : "", ans ? ":" : "", a};
  const char *pb[3] = {bns ? bns : "", bns ? ":" : "", b};
  const char *x = pa[0];
  const char *y = pb[0];
  int ia = 0;
  int ib = 0;
  for (;;) {
    while (!*x && ia < 2) x = pa[++ia];
    while (!*y && ib < 2) y = pb[++ib];
    if (*x != *y || !*x) return (unsigned char)*x - (unsigned char)*y;
    x++;
    y++;
  }
}

char *xc_cat(cc_t *c, const char *const *parts, int n) {
  size_t len = 1;
  char *s;
  char *p;
  for (int i = 0; i < n; i++) len += strlen(parts[i]);
  if (!(s = xc_alloc(c, len))) return NULL;
  p = s;
  for (int i = 0; i < n; i++) {
    size_t l = strlen(parts[i]);
    memcpy(p, parts[i], l);
    p += l;
  }
  *p = '\0';
  return s;
}

char *xc_ename(cc_t *c, const char *ns, const char *name) {
  const char *parts[3] = {ns ? ns : "", ns ? ":" : "", name};
  return xc_cat(c, parts, 3);
}

int xc_strp_cmp(const void *a, const void *b) {
  const char *x = *(const char *const *)a;
  const char *y = *(const char *const *)b;
  return strcmp(x ? x : "", y ? y : "");
}

int xc_ns_eq(const char *a, const char *b) { return a && b ? !strcmp(a, b) : a == b; }

int xc_add_codec(cc_t *c, const char *ns, const char *name, bim_codec_t codec, int derived) {
  xsd_codec_map_t *m = &c->codecs[c->ncodecs++];
  m->ns = ns;
  m->name = name;
  m->codec = codec;
  m->derived = derived;
  return 0;
}
