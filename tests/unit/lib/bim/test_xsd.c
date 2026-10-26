/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lib/bim/xsd/xsd.h"

static const char schema_a[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:a=\"urn:a\" targetNamespace=\"urn:a\""
  " elementFormDefault=\"qualified\">"
  "<xs:import namespace=\"urn:b\" schemaLocation=\"b.xsd\"/>"
  "<xs:simpleType name=\"Mode\"><xs:restriction base=\"xs:string\">"
  "<xs:enumeration value=\"on\"/><xs:enumeration value=\"off\"/></xs:restriction></xs:simpleType>"
  "<xs:simpleType name=\"Modes\"><xs:list itemType=\"a:Mode\"/></xs:simpleType>"
  "<xs:simpleType name=\"Pct\"><xs:restriction base=\"xs:integer\">"
  "<xs:minInclusive value=\" -100\"/><xs:maxInclusive value=\"100\"/></xs:restriction></xs:simpleType>"
  "<xs:simpleType name=\"ModeOrPct\"><xs:union memberTypes=\"a:Mode a:Pct\">"
  "<xs:simpleType><xs:restriction base=\"xs:anyURI\"/></xs:simpleType></xs:union></xs:simpleType>"
  "<xs:attribute name=\"g\" type=\"xs:boolean\"/>"
  "<xs:attributeGroup name=\"AG\"><xs:attribute name=\"ag\" type=\"xs:int\" use=\"required\"/></xs:attributeGroup>"
  "<xs:group name=\"G\"><xs:choice><xs:element name=\"X\" type=\"xs:string\"/><xs:element ref=\"a:Head\"/></xs:choice></xs:group>"
  "<xs:complexType name=\"Base\" abstract=\"true\"><xs:sequence>"
  "<xs:element name=\"N\" type=\"xs:string\" minOccurs=\"0\" maxOccurs=\" unbounded\"/></xs:sequence>"
  "<xs:attribute name=\"id\" type=\"xs:ID\"/><xs:attribute ref=\"a:g\" use=\"required\"/></xs:complexType>"
  "<xs:complexType name=\"Ext\"><xs:complexContent><xs:extension base=\"a:Base\"><xs:sequence>"
  "<xs:group ref=\"a:G\" maxOccurs=\"2\"/><xs:any namespace=\"##other\" processContents=\"lax\" minOccurs=\"0\"/>"
  "</xs:sequence><xs:attributeGroup ref=\"a:AG\"/><xs:anyAttribute namespace=\"##local urn:z\"/>"
  "</xs:extension></xs:complexContent></xs:complexType>"
  "<xs:complexType name=\"Txt\"><xs:simpleContent><xs:extension base=\"a:Pct\">"
  "<xs:attribute name=\"m\" type=\"a:Mode\" default=\"on\" form=\"qualified\"/></xs:extension></xs:simpleContent></xs:complexType>"
  "<xs:element name=\"Head\" type=\"a:Base\" abstract=\"true\"/>"
  "<xs:element name=\"Member\" substitutionGroup=\"a:Head\"/>"
  "<xs:element name=\"Root\" nillable=\"true\"><xs:complexType><xs:sequence>"
  "<xs:element name=\"E\" type=\"a:Ext\"/><xs:element name=\"L\" form=\"unqualified\" type=\"a:Modes\"/>"
  "</xs:sequence></xs:complexType></xs:element>"
  "</xs:schema>";

static xsd_set_t *load(const char *xsd, int resolve_rc) {
  xsd_set_t *s = xsd_set_new();
  xml_doc_t *doc = NULL;
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(xml_doc_parse(xsd, strlen(xsd), &doc, NULL), 0);
  ck_assert_int_eq(xsd_set_add_doc(s, doc, "a.xsd"), 0);
  ck_assert_int_eq(xsd_set_resolve(s), resolve_rc);
  return s;
}

static const char *facet(const xsd_type_t *t, xsd_facet_kind_t k) {
  for (int i = 0; i < t->nfacets; i++)
    if (t->facets[i].kind == k) return t->facets[i].value;
  return NULL;
}

START_TEST(xsd_builds_components) {
  xsd_set_t *s = load(schema_a, 0);
  const xsd_ref_t *refs;
  const xsd_type_t *t;
  const xsd_elem_t *e;
  const xsd_particle_t *p;
  ck_assert_int_eq(xsd_set_doc_refs(s, &refs), 1);
  ck_assert_int_eq(refs[0].kind, 'm');
  ck_assert_str_eq(refs[0].ns, "urn:b");
  ck_assert_str_eq(refs[0].location, "b.xsd");

  t = xsd_find_type(s, "urn:a", "Modes");
  ck_assert_int_eq(t->deriv, XSD_D_LIST);
  ck_assert_ptr_eq(t->item, xsd_find_type(s, "urn:a", "Mode"));
  t = xsd_find_type(s, "urn:a", "Pct");
  ck_assert_ptr_eq(t->base, xsd_find_type(s, XSD_NS, "integer"));
  ck_assert_str_eq(facet(t, XSD_F_MININCLUSIVE), "-100");
  t = xsd_find_type(s, "urn:a", "ModeOrPct");
  ck_assert_int_eq(t->nmembers, 3);
  ck_assert_ptr_eq(t->members[1], xsd_find_type(s, "urn:a", "Pct"));
  ck_assert_ptr_eq(t->members[2]->base, xsd_find_type(s, XSD_NS, "anyURI"));

  t = xsd_find_type(s, "urn:a", "Base");
  ck_assert_int_eq(t->abstract, 1);
  ck_assert_ptr_eq(t->base, xsd_find_type(s, XSD_NS, "anyType"));
  ck_assert_uint_eq(t->particle->kids[0].max, XSD_UNBOUNDED);
  ck_assert_str_eq(t->particle->kids[0].elem->q.ns, "urn:a");
  ck_assert_int_eq(t->attrs.nuses, 2);
  ck_assert_ptr_eq(t->attrs.uses[1].attr, xsd_find_attr(s, "urn:a", "g"));
  ck_assert_int_eq(t->attrs.uses[1].use, XSD_USE_REQUIRED);

  t = xsd_find_type(s, "urn:a", "Ext");
  ck_assert_int_eq(t->deriv, XSD_D_EXTENSION);
  ck_assert_ptr_eq(t->base, xsd_find_type(s, "urn:a", "Base"));
  p = t->particle;
  ck_assert_int_eq(p->kids[0].kind, XSD_P_GROUP);
  ck_assert_uint_eq(p->kids[0].max, 2);
  ck_assert_ptr_eq(p->kids[0].group->particle->kids[1].elem, xsd_find_elem(s, "urn:a", "Head"));
  ck_assert_int_eq(p->kids[1].wc->kind, XSD_WC_NOT);
  ck_assert_str_eq(p->kids[1].wc->ns[0], "urn:a");
  ck_assert_int_eq(p->kids[1].wc->process, XSD_PC_LAX);
  ck_assert_int_eq(t->attrs.groups[0]->attrs.uses[0].use, XSD_USE_REQUIRED);
  ck_assert_int_eq(t->attrs.wc->nns, 2);
  ck_assert_ptr_null(t->attrs.wc->ns[0]);

  t = xsd_find_type(s, "urn:a", "Txt");
  ck_assert_int_eq(t->content, XSD_C_SIMPLE);
  ck_assert_str_eq(t->attrs.uses[0].attr->q.ns, "urn:a");
  ck_assert_str_eq(t->attrs.uses[0].attr->dflt, "on");

  e = xsd_find_elem(s, "urn:a", "Member");
  ck_assert_ptr_eq(e->subst_head, xsd_find_elem(s, "urn:a", "Head"));
  ck_assert_ptr_eq(e->type, xsd_find_type(s, "urn:a", "Base"));
  e = xsd_find_elem(s, "urn:a", "Root");
  ck_assert_int_eq(e->nillable, 1);
  ck_assert_ptr_null(e->type->q.name);
  ck_assert_ptr_null(e->type->particle->kids[1].elem->q.ns);
  xsd_set_free(s);
}
END_TEST

/* XML Schema Part 2 3.3 facets of derived built-ins */
START_TEST(xsd_builtins_have_facets) {
  xsd_set_t *s = xsd_set_new();
  const xsd_type_t *t = xsd_find_type(s, XSD_NS, "byte");
  ck_assert_ptr_eq(t->base, xsd_find_type(s, XSD_NS, "short"));
  ck_assert_str_eq(facet(t, XSD_F_MININCLUSIVE), "-128");
  ck_assert_str_eq(facet(xsd_find_type(s, XSD_NS, "unsignedLong"), XSD_F_MAXINCLUSIVE), "18446744073709551615");
  t = xsd_find_type(s, XSD_NS, "NMTOKENS");
  ck_assert_int_eq(t->deriv, XSD_D_LIST);
  ck_assert_ptr_eq(t->item, xsd_find_type(s, XSD_NS, "NMTOKEN"));
  t = xsd_find_type(s, XSD_NS, "anyType");
  ck_assert_int_eq(t->kind, XSD_COMPLEX);
  ck_assert_int_eq(t->particle->kids[0].kind, XSD_P_ANY);
  xsd_set_free(s);
}
END_TEST

START_TEST(xsd_reports_unresolved_and_invalid) {
  static const char unresolved[] =
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:b=\"urn:b\">"
    "<xs:element name=\"R\" type=\"b:Missing\"/></xs:schema>";
  static const char *const invalid[] = {
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\"><xs:element name=\"R\" type=\"q:T\"/></xs:schema>",
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\"><xs:complexType/></xs:schema>",
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\"><xs:element name=\"R\"/><xs:element name=\"R\"/></xs:schema>",
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\"><xs:element name=\"R\" minOccurs=\"x\"/></xs:schema>",
    "<schema/>",
  };
  xsd_set_t *s = load(unresolved, -1);
  ck_assert_str_eq(xsd_set_error(s), "bim: unresolved type {urn:b}Missing");
  ck_assert_str_eq(xsd_set_error_code(s), "LS400282");
  xsd_set_free(s);
  for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; i++) {
    xml_doc_t *doc = NULL;
    s = xsd_set_new();
    ck_assert_int_eq(xml_doc_parse(invalid[i], strlen(invalid[i]), &doc, NULL), 0);
    ck_assert_int_eq(xsd_set_add_doc(s, doc, "x.xsd"), -1);
    ck_assert_int_eq(strncmp(xsd_set_error(s), "bim: x.xsd: ", 12), 0);
    xsd_set_free(s);
  }
}
END_TEST

