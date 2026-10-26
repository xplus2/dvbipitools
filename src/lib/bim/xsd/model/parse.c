/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/xsd/model/priv.h"

static int parse_particle(ctx_t *c, const bim_node_t *n, xsd_particle_t *p);

int xm_is_xs(const bim_node_t *n, const char *name) { return n->name && n->ns && !strcmp(n->ns, XSD_NS) && !strcmp(n->name, name); }

static const bim_node_t *xs_kid(const bim_node_t *n, const char *name) {
  for (int i = 0; i < n->nkids; i++)
    if (xm_is_xs(n->kids[i], name)) return n->kids[i];
  return NULL;
}

/* unqualified attribute of XSD element */
const char *xm_xattr(const bim_node_t *n, const char *name) {
  for (int i = 0; i < n->nattrs; i++)
    if (!n->attrs[i].ns && !strcmp(n->attrs[i].name, name)) return n->attrs[i].value;
  return NULL;
}

static const char *trim(xsd_set_t *s, const char *v) {
  size_t n;
  while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r') v++;
  n = strlen(v);
  while (n && (v[n - 1] == ' ' || v[n - 1] == '\t' || v[n - 1] == '\n' || v[n - 1] == '\r')) n--;
  return v[n] ? xm_arena_strndup(s, v, n) : v;
}

static int bool_attr(const bim_node_t *n, const char *name) {
  const char *v = xm_xattr(n, name);
  return v && (!strcmp(v, "true") || !strcmp(v, "1"));
}

/* QName value in scope of n, unprefixed: default namespace (XML Schema 3.15.3) */
static int qname_of(ctx_t *c, const bim_node_t *n, const char *v, xsd_qname_t *q) {
  const char *t = trim(c->s, v);
  int rc;
  if (!t) return -2;
  rc = bim_node_qname(n, t, &q->ns, &q->name);
  if (rc == -1) return xsd_set_fail(c->s, "LS400251", "bim: %s: bad QName \"%s\"", c->path, v);
  if (rc) return xsd_set_fail(c->s, "LS400252", "bim: %s: unbound prefix in \"%s\"", c->path, v);
  return 0;
}

static int qname_attr(ctx_t *c, const bim_node_t *n, const char *attr, xsd_qname_t *q) {
  const char *v = xm_xattr(n, attr);
  return v ? qname_of(c, n, v, q) : 0;
}

static int parse_occurs(ctx_t *c, const bim_node_t *n, uint64_t *min, uint64_t *max) {
  const char *mi = xm_xattr(n, "minOccurs");
  const char *ma = xm_xattr(n, "maxOccurs");
  char *end;
  unsigned long long v;
  *min = *max = 1;
  if (mi) {
    mi = trim(c->s, mi);
    if (!mi) return -2;
    errno = 0;
    v = strtoull(mi, &end, 10);
    if (end == mi || *end || errno || *mi == '-' || v >= XSD_UNBOUNDED) return xsd_set_fail(c->s, "LS400253", "bim: %s: bad minOccurs \"%s\"", c->path, mi);
    *min = v;
  }
  if (ma) {
    ma = trim(c->s, ma);
    if (!ma) return -2;
    if (!strcmp(ma, "unbounded")) {
      *max = XSD_UNBOUNDED;
    } else {
      errno = 0;
      v = strtoull(ma, &end, 10);
      if (end == ma || *end || errno || *ma == '-' || v >= XSD_UNBOUNDED) return xsd_set_fail(c->s, "LS400254", "bim: %s: bad maxOccurs \"%s\"", c->path, ma);
      *max = v;
    }
  }
  return *max < *min ? xsd_set_fail(c->s, "LS400255", "bim: %s: maxOccurs below minOccurs", c->path) : 0;
}

