/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/xsd/xsd.h"

#define E_SUU "LE400302"

typedef struct {
  const xsd_compiled_t *enc;
  const xsd_suu_src_t *in;
  const xsd_type_t **types; /* named types of tns, TypeID order */
  int ntypes;
  const xsd_elem_t **elems; /* global elements of tns, ElementID order */
  int nelems;
  const xsd_type_t **anon; /* anonymous types, id order */
  int nanon;
  int canon;
  bim_err_t *err;
} gn_t;

#define fail(c, ...) \
  ({ \
    char what_[120]; \
    snprintf(what_, sizeof what_, __VA_ARGS__); \
    bim_fail((c)->err, E_SUU, "bim: binary encoded schema %s", what_); \
  })

static int eq(const char *a, const char *b) { return (!a && !b) || (a && b && !strcmp(a, b)); }

static bim_node_t *node(const gn_t *c, bim_node_t *parent, const char *name, const char *cast) {
  bim_node_t *n = parent ? bim_node_add_new(parent, name, NULL) : bim_node_new(name);
  if (!n) return NULL;
  n->ns = XSD_ENCODING_NS;
  if (cast) {
    n->cast = xsd_compiled_type(c->enc, XSD_ENCODING_NS, cast);
    if (!n->cast) return NULL;
  }
  return n;
}

static int attr(bim_node_t *n, const char *name, const char *v) { return v ? bim_node_set_attr(n, name, v) : 0; }

static int attru(bim_node_t *n, const char *name, uint64_t v) {
  char b[24];
  snprintf(b, sizeof b, "%llu", (unsigned long long)v);
  return bim_node_set_attr(n, name, b);
}

static int ns_id(gn_t *c, const char *ns) {
  for (int i = 0; i < c->in->nimported; i++)
    if (eq(c->in->imported[i], ns)) return i;
  return fail(c, "namespace %s not imported", ns ? ns : "(none)");
}

/* index among components of namespace in known schemas */
static int known_id(gn_t *c, int elems, const char *ns, const char *name, uint64_t *id) {
  uint64_t k = 0;
  const xsd_compiled_t *kn = c->in->known;
  for (int i = 0; kn && i < (elems ? kn->nelems : kn->ntypes); i++) {
    const char *n = elems ? kn->elems[i]->name : kn->types[i]->name;
    const char *s = elems ? kn->elems[i]->ns : kn->types[i]->ns;
    if (n && eq(s, ns)) {
      if (!strcmp(n, name)) {
        *id = k;
        return 0;
      }
      k++;
    }
  }
  return fail(c, "%s %s of %s unknown", elems ? "element" : "type", name, ns ? ns : "(none)");
}

static int anon_id(gn_t *c, const xsd_type_t *t) {
  for (int i = 0; i < c->nanon; i++)
    if (c->anon[i] == t) return i;
  if (c->nanon == c->canon) {
    int nc = c->canon ? c->canon * 2 : 8;
    const xsd_type_t **n = realloc(c->anon, (size_t)nc * sizeof *n);
    if (!n) return fail(c, "out of memory");
    c->anon = n;
    c->canon = nc;
  }
  if (!c->anon) return fail(c, "out of memory");
  c->anon[c->nanon] = t;
  return c->nanon++;
}

static int type_id(gn_t *c, const xsd_type_t *t, uint64_t *id) {
  if (!eq(t->q.ns, c->in->tns)) return known_id(c, 0, t->q.ns, t->q.name, id) ? -1 : 0;
  for (int i = 0; i < c->ntypes; i++)
    if (c->types[i] == t) {
      *id = (uint64_t)i;
      return 0;
    }
  return fail(c, "type %s not in schema", t->q.name);
}