#define XS_OPEN "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" "

static void put_file(const char *dir, const char *name, const char *body) {
  char p[512];
  FILE *f;
  snprintf(p, sizeof p, "%s/%s", dir, name);
  f = fopen(p, "w");
  if (!f) abort();
  fputs(body, f);
  fclose(f);
}

static void rm_tree(const char *dir) {
  char cmd[600];
  snprintf(cmd, sizeof cmd, "rm -r -- '%s'", dir);
  ck_assert_int_eq(system(cmd), 0);
}

/* XML Schema Part 1 4.2.1 chameleon include, 4.2.2 redefine, 4.3.2 location hints */
START_TEST(xsd_load_follows_imports_includes_redefines) {
  char dir[] = "/tmp/xsdtestXXXXXX";
  char sub[64];
  char main_path[600];
  const char *dirs[1];
  const xsd_type_t *t;
  xsd_set_t *s;
  ck_assert_ptr_nonnull(mkdtemp(dir));
  snprintf(sub, sizeof sub, "%s/sub", dir);
  ck_assert_int_eq(mkdir(sub, 0700), 0);
  put_file(dir, "a.xsd",
           XS_OPEN "xmlns:a=\"urn:a\" xmlns:b=\"urn:b\" xmlns:x=\"urn:x\" targetNamespace=\"urn:a\">"
                   "<xs:import namespace=\"urn:b\" schemaLocation=\"nowhere/b.xsd\"/><xs:import namespace=\"urn:x\"/>"
                   "<xs:redefine schemaLocation=\"r.xsd\"><xs:complexType name=\"R\"><xs:complexContent><xs:extension base=\"a:R\">"
                   "<xs:sequence><xs:element name=\"More\" type=\"b:C\"/></xs:sequence></xs:extension></xs:complexContent></xs:complexType>"
                   "</xs:redefine><xs:element name=\"Root\" type=\"a:R\"/><xs:element name=\"X\" type=\"x:T\"/></xs:schema>");
  put_file(dir, "r.xsd", XS_OPEN "targetNamespace=\"urn:a\"><xs:complexType name=\"R\"><xs:sequence>"
                                 "<xs:element name=\"First\" type=\"xs:string\"/></xs:sequence></xs:complexType></xs:schema>");
  put_file(sub, "b.xsd", XS_OPEN "targetNamespace=\"urn:b\"><xs:include schemaLocation=\"c.xsd\"/></xs:schema>");
  put_file(sub, "c.xsd", XS_OPEN "><xs:simpleType name=\"C\"><xs:restriction base=\"xs:int\"/></xs:simpleType></xs:schema>");
  put_file(sub, "x.xsd", XS_OPEN "targetNamespace=\"urn:x\"><xs:simpleType name=\"T\"><xs:restriction base=\"xs:token\"/></xs:simpleType></xs:schema>");
  snprintf(main_path, sizeof main_path, "%s/a.xsd", dir);
  dirs[0] = dir;
  s = xsd_set_new();
  ck_assert_int_eq(xsd_set_load(s, NULL, main_path, dirs, 1), 0);
  ck_assert_ptr_nonnull(xsd_find_type(s, "urn:b", "C"));
  ck_assert_ptr_eq(xsd_find_elem(s, "urn:a", "X")->type, xsd_find_type(s, "urn:x", "T"));
  t = xsd_find_type(s, "urn:a", "R");
  ck_assert_int_eq(t->deriv, XSD_D_EXTENSION);
  ck_assert_str_eq(t->base->particle->kids[0].elem->q.name, "First");
  ck_assert_ptr_eq(t->particle->kids[0].elem->type, xsd_find_type(s, "urn:b", "C"));
  ck_assert_ptr_eq(xsd_find_elem(s, "urn:a", "Root")->type, t);
  xsd_set_free(s);

  /* location hint not found, basename twice below the search directory */
  put_file(dir, "b.xsd", XS_OPEN "targetNamespace=\"urn:b\"/>");
  s = xsd_set_new();
  ck_assert_int_eq(xsd_set_load(s, NULL, main_path, dirs, 1), -1);
  ck_assert_ptr_nonnull(strstr(xsd_set_error(s), "nowhere/b.xsd matches several files"));
  ck_assert_str_eq(xsd_set_error_code(s), "LS000745");
  xsd_set_free(s);

  /* XML Schema Part 1 4.2.3: location with another targetNamespace */
  put_file(dir, "m.xsd", XS_OPEN "><xs:import namespace=\"urn:q\" schemaLocation=\"sub/x.xsd\"/></xs:schema>");
  snprintf(main_path, sizeof main_path, "%s/m.xsd", dir);
  s = xsd_set_new();
  ck_assert_int_eq(xsd_set_load(s, NULL, main_path, dirs, 1), -1);
  ck_assert_ptr_nonnull(strstr(xsd_set_error(s), "import of namespace urn:q,"));
  ck_assert_str_eq(xsd_set_error_code(s), "LS400276");
  xsd_set_free(s);
  rm_tree(dir);
}
END_TEST