static xsd_wildcard_t *parse_wildcard(ctx_t *c, const bim_node_t *n) {
  xsd_wildcard_t *w = xm_arena_alloc(c->s, sizeof *w);
  const char *v = xm_xattr(n, "namespace");
  const char *pc = xm_xattr(n, "processContents");
  if (!w || (v && !(v = trim(c->s, v)))) return NULL;
  w->process = !pc || !strcmp(pc, "strict") ? XSD_PC_STRICT : !strcmp(pc, "lax") ? XSD_PC_LAX : XSD_PC_SKIP;
  if (pc && w->process == XSD_PC_SKIP && strcmp(pc, "skip")) {
    xsd_set_fail(c->s, "LS400256", "bim: %s: bad processContents \"%s\"", c->path, pc);
    return NULL;
  }
  if (!v || !strcmp(v, "##any")) {
    w->kind = XSD_WC_ANY;
    return w;
  }
  if (!strcmp(v, "##other")) {
    w->kind = XSD_WC_NOT;
    w->ns = xm_arena_alloc(c->s, sizeof *w->ns);
    if (!w->ns) return NULL;
    w->ns[0] = c->tns;
    w->nns = 1;
    return w;
  }
  w->kind = XSD_WC_LIST;
  for (const char *p = v; *p;) {
    size_t len;
    const char **nn;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    len = strcspn(p, " \t\n\r");
    if (!len) break;
    nn = xm_arena_alloc(c->s, sizeof *nn * (size_t)(w->nns + 1));
    if (!nn) return NULL;
    if (w->nns) memcpy(nn, w->ns, sizeof *nn * (size_t)w->nns);
    if (len == 17 && !memcmp(p, "##targetNamespace", 17)) nn[w->nns] = c->tns;
    else if (len == 7 && !memcmp(p, "##local", 7)) nn[w->nns] = NULL;
    else if (!(nn[w->nns] = xm_arena_strndup(c->s, p, len))) return NULL;
    w->ns = nn;
    w->nns++;
    p += len;
  }
  return w;
}

static xsd_type_t *new_type(ctx_t *c, const bim_node_t *n, xsd_kind_t kind) {
  xsd_type_t *t = xm_arena_alloc(c->s, sizeof *t);
  const char *name = xm_xattr(n, "name");
  if (!t) return NULL;
  t->kind = kind;
  if (name) {
    t->q.ns = c->tns;
    t->q.name = trim(c->s, name);
  }
  if (xm_plist_add(c->s, &c->s->all_types, t)) return NULL;
  return t;
}

static int parse_facets(ctx_t *c, const bim_node_t *n, xsd_type_t *t) {
  static const char *const names[XSD_F_COUNT] = {"length",       "minLength",    "maxLength",    "pattern",      "enumeration", "whiteSpace",
                                                 "maxInclusive", "maxExclusive", "minInclusive", "minExclusive", "totalDigits", "fractionDigits"};
  int nf = 0;
  for (int i = 0; i < n->nkids; i++)
    for (int k = 0; k < XSD_F_COUNT; k++)
      if (xm_is_xs(n->kids[i], names[k])) nf++;
  if (!nf) return 0;
  t->facets = xm_arena_alloc(c->s, sizeof *t->facets * (size_t)nf);
  if (!t->facets) return -2;
  for (int i = 0; i < n->nkids; i++)
    for (int k = 0; k < XSD_F_COUNT; k++) {
      const char *v;
      if (!xm_is_xs(n->kids[i], names[k])) continue;
      v = xm_xattr(n->kids[i], "value");
      if (!v) return xsd_set_fail(c->s, "LS400258", "bim: %s: %s without value", c->path, names[k]);
      t->facets[t->nfacets].kind = (xsd_facet_kind_t)k;
      t->facets[t->nfacets++].value = k == XSD_F_ENUMERATION || k == XSD_F_PATTERN ? v : trim(c->s, v);
    }
  return 0;
}