/* typeReferenceType child elname of parent */
static int typeref(gn_t *c, bim_node_t *parent, const char *elname, const xsd_type_t *t) {
  bim_node_t *tr = node(c, parent, elname, NULL);
  bim_node_t *r;
  uint64_t id = 0;
  int id_ns;
  if (!tr || !t) return t ? fail(c, "out of memory") : fail(c, "type missing");
  if (!t->q.name) {
    int a = anon_id(c, t);
    if (a < 0) return -1;
    r = node(c, tr, "anonymousTypeReference", NULL);
    if (!r) return -1;
    return attru(r, "idref", (uint64_t)a) ? -1 : 0;
  }
  if (type_id(c, t, &id)) return -1;
  if (!(r = node(c, tr, "namedTypeReference", NULL))) return fail(c, "out of memory");
  if (!eq(t->q.ns, c->in->tns)) {
    id_ns = ns_id(c, t->q.ns);
    if (id_ns < 0 || attru(r, "NamespaceID", (uint64_t)id_ns)) return -1;
  }
  return attru(r, "TypeID", id);
}

static int elemref(gn_t *c, bim_node_t *parent, const xsd_elem_t *e) {
  bim_node_t *r = node(c, parent, "namedElementReference", NULL);
  uint64_t id = 0;
  int found = 0;
  if (!r) return fail(c, "out of memory");
  if (eq(e->q.ns, c->in->tns)) {
    for (int i = 0; i < c->nelems && !found; i++)
      if (c->elems[i] == e) {
        id = (uint64_t)i;
        found = 1;
      }
    if (!found) return fail(c, "element %s not in schema", e->q.name);
  } else {
    int ns = ns_id(c, e->q.ns);
    if (ns < 0 || known_id(c, 1, e->q.ns, e->q.name, &id) || attru(r, "NamespaceID", (uint64_t)ns)) return -1;
  }
  return attru(r, "ElementID", id);
}

static int occurs(bim_node_t *n, uint64_t min, uint64_t max) {
  char b[24];
  snprintf(b, sizeof b, "%llu", (unsigned long long)max);
  return attru(n, "minOccurs", min) || bim_node_set_attr(n, "maxOccurs", max == XSD_UNBOUNDED ? "unbounded" : b);
}

static const char *form(const xsd_qname_t *q) { return q->ns ? "qualified" : "unqualified"; }

static const char *compositor_name(int kind) {
  if (kind == XSD_P_SEQ) return "sequence";
  if (kind == XSD_P_CHOICE) return "choice";
  return "all";
}

static int particle(gn_t *c, bim_node_t *parent, const xsd_particle_t *p, uint64_t min, uint64_t max) {
  bim_node_t *n;
  switch (p->kind) {
    case XSD_P_GROUP:
      if (!p->group || !p->group->particle) return fail(c, "group unresolved");
      return particle(c, parent, p->group->particle, p->min, p->max);
    case XSD_P_SEQ:
    case XSD_P_CHOICE:
    case XSD_P_ALL:
      if (!(n = node(c, parent, "particle", compositor_name(p->kind))) || occurs(n, min, max)) return -1;
      for (int i = 0; i < p->nkids; i++)
        if (particle(c, n, &p->kids[i], p->kids[i].min, p->kids[i].max)) return -1;
      return 0;
    case XSD_P_ANY: return !(n = node(c, parent, "particle", "anyParticleType")) || occurs(n, min, max) ? fail(c, "out of memory") : 0;
    default: break;
  }
  if (p->elem->global) {
    bim_node_t *ref;
    n = node(c, parent, "particle", "elementRef");
    if (!n) return fail(c, "out of memory");
    if (occurs(n, min, max)) return fail(c, "out of memory");
    ref = node(c, n, "ref", NULL);
    if (!ref) return fail(c, "out of memory");
    return elemref(c, ref, p->elem);
  }
  if (!(n = node(c, parent, "particle", "element")) || occurs(n, min, max) || attr(n, "name", p->elem->q.name) || attr(n, "form", form(&p->elem->q)) ||
      attr(n, "nillable", p->elem->nillable ? "true" : "false"))
    return fail(c, "out of memory");
  return typeref(c, n, "type", p->elem->type);
}

