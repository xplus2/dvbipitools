/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/codec/encode/priv.h"

int enc_global_ptr(const enc_t *e, const bim_elem_t *x, int *sid, uint64_t *code) {
  for (int i = 0; i < e->nschemas; i++)
    for (int c = 0; e->schemas[i].known && c < e->schemas[i].nelems; c++)
      if (e->schemas[i].elems[c] == x) {
        if (sid) *sid = i;
        if (code) *code = (uint64_t)c;
        return 0;
      }
  return -1;
}

int enc_global_name(const enc_t *e, const bim_node_t *k, const bim_elem_t **x, int *sid, uint64_t *code) {
  for (int i = 0; i < e->nschemas; i++) {
    if (!e->schemas[i].known || i == e->o->lax_schema) continue;
    for (int c = 0; c < e->schemas[i].nelems; c++)
      if (bim_node_is(k, e->schemas[i].elems[c]->ns, e->schemas[i].elems[c]->name)) {
        *x = e->schemas[i].elems[c];
        if (sid) *sid = i;
        if (code) *code = (uint64_t)c;
        return 0;
      }
  }
  return -1;
}

typedef struct {
  const bim_node_t *k;
  const bim_elem_t *x;
  uint64_t code;
  int sid;
} any_t;

static int known_inner(enc_t *e, void *arg) {
  const any_t *a = arg;
  int schema = e->schema;
  int rc;
  if (bitwriter_put_vluimsbf5(e->bw, a->code)) return -1;
  e->schema = a->sid;
  rc = enc_element(e, NULL, a->x->type, a->k, 0);
  e->schema = schema;
  return rc;
}

static int lax_inner(enc_t *e, void *arg) {
  const any_t *a = arg;
  return enc_lax(e, a->k);
}

int enc_any_element(enc_t *e, const bim_node_t *k) {
  any_t a = {k, NULL, 0, 0};
  int w = bim_width((uint64_t)e->nschemas);
  if (!enc_global_name(e, k, &a.x, &a.sid, &a.code)) return bitwriter_put(e->bw, (uint64_t)a.sid, w) || enc_counted(e, known_inner, &a);
  if (e->o->lax_schema < 0) return -1;
  return bitwriter_put(e->bw, (uint64_t)e->o->lax_schema, w) || enc_counted(e, lax_inner, &a);
}

typedef struct {
  int sid; /* -1: not an extra attribute */
  uint64_t code;
  int used;
} ex_t;

typedef struct {
  const bim_node_t *node;
  const ex_t *ex;
  const int *idx; /* attributes of group */
  int n;
  int lax;
  const bim_attr_t *attrs;
} grp_t;

static int put_str(bitwriter_t *bw, const char *s) {
  size_t n = strlen(s);
  return bitwriter_put_vluimsbf5(bw, n) || (n && bitwriter_put_bytes(bw, (const unsigned char *)s, n));
}

static int group_inner(enc_t *e, void *arg) {
  const grp_t *g = arg;
  if (g->lax) {
    if (bitwriter_put_vluimsbf5(e->bw, (uint64_t)g->n)) return -1;
    for (int i = 0; i < g->n; i++)
      if (put_str(e->bw, g->node->attrs[g->idx[i]].name) || put_str(e->bw, g->node->attrs[g->idx[i]].value)) return -1;
    return 0;
  }
  for (int i = 0; i < g->n; i++) {
    int a = g->idx[i];
    if (bitwriter_put_vluimsbf5(e->bw, g->ex[a].code) || enc_text(e, g->attrs[g->ex[a].code].simple, g->node->attrs[a].value) || enc_bit(e->bw, i + 1 < g->n)) return -1;
  }
  return 0;
}

static int resolve_attr(const enc_t *e, const bim_node_t *node, int i, ex_t *x) {
  x->sid = -1;
  for (int s = 0; s < e->nschemas; s++) {
    if (!e->schemas[s].known || s == e->o->lax_schema) continue;
    for (int c = 0; c < e->schemas[s].nattrs; c++) {
      const bim_attr_t *a = &e->schemas[s].attrs[c];
      if (!strcmp(a->name, node->attrs[i].name) && ((!a->ns && !node->attrs[i].ns) || (a->ns && node->attrs[i].ns && !strcmp(a->ns, node->attrs[i].ns)))) {
        x->sid = s;
        x->code = (uint64_t)c;
        return 0;
      }
    }
  }
  if (e->o->lax_schema < 0 || node->attrs[i].ns) return -1;
  x->sid = e->o->lax_schema;
  return 0;
}

int enc_any_attributes(enc_t *e, const bim_type_t *t, const bim_node_t *node) {
  ex_t *ex = calloc((size_t)node->nattrs + 1, sizeof *ex);
  int *g = calloc((size_t)node->nattrs + 1, sizeof *g);
  int w = bim_width((uint64_t)e->nschemas);
  int n = 0;
  int done = 0;
  int rc = -1;
  if (!ex || !g) goto out;
  for (int i = 0; i < node->nattrs; i++) {
    ex[i].sid = -1;
    if (enc_extra_attr(t, node, i)) {
      if (resolve_attr(e, node, i, &ex[i])) goto out;
      n++;
    }
  }
  if (enc_bit(e->bw, n)) goto out;
  while (done < n) {
    grp_t grp;
    int ng = 0;
    int first = 0;
    while (first < node->nattrs && (ex[first].sid < 0 || ex[first].used)) first++;
    if (first >= node->nattrs) goto out;
    for (int i = first; i < node->nattrs; i++)
      if (ex[i].sid == ex[first].sid && !ex[i].used) {
        ex[i].used = 1;
        g[ng++] = i;
      }
    grp = (grp_t){node, ex, g, ng, ex[first].sid == e->o->lax_schema, e->schemas[ex[first].sid].attrs};
    done += ng;
    if (bitwriter_put(e->bw, (uint64_t)ex[first].sid, w) || enc_counted(e, group_inner, &grp) || enc_bit(e->bw, done < n)) goto out;
  }
  rc = 0;
out:
  free(ex);
  free(g);
  return rc;
}