xsd_type_t *xm_parse_simple_type(ctx_t *c, const bim_node_t *n) {
  xsd_type_t *t = new_type(c, n, XSD_SIMPLE);
  const bim_node_t *d;
  if (!t) return NULL;
  if ((d = xs_kid(n, "restriction"))) {
    const bim_node_t *inl = xs_kid(d, "simpleType");
    t->deriv = XSD_D_RESTRICTION;
    if (qname_attr(c, d, "base", &t->base_ref)) return NULL;
    if (inl && !(t->base = xm_parse_simple_type(c, inl))) return NULL;
    if (!t->base_ref.name && !t->base) {
      xsd_set_fail(c->s, "LS400259", "bim: %s: restriction without base", c->path);
      return NULL;
    }
    return parse_facets(c, d, t) ? NULL : t;
  }
  if ((d = xs_kid(n, "list"))) {
    const bim_node_t *inl = xs_kid(d, "simpleType");
    t->deriv = XSD_D_LIST;
    if (qname_attr(c, d, "itemType", &t->item_ref)) return NULL;
    if (inl && !(t->item = xm_parse_simple_type(c, inl))) return NULL;
    if (!t->item_ref.name && !t->item) {
      xsd_set_fail(c->s, "LS400260", "bim: %s: list without itemType", c->path);
      return NULL;
    }
    return t;
  }
  if ((d = xs_kid(n, "union"))) {
    const char *mt = xm_xattr(d, "memberTypes");
    int ninl = 0;
    t->deriv = XSD_D_UNION;
    for (const char *p = mt ? mt : ""; *p;) {
      size_t len;
      char *tok;
      xsd_qname_t *nr;
      while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
      len = strcspn(p, " \t\n\r");
      if (!len) break;
      tok = xm_arena_strndup(c->s, p, len);
      nr = xm_arena_alloc(c->s, sizeof *nr * (size_t)(t->nmember_refs + 1));
      if (!tok || !nr) return NULL;
      if (t->nmember_refs) memcpy(nr, t->member_refs, sizeof *nr * (size_t)t->nmember_refs);
      if (qname_of(c, d, tok, &nr[t->nmember_refs])) return NULL;
      t->member_refs = nr;
      t->nmember_refs++;
      p += len;
    }
    for (int i = 0; i < d->nkids; i++) ninl += xm_is_xs(d->kids[i], "simpleType");
    if (!t->nmember_refs && !ninl) {
      xsd_set_fail(c->s, "LS400261", "bim: %s: union without members", c->path);
      return NULL;
    }
    t->members = xm_arena_alloc(c->s, sizeof *t->members * (size_t)(t->nmember_refs + ninl));
    if (!t->members) return NULL;
    t->nmembers = t->nmember_refs; /* refs bound in order first */
    for (int i = 0; i < d->nkids; i++)
      if (xm_is_xs(d->kids[i], "simpleType") && !(t->members[t->nmembers++] = xm_parse_simple_type(c, d->kids[i]))) return NULL;
    return t;
  }
  xsd_set_fail(c->s, "LS400262", "bim: %s: simpleType without restriction, list or union", c->path);
  return NULL;
}

static int parse_attr_decl(ctx_t *c, const bim_node_t *n, int global, xsd_attr_t **out) {
  xsd_attr_t *a = xm_arena_alloc(c->s, sizeof *a);
  const char *name = xm_xattr(n, "name");
  const char *form = xm_xattr(n, "form");
  const bim_node_t *inl = xs_kid(n, "simpleType");
  if (!a) return -2;
  if (!name) return xsd_set_fail(c->s, "LS400263", "bim: %s: attribute without name", c->path);
  a->q.name = trim(c->s, name);
  a->q.ns = global || (form ? !strcmp(form, "qualified") : c->attr_qualified) ? c->tns : NULL;
  a->global = global;
  a->dflt = xm_xattr(n, "default");
  a->fixed = xm_xattr(n, "fixed");
  if (qname_attr(c, n, "type", &a->type_ref)) return -1;
  if (inl && !(a->type = xm_parse_simple_type(c, inl))) return -1;
  if (xm_plist_add(c->s, &c->s->all_attrs, a)) return -2;
  *out = a;
  return 0;
}

