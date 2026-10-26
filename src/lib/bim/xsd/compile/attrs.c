/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/xsd/compile/priv.h"

static bim_wildcard_t *wc_new(cc_t *c, bim_wc_kind_t kind, const char *const *ns, int nns, bim_process_t pc) {
  bim_wildcard_t *w = xc_alloc(c, sizeof *w);
  const char **v = xc_alloc(c, sizeof *v * (size_t)nns);
  if (!w || !v) {
    xc_oom(c);
    return NULL;
  }
  if (nns) memcpy(v, ns, sizeof *v * (size_t)nns);
  w->kind = kind;
  w->ns = v;
  w->nns = nns;
  w->process = pc;
  return w;
}

const bim_wildcard_t *xc_wc_from(cc_t *c, const xsd_wildcard_t *x) {
  bim_wc_kind_t k = BIM_WC_LIST;
  bim_process_t pc = BIM_PC_SKIP;
  if (x->kind == XSD_WC_ANY) k = BIM_WC_ANY;
  else if (x->kind == XSD_WC_NOT) k = BIM_WC_NOT;
  if (x->process == XSD_PC_STRICT) pc = BIM_PC_STRICT;
  else if (x->process == XSD_PC_LAX) pc = BIM_PC_LAX;
  return wc_new(c, k, x->ns, x->nns, pc);
}

static int ns_in(const bim_wildcard_t *w, const char *ns) {
  for (int i = 0; i < w->nns; i++)
    if (xc_ns_eq(w->ns[i], ns)) return 1;
  return 0;
}

static int wc_same(const bim_wildcard_t *a, const bim_wildcard_t *b) {
  if (a->kind != b->kind || a->nns != b->nns) return 0;
  for (int i = 0; i < a->nns; i++)
    if (!ns_in(b, a->ns[i])) return 0;
  return 1;
}

/* XML Schema Part 1 3.10.6.2 (Second Edition), process contents of a */
static const bim_wildcard_t *wc_intersect(cc_t *c, const xsd_type_t *t, const bim_wildcard_t *a, const bim_wildcard_t *b) {
  const char *v[256];
  int n = 0;
  if (wc_same(a, b) || b->kind == BIM_WC_ANY) return a;
  if (a->kind == BIM_WC_ANY) return wc_new(c, b->kind, b->ns, b->nns, a->process);
  if (a->kind == BIM_WC_NOT && b->kind == BIM_WC_NOT) {
    if (!a->ns[0]) return wc_new(c, BIM_WC_NOT, b->ns, 1, a->process);
    if (!b->ns[0]) return a;
    xsd_set_fail(c->s, "LS400286", "bim: %s: attribute wildcard intersection not expressible", xc_label(t));
    return NULL;
  }
  if (a->kind == BIM_WC_NOT || b->kind == BIM_WC_NOT) {
    const bim_wildcard_t *neg = a->kind == BIM_WC_NOT ? a : b;
    const bim_wildcard_t *set = a->kind == BIM_WC_NOT ? b : a;
    for (int i = 0; i < set->nns && n < 256; i++)
      if (set->ns[i] && !xc_ns_eq(set->ns[i], neg->ns[0])) v[n++] = set->ns[i];
  } else {
    for (int i = 0; i < a->nns && n < 256; i++)
      if (ns_in(b, a->ns[i])) v[n++] = a->ns[i];
  }
  return wc_new(c, BIM_WC_LIST, v, n, a->process);
}