static int attribute(gn_t *c, bim_node_t *attrs, const xsd_attr_use_t *u) {
  bim_node_t *n;
  if (u->use == XSD_USE_PROHIBITED) return 0;
  if (u->fixed || u->attr->fixed) return fail(c, "fixed attribute %s not representable", u->attr->q.name);
  if (u->attr->global) {
    if (!eq(u->attr->q.ns, c->in->tns)) return fail(c, "attribute %s of another namespace", u->attr->q.name);
    return !(n = node(c, attrs, "attributeRef", NULL)) || attr(n, "idref", u->attr->q.name) ? fail(c, "out of memory") : 0;
  }
  if (!(n = node(c, attrs, "attribute", NULL)) || attr(n, "name", u->attr->q.name) || attr(n, "use", u->use == XSD_USE_REQUIRED ? "required" : "optional") || attr(n, "form", form(&u->attr->q)) ||
      attr(n, "defaultValue", u->dflt ? u->dflt : u->attr->dflt))
    return fail(c, "out of memory");
  return typeref(c, n, "typeReference", u->attr->type);
}

static int attrs_of(gn_t *c, bim_node_t *attrs, const xsd_attrs_t *a, int *any, int depth) {
  for (int i = 0; i < a->nuses; i++)
    if (attribute(c, attrs, &a->uses[i])) return -1;
  for (int i = 0; i < a->ngroups; i++) {
    if (!a->groups[i] || depth > 32) return fail(c, "attribute group unresolved");
    if (attrs_of(c, attrs, &a->groups[i]->attrs, any, depth + 1)) return -1;
  }
  if (a->wc) *any = 1;
  return 0;
}

static int complex_type(gn_t *c, bim_node_t *n, const xsd_type_t *t) {
  bim_node_t *attrs;
  bim_node_t *cm;
  int any = 0;
  if (t->base && t->deriv != XSD_D_NONE && !(t->base->builtin && t->base->q.name && !strcmp(t->base->q.name, "anyType"))) {
    bim_node_t *d = node(c, n, "derivation", NULL);
    if (!d || attr(d, "type", t->deriv == XSD_D_RESTRICTION ? "restriction" : "extension") || typeref(c, d, "baseTypeReference", t->base)) return -1;
  }
  if (!(attrs = node(c, n, "attributes", NULL))) return fail(c, "out of memory");
  if (attrs_of(c, attrs, &t->attrs, &any, 0)) return -1;
  if (any && !node(c, attrs, "anyAttribute", NULL)) return fail(c, "out of memory");
  if (!attrs->nkids) {
    bim_node_free(n->kids[n->nkids - 1]);
    n->nkids--;
  }
  if (t->content == XSD_C_SIMPLE) {
    if (!(cm = node(c, n, "content", "simpleContentModelType"))) return fail(c, "out of memory");
    return typeref(c, cm, "simpleTypeReference", t->simple ? t->simple : t->base);
  }
  if (t->content == XSD_C_EMPTY || !t->particle) {
    if (t->mixed) return fail(c, "mixed complex type without content");
    return node(c, n, "content", "emptyContentModelType") ? 0 : fail(c, "out of memory");
  }
  if (!(cm = node(c, n, "content", "complexContentModelType")) || attr(cm, "mixed", t->mixed ? "true" : "false")) return fail(c, "out of memory");
  return particle(c, cm, t->particle, t->particle->min, t->particle->max);
}

static const char *facet_name(xsd_facet_kind_t k) {
  switch (k) {
    case XSD_F_LENGTH: return "length";
    case XSD_F_MINLENGTH: return "minLength";
    case XSD_F_MAXLENGTH: return "maxLength";
    case XSD_F_ENUMERATION: return "enumeration";
    case XSD_F_MAXINCLUSIVE: return "maxInclusive";
    case XSD_F_MAXEXCLUSIVE: return "maxExclusive";
    case XSD_F_MININCLUSIVE: return "minInclusive";
    case XSD_F_MINEXCLUSIVE: return "minExclusive";
    default: return NULL;
  }
}