static int parse_attrs(ctx_t *c, const bim_node_t *n, xsd_attrs_t *as) {
  int nu = 0;
  int ng = 0;
  for (int i = 0; i < n->nkids; i++) {
    nu += xm_is_xs(n->kids[i], "attribute");
    ng += xm_is_xs(n->kids[i], "attributeGroup");
  }
  if (nu && !(as->uses = xm_arena_alloc(c->s, sizeof *as->uses * (size_t)nu))) return -2;
  if (ng && (!(as->groups = xm_arena_alloc(c->s, sizeof *as->groups * (size_t)ng)) || !(as->group_refs = xm_arena_alloc(c->s, sizeof *as->group_refs * (size_t)ng))))
    return -2;
  for (int i = 0; i < n->nkids; i++) {
    const bim_node_t *k = n->kids[i];
    if (xm_is_xs(k, "attribute")) {
      xsd_attr_use_t *u = &as->uses[as->nuses++];
      const char *use = xm_xattr(k, "use");
      u->use = !use || !strcmp(use, "optional") ? XSD_USE_OPTIONAL : !strcmp(use, "required") ? XSD_USE_REQUIRED : XSD_USE_PROHIBITED;
      if (use && u->use == XSD_USE_PROHIBITED && strcmp(use, "prohibited")) return xsd_set_fail(c->s, "LS400257", "bim: %s: bad use \"%s\"", c->path, use);
      if (xm_xattr(k, "ref")) {
        if (qname_attr(c, k, "ref", &u->ref)) return -1;
        u->dflt = xm_xattr(k, "default");
        u->fixed = xm_xattr(k, "fixed");
      } else if (parse_attr_decl(c, k, 0, &u->attr)) {
        return -1;
      }
    } else if (xm_is_xs(k, "attributeGroup")) {
      if (!xm_xattr(k, "ref")) return xsd_set_fail(c->s, "LS400265", "bim: %s: local attributeGroup without ref", c->path);
      if (qname_attr(c, k, "ref", &as->group_refs[as->ngroups++])) return -1;
    } else if (xm_is_xs(k, "anyAttribute")) {
      if (!(as->wc = parse_wildcard(c, k))) return -1;
    }
  }
  return xm_plist_add(c->s, &c->s->all_attrsets, as) ? -2 : 0;
}

static int parse_elem_decl(ctx_t *c, const bim_node_t *n, int global, xsd_elem_t **out) {
  xsd_elem_t *e = xm_arena_alloc(c->s, sizeof *e);
  const char *name = xm_xattr(n, "name");
  const char *form = xm_xattr(n, "form");
  const bim_node_t *st = xs_kid(n, "simpleType");
  const bim_node_t *ct = xs_kid(n, "complexType");
  if (!e) return -2;
  if (!name) return xsd_set_fail(c->s, "LS400264", "bim: %s: element without name", c->path);
  e->q.name = trim(c->s, name);
  e->q.ns = global || (form ? !strcmp(form, "qualified") : c->elem_qualified) ? c->tns : NULL;
  e->global = global;
  e->nillable = bool_attr(n, "nillable");
  e->abstract = bool_attr(n, "abstract");
  e->dflt = xm_xattr(n, "default");
  e->fixed = xm_xattr(n, "fixed");
  if (qname_attr(c, n, "type", &e->type_ref) || qname_attr(c, n, "substitutionGroup", &e->subst_ref)) return -1;
  if (st && !(e->type = xm_parse_simple_type(c, st))) return -1;
  if (ct && !(e->type = xm_parse_complex_type(c, ct))) return -1;
  if (xm_plist_add(c->s, &c->s->all_elems, e)) return -2;
  *out = e;
  return 0;
}

static int is_particle(const bim_node_t *n) {
  return xm_is_xs(n, "element") || xm_is_xs(n, "group") || xm_is_xs(n, "sequence") || xm_is_xs(n, "choice") || xm_is_xs(n, "all") || xm_is_xs(n, "any");
}

