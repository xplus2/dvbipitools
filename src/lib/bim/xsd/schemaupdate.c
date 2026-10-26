/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/bim/xsd/xsd.h"
#include "lib/helper/xml_util.h"
#include "lib/sys/ioutil.h"

#define E_SUU "LE400302"

typedef struct {
  const xsd_suu_t *in;
  const bim_node_t *types; /* typeDefinitions */
  const bim_node_t *anon; /* anonymousTypeDefinitions */
  const bim_node_t *elems; /* elementDeclarations */
  dstrbuf_t b;
  bim_err_t *err;
  int depth;
} cv_t;

static int fail(bim_err_t *err, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

static int fail(bim_err_t *err, const char *fmt, ...) {
  char what[120];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(what, sizeof what, fmt, ap);
  va_end(ap);
  return bim_fail(err, E_SUU, "bim: binary encoded schema %s", what);
}

int xsd_encoding_compile(xsd_set_t **s, const xsd_compiled_t **c) {
  xml_doc_t *doc = NULL;
  xsd_compile_opts_t o = {1, 0, 0, NULL, 0, 0};
  *s = xsd_set_new();
  if (!*s) return -2;
  if (xml_doc_parse(xsd_encoding_schema, strlen(xsd_encoding_schema), &doc, NULL)) return -1;
  if (xsd_set_add_doc(*s, doc, "encoding schema") || xsd_set_resolve(*s)) return -1;
  return xsd_compile(*s, &o, c);
}

/* 6.7.8.2: one FUU, AddContent, ContextModeCode 001 to topmost node, no position codes */
int xsd_suu_decode(const xsd_compiled_t *enc, const unsigned char *bits, uint64_t nbits, bim_node_t **tree, bim_err_t *err) {
  bitreader_t br;
  uint64_t v;
  uint64_t len;
  size_t start;
  const bim_elem_t *root = xsd_compiled_elem(enc, XSD_ENCODING_NS, "schema");
  bim_decode_opts_t o = {.schemas = &enc->schema, .nschemas = 1, .mpegb = 1, .lax_schema = -1};
  memset(err, 0, sizeof *err);
  bitreader_init(&br, bits, (size_t)((nbits + 7) / 8));
  if (!root || bitreader_get_vluimsbf8(&br, &v) || v != 1 || bitreader_get_vluimsbf8(&br, &len)) return fail(err, "access unit malformed");
  start = bitreader_bits_left(&br);
  if (len * 8 > start || bitreader_get(&br, 4, &v) || v != 1) return fail(err, "command not AddContent");
  /* AdvancedOptimisedDecodersFlag 1 in 6.7.8.2.1, decoder instances need D6 */
  if (bitreader_get(&br, 2, &v) || v == 3) return fail(err, "OptimisedDecodersReparameterization invalid");
  if (!v) {
    uint64_t n;
    if (bitreader_get_vluimsbf8(&br, &n) || n || bitreader_get_vluimsbf8(&br, &n)) return fail(err, "advanced optimised decoder instances unsupported");
    for (uint64_t k = 0; k < n; k++) {
      uint64_t ni;
      uint64_t nt;
      if (bitreader_get(&br, 8, &v) || bitreader_get_vluimsbf8(&br, &ni) || ni || bitreader_get_vluimsbf8(&br, &nt)) return fail(err, "mapping malformed");
      for (uint64_t i = 0; i < nt; i++)
        if (bitreader_get_vluimsbf8(&br, &v)) return fail(err, "mapping malformed");
    }
  }
  /* SchemaID 0 bits, ContextModeCode 001, selector: Path Termination Code, operand selector 0 bits */
  if (bitreader_get(&br, 3, &v) || v != 1 || bitreader_get(&br, 1, &v) || v != 1) return fail(err, "context path not topmost node");
  if (bim_decode_payload(root->type, "schema", &o, &br, NULL, tree)) return fail(err, "payload malformed or unsupported");
  (*tree)->ns = XSD_ENCODING_NS;
  if (start - bitreader_bits_left(&br) > len * 8) {
    bim_node_free(*tree);
    *tree = NULL;
    return fail(err, "FUU_Length exceeded");
  }
  return 0;
}

static void put(void *ctx, const char *s, size_t n) { dstrbuf_add_n(ctx, s, n); }

static void add_attr(cv_t *c, const char *name, const char *v) {
  if (!v) return;
  dstrbuf_appendf(&c->b, " %s=\"", name);
  xml_escape_to(v, put, &c->b);
  dstrbuf_add(&c->b, "\"");
}

static const char *cast_of(const bim_node_t *n) { return n->cast ? n->cast->name : NULL; }

static int eq(const char *a, const char *b) { return a && b && !strcmp(a, b); }

static int ns_index(cv_t *c, const char *ns) {
  for (int i = 0; i < c->in->nimported; i++)
    if (eq(c->in->imported[i], ns)) return i;
  return -1;
}

/* prefix t: target namespace, xs: XML Schema, nI: imported namespace I */
static void add_qname(cv_t *c, const char *ns, const char *name) {
  if (eq(ns, c->in->tns)) dstrbuf_add(&c->b, "t:");
  else if (eq(ns, XSD_NS)) dstrbuf_add(&c->b, "xs:");
  else dstrbuf_appendf(&c->b, "n%d:", ns_index(c, ns));
  xml_escape_to(name, put, &c->b);
}

static int u64_attr(cv_t *c, const bim_node_t *n, const char *name, int opt, uint64_t *v) {
  const char *s = bim_node_attr(n, name);
  char *end;
  if (!s) return opt ? 1 : fail(c->err, "%s missing", name);
  *v = strtoull(s, &end, 10);
  return *end ? fail(c->err, "%s invalid", name) : 0;
}

/* name of k-th global component of ns: carried in this SUU, else compiled known schemas (earlier SUUs too) */
static const char *global_name(cv_t *c, int elems, const char *ns, uint64_t k) {
  const bim_node_t *list = elems ? c->elems : c->types;
  const uint64_t *codes = elems ? c->in->elem_codes : c->in->type_codes;
  size_t ncodes = elems ? c->in->nelem_codes : c->in->ntype_codes;
  if (eq(ns, c->in->tns)) {
    for (int i = 0; list && i < list->nkids; i++)
      if ((codes ? (size_t)i < ncodes && codes[i] == k : (uint64_t)i == k)) return bim_node_attr(list->kids[i], "name");
  }
  if (!c->in->known) return NULL;
  if (elems) {
    for (int i = 0; i < c->in->known->nelems; i++) {
      const bim_elem_t *e = c->in->known->elems[i];
      if (eq(e->ns, ns) && !k--) return e->name;
    }
    return NULL;
  }
  for (int i = 0; i < c->in->known->ntypes; i++) {
    const bim_type_t *t = c->in->known->types[i];
    if (t->name && eq(t->ns, ns) && !k--) return t->name;
  }
  return NULL;
}

static int ref_ns(cv_t *c, const bim_node_t *r, const char **ns) {
  uint64_t id;
  int rc = u64_attr(c, r, "NamespaceID", 1, &id);
  if (rc < 0) return -1;
  if (rc) {
    *ns = c->in->tns;
    return 0;
  }
  if (id >= (uint64_t)c->in->nimported) return fail(c->err, "NamespaceID %llu out of range", (unsigned long long)id);
  *ns = c->in->imported[id];
  return 0;
}

static int named_ref(cv_t *c, const bim_node_t *r, int elems, const char **ns, const char **name) {
  uint64_t k = 0;
  if (ref_ns(c, r, ns) || u64_attr(c, r, elems ? "ElementID" : "TypeID", 0, &k)) return -1;
  *name = global_name(c, elems, *ns, k);
  return *name ? 0 : fail(c->err, "%s %llu unresolved", elems ? "ElementID" : "TypeID", (unsigned long long)k);
}

static int emit_complex(cv_t *c, const bim_node_t *ct, const char *name);
static int emit_simple(cv_t *c, const bim_node_t *st, const char *name);

static const bim_node_t *anon_type(cv_t *c, const bim_node_t *r) {
  const char *id = bim_node_attr(r, "idref");
  for (int i = 0; id && c->anon && i < c->anon->nkids; i++)
    if (eq(bim_node_attr(c->anon->kids[i], "id"), id)) return c->anon->kids[i];
  return NULL;
}

/* typeReferenceType: named as attr (closes start tag), anonymous inline. *open: element still needs end tag */
static int emit_typeref(cv_t *c, const bim_node_t *tr, const char *attr, int *open) {
  const bim_node_t *r = tr ? bim_node_child(tr, "namedTypeReference") : NULL;
  const bim_node_t *a;
  const char *ns;
  const char *name;
  if (r) {
    if (named_ref(c, r, 0, &ns, &name)) return -1;
    dstrbuf_appendf(&c->b, " %s=\"", attr);
    add_qname(c, ns, name);
    dstrbuf_add(&c->b, "\"/>");
    *open = 0;
    return 0;
  }
  r = tr ? bim_node_child(tr, "anonymousTypeReference") : NULL;
  a = r ? anon_type(c, r) : NULL;
  if (!a) return fail(c->err, "type reference unresolved");
  if (++c->depth > 64) return fail(c->err, "anonymous type nesting too deep");
  dstrbuf_add(&c->b, ">");
  *open = 1;
  if (bim_node_is(a, XSD_ENCODING_NS, "complexType") ? emit_complex(c, a, NULL) : emit_simple(c, a, NULL)) return -1;
  c->depth--;
  return 0;
}

/* QName only (base, itemType, memberTypes) */
static int typeref_qname(cv_t *c, const bim_node_t *tr) {
  const bim_node_t *r = tr ? bim_node_child(tr, "namedTypeReference") : NULL;
  const char *ns;
  const char *name;
  if (!r) return fail(c->err, "anonymous type where XML Schema needs a QName");
  if (named_ref(c, r, 0, &ns, &name)) return -1;
  add_qname(c, ns, name);
  return 0;
}

static void occurs(cv_t *c, const bim_node_t *p) {
  add_attr(c, "minOccurs", bim_node_attr(p, "minOccurs"));
  add_attr(c, "maxOccurs", bim_node_attr(p, "maxOccurs"));
}

static int emit_particle(cv_t *c, const bim_node_t *p) {
  const char *k = cast_of(p);
  int open;
  if (!k) return fail(c->err, "particle without type cast");
  if (!strcmp(k, "sequence") || !strcmp(k, "choice") || !strcmp(k, "all")) {
    dstrbuf_appendf(&c->b, "<xs:%s", k);
    occurs(c, p);
    dstrbuf_add(&c->b, ">");
    for (int i = 0; i < p->nkids; i++)
      if (emit_particle(c, p->kids[i])) return -1;
    dstrbuf_appendf(&c->b, "</xs:%s>", k);
    return 0;
  }
  if (!strcmp(k, "anyParticleType")) {
    dstrbuf_add(&c->b, "<xs:any");
    occurs(c, p);
    dstrbuf_add(&c->b, "/>");
    return 0;
  }
  if (!strcmp(k, "elementRef")) {
    const bim_node_t *ref = bim_node_child(p, "ref");
    const bim_node_t *r = ref ? bim_node_child(ref, "namedElementReference") : NULL;
    const char *ns;
    const char *name;
    if (!r || named_ref(c, r, 1, &ns, &name)) return r ? -1 : fail(c->err, "element reference missing");
    dstrbuf_add(&c->b, "<xs:element ref=\"");
    add_qname(c, ns, name);
    dstrbuf_add(&c->b, "\"");
    occurs(c, p);
    dstrbuf_add(&c->b, "/>");
    return 0;
  }
  if (strcmp(k, "element")) return fail(c->err, "particle type %s unknown", k);
  dstrbuf_add(&c->b, "<xs:element");
  add_attr(c, "name", bim_node_attr(p, "name"));
  add_attr(c, "form", bim_node_attr(p, "form"));
  add_attr(c, "nillable", bim_node_attr(p, "nillable"));
  occurs(c, p);
  if (emit_typeref(c, bim_node_child(p, "type"), "type", &open)) return -1;
  if (open) dstrbuf_add(&c->b, "</xs:element>");
  return 0;
}

static int emit_attrs(cv_t *c, const bim_node_t *ct) {
  const bim_node_t *as = bim_node_child(ct, "attributes");
  for (int i = 0; as && i < as->nkids; i++) {
    const bim_node_t *a = as->kids[i];
    int open;
    if (bim_node_is(a, XSD_ENCODING_NS, "anyAttribute")) {
      dstrbuf_add(&c->b, "<xs:anyAttribute/>");
      continue;
    }
    if (bim_node_is(a, XSD_ENCODING_NS, "attributeRef")) {
      dstrbuf_add(&c->b, "<xs:attribute ref=\"");
      add_qname(c, c->in->tns, bim_node_attr(a, "idref") ? bim_node_attr(a, "idref") : "");
      dstrbuf_add(&c->b, "\"/>");
      continue;
    }
    dstrbuf_add(&c->b, "<xs:attribute");
    add_attr(c, "name", bim_node_attr(a, "name"));
    add_attr(c, "use", bim_node_attr(a, "use"));
    add_attr(c, "form", bim_node_attr(a, "form"));
    add_attr(c, "default", bim_node_attr(a, "defaultValue"));
    if (emit_typeref(c, bim_node_child(a, "typeReference"), "type", &open)) return -1;
    if (open) dstrbuf_add(&c->b, "</xs:attribute>");
  }
  return 0;
}

static int emit_complex(cv_t *c, const bim_node_t *ct, const char *name) {
  const bim_node_t *content = bim_node_child(ct, "content");
  const bim_node_t *der = bim_node_child(ct, "derivation");
  const char *how = der ? bim_node_attr(der, "type") : NULL;
  const char *model;
  if (!content || !(model = cast_of(content))) return fail(c->err, "complex type content missing");
  dstrbuf_add(&c->b, "<xs:complexType");
  add_attr(c, "name", name);
  if (!strcmp(model, "complexContentModelType")) add_attr(c, "mixed", bim_node_attr(content, "mixed"));
  dstrbuf_add(&c->b, ">");
  if (!strcmp(model, "simpleContentModelType")) {
    dstrbuf_appendf(&c->b, "<xs:simpleContent><xs:%s base=\"", how ? how : "extension");
    if (typeref_qname(c, der ? bim_node_child(der, "baseTypeReference") : bim_node_child(content, "simpleTypeReference"))) return -1;
    dstrbuf_add(&c->b, "\">");
    if (emit_attrs(c, ct)) return -1;
    dstrbuf_appendf(&c->b, "</xs:%s></xs:simpleContent>", how ? how : "extension");
  } else {
    const bim_node_t *p = bim_node_child(content, "particle");
    if (how) {
      dstrbuf_appendf(&c->b, "<xs:complexContent><xs:%s base=\"", how);
      if (typeref_qname(c, bim_node_child(der, "baseTypeReference"))) return -1;
      dstrbuf_add(&c->b, "\">");
    }
    if (p && emit_particle(c, p)) return -1;
    if (emit_attrs(c, ct)) return -1;
    if (how) dstrbuf_appendf(&c->b, "</xs:%s></xs:complexContent>", how);
  }
  dstrbuf_add(&c->b, "</xs:complexType>");
  return 0;
}

static int emit_facets(cv_t *c, const bim_node_t *st) {
  for (int i = 0; i < st->nkids; i++) {
    const bim_node_t *f = st->kids[i];
    if (!bim_node_is(f, XSD_ENCODING_NS, "facet")) continue;
    if (!bim_node_attr(f, "name")) return fail(c->err, "facet name missing");
    dstrbuf_appendf(&c->b, "<xs:%s", bim_node_attr(f, "name"));
    add_attr(c, "value", bim_node_attr(f, "value"));
    dstrbuf_add(&c->b, "/>");
  }
  return 0;
}

static int has_facets(const bim_node_t *st) {
  for (int i = 0; i < st->nkids; i++)
    if (bim_node_is(st->kids[i], XSD_ENCODING_NS, "facet")) return 1;
  return 0;
}

static int emit_list_union(cv_t *c, const bim_node_t *lu) {
  int list = bim_node_is(lu, XSD_ENCODING_NS, "list");
  dstrbuf_appendf(&c->b, "<xs:%s>", list ? "list" : "union");
  for (int i = 0; i < lu->nkids; i++) {
    int open;
    dstrbuf_add(&c->b, "<xs:simpleType><xs:restriction");
    if (emit_typeref(c, lu->kids[i], "base", &open)) return -1;
    if (open) dstrbuf_add(&c->b, "</xs:restriction>");
    dstrbuf_add(&c->b, "</xs:simpleType>");
  }
  dstrbuf_appendf(&c->b, "</xs:%s>", list ? "list" : "union");
  return 0;
}

static int emit_simple(cv_t *c, const bim_node_t *st, const char *name) {
  const bim_node_t *lu = bim_node_child(st, "list");
  const bim_node_t *der = bim_node_child(st, "derivation");
  if (!lu) lu = bim_node_child(st, "union");
  dstrbuf_add(&c->b, "<xs:simpleType");
  add_attr(c, "name", name);
  dstrbuf_add(&c->b, ">");
  if (lu && !has_facets(st)) {
    if (emit_list_union(c, lu)) return -1;
  } else if (lu) {
    dstrbuf_add(&c->b, "<xs:restriction><xs:simpleType>");
    if (emit_list_union(c, lu)) return -1;
    dstrbuf_add(&c->b, "</xs:simpleType>");
    if (emit_facets(c, st)) return -1;
    dstrbuf_add(&c->b, "</xs:restriction>");
  } else {
    if (!der) return fail(c->err, "simple type without derivation, list or union");
    dstrbuf_add(&c->b, "<xs:restriction base=\"");
    if (typeref_qname(c, bim_node_child(der, "baseTypeReference"))) return -1;
    dstrbuf_add(&c->b, "\">");
    if (emit_facets(c, st)) return -1;
    dstrbuf_add(&c->b, "</xs:restriction>");
  }
  dstrbuf_add(&c->b, "</xs:simpleType>");
  return 0;
}

static int emit_schema(cv_t *c) {
  const bim_node_t *t = c->in->tree;
  const bim_node_t *ads = bim_node_child(t, "attributeDeclarations");
  dstrbuf_add(&c->b, "<xs:schema xmlns:xs=\"" XSD_NS "\"");
  add_attr(c, "xmlns:t", c->in->tns);
  add_attr(c, "targetNamespace", c->in->tns);
  add_attr(c, "elementFormDefault", bim_node_attr(t, "elementFormDefault"));
  add_attr(c, "attributeFormDefault", bim_node_attr(t, "attributeFormDefault"));
  for (int i = 0; i < c->in->nimported; i++) {
    dstrbuf_appendf(&c->b, " xmlns:n%d=\"", i);
    xml_escape_to(c->in->imported[i], put, &c->b);
    dstrbuf_add(&c->b, "\"");
  }
  dstrbuf_add(&c->b, ">");
  for (int i = 0; i < c->in->nimported; i++) {
    if (eq(c->in->imported[i], c->in->tns) || eq(c->in->imported[i], XSD_NS)) continue;
    dstrbuf_add(&c->b, "<xs:import");
    add_attr(c, "namespace", c->in->imported[i]);
    dstrbuf_add(&c->b, "/>");
  }
  for (int i = 0; c->types && i < c->types->nkids; i++) {
    const bim_node_t *d = c->types->kids[i];
    const char *name = bim_node_attr(d, "name");
    if (!name) return fail(c->err, "named type without name");
    if (bim_node_is(d, XSD_ENCODING_NS, "complexType") ? emit_complex(c, d, name) : emit_simple(c, d, name)) return -1;
  }
  for (int i = 0; c->elems && i < c->elems->nkids; i++) {
    const bim_node_t *e = c->elems->kids[i];
    int open;
    dstrbuf_add(&c->b, "<xs:element");
    add_attr(c, "name", bim_node_attr(e, "name"));
    if (emit_typeref(c, bim_node_child(e, "elementTypeReference"), "type", &open)) return -1;
    if (open) dstrbuf_add(&c->b, "</xs:element>");
  }
  for (int i = 0; ads && i < ads->nkids; i++) {
    const bim_node_t *a = ads->kids[i];
    int open;
    dstrbuf_add(&c->b, "<xs:attribute");
    add_attr(c, "name", bim_node_attr(a, "name"));
    add_attr(c, "default", bim_node_attr(a, "defaultValue"));
    if (emit_typeref(c, bim_node_child(a, "typeReference"), "type", &open)) return -1;
    if (open) dstrbuf_add(&c->b, "</xs:attribute>");
  }
  dstrbuf_add(&c->b, "</xs:schema>\n");
  return 0;
}

int xsd_suu_to_xsd(const xsd_suu_t *in, char **text, bim_err_t *err) {
  cv_t c;
  memset(&c, 0, sizeof c);
  memset(err, 0, sizeof *err);
  c.in = in;
  c.err = err;
  c.types = bim_node_child(in->tree, "typeDefinitions");
  c.anon = bim_node_child(in->tree, "anonymousTypeDefinitions");
  c.elems = bim_node_child(in->tree, "elementDeclarations");
  dstrbuf_init(&c.b);
  if (emit_schema(&c)) {
    free(c.b.buf);
    return -1;
  }
  if (!c.b.buf) return fail(err, "out of memory");
  *text = c.b.buf;
  return 0;
}