static int simple_type(gn_t *c, bim_node_t *n, const xsd_type_t *t) {
  if (t->deriv == XSD_D_LIST) {
    bim_node_t *l = node(c, n, "list", NULL);
    return l ? typeref(c, l, "itemTypeReference", t->item) : fail(c, "out of memory");
  }
  if (t->deriv == XSD_D_UNION) {
    bim_node_t *u = node(c, n, "union", NULL);
    if (!u) return fail(c, "out of memory");
    for (int i = 0; i < t->nmembers; i++)
      if (typeref(c, u, "memberTypeReference", t->members[i])) return -1;
  } else {
    bim_node_t *d = node(c, n, "derivation", NULL);
    if (!d || attr(d, "type", "restriction") || typeref(c, d, "baseTypeReference", t->base)) return fail(c, "out of memory");
  }
  for (int i = 0; i < t->nfacets; i++) {
    const char *name = facet_name(t->facets[i].kind);
    bim_node_t *f;
    if (!name) return fail(c, "facet of type %s not representable", t->q.name ? t->q.name : "(anonymous)");
    if (!(f = node(c, n, "facet", NULL)) || attr(f, "name", name) || attr(f, "value", t->facets[i].value)) return fail(c, "out of memory");
  }
  return 0;
}

static int type_node(gn_t *c, bim_node_t *list, const xsd_type_t *t, int anon, int id) {
  int cx = t->kind == XSD_COMPLEX;
  bim_node_t *n = node(c, list, cx ? "complexType" : "simpleType", NULL);
  if (!n || (anon ? attru(n, "id", (uint64_t)id) : attr(n, "name", t->q.name))) return fail(c, "out of memory");
  return cx ? complex_type(c, n, t) : simple_type(c, n, t);
}

static int collect(gn_t *c, const xsd_set_t *s) {
  const xsd_type_t *const *ts;
  const xsd_elem_t *const *es;
  int nt = xsd_types(s, &ts);
  int ne = xsd_elems(s, &es);
  c->types = calloc((size_t)nt + 1, sizeof *c->types);
  c->elems = calloc((size_t)ne + 1, sizeof *c->elems);
  if (!c->types || !c->elems) return fail(c, "out of memory");
  for (int i = 0; i < nt; i++) {
    if (!ts[i]->q.name || ts[i]->builtin || !eq(ts[i]->q.ns, c->in->tns)) continue;
    if (ts[i]->abstract) return fail(c, "abstract type %s not representable", ts[i]->q.name);
    c->types[c->ntypes++] = ts[i];
  }
  for (int i = 0; i < ne; i++) {
    if (!eq(es[i]->q.ns, c->in->tns)) continue;
    if (es[i]->nillable || es[i]->abstract || es[i]->subst_head) return fail(c, "global element %s: nillable, abstract or substitution not representable", es[i]->q.name);
    for (int k = 0; k < ne; k++)
      if (es[k]->subst_head == es[i]) return fail(c, "global element %s heads a substitution group", es[i]->q.name);
    c->elems[c->nelems++] = es[i];
  }
  return 0;
}