static int parse_particle(ctx_t *c, const bim_node_t *n, xsd_particle_t *p) {
  int rc = parse_occurs(c, n, &p->min, &p->max);
  if (rc) return rc;
  if (xm_is_xs(n, "element")) {
    p->kind = XSD_P_ELEM;
    return xm_xattr(n, "ref") ? qname_attr(c, n, "ref", &p->ref) : parse_elem_decl(c, n, 0, &p->elem);
  }
  if (xm_is_xs(n, "group")) {
    p->kind = XSD_P_GROUP;
    return xm_xattr(n, "ref") ? qname_attr(c, n, "ref", &p->ref) : xsd_set_fail(c->s, "LS400266", "bim: %s: local group without ref", c->path);
  }
  if (xm_is_xs(n, "any")) {
    p->kind = XSD_P_ANY;
    return (p->wc = parse_wildcard(c, n)) ? 0 : -1;
  }
  p->kind = xm_is_xs(n, "sequence") ? XSD_P_SEQ : xm_is_xs(n, "choice") ? XSD_P_CHOICE : XSD_P_ALL;
  for (int i = 0; i < n->nkids; i++) p->nkids += is_particle(n->kids[i]);
  if (p->nkids && !(p->kids = xm_arena_alloc(c->s, sizeof *p->kids * (size_t)p->nkids))) return -2;
  p->nkids = 0;
  for (int i = 0; i < n->nkids; i++)
    if (is_particle(n->kids[i]) && (rc = parse_particle(c, n->kids[i], &p->kids[p->nkids++]))) return rc;
  return 0;
}

/* first model group child (group, all, choice, sequence) of n, NULL none */
static int content_particle(ctx_t *c, const bim_node_t *n, xsd_particle_t **out) {
  for (int i = 0; i < n->nkids; i++) {
    const bim_node_t *k = n->kids[i];
    if (xm_is_xs(k, "group") || xm_is_xs(k, "all") || xm_is_xs(k, "choice") || xm_is_xs(k, "sequence")) {
      xsd_particle_t *p = xm_arena_alloc(c->s, sizeof *p);
      int rc;
      if (!p) return -2;
      if ((rc = parse_particle(c, k, p))) return rc;
      *out = p;
      return xm_plist_add(c->s, &c->s->particles, p) ? -2 : 0;
    }
  }
  return 0;
}

xsd_type_t *xm_parse_complex_type(ctx_t *c, const bim_node_t *n) {
  xsd_type_t *t = new_type(c, n, XSD_COMPLEX);
  const bim_node_t *sc = xs_kid(n, "simpleContent");
  const bim_node_t *cc = xs_kid(n, "complexContent");
  const bim_node_t *body = n;
  if (!t) return NULL;
  t->abstract = bool_attr(n, "abstract");
  t->mixed = bool_attr(n, "mixed");
  t->deriv = XSD_D_RESTRICTION;
  t->base_ref.ns = XSD_NS;
  t->base_ref.name = "anyType";
  if (sc || cc) {
    const bim_node_t *p = sc ? sc : cc;
    const bim_node_t *ext = xs_kid(p, "extension");
    body = ext ? ext : xs_kid(p, "restriction");
    if (!body) {
      xsd_set_fail(c->s, "LS400267", "bim: %s: content without extension or restriction", c->path);
      return NULL;
    }
    t->deriv = ext ? XSD_D_EXTENSION : XSD_D_RESTRICTION;
    t->base_ref.name = NULL;
    if (qname_attr(c, body, "base", &t->base_ref)) return NULL;
    if (!t->base_ref.name) {
      xsd_set_fail(c->s, "LS400268", "bim: %s: derivation without base", c->path);
      return NULL;
    }
    if (sc) {
      const bim_node_t *inl = xs_kid(body, "simpleType");
      t->content = XSD_C_SIMPLE;
      if (inl && !(t->simple = xm_parse_simple_type(c, inl))) return NULL;
      if (!ext && parse_facets(c, body, t)) return NULL;
    } else if (xm_xattr(cc, "mixed")) {
      t->mixed = bool_attr(cc, "mixed");
    }
  }
  if (t->content != XSD_C_SIMPLE) {
    if (content_particle(c, body, &t->particle)) return NULL;
    t->content = t->particle ? XSD_C_ELEMENTS : XSD_C_EMPTY;
  }
  return parse_attrs(c, body, &t->attrs) ? NULL : t;
}

static int add_ref(ctx_t *c, char kind, const bim_node_t *n) {
  xsd_set_t *s = c->s;
  if (s->nrefs >= s->rcap) {
    void *np = array_grow(s->refs, &s->rcap, s->nrefs + 1, sizeof *s->refs);
    if (!np) {
      s->oom = 1;
      return -2;
    }
    s->refs = np;
  }
  s->refs[s->nrefs].kind = kind;
  s->refs[s->nrefs].ns = kind == 'm' ? xm_xattr(n, "namespace") : c->tns;
  s->refs[s->nrefs].location = xm_xattr(n, "schemaLocation");
  s->refs[s->nrefs].node = n;
  s->nrefs++;
  return 0;
}

