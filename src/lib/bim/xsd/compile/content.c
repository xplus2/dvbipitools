/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/xsd/compile/priv.h"

static snode_t *snode_new(cc_t *c, xsd_pkind_t kind, uint64_t min, uint64_t max, int nkids) {
  snode_t *n = xc_alloc(c, sizeof *n);
  if (!n || (nkids && !(n->kids = xc_alloc(c, sizeof *n->kids * (size_t)nkids)))) {
    xc_oom(c);
    return NULL;
  }
  n->kind = kind;
  n->min = min;
  n->max = max;
  n->nkids = nkids;
  return n;
}

snode_t *xc_build(cc_t *c, const xsd_type_t *t, const xsd_particle_t *p, int depth) {
  const xsd_particle_t *g = p;
  snode_t *n;
  if (depth > DEPTH_MAX) {
    xsd_set_fail(c->s, "LS400129", "nesting too deep");
    return NULL;
  }
  if (p->kind == XSD_P_GROUP) g = p->group->particle;
  if (!(n = snode_new(c, g->kind, p->min, p->max, g->kind == XSD_P_ELEM || g->kind == XSD_P_ANY ? 0 : g->nkids))) return NULL;
  n->elem = g->elem;
  n->wc = g->wc;
  for (int i = 0; i < n->nkids; i++)
    if (!(n->kids[i] = xc_build(c, t, &g->kids[i], depth + 1))) return NULL;
  return n;
}

int xc_content_empty(const xsd_particle_t *p) {
  const xsd_particle_t *g = p;
  if (!p) return 1;
  if (p->kind == XSD_P_GROUP) g = p->group->particle;
  if (!p->max) return 1;
  if ((g->kind == XSD_P_SEQ || g->kind == XSD_P_ALL) && !g->nkids) return 1;
  return g->kind == XSD_P_CHOICE && !g->nkids && !p->min;
}

int xc_has_simple_content(const xsd_type_t *t) {
  for (int depth = 0; t && t->kind == XSD_COMPLEX && depth <= DEPTH_MAX; depth++, t = t->base) {
    if (t->content == XSD_C_SIMPLE) return 1;
    if (t->deriv != XSD_D_EXTENSION || !xc_content_empty(t->particle)) return 0;
  }
  return 0;
}

/* effective content particle, ISO 23001-1 7.5.2.2.2. *out NULL: empty */
static int effective(cc_t *c, const xsd_type_t *t, int depth, snode_t **out) {
  snode_t *own = NULL;
  snode_t *base = NULL;
  *out = NULL;
  if (depth > DEPTH_MAX) return xsd_set_fail(c->s, "LS400129", "nesting too deep");
  if (!xc_content_empty(t->particle) && !(own = xc_build(c, t, t->particle, 0))) return -1;
  if (t->deriv == XSD_D_EXTENSION && t->base && t->base->kind == XSD_COMPLEX && !xc_has_simple_content(t->base) &&
      effective(c, t->base, depth + 1, &base))
    return -1;
  if (own && base) {
    if (!(*out = snode_new(c, XSD_P_SEQ, 1, 1, 2))) return -2;
    (*out)->kids[0] = base;
    (*out)->kids[1] = own;
  } else {
    *out = own ? own : base;
  }
  return 0;
}

static int is_group(const snode_t *n) { return n->kind == XSD_P_SEQ || n->kind == XSD_P_CHOICE || n->kind == XSD_P_ALL; }

static int occ_mul(uint64_t a, uint64_t b, uint64_t *out) {
  if (!a || !b) *out = 0;
  else if (a == XSD_UNBOUNDED || b == XSD_UNBOUNDED) *out = XSD_UNBOUNDED;
  else if (a > (XSD_UNBOUNDED - 1) / b) return -1;
  else *out = a * b;
  return 0;
}

/* ISO 23001-1 7.5.2.2.3.2 on n itself. 1 changed */
static int group_simplify(cc_t *c, const xsd_type_t *t, snode_t **slot) {
  snode_t *g = *slot;
  snode_t *k;
  uint64_t min;
  uint64_t max;
  if (!is_group(g) || g->nkids != 1 || g->kids[0]->min > 1) return 0;
  k = g->kids[0];
  if (occ_mul(k->min, g->min, &min) || occ_mul(k->max, g->max, &max)) return xsd_set_fail(c->s, "LS400288", "bim: %s: occurrence product too large", xc_label(t));
  k->min = min;
  k->max = max;
  *slot = k;
  return 1;
}

