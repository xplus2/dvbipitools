/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/xsd/compile/priv.h"

/* stable merge sort of indices by expanded name */
static void isort(int *v, int *tmp, int n, const ename_t *e) {
  int h = n / 2;
  int i = 0;
  int j = h;
  int k = 0;
  if (n < 2) return;
  isort(v, tmp, h, e);
  isort(v + h, tmp, n - h, e);
  while (i < h && j < n) tmp[k++] = xc_ename_cmp(e[v[j]].ns, e[v[j]].name, e[v[i]].ns, e[v[i]].name) < 0 ? v[j++] : v[i++];
  while (i < h) tmp[k++] = v[i++];
  while (j < n) tmp[k++] = v[j++];
  memcpy(v, tmp, sizeof *v * (size_t)n);
}

static int forest_build(cc_t *c, forest_t *f, const int *parent, int n, const ename_t *e) {
  int *cnt = calloc((size_t)n + 1, sizeof *cnt);
  int *tmp = malloc(sizeof *tmp * ((size_t)n + 1));
  f->n = n;
  f->first = calloc((size_t)n + 2, sizeof *f->first);
  f->kids = malloc(sizeof *f->kids * ((size_t)n + 1));
  if (!cnt || !tmp || !f->first || !f->kids) {
    free(cnt);
    free(tmp);
    return xc_oom(c);
  }
  for (int i = 0; i < n; i++) cnt[parent[i] < 0 ? n : parent[i]]++;
  for (int i = 0; i <= n; i++) f->first[i + 1] = f->first[i] + cnt[i];
  memset(cnt, 0, sizeof *cnt * ((size_t)n + 1));
  for (int i = 0; i < n; i++) {
    int p = parent[i] < 0 ? n : parent[i];
    f->kids[f->first[p] + cnt[p]++] = i;
  }
  for (int i = 0; i <= n; i++) isort(f->kids + f->first[i], tmp, cnt[i], e);
  free(cnt);
  free(tmp);
  return 0;
}

static void forest_free(forest_t *f) {
  free(f->first);
  free(f->kids);
}

/* depth-first preorder below node (n: all roots), pos + size per node */
static int forest_walk(cc_t *c, const forest_t *f, int node, int depth, int *order, int *k, int *pos, int *size) {
  if (depth > DEPTH_MAX) return xsd_set_fail(c->s, "LS400129", "nesting too deep");
  for (int i = f->first[node]; i < f->first[node + 1]; i++) {
    int ch = f->kids[i];
    pos[ch] = *k;
    order[(*k)++] = ch;
    if (forest_walk(c, f, ch, depth + 1, order, k, pos, size)) return -1;
    size[ch] = *k - pos[ch];
  }
  return 0;
}

int xc_type_codes(cc_t *c, xsd_compiled_t *out) {
  const xsd_type_t *const *xt;
  int n = xsd_types(c->s, &xt);
  int *parent = malloc(sizeof *parent * ((size_t)n + 1));
  int *order = malloc(sizeof *order * ((size_t)n + 1));
  int *pos = calloc((size_t)n + 1, sizeof *pos);
  int *size = calloc((size_t)n + 1, sizeof *size);
  ename_t *e = malloc(sizeof *e * ((size_t)n + 1));
  const bim_type_t **types = xc_alloc(c, sizeof *types * ((size_t)n + 1));
  bim_subtypes_t *subs = xc_alloc(c, sizeof *subs * ((size_t)n + 1));
  forest_t f = {NULL, NULL, 0};
  pmap_t index = {NULL, NULL, 0, 0};
  int k = 0;
  int nsubs = 0;
  int rc = 0;
  if (!parent || !order || !pos || !size || !e || !types || !subs) {
    rc = xc_oom(c);
    goto done;
  }
  for (int i = 0; !rc && i < n; i++) rc = xc_pmap_put(c, &index, xt[i], (void *)(intptr_t)(i + 1));
  for (int i = 0; !rc && i < n; i++) {
    const xsd_type_t *b = xt[i]->base;
    int depth = 0;
    e[i].ns = xt[i]->q.ns;
    e[i].name = xt[i]->q.name;
    /* named types only (7.4.5), anonymous bases skipped */
    while (b && !xc_pmap_get(&index, b) && depth++ < DEPTH_MAX) b = b->base;
    parent[i] = b ? (int)(intptr_t)xc_pmap_get(&index, b) - 1 : -1;
  }
  if (rc || (rc = forest_build(c, &f, parent, n, e)) || (rc = forest_walk(c, &f, n, 0, order, &k, pos, size))) goto done;
  for (int i = 0; i < n; i++) {
    int at = order[i];
    if (!(types[i] = xc_pmap_get(&c->types, xt[at]))) {
      rc = -1;
      goto done;
    }
  }
  for (int i = 0; i < n; i++)
    if (size[order[i]] > 1) {
      const bim_type_t **dl = xc_alloc(c, sizeof *dl * (size_t)(size[order[i]] - 1));
      if (!dl) {
        rc = xc_oom(c);
        goto done;
      }
      for (int k = 0; k < size[order[i]] - 1; k++)
        if (!(dl[k] = xc_delta_type(c, xt[order[i]], xt[order[i + 1 + k]]))) {
          rc = -1;
          goto done;
        }
      subs[nsubs].base = types[i];
      subs[nsubs].subs = &types[i + 1];
      subs[nsubs].deltas = dl;
      subs[nsubs++].nsubs = size[order[i]] - 1;
    }
  out->types = types;
  out->ntypes = n;
  out->schema.subs = subs;
  out->schema.nsubs = nsubs;
  out->schema.known = 1;
done:
  forest_free(&f);
  free(parent);
  free(order);
  free(pos);
  free(size);
  free(e);
  return rc;
}