/* XML Schema Part 1 3.10.6.1 (Second Edition), process contents of a */
static const bim_wildcard_t *wc_union(cc_t *c, const xsd_type_t *t, const bim_wildcard_t *a, const bim_wildcard_t *b) {
  const char *v[256] = {0};
  const char *absent = NULL;
  int n = 0;
  if (wc_same(a, b)) return a;
  if (a->kind == BIM_WC_ANY || b->kind == BIM_WC_ANY) return wc_new(c, BIM_WC_ANY, NULL, 0, a->process);
  if (a->kind == BIM_WC_LIST && b->kind == BIM_WC_LIST) {
    for (int i = 0; i < a->nns && n < 256; i++) v[n++] = a->ns[i];
    for (int i = 0; i < b->nns && n < 256; i++)
      if (!ns_in(a, b->ns[i])) v[n++] = b->ns[i];
    return wc_new(c, BIM_WC_LIST, v, n, a->process);
  }
  if (a->kind == BIM_WC_NOT && b->kind == BIM_WC_NOT) return wc_new(c, BIM_WC_NOT, &absent, 1, a->process);
  {
    const bim_wildcard_t *neg = a->kind == BIM_WC_NOT ? a : b;
    const bim_wildcard_t *set = a->kind == BIM_WC_NOT ? b : a;
    int has_x = ns_in(set, neg->ns[0]);
    int has_absent = ns_in(set, NULL);
    if (!neg->ns[0]) return has_absent ? wc_new(c, BIM_WC_ANY, NULL, 0, a->process) : wc_new(c, BIM_WC_NOT, &absent, 1, a->process);
    if (has_x && has_absent) return wc_new(c, BIM_WC_ANY, NULL, 0, a->process);
    if (has_x) return wc_new(c, BIM_WC_NOT, &absent, 1, a->process);
    if (has_absent) {
      xsd_set_fail(c->s, "LS400287", "bim: %s: attribute wildcard union not expressible", xc_label(t));
      return NULL;
    }
    return wc_new(c, BIM_WC_NOT, neg->ns, 1, a->process);
  }
}

int xc_local_wildcard(cc_t *c, const xsd_type_t *t, const xsd_attrs_t *as, int depth, const bim_wildcard_t **out) {
  const bim_wildcard_t *w = as->wc ? xc_wc_from(c, as->wc) : NULL;
  if (as->wc && !w) return -1;
  if (depth > DEPTH_MAX) return xsd_set_fail(c->s, "LS400129", "nesting too deep");
  for (int i = 0; i < as->ngroups; i++) {
    const bim_wildcard_t *g;
    if (xc_local_wildcard(c, t, &as->groups[i]->attrs, depth + 1, &g)) return -1;
    if (g) {
      w = w ? wc_intersect(c, t, w, g) : g;
      if (!w) return -1;
    }
  }
  *out = w;
  return 0;
}

static int complete_wildcard(cc_t *c, const xsd_type_t *t, const bim_wildcard_t **out) {
  const bim_wildcard_t *w;
  const bim_wildcard_t *bw = NULL;
  if (t->kind != XSD_COMPLEX) {
    *out = NULL;
    return 0;
  }
  if (xc_local_wildcard(c, t, &t->attrs, 0, &w)) return -1;
  if (t->deriv == XSD_D_EXTENSION && t->base && t->base->kind == XSD_COMPLEX && complete_wildcard(c, t->base, &bw)) return -1;
  if (bw) {
    w = w ? wc_union(c, t, w, bw) : bw;
    if (!w) return -1;
  }
  *out = w;
  return 0;
}

static int alist_find(const alist_t *l, const xsd_attr_t *a) {
  for (int i = 0; i < l->n; i++)
    if (xc_ns_eq(l->v[i].attr->q.ns, a->q.ns) && !strcmp(l->v[i].attr->q.name, a->q.name)) return i;
  return -1;
}

/* raw: prohibited kept as entry. else prohibited removes. replaces same expanded name or appends */
static int alist_put(cc_t *c, alist_t *l, const xsd_attr_t *attr, xsd_use_t use, const char *fixed, int raw) {
  int i = alist_find(l, attr);
  if (!raw && use == XSD_USE_PROHIBITED) {
    if (i >= 0 && l->v) memmove(&l->v[i], &l->v[i + 1], sizeof *l->v * (size_t)(l->n-- - i - 1));
    return 0;
  }
  if (i < 0) {
    if (l->n >= l->cap) {
      void *np = array_grow(l->v, &l->cap, l->n + 1, sizeof *l->v);
      if (!np) return xc_oom(c);
      l->v = np;
    }
    i = l->n++;
  }
  if (!l->v) return xc_oom(c);
  l->v[i].attr = attr;
  l->v[i].use = use;
  l->v[i].fixed = fixed ? fixed : attr->fixed;
  return 0;
}

