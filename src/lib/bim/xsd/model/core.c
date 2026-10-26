/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/xsd/model/priv.h"

void *xm_arena_alloc(xsd_set_t *s, size_t n) {
  arena_chunk_t *c = s->arena;
  n = (n + sizeof(max_align_t) - 1) / sizeof(max_align_t) * sizeof(max_align_t);
  if (!c || c->cap - c->used < n) {
    size_t cap = n > ARENA_CHUNK ? n : ARENA_CHUNK;
    c = malloc(sizeof *c + cap);
    if (!c) {
      s->oom = 1;
      return NULL;
    }
    c->next = s->arena;
    c->used = 0;
    c->cap = cap;
    s->arena = c;
  }
  c->used += n;
  return memset((char *)c->data + c->used - n, 0, n);
}

char *xm_arena_strndup(xsd_set_t *s, const char *p, size_t n) {
  char *c = xm_arena_alloc(s, n + 1);
  if (!c) return NULL;
  memcpy(c, p, n);
  c[n] = '\0';
  return c;
}

int xm_plist_add(xsd_set_t *s, plist_t *l, void *p) {
  if (l->n >= l->cap) {
    void *np = array_grow(l->v, &l->cap, l->n + 1, sizeof *l->v);
    if (!np) {
      s->oom = 1;
      return -1;
    }
    l->v = np;
  }
  l->v[l->n++] = p;
  return 0;
}

int xsd_set_fail_str(xsd_set_t *s, const char *code, const char *msg) {
  if (!s->err[0]) {
    s->code = code;
    snprintf(s->err, sizeof s->err, "%s", msg);
  }
  return -1;
}

int xm_qeq(const xsd_qname_t *a, const char *ns, const char *name) {
  return !strcmp(a->name, name) && (a->ns == ns || (a->ns && ns && !strcmp(a->ns, ns)));
}

static size_t qhash(const char *ns, const char *name) {
  uint64_t h = 1469598103934665603ULL;
  for (const char *p = ns ? ns : ""; *p; p++) h = (h ^ (unsigned char)*p) * 1099511628211ULL;
  h = (h ^ '}') * 1099511628211ULL;
  for (const char *p = name; *p; p++) h = (h ^ (unsigned char)*p) * 1099511628211ULL;
  return (size_t)h;
}

static size_t cmap_slot(const cmap_t *m, const char *ns, const char *name) {
  size_t i = qhash(ns, name) & (m->nslots - 1);
  while (m->slots[i] && !xm_qeq(m->slots[i], ns, name)) i = (i + 1) & (m->nslots - 1);
  return i;
}

void *xm_cmap_get(const cmap_t *m, const char *ns, const char *name) {
  size_t i;
  if (!m->nslots || !name) return NULL;
  i = cmap_slot(m, ns, name);
  return m->slots[i];
}

/* -1 oom, -2 duplicate */
int xm_cmap_put(xsd_set_t *s, cmap_t *m, xsd_qname_t *q) {
  size_t i;
  if ((m->nused + 1) * 2 > m->nslots) {
    size_t old = m->nslots;
    xsd_qname_t **os = m->slots;
    m->nslots = old ? old * 2 : 64;
    m->slots = calloc(m->nslots, sizeof *m->slots);
    if (!m->slots) {
      m->slots = os;
      m->nslots = old;
      s->oom = 1;
      return -1;
    }
    for (size_t k = 0; k < old; k++)
      if (os[k]) m->slots[cmap_slot(m, os[k]->ns, os[k]->name)] = os[k];
    free(os);
  }
  i = cmap_slot(m, q->ns, q->name);
  if (m->slots[i]) return -2;
  if (m->n >= m->cap) {
    void *np = array_grow(m->order, &m->cap, m->n + 1, sizeof *m->order);
    if (!np) {
      s->oom = 1;
      return -1;
    }
    m->order = np;
  }
  m->slots[i] = q;
  m->order[m->n++] = q;
  m->nused++;
  return 0;
}

void xm_cmap_replace(cmap_t *m, const xsd_qname_t *old, xsd_qname_t *q) {
  m->slots[cmap_slot(m, q->ns, q->name)] = q;
  for (int i = 0; i < m->n; i++)
    if (m->order[i] == old) m->order[i] = q;
}

static void cmap_free(cmap_t *m) {
  free(m->slots);
  free(m->order);
}