/* ISO 23001-1 7.5.2.2.3.2 .. 7.5.2.2.3.4, bottom-up to a fixed point */
static int simplify(cc_t *c, const xsd_type_t *t, snode_t *n) {
  int changed = 1;
  for (int i = 0; i < n->nkids; i++)
    if (simplify(c, t, n->kids[i])) return -1;
  while (changed) {
    changed = 0;
    for (int i = 0; i < n->nkids && !changed; i++) {
      snode_t *k = n->kids[i];
      int rc = group_simplify(c, t, &n->kids[i]);
      if (rc < 0) return -1;
      if (rc) {
        changed = 1;
      } else if (n->kind == XSD_P_CHOICE && k->kind == XSD_P_CHOICE && k->min == 1 && k->max == 1) {
        snode_t **v = xc_alloc(c, sizeof *v * (size_t)(n->nkids - 1 + k->nkids));
        if (!v) return xc_oom(c);
        memcpy(v, n->kids, sizeof *v * (size_t)i);
        memcpy(v + i, k->kids, sizeof *v * (size_t)k->nkids);
        memcpy(v + i + k->nkids, n->kids + i + 1, sizeof *v * (size_t)(n->nkids - i - 1));
        n->kids = v;
        n->nkids += k->nkids - 1;
        changed = 1;
      }
    }
    if (n->kind != XSD_P_CHOICE) continue;
    for (int i = 0; i < n->nkids; i++) {
      if (n->kids[i]->min) continue;
      if (!n->kids[i]->max) return xsd_set_fail(c->s, "LS400289", "bim: %s: maxOccurs 0 in choice", xc_label(t));
      n->kids[i]->min = 1;
      n->min = 0;
      changed = 1;
    }
  }
  return 0;
}

static const char *process_kw(xsd_process_t p) { return p == XSD_PC_STRICT ? ":strict" : p == XSD_PC_LAX ? ":lax" : ":skip"; }

char *xc_wc_sig(cc_t *c, const xsd_wildcard_t *w) {
  const char **v = xc_alloc(c, sizeof *v * (size_t)(w->nns * 2 + 6));
  int n = 0;
  if (!v) return NULL;
  v[n++] = ":wildcard ";
  v[n++] = process_kw(w->process);
  if (w->kind == XSD_WC_ANY) {
    v[n++] = " :any";
  } else {
    const char **ns = xc_alloc(c, sizeof *ns * (size_t)w->nns);
    if (!ns) return NULL;
    for (int i = 0; i < w->nns; i++) ns[i] = w->ns[i] ? w->ns[i] : ":absent";
    if (w->kind == XSD_WC_NOT) v[n++] = " :not";
    else qsort(ns, (size_t)w->nns, sizeof *ns, xc_strp_cmp);
    for (int i = 0; i < w->nns; i++) {
      v[n++] = " ";
      v[n++] = ns[i];
    }
  }
  return xc_cat(c, v, n);
}

static int snode_cmp(const void *a, const void *b) { return strcmp((*(snode_t *const *)a)->sig, (*(snode_t *const *)b)->sig); }

int xc_sign(cc_t *c, snode_t *n) {
  const char **v;
  int k = 0;
  if (n->kind == XSD_P_ELEM) return (n->sig = xc_ename(c, n->elem->q.ns, n->elem->q.name)) ? 0 : xc_oom(c);
  if (n->kind == XSD_P_ANY) return (n->sig = xc_wc_sig(c, n->wc)) ? 0 : xc_oom(c);
  for (int i = 0; i < n->nkids; i++)
    if (xc_sign(c, n->kids[i])) return -2;
  if (n->kind != XSD_P_SEQ) qsort(n->kids, (size_t)n->nkids, sizeof *n->kids, snode_cmp);
  if (!(v = xc_alloc(c, sizeof *v * (size_t)(n->nkids * 2 + 1)))) return xc_oom(c);
  v[k++] = n->kind == XSD_P_SEQ ? ":sequence" : n->kind == XSD_P_CHOICE ? ":choice" : ":all";
  for (int i = 0; i < n->nkids; i++) {
    v[k++] = " ";
    v[k++] = n->kids[i]->sig;
  }
  return (n->sig = xc_cat(c, v, k)) ? 0 : xc_oom(c);
}