static int put_global(ctx_t *c, cmap_t *m, const xsd_qname_t *q, const char *what) {
  int rc = xm_cmap_put(c->s, m, q);
  if (rc == -2) return xsd_set_fail(c->s, "LS400274", "bim: %s: duplicate %s {%s}%s", c->path, what, q->ns ? q->ns : "", q->name);
  return rc ? -2 : 0;
}

int xm_parse_group(ctx_t *c, const bim_node_t *k, xsd_group_t **out) {
  xsd_group_t *g = xm_arena_alloc(c->s, sizeof *g);
  const char *name = xm_xattr(k, "name");
  int rc;
  if (!g) return -2;
  if (!name) return xsd_set_fail(c->s, "LS400271", "bim: %s: global group without name", c->path);
  g->q.ns = c->tns;
  g->q.name = trim(c->s, name);
  if ((rc = content_particle(c, k, &g->particle))) return rc;
  if (!g->particle || g->particle->kind == XSD_P_GROUP) return xsd_set_fail(c->s, "LS400272", "bim: %s: group %s without model group", c->path, g->q.name);
  *out = g;
  return 0;
}

int xm_parse_agroup(ctx_t *c, const bim_node_t *k, xsd_agroup_t **out) {
  xsd_agroup_t *g = xm_arena_alloc(c->s, sizeof *g);
  const char *name = xm_xattr(k, "name");
  int rc;
  if (!g) return -2;
  if (!name) return xsd_set_fail(c->s, "LS400273", "bim: %s: global attributeGroup without name", c->path);
  g->q.ns = c->tns;
  g->q.name = trim(c->s, name);
  if ((rc = parse_attrs(c, k, &g->attrs))) return rc;
  *out = g;
  return 0;
}

int xm_add_component(ctx_t *c, const bim_node_t *k) {
  xsd_set_t *s = c->s;
  if (xm_is_xs(k, "simpleType") || xm_is_xs(k, "complexType")) {
    xsd_type_t *t = xm_is_xs(k, "simpleType") ? xm_parse_simple_type(c, k) : xm_parse_complex_type(c, k);
    if (!t) return -1;
    if (!t->q.name) return xsd_set_fail(s, "LS400269", "bim: %s: global type without name", c->path);
    return put_global(c, &s->types, &t->q, "type");
  }
  if (xm_is_xs(k, "element")) {
    xsd_elem_t *e;
    int rc;
    if (xm_xattr(k, "minOccurs") || xm_xattr(k, "maxOccurs") || xm_xattr(k, "ref") || xm_xattr(k, "form")) return xsd_set_fail(s, "LS400270", "bim: %s: global element with local attribute", c->path);
    rc = parse_elem_decl(c, k, 1, &e);
    return rc ? rc : put_global(c, &s->elems, &e->q, "element");
  }
  if (xm_is_xs(k, "attribute")) {
    xsd_attr_t *a;
    int rc = parse_attr_decl(c, k, 1, &a);
    return rc ? rc : put_global(c, &s->attrs, &a->q, "attribute");
  }
  if (xm_is_xs(k, "group")) {
    xsd_group_t *g;
    int rc = xm_parse_group(c, k, &g);
    return rc ? rc : put_global(c, &s->groups, &g->q, "group");
  }
  if (xm_is_xs(k, "attributeGroup")) {
    xsd_agroup_t *g;
    int rc = xm_parse_agroup(c, k, &g);
    return rc ? rc : put_global(c, &s->agroups, &g->q, "attributeGroup");
  }
  if (xm_is_xs(k, "import")) return add_ref(c, 'm', k);
  if (xm_is_xs(k, "include")) return add_ref(c, 'i', k);
  if (xm_is_xs(k, "redefine")) return add_ref(c, 'r', k);
  return 0;
}