/* XML Schema Part 2 3.2, 3.3 built-in types. base "": anyType */
static const struct {
  const char *name;
  const char *base;
  xsd_deriv_t deriv;
  const char *min;
  const char *max;
} builtins[] = {
  {"anySimpleType", "", XSD_D_RESTRICTION, NULL, NULL},
  {"string", "anySimpleType", XSD_D_RESTRICTION, NULL, NULL},
  {"boolean", "anySimpleType", XSD_D_RESTRICTION, NULL, NULL},
  {"decimal", "anySimpleType", XSD_D_RESTRICTION, NULL, NULL},
  {"float", "anySimpleType", XSD_D_RESTRICTION, NULL, NULL},
  {"double", "anySimpleType", XSD_D_RESTRICTION, NULL, NULL},
  {"duration", "anySimpleType", XSD_D_RESTRICTION, NULL, NULL},
  {"dateTime", "anySimpleType", XSD_D_RESTRICTION, NULL, NULL},
  {"time", "anySimpleType", XSD_D_RESTRICTION, NULL, NULL},
  {"date", "anySimpleType", XSD_D_RESTRICTION, NULL, NULL},
  {"gYearMonth", "anySimpleType", XSD_D_RESTRICTION, NULL, NULL},
  {"gYear", "anySimpleType", XSD_D_RESTRICTION, NULL, NULL},
  {"gMonthDay", "anySimpleType", XSD_D_RESTRICTION, NULL, NULL},
  {"gDay", "anySimpleType", XSD_D_RESTRICTION, NULL, NULL},
  {"gMonth", "anySimpleType", XSD_D_RESTRICTION, NULL, NULL},
  {"hexBinary", "anySimpleType", XSD_D_RESTRICTION, NULL, NULL},
  {"base64Binary", "anySimpleType", XSD_D_RESTRICTION, NULL, NULL},
  {"anyURI", "anySimpleType", XSD_D_RESTRICTION, NULL, NULL},
  {"QName", "anySimpleType", XSD_D_RESTRICTION, NULL, NULL},
  {"NOTATION", "anySimpleType", XSD_D_RESTRICTION, NULL, NULL},
  {"normalizedString", "string", XSD_D_RESTRICTION, NULL, NULL},
  {"token", "normalizedString", XSD_D_RESTRICTION, NULL, NULL},
  {"language", "token", XSD_D_RESTRICTION, NULL, NULL},
  {"NMTOKEN", "token", XSD_D_RESTRICTION, NULL, NULL},
  {"Name", "token", XSD_D_RESTRICTION, NULL, NULL},
  {"NCName", "Name", XSD_D_RESTRICTION, NULL, NULL},
  {"ID", "NCName", XSD_D_RESTRICTION, NULL, NULL},
  {"IDREF", "NCName", XSD_D_RESTRICTION, NULL, NULL},
  {"ENTITY", "NCName", XSD_D_RESTRICTION, NULL, NULL},
  {"NMTOKENS", "NMTOKEN", XSD_D_LIST, NULL, NULL},
  {"IDREFS", "IDREF", XSD_D_LIST, NULL, NULL},
  {"ENTITIES", "ENTITY", XSD_D_LIST, NULL, NULL},
  {"integer", "decimal", XSD_D_RESTRICTION, NULL, NULL},
  {"nonPositiveInteger", "integer", XSD_D_RESTRICTION, NULL, "0"},
  {"negativeInteger", "nonPositiveInteger", XSD_D_RESTRICTION, NULL, "-1"},
  {"long", "integer", XSD_D_RESTRICTION, "-9223372036854775808", "9223372036854775807"},
  {"int", "long", XSD_D_RESTRICTION, "-2147483648", "2147483647"},
  {"short", "int", XSD_D_RESTRICTION, "-32768", "32767"},
  {"byte", "short", XSD_D_RESTRICTION, "-128", "127"},
  {"nonNegativeInteger", "integer", XSD_D_RESTRICTION, "0", NULL},
  {"unsignedLong", "nonNegativeInteger", XSD_D_RESTRICTION, NULL, "18446744073709551615"},
  {"unsignedInt", "unsignedLong", XSD_D_RESTRICTION, NULL, "4294967295"},
  {"unsignedShort", "unsignedInt", XSD_D_RESTRICTION, NULL, "65535"},
  {"unsignedByte", "unsignedShort", XSD_D_RESTRICTION, NULL, "255"},
  {"positiveInteger", "nonNegativeInteger", XSD_D_RESTRICTION, "1", NULL},
};

static xsd_type_t *builtin_type(xsd_set_t *s, const char *name, xsd_kind_t kind) {
  xsd_type_t *t = xm_arena_alloc(s, sizeof *t);
  if (!t || xm_plist_add(s, &s->all_types, t)) return NULL;
  t->q.ns = XSD_NS;
  t->q.name = name;
  t->kind = kind;
  t->builtin = 1;
  return xm_cmap_put(s, &s->types, &t->q) ? NULL : t;
}