static int emit(cc_t *c, const snode_t *n, bim_part_t *p) {
  static const bim_part_kind_t kinds[] = {BIM_PART_ELEM, BIM_PART_ELEM, BIM_PART_SEQ, BIM_PART_CHOICE, BIM_PART_ALL, BIM_PART_ANY};
  bim_part_t *kids = NULL;
  p->kind = kinds[n->kind];
  p->min = n->min;
  p->max = n->max;
  if (n->kind == XSD_P_ELEM) {
    p->name = n->elem->q.name;
    p->ns = n->elem->q.ns;
    p->nillable = n->elem->nillable;
    p->elem = n->elem->global ? xc_pmap_get(&c->elems, n->elem) : NULL;
    return (p->type = xc_get_type(c, n->elem->type)) ? 0 : -2;
  }
  if (n->kind == XSD_P_ANY) return (p->wc = xc_wc_from(c, n->wc)) ? 0 : -2;
  if (n->nkids && !(kids = xc_alloc(c, sizeof *kids * (size_t)n->nkids))) return xc_oom(c);
  for (int i = 0; i < n->nkids; i++)
    if (emit(c, n->kids[i], &kids[i])) return -2;
  p->kids = kids;
  p->nkids = n->nkids;
  return 0;
}

/* XML Schema Part 1 3.4.2: extension with empty explicit content takes base content type */
static int eff_mixed(const xsd_type_t *t) {
  for (int i = 0; t && i < DEPTH_MAX; i++, t = t->base) {
    if (t->kind != XSD_COMPLEX) return 0;
    if (t->mixed || t->deriv != XSD_D_EXTENSION || !xc_content_empty(t->particle)) return t->mixed;
  }
  return 0;
}

/* ISO 23001-1 7.5.2.2.3, 7.5.2.2.4 on root, emitted as top */
static int finish_content(cc_t *c, const xsd_type_t *t, snode_t *root, bim_type_t *b) {
  bim_part_t *top;
  int rc;
  if (!root && !eff_mixed(t)) {
    b->content = BIM_CONTENT_NONE;
    return 0;
  }
  if (!root && !(root = snode_new(c, XSD_P_SEQ, 1, 1, 0))) return -2;
  if (simplify(c, t, root)) return -1;
  while ((rc = group_simplify(c, t, &root)) > 0) {
  }
  if (rc < 0 || xc_sign(c, root)) return -1;
  if (!(top = xc_alloc(c, sizeof *top))) return xc_oom(c);
  if (emit(c, root, top)) return -1;
  b->content = BIM_CONTENT_SEQ;
  b->top = top;
  return 0;
}

/* ISO 23001-1 7.5.2.2.2 .. 7.5.2.2.4 */
static int compile_content(cc_t *c, const xsd_type_t *t, bim_type_t *b) {
  snode_t *root;
  if (effective(c, t, 0, &root)) return -1;
  return finish_content(c, t, root, b);
}

const bim_type_t *xc_delta_type(cc_t *c, const xsd_type_t *base, const xsd_type_t *sub) {
  bim_type_t *d = xc_alloc(c, sizeof *d);
  snode_t *parts[DEPTH_MAX];
  snode_t *root = NULL;
  int n = 0;
  int ext = 0;
  if (!d) {
    xc_oom(c);
    return NULL;
  }
  for (const xsd_type_t *x = sub; x && x != base; x = x->base) {
    if (x->deriv != XSD_D_EXTENSION) continue;
    ext = 1;
    if (x->kind != XSD_COMPLEX || x->content == XSD_C_SIMPLE || xc_content_empty(x->particle)) continue;
    if (n >= DEPTH_MAX) {
      xsd_set_fail(c->s, "LS400129", "nesting too deep");
      return NULL;
    }
    if (!(parts[n++] = xc_build(c, x, x->particle, 0))) return NULL;
  }
  d->content = BIM_CONTENT_NONE;
  if (!ext || sub->kind != XSD_COMPLEX) return d;
  if (xc_compile_attrs(c, sub, base, d)) return NULL;
  if (n > 1) {
    if (!(root = snode_new(c, XSD_P_SEQ, 1, 1, n))) return NULL;
    for (int i = 0; i < n; i++) root->kids[i] = parts[n - 1 - i];
  } else if (n) {
    root = parts[0];
  }
  if (!root) return d;
  d->mixed = sub->mixed;
  return finish_content(c, sub, root, d) ? NULL : d;
}

int xc_fill_type(cc_t *c, const xsd_type_t *t, bim_type_t *b) {
  b->name = t->q.name;
  b->ns = t->q.ns;
  b->abstract = t->abstract;
  if (t->base && !(b->base = xc_get_type(c, t->base))) return -2;
  if (t->kind == XSD_SIMPLE) {
    b->content = BIM_CONTENT_SIMPLE;
    return (b->simple = xc_get_simple(c, t)) ? 0 : -1;
  }
  b->mixed = eff_mixed(t);
  if (xc_compile_attrs(c, t, NULL, b)) return -1;
  if (xc_has_simple_content(t)) {
    b->content = BIM_CONTENT_SIMPLE;
    return (b->simple = xc_simple_content(c, t)) ? 0 : -1;
  }
  return compile_content(c, t, b);
}