int xc_global_attrs(cc_t *c, xsd_compiled_t *res) {
  const xsd_attr_t *const *xa;
  int n = xsd_attrs(c->s, &xa);
  bim_attr_t *v = n ? xc_alloc(c, sizeof *v * (size_t)n) : NULL;
  if (n && !v) return xc_oom(c);
  for (int i = 0; i < n; i++) {
    v[i].name = xa[i]->q.name;
    v[i].ns = xa[i]->q.ns;
    if (!(v[i].simple = xc_get_simple(c, xa[i]->type))) return -1;
  }
  if (n) qsort(v, (size_t)n, sizeof *v, xc_battr_cmp);
  res->schema.attrs = v;
  res->schema.nattrs = n;
  return 0;
}

int xc_elem_codes(cc_t *c, xsd_compiled_t *out) {
  const xsd_elem_t *const *xe;
  int n = xsd_elems(c->s, &xe);
  int *parent = malloc(sizeof *parent * ((size_t)n + 1));
  int *pre = malloc(sizeof *pre * ((size_t)n + 1));
  int *pos = calloc((size_t)n + 1, sizeof *pos);
  int *size = calloc((size_t)n + 1, sizeof *size);
  int *v = malloc(sizeof *v * ((size_t)n + 1));
  int *tmp = malloc(sizeof *tmp * ((size_t)n + 1));
  ename_t *e = malloc(sizeof *e * ((size_t)n + 1));
  const bim_elem_t **sel = xc_alloc(c, sizeof *sel * ((size_t)n + 1));
  forest_t f = {NULL, NULL, 0};
  pmap_t index = {NULL, NULL, 0, 0};
  int k = 0;
  int rc = 0;
  if (!parent || !pre || !pos || !size || !v || !tmp || !e || !sel) {
    rc = xc_oom(c);
    goto done;
  }
  for (int i = 0; !rc && i < n; i++) rc = xc_pmap_put(c, &index, xe[i], (void *)(intptr_t)(i + 1));
  for (int i = 0; !rc && i < n; i++) {
    e[i].ns = xe[i]->q.ns;
    e[i].name = xe[i]->q.name;
    parent[i] = xe[i]->subst_head ? (int)(intptr_t)xc_pmap_get(&index, xe[i]->subst_head) - 1 : -1;
  }
  if (rc || (rc = forest_build(c, &f, parent, n, e)) || (rc = forest_walk(c, &f, n, 0, pre, &k, pos, size))) goto done;
  memcpy(v, pre, sizeof *v * (size_t)n);
  if (!c->o->hier) isort(v, tmp, n, e);
  for (int i = 0; i < n; i++) sel[i] = xc_pmap_get(&c->elems, xe[v[i]]);
  for (int i = 0; i < n; i++) {
    bim_elem_t *h = xc_pmap_get(&c->elems, xe[i]);
    const bim_elem_t **m;
    int nm = size[i] - 1;
    if (nm <= 0) continue;
    if (!(m = xc_alloc(c, sizeof *m * (size_t)nm))) {
      rc = xc_oom(c);
      goto done;
    }
    memcpy(v, &pre[pos[i] + 1], sizeof *v * (size_t)nm);
    if (!c->o->hier) isort(v, tmp, nm, e);
    for (int p = 0; p < nm; p++) m[p] = xc_pmap_get(&c->elems, xe[v[p]]);
    h->substs = m;
    h->nsubsts = nm;
  }
  out->elems = sel;
  out->nelems = n;
done:
  forest_free(&f);
  free(parent);
  free(pre);
  free(pos);
  free(size);
  free(v);
  free(tmp);
  free(e);
  return rc;
}
