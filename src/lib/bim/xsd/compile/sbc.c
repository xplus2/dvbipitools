/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/xsd/compile/priv.h"

static int sbc_add(cc_t *c, sbclist_t *l, const bim_sbc_t *e, char *key) {
  if (l->n >= l->cap) {
    int cap = l->cap;
    void *np = array_grow(l->v, &cap, l->n + 1, sizeof *l->v);
    void *nk;
    if (!np) return xc_oom(c);
    l->v = np;
    if (!(nk = realloc(l->keys, sizeof *l->keys * (size_t)cap))) return xc_oom(c);
    l->keys = nk;
    l->cap = cap;
  }
  l->v[l->n] = *e;
  l->keys[l->n++] = key;
  return 0;
}

/* ISO 23001-1 6.6.5.2.2: choice in choice merged, choice + all childs by sig, groups flattened */
static int sbc_tree(cc_t *c, snode_t *n) {
  for (int i = 0; i < n->nkids; i++)
    if (sbc_tree(c, n->kids[i])) return -1;
  for (int i = 0; n->kind == XSD_P_CHOICE && i < n->nkids; i++) {
    snode_t *k = n->kids[i];
    snode_t **v;
    if (k->kind != XSD_P_CHOICE) continue;
    if (!(v = xc_alloc(c, sizeof *v * (size_t)(n->nkids - 1 + k->nkids)))) return xc_oom(c);
    memcpy(v, n->kids, sizeof *v * (size_t)i);
    memcpy(v + i, k->kids, sizeof *v * (size_t)k->nkids);
    memcpy(v + i + k->nkids, n->kids + i + 1, sizeof *v * (size_t)(n->nkids - i - 1));
    n->kids = v;
    n->nkids += k->nkids - 1;
    i--;
  }
  return 0;
}

static int sbc_leaves(cc_t *c, const snode_t *n, sbclist_t *l) {
  bim_sbc_t e;
  memset(&e, 0, sizeof e);
  if (n->kind == XSD_P_ELEM) {
    e.kind = BIM_SBC_ELEM;
    e.name = n->elem->q.name;
    e.ns = n->elem->q.ns;
    return (e.type = xc_get_type(c, n->elem->type)) ? sbc_add(c, l, &e, NULL) : -2;
  }
  if (n->kind == XSD_P_ANY) {
    e.kind = BIM_SBC_ANY;
    return (e.wc = xc_wc_from(c, n->wc)) ? sbc_add(c, l, &e, NULL) : -2;
  }
  for (int i = 0; i < n->nkids; i++)
    if (sbc_leaves(c, n->kids[i], l)) return -2;
  return 0;
}

static int has_kind(const sbclist_t *l, bim_sbc_kind_t k) {
  for (int i = 0; i < l->n; i++)
    if (l->v[i].kind == k) return 1;
  return 0;
}

/* own attributes + attribute wildcard, by expanded name or signature */
static int sbc_attrs(cc_t *c, const xsd_type_t *t, sbclist_t *l) {
  alist_t own = {NULL, 0, 0};
  const bim_wildcard_t *w;
  int start = l->n;
  int rc = xc_own_attrs(c, t, &t->attrs, &own, 0);
  for (int i = 0; !rc && i < own.n; i++) {
    bim_sbc_t e;
    char *key;
    if (own.v[i].use == XSD_USE_PROHIBITED) continue;
    memset(&e, 0, sizeof e);
    e.kind = BIM_SBC_ATTR;
    e.name = own.v[i].attr->q.name;
    e.ns = own.v[i].attr->q.ns;
    if (!(e.simple = xc_get_simple(c, own.v[i].attr->type)) || !(key = xc_ename(c, e.ns, e.name)) || sbc_add(c, l, &e, key)) rc = -1;
  }
  free(own.v);
  if (rc) return -1;
  if (xc_local_wildcard(c, t, &t->attrs, 0, &w)) return -1;
  if (w && !has_kind(l, BIM_SBC_ANYATTR)) {
    bim_sbc_t e;
    xsd_wildcard_t x;
    char *key;
    memset(&e, 0, sizeof e);
    e.kind = BIM_SBC_ANYATTR;
    e.wc = w;
    x.kind = w->kind == BIM_WC_ANY ? XSD_WC_ANY : w->kind == BIM_WC_NOT ? XSD_WC_NOT : XSD_WC_LIST;
    x.ns = (const char **)w->ns;
    x.nns = w->nns;
    x.process = w->process == BIM_PC_STRICT ? XSD_PC_STRICT : w->process == BIM_PC_LAX ? XSD_PC_LAX : XSD_PC_SKIP;
    if (!(key = xc_wc_sig(c, &x)) || sbc_add(c, l, &e, key)) return -1;
  }
  for (int i = start + 1; i < l->n; i++)
    for (int k = i; k > start && strcmp(l->keys[k - 1], l->keys[k]) > 0; k--) {
      bim_sbc_t te = l->v[k];
      char *tk = l->keys[k];
      l->v[k] = l->v[k - 1];
      l->keys[k] = l->keys[k - 1];
      l->v[k - 1] = te;
      l->keys[k - 1] = tk;
    }
  return 0;
}

int xc_compile_sbc(cc_t *c, const xsd_type_t *t, int depth) {
  bim_type_t *b = xc_pmap_get(&c->types, t);
  sbclist_t l = {NULL, 0, 0, NULL};
  int derived;
  int rc = 0;
  if (!b || xc_pmap_get(&c->sbc_done, t) || t->kind != XSD_COMPLEX) return 0;
  if (depth > DEPTH_MAX) return xsd_set_fail(c->s, "LS400129", "nesting too deep");
  if (xc_pmap_put(c, &c->sbc_done, t, b)) return -2;
  derived = t->base && t->base->kind == XSD_COMPLEX && !xc_is_any_type(t->base);
  if (derived) {
    const bim_type_t *bb = xc_pmap_get(&c->types, t->base);
    if (!bb || xc_compile_sbc(c, t->base, depth + 1)) return -1;
    for (int i = 0; i < bb->nsbc; i++)
      if (sbc_add(c, &l, &bb->sbc[i], NULL)) rc = -2;
  }
  if (!rc && !(derived && t->deriv == XSD_D_RESTRICTION)) {
    if (xc_has_simple_content(t)) {
      if (!has_kind(&l, BIM_SBC_SIMPLE)) {
        bim_sbc_t e;
        memset(&e, 0, sizeof e);
        e.kind = BIM_SBC_SIMPLE;
        e.simple = b->simple;
        rc = sbc_add(c, &l, &e, NULL);
      }
    } else if (!xc_content_empty(t->particle)) {
      snode_t *n = xc_build(c, t, t->particle, 0);
      rc = !n || sbc_tree(c, n) || xc_sign(c, n) || sbc_leaves(c, n, &l) ? -1 : 0;
    }
    if (!rc) rc = sbc_attrs(c, t, &l);
  }
  if (!rc && l.n && !(b->sbc = xc_alloc(c, sizeof *b->sbc * (size_t)l.n))) rc = xc_oom(c);
  if (!rc && l.n) memcpy((void *)b->sbc, l.v, sizeof *b->sbc * (size_t)l.n);
  b->nsbc = rc ? 0 : l.n;
  free(l.v);
  free(l.keys);
  return rc;
}