int xsd_suu_from_set(const xsd_compiled_t *enc, const xsd_suu_src_t *in, bim_node_t **tree, bim_err_t *err) {
  gn_t c;
  bim_node_t *root = NULL;
  bim_node_t *list = NULL;
  bim_node_t *an = NULL;
  const xsd_attr_t *const *as;
  int na = xsd_attrs(in->set, &as);
  int rc = -1;
  memset(&c, 0, sizeof c);
  memset(err, 0, sizeof *err);
  c.enc = enc;
  c.in = in;
  c.err = err;
  if (collect(&c, in->set)) goto out;
  if (!(root = node(&c, NULL, "schema", NULL)) || attr(root, "elementFormDefault", "unqualified") || attr(root, "attributeFormDefault", "unqualified")) goto oom;
  if (c.ntypes) {
    if (!(list = node(&c, root, "typeDefinitions", NULL))) goto oom;
    for (int i = 0; i < c.ntypes; i++)
      if (type_node(&c, list, c.types[i], 0, 0)) goto bad;
  }
  if (c.nelems) {
    if (!(list = node(&c, root, "elementDeclarations", NULL))) goto oom;
    for (int i = 0; i < c.nelems; i++) {
      bim_node_t *e = node(&c, list, "globalElement", NULL);
      if (!e || attr(e, "name", c.elems[i]->q.name) || typeref(&c, e, "elementTypeReference", c.elems[i]->type)) goto bad;
    }
  }
  for (int i = 0, first = 1; i < na; i++) {
    bim_node_t *a;
    if (!as[i]->global) continue;
    if (!eq(as[i]->q.ns, in->tns)) continue;
    if (as[i]->fixed) {
      fail(&c, "fixed attribute %s not representable", as[i]->q.name);
      goto bad;
    }
    if (first) {
      list = node(&c, root, "attributeDeclarations", NULL);
      if (!list) goto oom;
    }
    first = 0;
    if (!(a = node(&c, list, "attribute", NULL))) goto bad;
    if (attr(a, "name", as[i]->q.name)) goto bad;
    if (attr(a, "defaultValue", as[i]->dflt)) goto bad;
    if (typeref(&c, a, "typeReference", as[i]->type)) goto bad;
  }
  for (int i = 0; i < c.nanon; i++) {
    if (!an) an = node(&c, NULL, "anonymousTypeDefinitions", NULL);
    if (!an) goto oom;
    if (type_node(&c, an, c.anon[i], 1, i)) goto bad;
  }
  if (an) {
    /* schemaType order: types, anonymous types, elements, attributes */
    int at = 0;
    for (int i = 0; i < root->nkids; i++)
      if (bim_node_is(root->kids[i], XSD_ENCODING_NS, "typeDefinitions")) at = i + 1;
    if (bim_node_add(root, an)) goto oom;
    memmove(&root->kids[at + 1], &root->kids[at], (size_t)(root->nkids - 1 - at) * sizeof *root->kids);
    root->kids[at] = an;
    an = NULL;
  }
  *tree = root;
  rc = 0;
  goto out;
oom:
  fail(&c, "out of memory");
bad:
  bim_node_free(root);
  bim_node_free(an);
out:
  free(c.types);
  free(c.elems);
  free(c.anon);
  return rc;
}

int xsd_suu_encode(const xsd_compiled_t *enc, const bim_node_t *tree, bitwriter_t *bw, bim_err_t *err) {
  const bim_elem_t *root = xsd_compiled_elem(enc, XSD_ENCODING_NS, "schema");
  bim_encode_opts_t eo;
  bitwriter_t fuu;
  strrepo_writer_t sw;
  size_t n;
  const unsigned char *p;
  int rc = -1;
  memset(err, 0, sizeof *err);
  memset(&eo, 0, sizeof eo);
  eo.schemas = &enc->schema;
  eo.nschemas = 1;
  eo.mpegb = 1;
  eo.cast = 1;
  eo.lax_schema = -1;
  eo.frag_format = 1;
  bitwriter_init(&fuu);
  if (!root || strrepo_writer_init(&sw)) return bim_fail(err, E_SUU, "bim: binary encoded schema %s", "encoding schema unavailable");
  /* 6.7.8.2: AddContent, reparameterization keep, SchemaID 0 bits, ContextModeCode 001, topmost node */
  if (bitwriter_put(&fuu, 1, 4) || bitwriter_put(&fuu, 1, 2) || bitwriter_put(&fuu, 1, 3) || bitwriter_put(&fuu, 1, 1)) goto out;
  if (bim_encode_payload_opts(root->type, tree, &eo, &fuu, &sw)) {
    bim_fail(err, E_SUU, "bim: binary encoded schema %s", "tree not representable");
    goto out;
  }
  p = bitwriter_data(&fuu, &n);
  rc = !p || bitwriter_put_vluimsbf8(bw, 1) || bitwriter_put_vluimsbf8(bw, n) || bitwriter_put_bytes(bw, p, n) ? -1 : 0;
  if (rc) bim_fail(err, E_SUU, "bim: binary encoded schema %s", "out of memory");
out:
  if (rc && !err->code) bim_fail(err, E_SUU, "bim: binary encoded schema %s", "out of memory");
  strrepo_writer_free(&sw);
  bitwriter_free(&fuu);
  return rc;
}
