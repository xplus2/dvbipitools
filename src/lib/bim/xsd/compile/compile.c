/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/bim/xsd/compile/priv.h"

bim_type_t *xc_get_type(cc_t *c, const xsd_type_t *t) {
  bim_type_t *b = xc_pmap_get(&c->types, t);
  if (b) return b;
  if (c->nq >= c->qcap) {
    void *np = array_grow(c->queue, &c->qcap, c->nq + 1, sizeof *c->queue);
    if (!np) {
      xc_oom(c);
      return NULL;
    }
    c->queue = np;
  }
  if (!(b = xc_alloc(c, sizeof *b)) || xc_pmap_put(c, &c->types, t, b)) {
    xc_oom(c);
    return NULL;
  }
  c->queue[c->nq++] = t;
  return b;
}

/* fills and branches every queued type */
static int filled_all(cc_t *c) {
  int rc = 0;
  while (!rc && c->nsbc < c->nq) {
    while (!rc && c->nfilled < c->nq) {
      rc = xc_fill_type(c, c->queue[c->nfilled], xc_pmap_get(&c->types, c->queue[c->nfilled]));
      c->nfilled++;
    }
    while (!rc && c->nsbc < c->nfilled) rc = xc_compile_sbc(c, c->queue[c->nsbc++], 0);
  }
  return rc;
}

int xsd_compile(xsd_set_t *s, const xsd_compile_opts_t *o, const xsd_compiled_t **out) {
  cc_t c;
  xsd_compiled_t *res;
  const xsd_elem_t *const *xe;
  const xsd_type_t *const *xt;
  int ne;
  int nt;
  int rc = 0;
  memset(&c, 0, sizeof c);
  c.s = s;
  c.o = o;
  c.codecs = xc_alloc(&c, sizeof *c.codecs * (size_t)(o->ncodecs + 6));
  if (!c.codecs) return xc_oom(&c);
  res = xc_alloc(&c, sizeof *res);
  if (!res) return xc_oom(&c);
  for (int i = 0; i < o->ncodecs; i++) c.codecs[c.ncodecs++] = o->codecs[i];
  /* ISO 15938-1:2002 8.5.4.2.5, 8.5.4.2.6 */
  if (!o->mpegb) {
    int derived = !(o->compat & BIM_COMPAT_M7_FLAGS);
    xc_add_codec(&c, M7_NS, "basicTimePointType", BIM_M7_TIMEPOINT, derived);
    xc_add_codec(&c, M7_NS, "basicDurationType", BIM_M7_DURATION, derived);
    if (!derived) { /* other derived types are strings */
      xc_add_codec(&c, M7_NS, "timePointType", BIM_M7_TIMEPOINT, 0);
      xc_add_codec(&c, M7_NS, "mediaTimePointType", BIM_M7_TIMEPOINT, 0);
      xc_add_codec(&c, M7_NS, "durationType", BIM_M7_DURATION, 0);
      xc_add_codec(&c, M7_NS, "mediaDurationType", BIM_M7_DURATION, 0);
    }
  }
  ne = xsd_elems(s, &xe);
  for (int i = 0; !rc && i < ne; i++) {
    bim_elem_t *e = xc_alloc(&c, sizeof *e);
    if (!e || xc_pmap_put(&c, &c.elems, xe[i], e)) rc = xc_oom(&c);
    else {
      e->name = xe[i]->q.name;
      e->ns = xe[i]->q.ns;
      e->nillable = xe[i]->nillable;
      e->abstract = xe[i]->abstract;
    }
  }
  nt = xsd_types(s, &xt);
  for (int i = 0; !rc && i < nt; i++)
    if (!xc_get_type(&c, xt[i])) rc = -2;
  for (int i = 0; !rc && i < ne; i++) {
    bim_elem_t *e = xc_pmap_get(&c.elems, xe[i]);
    if (!(e->type = xc_get_type(&c, xe[i]->type))) rc = -2;
  }
  rc = rc ? rc : filled_all(&c);
  if (!rc) rc = xc_type_codes(&c, res);
  if (!rc) rc = filled_all(&c);
  if (!rc) rc = xc_elem_codes(&c, res);
  if (!rc) rc = xc_global_attrs(&c, res);
  if (!rc) {
    res->schema.elems = res->elems;
    res->schema.nelems = res->nelems;
    res->schema.types = res->types;
    res->schema.ntypes = res->ntypes;
    res->schema.string = xc_get_simple(&c, xsd_find_type(s, XSD_NS, "string"));
  }
  free(c.queue);
  if (rc) return xsd_set_error_code(s) && !strcmp(xsd_set_error_code(s), "LS500101") ? -2 : -1;
  *out = res;
  return 0;
}