int xc_own_attrs(cc_t *c, const xsd_type_t *t, const xsd_attrs_t *as, alist_t *l, int depth) {
  if (depth > DEPTH_MAX) return xsd_set_fail(c->s, "LS400129", "nesting too deep");
  for (int i = 0; i < as->nuses; i++)
    if (alist_put(c, l, as->uses[i].attr, as->uses[i].use, as->uses[i].fixed, 1)) return -2;
  for (int i = 0; i < as->ngroups; i++)
    if (xc_own_attrs(c, t, &as->groups[i]->attrs, l, depth + 1)) return -1;
  return 0;
}

/* XML Schema Part 1 3.4.2 {attribute uses} */
static int attr_uses(cc_t *c, const xsd_type_t *t, alist_t *l, int depth) {
  alist_t own = {NULL, 0, 0};
  int rc = 0;
  if (t->kind != XSD_COMPLEX) return 0;
  if (depth > DEPTH_MAX) return xsd_set_fail(c->s, "LS400129", "nesting too deep");
  if (t->base && t->base->kind == XSD_COMPLEX && attr_uses(c, t->base, l, depth + 1)) return -1;
  rc = xc_own_attrs(c, t, &t->attrs, &own, 0);
  for (int i = 0; !rc && i < own.n; i++) {
    rc = alist_put(c, l, own.v[i].attr, own.v[i].use, own.v[i].fixed, 0);
  }
  free(own.v);
  return rc;
}

int xc_battr_cmp(const void *a, const void *b) {
  const bim_attr_t *x = a;
  const bim_attr_t *y = b;
  return xc_ename_cmp(x->ns, x->name, y->ns, y->name);
}

static int battr_cmp_local(const void *a, const void *b) {
  const bim_attr_t *x = a;
  const bim_attr_t *y = b;
  return strcmp(x->name, y->name);
}

int xc_compile_attrs(cc_t *c, const xsd_type_t *t, const xsd_type_t *minus, bim_type_t *b) {
  alist_t l = {NULL, 0, 0};
  alist_t m = {NULL, 0, 0};
  const bim_wildcard_t *mw = NULL;
  bim_attr_t *v = NULL;
  bim_attr_t *fx = NULL;
  int n = 0;
  int nf = 0;
  int rc = attr_uses(c, t, &l, 0);
  if (!rc && minus) rc = attr_uses(c, minus, &m, 0);
  if (!rc) {
    v = xc_alloc(c, sizeof *v * (size_t)l.n);
    if (v) fx = xc_alloc(c, sizeof *fx * (size_t)l.n);
    if (!v || !fx) rc = xc_oom(c);
  }
  if (!rc && l.n && !l.v) rc = xc_oom(c);
  for (int i = 0; !rc && i < l.n; i++) {
    int req = l.v[i].use == XSD_USE_REQUIRED;
    if (alist_find(&m, l.v[i].attr) >= 0) continue;
    if (l.v[i].fixed && ((!c->o->mpegb && !(c->o->compat & BIM_COMPAT_FIXED_ATTR)) || req)) {
      fx[nf].name = l.v[i].attr->q.name;
      fx[nf++].ns = l.v[i].attr->q.ns;
      continue;
    }
    v[n].name = l.v[i].attr->q.name;
    v[n].ns = l.v[i].attr->q.ns;
    v[n].required = req;
    if (!(v[n++].simple = xc_get_simple(c, l.v[i].attr->type))) rc = -1;
  }
  free(l.v);
  free(m.v);
  if (rc) return rc;
  if (n > 1) qsort(v, (size_t)n, sizeof *v, c->o->compat & BIM_COMPAT_ATTR_LOCAL ? &battr_cmp_local : &xc_battr_cmp);
  b->attrs = v;
  b->nattrs = n;
  b->fixed = fx;
  b->nfixed = nf;
  if (complete_wildcard(c, t, &b->anyattr) || (minus && complete_wildcard(c, minus, &mw))) return -1;
  if (mw) b->anyattr = NULL;
  return 0;
}