/* anyType: XML Schema Part 1 3.4.7 */
static int add_builtins(xsd_set_t *s) {
  xsd_type_t *any = builtin_type(s, "anyType", XSD_COMPLEX);
  xsd_particle_t *seq;
  xsd_wildcard_t *wc;
  if (!any) return -2;
  seq = xm_arena_alloc(s, sizeof *seq);
  if (!seq) return -2;
  seq->kids = xm_arena_alloc(s, sizeof *seq->kids);
  if (!seq->kids) return -2;
  wc = xm_arena_alloc(s, sizeof *wc);
  if (!wc) return -2;
  wc->kind = XSD_WC_ANY;
  wc->process = XSD_PC_LAX;
  seq->kind = XSD_P_SEQ;
  seq->max = 1;
  seq->min = seq->max;
  seq->nkids = 1;
  seq->kids[0].kind = XSD_P_ANY;
  seq->kids[0].max = XSD_UNBOUNDED;
  seq->kids[0].wc = wc;
  any->mixed = 1;
  any->content = XSD_C_ELEMENTS;
  any->particle = seq;
  any->attrs.wc = wc;
  for (size_t i = 0; i < sizeof builtins / sizeof builtins[0]; i++) {
    xsd_type_t *t = builtin_type(s, builtins[i].name, XSD_SIMPLE);
    xsd_type_t *b;
    int nf = !!builtins[i].min + !!builtins[i].max;
    if (!t) return -2;
    b = xm_cmap_get(&s->types, XSD_NS, *builtins[i].base ? builtins[i].base : "anyType");
    t->deriv = builtins[i].deriv;
    if (t->deriv == XSD_D_LIST) {
      t->item = b;
      b = xm_cmap_get(&s->types, XSD_NS, "anySimpleType");
    }
    t->base = b;
    if (nf) {
      t->facets = xm_arena_alloc(s, sizeof *t->facets * (size_t)nf);
      if (!t->facets) return -2;
    }
    if (builtins[i].min) t->facets[t->nfacets++] = (xsd_facet_t){XSD_F_MININCLUSIVE, builtins[i].min};
    if (builtins[i].max) t->facets[t->nfacets++] = (xsd_facet_t){XSD_F_MAXINCLUSIVE, builtins[i].max};
  }
  return 0;
}

xsd_set_t *xsd_set_new(void) {
  xsd_set_t *s = calloc(1, sizeof *s);
  if (s && add_builtins(s)) {
    xsd_set_free(s);
    return NULL;
  }
  return s;
}

void xsd_set_free(xsd_set_t *s) {
  if (!s) return;
  while (s->arena) {
    arena_chunk_t *c = s->arena;
    s->arena = c->next;
    free(c);
  }
  cmap_free(&s->types);
  cmap_free(&s->elems);
  cmap_free(&s->attrs);
  cmap_free(&s->groups);
  cmap_free(&s->agroups);
  for (int i = 0; i < s->docs.n; i++) xml_doc_free(s->docs.v[i]);
  free(s->docs.v);
  free(s->all_types.v);
  free(s->all_elems.v);
  free(s->all_attrs.v);
  free(s->all_attrsets.v);
  free(s->particles.v);
  free(s->refs);
  free(s);
}

void *xsd_set_alloc(xsd_set_t *s, size_t n) { return xm_arena_alloc(s, n); }

const char *xsd_set_error(const xsd_set_t *s) { return s->oom ? "out of memory" : s->err; }

const char *xsd_set_error_code(const xsd_set_t *s) { return s->oom ? "LS500101" : s->code; }

const xsd_type_t *xsd_find_type(const xsd_set_t *s, const char *ns, const char *name) { return xm_cmap_get(&s->types, ns, name); }

const xsd_elem_t *xsd_find_elem(const xsd_set_t *s, const char *ns, const char *name) { return xm_cmap_get(&s->elems, ns, name); }

const xsd_attr_t *xsd_find_attr(const xsd_set_t *s, const char *ns, const char *name) { return xm_cmap_get(&s->attrs, ns, name); }

int xsd_types(const xsd_set_t *s, const xsd_type_t *const **out) {
  *out = (const xsd_type_t *const *)s->types.order;
  return s->types.n;
}

int xsd_elems(const xsd_set_t *s, const xsd_elem_t *const **out) {
  *out = (const xsd_elem_t *const *)s->elems.order;
  return s->elems.n;
}

int xsd_attrs(const xsd_set_t *s, const xsd_attr_t *const **out) {
  *out = (const xsd_attr_t *const *)s->attrs.order;
  return s->attrs.n;
}