const bim_type_t *xsd_compiled_type(const xsd_compiled_t *c, const char *ns, const char *name) {
  for (int i = 0; i < c->ntypes; i++)
    if (c->types[i]->name && !strcmp(c->types[i]->name, name) && xc_ns_eq(c->types[i]->ns, ns)) return c->types[i];
  return NULL;
}

int xsd_cast_bind(const xsd_compiled_t *const *cs, size_t n, bim_node_t *root, const char **bad) {
  const char *qn = bim_node_attr_ns(root, XSI_NS, "type");
  if (qn) {
    const char *ns;
    const char *local;
    const bim_type_t *t = NULL;
    if (bim_node_qname(root, qn, &ns, &local)) {
      *bad = qn;
      return -1;
    }
    for (size_t i = 0; i < n && !t; i++) t = xsd_compiled_type(cs[i], ns, local);
    if (!t) {
      *bad = qn;
      return -1;
    }
    root->cast = t;
  }
  for (int i = 0; i < root->nkids; i++)
    if (root->kids[i]->name && xsd_cast_bind(cs, n, root->kids[i], bad)) return -1;
  return 0;
}

int xsd_compiled_has_ns(const xsd_compiled_t *c, const char *ns) {
  for (int i = 0; i < c->ntypes; i++)
    if (c->types[i]->ns && !strcmp(c->types[i]->ns, ns)) return 1;
  for (int i = 0; i < c->nelems; i++)
    if (c->elems[i]->ns && !strcmp(c->elems[i]->ns, ns)) return 1;
  return 0;
}

static int note_ns(const xsd_compiled_t *c, const char *ns, const char **out, int *n, size_t cap) {
  if (!ns || !*ns || !strcmp(ns, XML_NS) || !strcmp(ns, XSI_NS) || xsd_compiled_has_ns(c, ns)) return 0;
  for (int i = 0; i < *n; i++)
    if (!strcmp(out[i], ns)) return 0;
  if ((size_t)*n >= cap) return -1;
  out[(*n)++] = ns;
  return 0;
}

static int scan_ns(const xsd_compiled_t *c, const bim_node_t *e, const char **out, int *n, size_t cap) {
  const char *qn = bim_node_attr_ns(e, XSI_NS, "type");
  const char *ns;
  const char *local;
  if (note_ns(c, e->ns, out, n, cap)) return -1;
  for (int i = 0; i < e->nattrs; i++)
    if (note_ns(c, e->attrs[i].ns, out, n, cap)) return -1;
  if (qn && !bim_node_qname(e, qn, &ns, &local) && note_ns(c, ns, out, n, cap)) return -1;
  for (int i = 0; i < e->nkids; i++)
    if (e->kids[i]->name && scan_ns(c, e->kids[i], out, n, cap)) return -1;
  return 0;
}

int xsd_tree_extra_ns(const xsd_compiled_t *c, const bim_node_t *root, const char **ns, size_t cap) {
  int n = 0;
  return scan_ns(c, root, ns, &n, cap) ? -1 : n;
}

const bim_elem_t *xsd_compiled_elem(const xsd_compiled_t *c, const char *ns, const char *name) {
  for (int i = 0; i < c->nelems; i++)
    if (!strcmp(c->elems[i]->name, name) && xc_ns_eq(c->elems[i]->ns, ns)) return c->elems[i];
  return NULL;
}