static const char schema_c[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:c=\"urn:c\" targetNamespace=\"urn:c\""
  " elementFormDefault=\"qualified\">"
  "<xs:import namespace=\"http://www.w3.org/XML/1998/namespace\"/>"
  "<xs:simpleType name=\"IntEnum\"><xs:restriction base=\"xs:unsignedByte\">"
  "<xs:enumeration value=\"1\"/><xs:enumeration value=\"2\"/></xs:restriction></xs:simpleType>"
  "<xs:simpleType name=\"Gt\"><xs:restriction base=\"xs:short\"><xs:minExclusive value=\"9\"/></xs:restriction></xs:simpleType>"
  "<xs:simpleType name=\"Two\"><xs:restriction><xs:simpleType><xs:list itemType=\"xs:boolean\"/></xs:simpleType>"
  "<xs:minLength value=\"1\"/><xs:maxLength value=\"4\"/></xs:restriction></xs:simpleType>"
  "<xs:complexType name=\"B\"><xs:sequence><xs:sequence minOccurs=\"0\"><xs:element name=\"x\" type=\"xs:int\" maxOccurs=\"unbounded\"/>"
  "</xs:sequence></xs:sequence><xs:attribute name=\"z\" type=\"xs:string\"/><xs:attribute name=\"k\" type=\"xs:string\" fixed=\"v\"/>"
  "<xs:attribute name=\"r\" type=\"xs:string\" fixed=\"v\" use=\"required\"/><xs:attribute ref=\"xml:lang\"/></xs:complexType>"
  "<xs:complexType name=\"D\"><xs:complexContent><xs:extension base=\"c:B\"><xs:choice>"
  "<xs:choice><xs:element name=\"q\" type=\"xs:integer\"/><xs:element name=\"p\" type=\"c:Gt\"/></xs:choice>"
  "<xs:element name=\"o\" type=\"c:IntEnum\" minOccurs=\"0\"/></xs:choice><xs:attribute name=\"a\" type=\"c:Two\"/>"
  "</xs:extension></xs:complexContent></xs:complexType>"
  "<xs:complexType name=\"R\"><xs:complexContent><xs:restriction base=\"c:B\"><xs:sequence>"
  "<xs:element name=\"x\" type=\"xs:int\" maxOccurs=\"2\"/></xs:sequence><xs:attribute name=\"z\" use=\"prohibited\"/>"
  "</xs:restriction></xs:complexContent></xs:complexType>"
  "<xs:element name=\"Top\" type=\"c:B\"/><xs:element name=\"Zed\" substitutionGroup=\"c:Top\"/>"
  "<xs:element name=\"Alt\" type=\"c:D\" substitutionGroup=\"c:Top\"/>"
  "</xs:schema>";

static const char schema_xml[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" targetNamespace=\"http://www.w3.org/XML/1998/namespace\">"
  "<xs:attribute name=\"lang\" type=\"xs:language\"/></xs:schema>";

static const char schema_m7[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:m=\"urn:mpeg:mpeg7:schema:2001\""
  " targetNamespace=\"urn:mpeg:mpeg7:schema:2001\">"
  "<xs:simpleType name=\"basicTimePointType\"><xs:restriction base=\"xs:string\"/></xs:simpleType>"
  "<xs:simpleType name=\"timePointType\"><xs:restriction base=\"m:basicTimePointType\"/></xs:simpleType>"
  "</xs:schema>";

static xsd_set_t *load_docs(const char *const *xsd, int n) {
  xsd_set_t *s = xsd_set_new();
  ck_assert_ptr_nonnull(s);
  for (int i = 0; i < n; i++) {
    xml_doc_t *doc = NULL;
    ck_assert_int_eq(xml_doc_parse(xsd[i], strlen(xsd[i]), &doc, NULL), 0);
    if (xsd_set_add_doc(s, doc, "c.xsd")) ck_abort_msg("%s %s", xsd_set_error_code(s), xsd_set_error(s));
  }
  ck_assert_int_eq(xsd_set_resolve(s), 0);
  return s;
}

static const xsd_compiled_t *compile_c(xsd_set_t **s, int mpegb) {
  static const char *const docs[] = {schema_c, schema_xml};
  xsd_compile_opts_t o = {mpegb, 0, 0, NULL, 0, 0};
  const xsd_compiled_t *c = NULL;
  *s = load_docs(docs, 2);
  if (xsd_compile(*s, &o, &c)) ck_abort_msg("%s %s", xsd_set_error_code(*s), xsd_set_error(*s));
  return c;
}

/* ISO 23001-1 7.5.3.1 (Cor 1), ISO 15938-1:2002 8.5.3 */
START_TEST(compile_attributes_sorted_and_fixed_by_edition) {
  for (int mpegb = 0; mpegb < 2; mpegb++) {
    xsd_set_t *s;
    const xsd_compiled_t *c = compile_c(&s, mpegb);
    const bim_type_t *b = xsd_compiled_type(c, "urn:c", "B");
    const bim_type_t *d = xsd_compiled_type(c, "urn:c", "D");
    const bim_type_t *r = xsd_compiled_type(c, "urn:c", "R");
    int i = 0;
    ck_assert_ptr_nonnull(b);
    /* expanded names: xml:lang first, optional fixed k coded only by ISO 23001-1 */
    ck_assert_int_eq(b->nattrs, mpegb ? 3 : 2);
    ck_assert_str_eq(b->attrs[i].name, "lang");
    ck_assert_str_eq(b->attrs[i++].ns, "http://www.w3.org/XML/1998/namespace");
    if (mpegb) ck_assert_str_eq(b->attrs[i++].name, "k");
    ck_assert_str_eq(b->attrs[i].name, "z");
    ck_assert_int_eq(b->attrs[i].required, 0);
    ck_assert_int_eq(b->attrs[i].simple->codec, BIM_UTF8);
    ck_assert_int_eq(d->nattrs, b->nattrs + 1);
    ck_assert_str_eq(d->attrs[0].name, "a");
    ck_assert_int_eq(d->attrs[0].simple->codec, BIM_LIST);
    ck_assert_int_eq(d->attrs[0].simple->range.min, 1);
    ck_assert_uint_eq(d->attrs[0].simple->range.span, 3);
    ck_assert_int_eq(d->attrs[0].simple->item->codec, BIM_BOOLEAN);
    ck_assert_int_eq(r->nattrs, b->nattrs - 1);
    ck_assert_str_ne(r->attrs[r->nattrs - 1].name, "z");
    xsd_set_free(s);
  }
}
END_TEST

/* ISO 23001-1 7.5.2.2.3, 7.5.2.2.4, 7.5.4.2 */
START_TEST(compile_content_simplified_and_signed) {
  xsd_set_t *s;
  const xsd_compiled_t *c = compile_c(&s, 1);
  const bim_type_t *b = xsd_compiled_type(c, "urn:c", "B");
  const bim_type_t *d = xsd_compiled_type(c, "urn:c", "D");
  const bim_part_t *ch;
  /* sequence[1,1](sequence[0,1](x[1,*])) */
  ck_assert_int_eq(b->content, BIM_CONTENT_SEQ);
  ck_assert_int_eq(b->top->kind, BIM_PART_ELEM);
  ck_assert_str_eq(b->top->name, "x");
  ck_assert_str_eq(b->top->ns, "urn:c");
  ck_assert_uint_eq(b->top->min, 0);
  ck_assert_uint_eq(b->top->max, BIM_UNBOUNDED);
  ck_assert_int_eq(b->top->type->simple->codec, BIM_RANGE);
  ck_assert_int_eq(b->top->type->simple->range.min, INT32_MIN);
  /* extension: base content then choice(choice(q p) o[0,1]) flattened, emptied, sorted */
  ck_assert_int_eq(d->top->kind, BIM_PART_SEQ);
  ck_assert_int_eq(d->top->nkids, 2);
  ch = &d->top->kids[1];
  ck_assert_int_eq(ch->kind, BIM_PART_CHOICE);
  ck_assert_uint_eq(ch->min, 0);
  ck_assert_int_eq(ch->nkids, 3);
  ck_assert_str_eq(ch->kids[0].name, "o");
  ck_assert_uint_eq(ch->kids[0].min, 1);
  ck_assert_str_eq(ch->kids[1].name, "p");
  ck_assert_str_eq(ch->kids[2].name, "q");
  ck_assert_int_eq(ch->kids[0].type->simple->codec, BIM_ENUM);
  ck_assert_int_eq(ch->kids[0].type->simple->nenums, 2);
  ck_assert_int_eq(ch->kids[1].type->simple->codec, BIM_RANGE);
  ck_assert_int_eq(ch->kids[1].type->simple->range.min, 10);
  ck_assert_uint_eq(ch->kids[1].type->simple->range.span, 32757);
  ck_assert_int_eq(ch->kids[2].type->simple->codec, BIM_SVLU5);
  xsd_set_free(s);
}
END_TEST

/* ISO 23001-1 6.6.5.4, 6.6.5.3, 6.6.5.2 */
START_TEST(compile_codes_and_branches) {
  xsd_set_t *s;
  const xsd_compiled_t *c = compile_c(&s, 1);
  const bim_type_t *b = xsd_compiled_type(c, "urn:c", "B");
  const bim_type_t *d = xsd_compiled_type(c, "urn:c", "D");
  const bim_type_t *str = xsd_compiled_type(c, "http://www.w3.org/2001/XMLSchema", "string");
  const bim_elem_t *top = xsd_compiled_elem(c, "urn:c", "Top");
  const bim_subtypes_t *bs = NULL;
  const bim_subtypes_t *ss = NULL;
  for (int i = 0; i < c->schema.nsubs; i++) {
    if (c->schema.subs[i].base == b) bs = &c->schema.subs[i];
    if (c->schema.subs[i].base == str) ss = &c->schema.subs[i];
  }
  ck_assert_ptr_eq(c->types[0], xsd_compiled_type(c, "http://www.w3.org/2001/XMLSchema", "anyType"));
  /* anonymous base skipped: Two below anySimpleType */
  for (int i = 0; i < c->schema.nsubs; i++)
    if (c->schema.subs[i].base == xsd_compiled_type(c, "http://www.w3.org/2001/XMLSchema", "anySimpleType")) {
      int hit = 0;
      for (int k = 0; k < c->schema.subs[i].nsubs; k++) hit |= c->schema.subs[i].subs[k] == xsd_compiled_type(c, "urn:c", "Two");
      ck_assert_int_eq(hit, 1);
    }
  ck_assert_ptr_nonnull(bs);
  ck_assert_int_eq(bs->nsubs, 2);
  ck_assert_ptr_eq(bs->subs[0], d);
  ck_assert_str_eq(bs->subs[1]->name, "R");
  ck_assert_ptr_nonnull(ss);
  ck_assert_str_eq(ss->subs[0]->name, "normalizedString");
  ck_assert_int_eq(top->nsubsts, 2);
  ck_assert_str_eq(top->substs[0]->name, "Alt");
  ck_assert_ptr_eq(top->substs[0]->type, d);
  ck_assert_str_eq(top->substs[1]->name, "Zed");
  ck_assert_ptr_eq(top->substs[1]->type, b);
  ck_assert_int_eq(c->nelems, 3);
  ck_assert_str_eq(c->elems[0]->name, "Alt");
  /* B: x, attributes. D: base children, p, q, o by signature, then own attribute a */
  ck_assert_int_eq(b->nsbc, 5);
  ck_assert_str_eq(b->sbc[0].name, "x");
  ck_assert_int_eq(b->sbc[1].kind, BIM_SBC_ATTR);
  ck_assert_str_eq(b->sbc[1].name, "lang");
  ck_assert_str_eq(b->sbc[2].name, "k");
  ck_assert_str_eq(b->sbc[3].name, "r");
  ck_assert_int_eq(d->nsbc, 9);
  ck_assert_str_eq(d->sbc[5].name, "o");
  ck_assert_str_eq(d->sbc[6].name, "p");
  ck_assert_str_eq(d->sbc[7].name, "q");
  ck_assert_str_eq(d->sbc[8].name, "a");
  xsd_set_free(s);
}
END_TEST

/* ISO 23001-1 7.4.4 AttributesDelta, ContentDelta */
START_TEST(compile_multi_version_deltas) {
  xsd_set_t *s;
  const xsd_compiled_t *c = compile_c(&s, 1);
  const bim_type_t *b = xsd_compiled_type(c, "urn:c", "B");
  const bim_subtypes_t *bs = NULL;
  const bim_type_t *dd;
  const bim_type_t *rd;
  for (int i = 0; i < c->schema.nsubs; i++)
    if (c->schema.subs[i].base == b) bs = &c->schema.subs[i];
  ck_assert_ptr_nonnull(bs);
  ck_assert_ptr_nonnull(bs->deltas);
  dd = bs->deltas[0];
  rd = bs->deltas[1];
  ck_assert_int_eq(dd->nattrs, 1);
  ck_assert_str_eq(dd->attrs[0].name, "a");
  ck_assert_int_eq(dd->content, BIM_CONTENT_SEQ);
  ck_assert_int_eq(dd->top->kind, BIM_PART_CHOICE);
  ck_assert_uint_eq(dd->top->min, 0);
  ck_assert_int_eq(dd->top->nkids, 3);
  ck_assert_int_eq(rd->nattrs, 0);
  ck_assert_int_eq(rd->content, BIM_CONTENT_NONE);
  xsd_set_free(s);
}
END_TEST

/* ISO 15938-1:2002 8.5.4.2.5: default codec, not in ISO 23001-1 7.5.4 */
START_TEST(compile_mpeg7_time_codec_by_edition) {
  static const char *const docs[] = {schema_m7};
  for (int mpegb = 0; mpegb < 2; mpegb++) {
    xsd_set_t *s = load_docs(docs, 1);
    xsd_compile_opts_t o = {mpegb, 1, 0, NULL, 0, 0};
    const xsd_compiled_t *c = NULL;
    ck_assert_int_eq(xsd_compile(s, &o, &c), 0);
    ck_assert_int_eq(xsd_compiled_type(c, "urn:mpeg:mpeg7:schema:2001", "timePointType")->simple->codec, mpegb ? BIM_ZSTRING : BIM_M7_TIMEPOINT);
    xsd_set_free(s);
  }
}
END_TEST

static const char schema_compat[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:m=\"urn:mpeg:mpeg7:schema:2001\" xmlns:c=\"urn:cm\" targetNamespace=\"urn:cm\">"
  "<xs:import namespace=\"urn:mpeg:mpeg7:schema:2001\" schemaLocation=\"m.xsd\"/>"
  "<xs:attribute name=\"dim\" type=\"xs:string\"/>"
  "<xs:complexType name=\"T\"><xs:sequence><xs:element name=\"E\" type=\"xs:string\" minOccurs=\"0\"/></xs:sequence>"
  "<xs:attribute ref=\"c:dim\"/><xs:attribute name=\"order\" type=\"xs:string\"/><xs:attribute name=\"k\" type=\"xs:string\" fixed=\"v\"/>"
  "<xs:attribute name=\"ids\" type=\"xs:IDREFS\"/><xs:attribute name=\"b\" type=\"m:basicDurationType\"/>"
  "<xs:attribute name=\"d\" type=\"m:durationType\"/><xs:attribute name=\"o\" type=\"m:offsetType\"/></xs:complexType>"
  "</xs:schema>";

static const char schema_compat_m7[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:m=\"urn:mpeg:mpeg7:schema:2001\" targetNamespace=\"urn:mpeg:mpeg7:schema:2001\">"
  "<xs:simpleType name=\"basicDurationType\"><xs:restriction base=\"xs:string\"/></xs:simpleType>"
  "<xs:simpleType name=\"durationType\"><xs:restriction base=\"m:basicDurationType\"/></xs:simpleType>"
  "<xs:simpleType name=\"offsetType\"><xs:restriction base=\"m:basicDurationType\"/></xs:simpleType>"
  "</xs:schema>";

static const bim_attr_t *attr_named(const bim_type_t *t, const char *name) {
  for (int i = 0; i < t->nattrs; i++)
    if (!strcmp(t->attrs[i].name, name)) return &t->attrs[i];
  return NULL;
}

/* conformance deviations: attr order, optional fixed attrs, built-in lists, derived time types */
START_TEST(compile_compat_options) {
  static const char *const docs[] = {schema_compat, schema_compat_m7};
  for (unsigned compat = 0; compat < 2; compat++) {
    xsd_set_t *s = load_docs(docs, 2);
    xsd_compile_opts_t o = {0, 0, 0, NULL, 0, compat ? BIM_COMPAT_ATTR_LOCAL | BIM_COMPAT_FIXED_ATTR | BIM_COMPAT_LIST_STRING | BIM_COMPAT_M7_FLAGS : 0};
    const xsd_compiled_t *c = NULL;
    const bim_type_t *t;
    ck_assert_int_eq(xsd_compile(s, &o, &c), 0);
    t = xsd_compiled_type(c, "urn:cm", "T");
    ck_assert_ptr_nonnull(t);
    ck_assert_int_eq(t->nattrs, compat ? 7 : 6);
    /* expanded names put the namespaced dim last, local names between d and ids */
    ck_assert_str_eq(t->attrs[t->nattrs - 1].name, compat ? "order" : "dim");
    ck_assert_str_eq(t->attrs[2].name, compat ? "dim" : "ids");
    ck_assert_int_eq(attr_named(t, "k") != NULL, compat);
    ck_assert_int_eq(attr_named(t, "ids")->simple->codec, compat ? BIM_UTF8 : BIM_LIST);
    ck_assert_int_eq(attr_named(t, "b")->simple->codec, BIM_M7_DURATION);
    ck_assert_int_eq(attr_named(t, "d")->simple->codec, BIM_M7_DURATION);
    ck_assert_int_eq(attr_named(t, "o")->simple->codec, compat ? BIM_UTF8 : BIM_M7_DURATION);
    xsd_set_free(s);
  }
}
END_TEST

/* XML Schema Part 2 Appendix F */
START_TEST(pattern_matches_whole_value) {
  static const struct {
    const char *re;
    const char *v;
    int want;
  } cases[] = {
    {"\\d{1,3}(\\.\\d{1,3}){3}", "10.0.0.1", 1}, {"\\d{1,3}(\\.\\d{1,3}){3}", "10.0.0", 0}, {"\\d{1,3}(\\.\\d{1,3}){3}", "10.0.0.1x", 0},
    {"[0-9a-fA-F:]+", "fe80::1", 1},           {"a|bc", "bc", 1},                          {"a|bc", "abc", 0},
    {"\\i\\c*", "x-1.y", 1},                    {"\\i\\c*", "1x", 0},                        {"[a-z-[aeiou]]+", "bcd", 1},
    {"[a-z-[aeiou]]+", "bad", 0},               {"[^0-9]?x", "x", 1},                        {"(ab)*c", "ababc", 1},
    {"(ab)*c", "abac", 0},                      {"\\p{L}", "a", -2},                         {"a{2,}", "aaa", 1},
    {"\\D\\S", "a b", 0},                       {".+", "\xc3\xa9\xe2\x82\xac", 1},           {"(a|)+b", "aab", 1},
  };
  xsd_set_t *s = xsd_set_new();
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    xsd_pattern_t *p = xsd_pattern_compile(s, cases[i].re);
    if (cases[i].want == -2) {
      ck_assert_ptr_null(p);
      continue;
    }
    ck_assert_ptr_nonnull(p);
    ck_assert_int_eq(xsd_pattern_match(p, cases[i].v), cases[i].want);
  }
  xsd_set_free(s);
}
END_TEST

/* XML Schema Part 2 2.5.1.3, 4.3.4: union members told apart by pattern facets */
START_TEST(compile_union_member_patterns) {
  static const char *const docs[] = {
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:u=\"urn:u\" targetNamespace=\"urn:u\">"
    "<xs:simpleType name=\"V4\"><xs:restriction base=\"xs:string\"><xs:pattern value=\"\\d+(\\.\\d+){3}\"/></xs:restriction></xs:simpleType>"
    "<xs:simpleType name=\"V6\"><xs:restriction base=\"xs:string\"><xs:pattern value=\"[0-9a-f:]+\"/><xs:pattern value=\"x\"/>"
    "<xs:maxLength value=\"39\"/></xs:restriction></xs:simpleType>"
    "<xs:simpleType name=\"A\"><xs:union memberTypes=\"u:V4 u:V6\"/></xs:simpleType></xs:schema>"};
  xsd_set_t *s = load_docs(docs, 1);
  xsd_compile_opts_t o = {1, 0, 0, NULL, 0, 0};
  const xsd_compiled_t *c = NULL;
  const bim_simple_t *u;
  ck_assert_int_eq(xsd_compile(s, &o, &c), 0);
  u = xsd_compiled_type(c, "urn:u", "A")->simple;
  ck_assert_int_eq(u->codec, BIM_UNION);
  ck_assert_int_eq(u->members[0]->npatterns, 1);
  ck_assert_int_eq(u->members[0]->match(u->members[0]->patterns[0], "1.2.3.4"), 1);
  ck_assert_int_eq(u->members[0]->match(u->members[0]->patterns[0], "fe80::1"), 0);
  ck_assert_int_eq(u->members[1]->match(u->members[1]->patterns[0], "fe80::1"), 1);
  ck_assert_int_eq(u->members[1]->match(u->members[1]->patterns[0], "x"), 1);
  ck_assert_int_eq(u->members[1]->has_length, 1);
  ck_assert_uint_eq(u->members[1]->maxlen, 39);
  xsd_set_free(s);
}
END_TEST

/* XML Schema Part 1 3.4.2: empty extensions keep base content */
START_TEST(compile_inherits_content_through_empty_extensions) {
  static const char *const docs[] = {
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:e=\"urn:e\" targetNamespace=\"urn:e\">"
    "<xs:complexType name=\"S\"><xs:simpleContent><xs:extension base=\"xs:string\"><xs:attribute name=\"a\"/></xs:extension>"
    "</xs:simpleContent></xs:complexType>"
    "<xs:complexType name=\"S2\"><xs:complexContent><xs:extension base=\"e:S\"><xs:attribute name=\"b\" type=\"xs:boolean\"/>"
    "</xs:extension></xs:complexContent></xs:complexType>"
    "<xs:complexType name=\"C\"><xs:sequence><xs:element name=\"n\" type=\"xs:string\"/></xs:sequence></xs:complexType>"
    "<xs:complexType name=\"C1\"><xs:complexContent><xs:extension base=\"e:C\"><xs:attribute name=\"x\" type=\"xs:boolean\"/>"
    "</xs:extension></xs:complexContent></xs:complexType>"
    "<xs:complexType name=\"C2\"><xs:complexContent><xs:extension base=\"e:C1\"><xs:attribute name=\"y\" type=\"xs:boolean\"/>"
    "</xs:extension></xs:complexContent></xs:complexType></xs:schema>"};
  xsd_set_t *s = load_docs(docs, 1);
  xsd_compile_opts_t o = {1, 0, 0, NULL, 0, 0};
  const xsd_compiled_t *c = NULL;
  const bim_type_t *t;
  ck_assert_int_eq(xsd_compile(s, &o, &c), 0);
  t = xsd_compiled_type(c, "urn:e", "S2");
  ck_assert_int_eq(t->content, BIM_CONTENT_SIMPLE);
  ck_assert_int_eq(t->simple->codec, BIM_UTF8);
  ck_assert_int_eq(t->nattrs, 2);
  t = xsd_compiled_type(c, "urn:e", "C2");
  ck_assert_int_eq(t->content, BIM_CONTENT_SEQ);
  ck_assert_str_eq(t->top->name, "n");
  ck_assert_int_eq(t->nattrs, 2);
  xsd_set_free(s);
}
END_TEST

START_TEST(compile_reports_errors) {
  static const char *const bad[][2] = {
    {"<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\"><xs:complexType name=\"T\"><xs:choice>"
     "<xs:element name=\"a\" minOccurs=\"0\" maxOccurs=\"0\"/><xs:element name=\"b\"/></xs:choice></xs:complexType></xs:schema>",
     "LS400289"},
    {"<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\"><xs:simpleType name=\"T\"><xs:restriction base=\"xs:integer\">"
     "<xs:minInclusive value=\"5\"/><xs:maxInclusive value=\"4\"/></xs:restriction></xs:simpleType></xs:schema>",
     "LS400284"},
  };
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    const char *docs[] = {bad[i][0]};
    xsd_set_t *s = load_docs(docs, 1);
    xsd_compile_opts_t o = {1, 0, 0, NULL, 0, 0};
    const xsd_compiled_t *c = NULL;
    ck_assert_int_eq(xsd_compile(s, &o, &c), -1);
    ck_assert_str_eq(xsd_set_error_code(s), bad[i][1]);
    ck_assert_int_eq(strncmp(xsd_set_error(s), "bim: ", 5), 0);
    xsd_set_free(s);
  }
}
END_TEST

/* XML Schema Part 1 3.4.2 */
START_TEST(compile_extension_inherits_mixed) {
  static const char sch[] = "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:m=\"urn:m\" targetNamespace=\"urn:m\">"
                            "<xs:complexType name=\"A\"><xs:complexContent><xs:extension base=\"xs:anyType\"/></xs:complexContent></xs:complexType>"
                            "<xs:complexType name=\"B\"><xs:complexContent><xs:extension base=\"m:A\">"
                            "<xs:attribute name=\"x\" type=\"xs:string\"/></xs:extension></xs:complexContent></xs:complexType>"
                            "<xs:complexType name=\"P\"><xs:sequence><xs:element name=\"e\" type=\"xs:string\"/></xs:sequence></xs:complexType>"
                            "<xs:complexType name=\"Q\"><xs:complexContent><xs:extension base=\"m:P\"/></xs:complexContent></xs:complexType>"
                            "</xs:schema>";
  static const char *const docs[] = {sch};
  xsd_compile_opts_t o = {1, 0, 0, NULL, 0, 0};
  const xsd_compiled_t *c = NULL;
  xsd_set_t *s = load_docs(docs, 1);
  if (xsd_compile(s, &o, &c)) ck_abort_msg("%s %s", xsd_set_error_code(s), xsd_set_error(s));
  ck_assert_int_eq(xsd_compiled_type(c, "urn:m", "A")->mixed, 1);
  ck_assert_int_eq(xsd_compiled_type(c, "urn:m", "B")->mixed, 1);
  ck_assert_int_eq(xsd_compiled_type(c, "urn:m", "Q")->mixed, 0);
  xsd_set_free(s);
}
END_TEST

/* ISO 23001-1 6.7.8.3 */
START_TEST(encoding_schema_compiles) {
  for (int mpegb = 0; mpegb < 2; mpegb++) {
    static const char *const docs[] = {xsd_encoding_schema};
    xsd_compile_opts_t o = {mpegb, 0, 0, NULL, 0, 0};
    const xsd_compiled_t *c = NULL;
    xsd_set_t *s = load_docs(docs, 1);
    if (xsd_compile(s, &o, &c)) ck_abort_msg("%s %s", xsd_set_error_code(s), xsd_set_error(s));
    ck_assert_ptr_nonnull(xsd_compiled_elem(c, XSD_ENCODING_NS, "schema"));
    ck_assert_ptr_nonnull(xsd_compiled_type(c, XSD_ENCODING_NS, "particleType"));
    xsd_set_free(s);
  }
}
END_TEST

static bim_node_t *enc_node(bim_node_t *parent, const char *name, const xsd_compiled_t *ec, const char *cast) {
  bim_node_t *n = bim_node_add_new(parent, name, NULL);
  ck_assert_ptr_nonnull(n);
  n->ns = XSD_ENCODING_NS;
  if (cast) {
    n->cast = xsd_compiled_type(ec, XSD_ENCODING_NS, cast);
    ck_assert_ptr_nonnull(n->cast);
  }
  return n;
}

static void enc_typeref(bim_node_t *parent, const char *name, const xsd_compiled_t *ec, const char *nsid, const char *tid) {
  bim_node_t *r = enc_node(enc_node(parent, name, ec, NULL), "namedTypeReference", ec, NULL);
  if (nsid) ck_assert_int_eq(bim_node_set_attr(r, "NamespaceID", nsid), 0);
  ck_assert_int_eq(bim_node_set_attr(r, "TypeID", tid), 0);
}

/* ISO 23001-1 6.7.8.2, 6.7.8.3 */
START_TEST(schema_update_decodes_and_converts) {
  static const char *const imported[] = {XSD_NS};
  xsd_set_t *es;
  const xsd_compiled_t *ec;
  bitwriter_t au;
  const unsigned char *p;
  size_t n;
  bim_node_t *tree = NULL;
  bim_node_t *root;
  bim_node_t *ct;
  bim_node_t *at;
  bim_node_t *cm;
  bim_node_t *el;
  bim_err_t err;
  xsd_suu_t in;
  char *text = NULL;
  char tid[16];
  xml_doc_t *doc = NULL;
  xsd_set_t *s;
  xsd_compile_opts_t o = {1, 0, 0, NULL, 0, 0};
  const xsd_compiled_t *c;
  const bim_elem_t *r;
  int k = 0;
  ck_assert_int_eq(xsd_encoding_compile(&es, &ec), 0);
  bitwriter_init(&au);
  /* one FUU of 3 bytes: AddContent, reparameterization 01, ContextModeCode 001, Path Termination Code,
     DecodingModes 0x1F, both attributes and all four elements absent */
  ck_assert_int_eq(bitwriter_put_vluimsbf8(&au, 1) || bitwriter_put_vluimsbf8(&au, 3) || bitwriter_put(&au, 1, 4) || bitwriter_put(&au, 1, 2) ||
                       bitwriter_put(&au, 1, 3) || bitwriter_put(&au, 1, 1) || bitwriter_put(&au, 0x1F, 8) || bitwriter_put(&au, 0, 6),
                   0);
  p = bitwriter_data(&au, &n);
  if (xsd_suu_decode(ec, p, n * 8, &tree, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  ck_assert_str_eq(tree->name, "schema");
  ck_assert_int_eq(tree->nkids, 0);
  bim_node_free(tree);
  ck_assert_int_eq(xsd_suu_decode(ec, p, n * 8 - 8, &tree, &err), -1);
  ck_assert_str_eq(err.code, "LE400302");

  for (int i = 0; i < ec->ntypes && strcmp(ec->types[i]->name ? ec->types[i]->name : "", "string"); i++) k += ec->types[i]->name && !strcmp(ec->types[i]->ns, XSD_NS);
  snprintf(tid, sizeof tid, "%d", k);
  root = bim_node_new("schema");
  root->ns = XSD_ENCODING_NS;
  ct = enc_node(enc_node(root, "typeDefinitions", ec, NULL), "complexType", ec, NULL);
  ck_assert_int_eq(bim_node_set_attr(ct, "name", "T"), 0);
  at = enc_node(enc_node(ct, "attributes", ec, NULL), "attribute", ec, NULL);
  ck_assert_int_eq(bim_node_set_attr(at, "name", "a") || bim_node_set_attr(at, "use", "optional") || bim_node_set_attr(at, "form", "unqualified"), 0);
  enc_typeref(at, "typeReference", ec, "0", tid);
  cm = enc_node(ct, "content", ec, "complexContentModelType");
  ck_assert_int_eq(bim_node_set_attr(cm, "mixed", "false"), 0);
  el = enc_node(enc_node(cm, "particle", ec, "sequence"), "particle", ec, "element");
  ck_assert_int_eq(bim_node_set_attr(el, "name", "E") || bim_node_set_attr(el, "nillable", "false") || bim_node_set_attr(el, "maxOccurs", "unbounded"), 0);
  enc_typeref(el, "type", ec, "0", tid);
  el = enc_node(enc_node(root, "elementDeclarations", ec, NULL), "globalElement", ec, NULL);
  ck_assert_int_eq(bim_node_set_attr(el, "name", "R"), 0);
  enc_typeref(el, "elementTypeReference", ec, NULL, "0");

  memset(&in, 0, sizeof in);
  in.tree = root;
  in.tns = "urn:x";
  in.imported = imported;
  in.nimported = 1;
  in.known = ec;
  if (xsd_suu_to_xsd(&in, &text, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  ck_assert_int_eq(xml_doc_parse(text, strlen(text), &doc, NULL), 0);
  s = xsd_set_new();
  if (xsd_set_add_doc(s, doc, "suu") || xsd_set_resolve(s) || xsd_compile(s, &o, &c)) ck_abort_msg("%s %s\n%s", xsd_set_error_code(s), xsd_set_error(s), text);
  r = xsd_compiled_elem(c, "urn:x", "R");
  ck_assert_ptr_nonnull(r);
  ck_assert_str_eq(r->type->name, "T");
  ck_assert_int_eq(r->type->nattrs, 1);
  ck_assert_str_eq(r->type->attrs[0].name, "a");
  ck_assert_int_eq(r->type->content, BIM_CONTENT_SEQ);
  free(text);
  bim_node_free(root);
  bitwriter_free(&au);
  xsd_set_free(s);
  xsd_set_free(es);
}
END_TEST

static const char schema_gen[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:t=\"urn:x\" targetNamespace=\"urn:x\" elementFormDefault=\"qualified\">"
  "<xs:simpleType name=\"Color\"><xs:restriction base=\"xs:string\"><xs:enumeration value=\"red\"/><xs:enumeration value=\"blue\"/></xs:restriction></xs:simpleType>"
  "<xs:simpleType name=\"Colors\"><xs:list itemType=\"t:Color\"/></xs:simpleType>"
  "<xs:simpleType name=\"ColorOrNum\"><xs:union memberTypes=\"t:Color xs:int\"/></xs:simpleType>"
  "<xs:simpleType name=\"Short\"><xs:restriction base=\"xs:int\"><xs:minInclusive value=\"1\"/><xs:maxInclusive value=\"9\"/></xs:restriction></xs:simpleType>"
  "<xs:attribute name=\"ga\" type=\"xs:string\"/>"
  "<xs:complexType name=\"Base\"><xs:sequence><xs:element name=\"A\" type=\"xs:string\" minOccurs=\"0\" maxOccurs=\"3\"/>"
  "<xs:element name=\"N\" type=\"xs:string\" nillable=\"true\" minOccurs=\"0\"/></xs:sequence>"
  "<xs:attribute name=\"k\" type=\"t:Color\" use=\"required\"/><xs:attribute name=\"d\" type=\"xs:int\" default=\"5\"/></xs:complexType>"
  "<xs:complexType name=\"Derived\"><xs:complexContent><xs:extension base=\"t:Base\"><xs:choice>"
  "<xs:element name=\"B\" type=\"xs:string\"/>"
  "<xs:element name=\"C\"><xs:complexType><xs:sequence><xs:element name=\"Z\" type=\"t:Short\"/></xs:sequence></xs:complexType></xs:element>"
  "</xs:choice><xs:attribute ref=\"t:ga\"/></xs:extension></xs:complexContent></xs:complexType>"
  "<xs:element name=\"Root\" type=\"t:Derived\"/>"
  "<xs:element name=\"Other\" type=\"t:Base\"/>"
  "<xs:complexType name=\"TextT\"><xs:simpleContent><xs:extension base=\"xs:string\"><xs:attribute name=\"u\" type=\"xs:string\"/></xs:extension></xs:simpleContent></xs:complexType>"
  "<xs:complexType name=\"MixT\" mixed=\"true\"><xs:sequence><xs:element ref=\"t:Other\" minOccurs=\"0\"/><xs:any minOccurs=\"0\"/></xs:sequence><xs:anyAttribute/></xs:complexType>"
  "</xs:schema>";

static void part_eq(const bim_part_t *a, const bim_part_t *b) {
  ck_assert_int_eq(a->kind, b->kind);
  ck_assert_uint_eq(a->min, b->min);
  ck_assert_uint_eq(a->max, b->max);
  ck_assert_int_eq(a->nkids, b->nkids);
  if (a->kind == BIM_PART_ELEM) {
    ck_assert_str_eq(a->name, b->name);
    ck_assert_int_eq(a->nillable, b->nillable);
  }
  for (int i = 0; i < a->nkids; i++) part_eq(&a->kids[i], &b->kids[i]);
}

static void simple_eq(const bim_simple_t *a, const bim_simple_t *b) {
  ck_assert_int_eq(a->codec, b->codec);
  ck_assert_int_eq(a->nenums, b->nenums);
  for (int i = 0; i < a->nenums; i++) ck_assert_str_eq(a->enums[i], b->enums[i]);
  ck_assert_int_eq(a->nmembers, b->nmembers);
  ck_assert_int_eq(a->range.min, b->range.min);
  ck_assert_uint_eq(a->range.span, b->range.span);
}

static void type_eq(const bim_type_t *a, const bim_type_t *b) {
  ck_assert_ptr_nonnull(b);
  ck_assert_int_eq(a->content, b->content);
  ck_assert_int_eq(a->mixed, b->mixed);
  ck_assert_int_eq(a->nattrs, b->nattrs);
  ck_assert_int_eq(a->nsbc, b->nsbc);
  ck_assert_int_eq(!!a->anyattr, !!b->anyattr);
  for (int i = 0; i < a->nattrs; i++) {
    ck_assert_str_eq(a->attrs[i].name, b->attrs[i].name);
    ck_assert_int_eq(a->attrs[i].required, b->attrs[i].required);
    simple_eq(a->attrs[i].simple, b->attrs[i].simple);
  }
  if (a->top) part_eq(a->top, b->top);
  if (a->simple) simple_eq(a->simple, b->simple);
}

/* ISO 23001-1 6.7.8: model to encoding schema tree, BinaryEncodedSchema, back to a schema with the same coding structures */
START_TEST(schema_update_encodes_from_model) {
  static const char *const imported[] = {XSD_NS};
  static const char *const names[] = {"Base", "Derived", "TextT", "MixT", "Color", "Colors", "ColorOrNum", "Short"};
  xsd_set_t *es;
  const xsd_compiled_t *ec;
  xsd_set_t *s1;
  const xsd_compiled_t *c1;
  xsd_set_t *s2;
  const xsd_compiled_t *c2;
  xsd_compile_opts_t o = {1, 0, 0, NULL, 0, 0};
  xml_doc_t *doc = NULL;
  xsd_suu_src_t src;
  xsd_suu_t in;
  bim_node_t *tree = NULL;
  bim_node_t *back = NULL;
  bim_err_t err;
  bitwriter_t bw;
  const unsigned char *p;
  size_t n;
  char *text = NULL;
  ck_assert_int_eq(xsd_encoding_compile(&es, &ec), 0);
  s1 = xsd_set_new();
  ck_assert_int_eq(xml_doc_parse(schema_gen, strlen(schema_gen), &doc, NULL), 0);
  if (xsd_set_add_doc(s1, doc, "g.xsd") || xsd_set_resolve(s1) || xsd_compile(s1, &o, &c1)) ck_abort_msg("%s %s", xsd_set_error_code(s1), xsd_set_error(s1));
  memset(&src, 0, sizeof src);
  src.set = s1;
  src.tns = "urn:x";
  src.imported = imported;
  src.nimported = 1;
  src.known = ec;
  if (xsd_suu_from_set(ec, &src, &tree, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  bitwriter_init(&bw);
  if (xsd_suu_encode(ec, tree, &bw, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  p = bitwriter_data(&bw, &n);
  if (xsd_suu_decode(ec, p, n * 8, &back, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  memset(&in, 0, sizeof in);
  in.tree = back;
  in.tns = "urn:x";
  in.imported = imported;
  in.nimported = 1;
  in.known = ec;
  if (xsd_suu_to_xsd(&in, &text, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  s2 = xsd_set_new();
  doc = NULL;
  ck_assert_int_eq(xml_doc_parse(text, strlen(text), &doc, NULL), 0);
  if (xsd_set_add_doc(s2, doc, "back.xsd") || xsd_set_resolve(s2) || xsd_compile(s2, &o, &c2)) ck_abort_msg("%s %s\n%s", xsd_set_error_code(s2), xsd_set_error(s2), text);
  for (size_t i = 0; i < sizeof names / sizeof *names; i++) type_eq(xsd_compiled_type(c1, "urn:x", names[i]), xsd_compiled_type(c2, "urn:x", names[i]));
  ck_assert_ptr_nonnull(xsd_compiled_elem(c2, "urn:x", "Root"));
  ck_assert_ptr_nonnull(xsd_compiled_elem(c2, "urn:x", "Other"));
  type_eq(xsd_compiled_elem(c1, "urn:x", "Root")->type, xsd_compiled_elem(c2, "urn:x", "Root")->type);
  free(text);
  bim_node_free(tree);
  bim_node_free(back);
  bitwriter_free(&bw);
  xsd_set_free(s1);
  xsd_set_free(s2);
  {
    static const char bad[] =
      "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" targetNamespace=\"urn:y\">"
      "<xs:element name=\"E\" type=\"xs:string\" nillable=\"true\"/></xs:schema>";
    xsd_set_t *s3 = xsd_set_new();
    doc = NULL;
    ck_assert_int_eq(xml_doc_parse(bad, strlen(bad), &doc, NULL), 0);
    ck_assert_int_eq(xsd_set_add_doc(s3, doc, "bad.xsd") || xsd_set_resolve(s3), 0);
    src.set = s3;
    src.tns = "urn:y";
    ck_assert_int_eq(xsd_suu_from_set(ec, &src, &tree, &err), -1);
    ck_assert_str_eq(err.code, "LE400302");
    xsd_set_free(s3);
  }
  xsd_set_free(es);
}
END_TEST

/* one file per namespace in dirs, several for one reported */
START_TEST(xsd_find_schema_by_namespace) {
  char dir[] = "/tmp/xsdfindXXXXXX";
  char sub[64];
  char path[600];
  const char *dirs[1] = {dir};
  xsd_set_t *s = xsd_set_new();
  ck_assert_ptr_nonnull(mkdtemp(dir));
  snprintf(sub, sizeof sub, "%s/v2", dir);
  ck_assert_int_eq(mkdir(sub, 0700), 0);
  put_file(dir, "a.xsd", XS_OPEN "targetNamespace=\"urn:a\"/>");
  put_file(dir, "b.xsd", XS_OPEN "targetNamespace=\"urn:b\"/>");
  put_file(dir, "note.txt", "targetNamespace=\"urn:c\"");
  ck_assert_int_eq(xsd_find_schema(s, NULL, dirs, 1, "urn:a", path, sizeof path), 0);
  ck_assert_ptr_nonnull(strstr(path, "/a.xsd"));
  ck_assert_int_eq(xsd_find_schema(s, NULL, dirs, 1, "urn:c", path, sizeof path), 1);
  put_file(sub, "a2.xsd", XS_OPEN "targetNamespace=\"urn:a\"/>");
  ck_assert_int_eq(xsd_find_schema(s, NULL, dirs, 1, "urn:a", path, sizeof path), 2);
  ck_assert_int_eq(xsd_find_schema(s, NULL, dirs, 1, "urn:b", path, sizeof path), 0);
  dirs[0] = "/tmp/xsdfind-does-not-exist";
  ck_assert_int_eq(xsd_find_schema(s, NULL, dirs, 1, "urn:a", path, sizeof path), -1);
  ck_assert_str_eq(xsd_set_error_code(s), "LS000743");
  xsd_set_free(s);
  rm_tree(dir);
}
END_TEST

/* file as is, directories by namespace, own tns reported */
START_TEST(xsd_load_for_ns_file_and_directories) {
  char dir[] = "/tmp/xsdloadXXXXXX";
  char fa[300];
  char fb[300];
  char tns[128];
  const char *culprit = NULL;
  const char *paths[3];
  xsd_set_t *s;
  ck_assert_ptr_nonnull(mkdtemp(dir));
  put_file(dir, "a.xsd", XS_OPEN "targetNamespace=\"urn:a\"/>");
  put_file(dir, "b.xsd", XS_OPEN "targetNamespace=\"urn:b\"/>");
  snprintf(fa, sizeof fa, "%s/a.xsd", dir);
  snprintf(fb, sizeof fb, "%s/b.xsd", dir);
  paths[0] = dir;
  s = xsd_set_new();
  ck_assert_int_eq(xsd_load_for_ns(s, NULL, paths, 1, "urn:b", &culprit, tns, sizeof tns), 0);
  ck_assert_str_eq(tns, "urn:b");
  xsd_set_free(s);
  paths[0] = fa;
  s = xsd_set_new();
  ck_assert_int_eq(xsd_load_for_ns(s, NULL, paths, 1, "urn:b", &culprit, tns, sizeof tns), 0);
  ck_assert_str_eq(tns, "urn:a");
  xsd_set_free(s);
  paths[1] = fb;
  s = xsd_set_new();
  ck_assert_int_eq(xsd_load_for_ns(s, NULL, paths, 2, NULL, &culprit, NULL, 0), XSD_LOAD_FILES);
  ck_assert_str_eq(culprit, fb);
  xsd_set_free(s);
  /* several files: by namespace, then file dirs */
  s = xsd_set_new();
  ck_assert_int_eq(xsd_load_for_ns(s, NULL, paths, 2, "urn:b", &culprit, tns, sizeof tns), 0);
  ck_assert_str_eq(tns, "urn:b");
  xsd_set_free(s);
  s = xsd_set_new();
  ck_assert_int_eq(xsd_load_for_ns(s, NULL, paths, 2, "urn:none", &culprit, NULL, 0), XSD_LOAD_NONE);
  xsd_set_free(s);
  put_file(dir, "a2.xsd", XS_OPEN "targetNamespace=\"urn:a\"/>");
  snprintf(fb, sizeof fb, "%s/a2.xsd", dir);
  paths[1] = fb;
  s = xsd_set_new();
  ck_assert_int_eq(xsd_load_for_ns(s, NULL, paths, 2, "urn:a", &culprit, NULL, 0), XSD_LOAD_FILES);
  ck_assert_str_eq(culprit, fb);
  xsd_set_free(s);
  /* extra schema: lone file not taken as is */
  paths[0] = fa;
  s = xsd_set_new();
  ck_assert_int_eq(xsd_load_extra(s, NULL, paths, 1, "urn:a", &culprit), 0);
  xsd_set_free(s);
  s = xsd_set_new();
  ck_assert_int_eq(xsd_load_extra(s, NULL, paths, 1, "urn:b", &culprit), 0);
  xsd_set_free(s);
  s = xsd_set_new();
  ck_assert_int_eq(xsd_load_extra(s, NULL, paths, 1, "urn:none", &culprit), XSD_LOAD_NONE);
  xsd_set_free(s);
  s = xsd_set_new();
  ck_assert_int_eq(xsd_load_extra(s, NULL, paths, 1, NULL, &culprit), XSD_LOAD_FILES);
  xsd_set_free(s);
  paths[0] = dir;
  s = xsd_set_new();
  ck_assert_int_eq(xsd_load_for_ns(s, NULL, paths, 1, "urn:none", &culprit, NULL, 0), XSD_LOAD_NONE);
  xsd_set_free(s);
  paths[0] = "/tmp/xsdload-does-not-exist";
  s = xsd_set_new();
  ck_assert_int_eq(xsd_load_for_ns(s, NULL, paths, 1, "urn:a", &culprit, NULL, 0), XSD_LOAD_STAT);
  ck_assert_str_eq(culprit, paths[0]);
  xsd_set_free(s);
  rm_tree(dir);
}
END_TEST

/* foreign namespaces of a document, never xml, xsi */
START_TEST(xsd_tree_extra_ns_lists_foreign_namespaces) {
  static const char sch[] = XS_OPEN "xmlns=\"urn:a\" targetNamespace=\"urn:a\"><xs:element name=\"R\" type=\"xs:string\"/></xs:schema>";
  static const char *const docs[] = {sch};
  static const char text[] = "<R xmlns=\"urn:a\" xmlns:b=\"urn:b\" xmlns:c=\"urn:c\" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
                             " xml:lang=\"en\" xsi:type=\"b:T\" c:q=\"1\"><b:K xsi:type=\"b:T\"/><b:L/></R>";
  xsd_compile_opts_t o = {0};
  const xsd_compiled_t *c = NULL;
  xsd_set_t *s = load_docs(docs, 1);
  xml_doc_t *doc = NULL;
  const char *ns[3];
  ck_assert_int_eq(xsd_compile(s, &o, &c), 0);
  ck_assert_int_eq(xsd_compiled_has_ns(c, "urn:a"), 1);
  ck_assert_int_eq(xsd_compiled_has_ns(c, "urn:b"), 0);
  ck_assert_int_eq(xml_doc_parse(text, strlen(text), &doc, NULL), 0);
  ck_assert_int_eq(xsd_tree_extra_ns(c, xml_doc_root(doc), ns, 3), 2);
  ck_assert_str_eq(ns[0], "urn:c");
  ck_assert_str_eq(ns[1], "urn:b");
  ck_assert_int_eq(xsd_tree_extra_ns(c, xml_doc_root(doc), ns, 1), -1);
  xml_doc_free(doc);
  xsd_set_free(s);
}
END_TEST

static Suite *xsd_suite(void) {
  Suite *s = suite_create("bim_xsd");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, xsd_builds_components);
  tcase_add_test(tc, xsd_builtins_have_facets);
  tcase_add_test(tc, xsd_reports_unresolved_and_invalid);
  tcase_add_test(tc, xsd_load_follows_imports_includes_redefines);
  tcase_add_test(tc, xsd_find_schema_by_namespace);
  tcase_add_test(tc, xsd_load_for_ns_file_and_directories);
  tcase_add_test(tc, xsd_tree_extra_ns_lists_foreign_namespaces);
  tcase_add_test(tc, compile_attributes_sorted_and_fixed_by_edition);
  tcase_add_test(tc, compile_content_simplified_and_signed);
  tcase_add_test(tc, compile_codes_and_branches);
  tcase_add_test(tc, compile_multi_version_deltas);
  tcase_add_test(tc, compile_mpeg7_time_codec_by_edition);
  tcase_add_test(tc, compile_compat_options);
  tcase_add_test(tc, compile_inherits_content_through_empty_extensions);
  tcase_add_test(tc, compile_reports_errors);
  tcase_add_test(tc, pattern_matches_whole_value);
  tcase_add_test(tc, compile_union_member_patterns);
  tcase_add_test(tc, compile_extension_inherits_mixed);
  tcase_add_test(tc, encoding_schema_compiles);
  tcase_add_test(tc, schema_update_decodes_and_converts);
  tcase_add_test(tc, schema_update_encodes_from_model);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(xsd_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
