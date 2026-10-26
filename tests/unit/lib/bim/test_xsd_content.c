/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/bim/xsd/xsd.h"

#define WRAP(body) "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:t=\"urn:t\" targetNamespace=\"urn:t\" elementFormDefault=\"qualified\">" body "</xs:schema>"

typedef struct {
  xsd_set_t *s;
  const xsd_compiled_t *c;
} cx_t;

static int compile_text(const char *text, cx_t *x, char *code) {
  xsd_compile_opts_t o = {1, 0, 0, NULL, 0, 0};
  xml_doc_t *doc = NULL;
  int rc;
  x->s = xsd_set_new();
  ck_assert_ptr_nonnull(x->s);
  ck_assert_int_eq(xml_doc_parse(text, strlen(text), &doc, NULL), 0);
  rc = xsd_set_add_doc(x->s, doc, "c.xsd");
  if (!rc) rc = xsd_set_resolve(x->s);
  if (!rc) rc = xsd_compile(x->s, &o, &x->c);
  if (code) snprintf(code, 16, "%s", rc ? xsd_set_error_code(x->s) : "");
  return rc;
}

static void fails(const char *text, const char *want) {
  cx_t x;
  char code[16];
  ck_assert_msg(compile_text(text, &x, code) != 0, "compiled: %s", text);
  ck_assert_msg(!strcmp(code, want), "wanted %s got %s", want, code);
  xsd_set_free(x.s);
}

static const bim_type_t *type_of(const cx_t *x, const char *name) {
  const bim_type_t *t = xsd_compiled_type(x->c, "urn:t", name);
  ck_assert_ptr_nonnull(t);
  return t;
}

START_TEST(content_model_rewrites) {
  cx_t x;
  char code[16];
  static const char doc[] = WRAP(
    "<xs:complexType name=\"Flat\"><xs:sequence><xs:sequence><xs:element name=\"a\" type=\"xs:string\" maxOccurs=\"3\"/></xs:sequence></xs:sequence></xs:complexType>"
    "<xs:complexType name=\"NestChoice\"><xs:choice><xs:choice><xs:element name=\"a\" type=\"xs:string\"/><xs:element name=\"b\" type=\"xs:string\"/></xs:choice><xs:element name=\"c\" type=\"xs:string\"/></xs:choice></xs:complexType>"
    "<xs:complexType name=\"OptChoice\"><xs:choice><xs:element name=\"a\" type=\"xs:string\" minOccurs=\"0\"/><xs:element name=\"b\" type=\"xs:string\"/></xs:choice></xs:complexType>"
    "<xs:complexType name=\"Wild\"><xs:sequence><xs:choice><xs:any namespace=\"##other\" minOccurs=\"0\"/><xs:any namespace=\"urn:b urn:a ##local\"/><xs:any/><xs:element name=\"z\" type=\"xs:string\"/></xs:choice></xs:sequence></xs:complexType>"
    "<xs:complexType name=\"AllT\"><xs:all><xs:element name=\"p\" type=\"xs:string\"/><xs:element name=\"q\" type=\"xs:string\" minOccurs=\"0\"/></xs:all></xs:complexType>"
    "<xs:complexType name=\"Base\"><xs:sequence><xs:element name=\"b1\" type=\"xs:string\"/></xs:sequence></xs:complexType>"
    "<xs:complexType name=\"Ext\"><xs:complexContent><xs:extension base=\"t:Base\"><xs:sequence><xs:element name=\"e1\" type=\"xs:string\"/></xs:sequence></xs:extension></xs:complexContent></xs:complexType>"
    "<xs:complexType name=\"Ext2\"><xs:complexContent><xs:extension base=\"t:Ext\"><xs:sequence><xs:element name=\"e2\" type=\"xs:string\"/></xs:sequence></xs:extension></xs:complexContent></xs:complexType>"
    "<xs:complexType name=\"ExtEmpty\"><xs:complexContent><xs:extension base=\"t:Ext2\"/></xs:complexContent></xs:complexType>"
    "<xs:complexType name=\"MixBase\" mixed=\"true\"><xs:sequence><xs:element name=\"m\" type=\"xs:string\" minOccurs=\"0\"/></xs:sequence></xs:complexType>"
    "<xs:complexType name=\"MixEmpty\"><xs:complexContent><xs:extension base=\"t:MixBase\"/></xs:complexContent></xs:complexType>"
    "<xs:complexType name=\"MixOnly\" mixed=\"true\"/>"
    "<xs:complexType name=\"Empty\"/>"
    "<xs:complexType name=\"EmptySeq\"><xs:sequence/></xs:complexType>"
    "<xs:complexType name=\"EmptyChoice\"><xs:choice minOccurs=\"0\"/></xs:complexType>"
    "<xs:complexType name=\"Zero\"><xs:sequence minOccurs=\"0\" maxOccurs=\"0\"><xs:element name=\"a\" type=\"xs:string\"/></xs:sequence></xs:complexType>"
    "<xs:complexType name=\"Simple\"><xs:simpleContent><xs:extension base=\"xs:int\"/></xs:simpleContent></xs:complexType>"
    "<xs:complexType name=\"SimpleExt\"><xs:complexContent><xs:extension base=\"t:Simple\"/></xs:complexContent></xs:complexType>"
    "<xs:group name=\"G\"><xs:sequence><xs:element name=\"g1\" type=\"xs:string\"/></xs:sequence></xs:group>"
    "<xs:complexType name=\"UsesG\"><xs:sequence><xs:group ref=\"t:G\" maxOccurs=\"2\"/></xs:sequence></xs:complexType>"
    "<xs:element name=\"Root\" type=\"t:Ext2\"/>");
  ck_assert_msg(compile_text(doc, &x, code) == 0, "code %s: %s", code, xsd_set_error(x.s));
  ck_assert_int_eq(type_of(&x, "Flat")->top->kind, BIM_PART_ELEM);
  ck_assert_int_eq(type_of(&x, "NestChoice")->top->nkids, 3);
  ck_assert_ptr_nonnull(type_of(&x, "Wild")->top);
  ck_assert_int_eq(type_of(&x, "AllT")->top->kind, BIM_PART_ALL);
  ck_assert_int_eq(type_of(&x, "Ext2")->top->kind, BIM_PART_SEQ);
  ck_assert_int_eq(type_of(&x, "MixEmpty")->mixed, 1);
  ck_assert_int_eq(type_of(&x, "MixOnly")->content, BIM_CONTENT_SEQ);
  ck_assert_int_eq(type_of(&x, "Empty")->content, BIM_CONTENT_NONE);
  ck_assert_int_eq(type_of(&x, "SimpleExt")->content, BIM_CONTENT_SIMPLE);
  xsd_set_free(x.s);
}
END_TEST

START_TEST(content_model_limits) {
  fails(WRAP("<xs:complexType name=\"T\"><xs:sequence maxOccurs=\"5000000000\"><xs:sequence maxOccurs=\"5000000000\"><xs:element name=\"a\" type=\"xs:string\"/></xs:sequence></xs:sequence></xs:complexType>"), "LS400288");
  fails(WRAP("<xs:complexType name=\"T\"><xs:choice><xs:element name=\"a\" type=\"xs:string\" minOccurs=\"0\" maxOccurs=\"0\"/><xs:element name=\"b\" type=\"xs:string\"/></xs:choice></xs:complexType>"), "LS400289");
}
END_TEST

static char *deep_doc(int n, int mode) {
  size_t cap = (size_t)n * 400 + 600;
  char *d = calloc(cap, 1);
  size_t at = 0;
  ck_assert_ptr_nonnull(d);
  at += (size_t)snprintf(d + at, cap - at, "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:t=\"urn:t\" targetNamespace=\"urn:t\" elementFormDefault=\"qualified\">");
  if (mode == 0) {
    for (int i = 0; i < n; i++) {
      if (i + 1 < n) at += (size_t)snprintf(d + at, cap - at, "<xs:group name=\"G%d\"><xs:sequence><xs:element name=\"x%d\" type=\"xs:string\" minOccurs=\"0\"/><xs:group ref=\"t:G%d\"/></xs:sequence></xs:group>", i, i, i + 1);
      else at += (size_t)snprintf(d + at, cap - at, "<xs:group name=\"G%d\"><xs:sequence><xs:element name=\"x%d\" type=\"xs:string\"/></xs:sequence></xs:group>", i, i);
    }
    at += (size_t)snprintf(d + at, cap - at, "<xs:complexType name=\"T\"><xs:sequence><xs:group ref=\"t:G0\"/></xs:sequence></xs:complexType><xs:element name=\"E\" type=\"t:T\"/>");
  } else {
    at += (size_t)snprintf(d + at, cap - at, "<xs:complexType name=\"C0\"><xs:sequence><xs:element name=\"c0\" type=\"xs:string\"/></xs:sequence></xs:complexType>");
    for (int i = 1; i < n; i++) {
      if (mode == 1)
        at += (size_t)snprintf(d + at, cap - at, "<xs:complexType name=\"C%d\"><xs:complexContent><xs:extension base=\"t:C%d\"><xs:sequence><xs:element name=\"c%d\" type=\"xs:string\"/></xs:sequence></xs:extension></xs:complexContent></xs:complexType>", i, i - 1, i);
      else
        at += (size_t)snprintf(d + at, cap - at, "<xs:complexType name=\"C%d\"><xs:complexContent><xs:extension base=\"t:C%d\"/></xs:complexContent></xs:complexType>", i, i - 1);
    }
    at += (size_t)snprintf(d + at, cap - at, "<xs:element name=\"E\" type=\"t:C%d\"/>", n - 1);
  }
  snprintf(d + at, cap - at, "</xs:schema>");
  return d;
}

START_TEST(content_depth_limits) {
  for (int mode = 0; mode < 3; mode++) {
    cx_t x;
    char code[16];
    char *d = deep_doc(300, mode);
    (void)compile_text(d, &x, code);
    xsd_set_free(x.s);
    free(d);
  }
}
END_TEST

START_TEST(content_compile_allocation_failures) {
  static const char doc[] = WRAP(
    "<xs:complexType name=\"Wild\"><xs:sequence><xs:choice><xs:any namespace=\"##other\" minOccurs=\"0\"/><xs:any namespace=\"urn:b urn:a ##local\"/><xs:choice><xs:element name=\"y\" type=\"xs:string\"/><xs:element name=\"z\" type=\"xs:string\"/></xs:choice></xs:choice></xs:sequence></xs:complexType>"
    "<xs:complexType name=\"Base\"><xs:sequence><xs:element name=\"b1\" type=\"xs:string\"/></xs:sequence></xs:complexType>"
    "<xs:complexType name=\"Ext\"><xs:complexContent><xs:extension base=\"t:Base\"><xs:sequence><xs:element name=\"e1\" type=\"xs:string\"/></xs:sequence></xs:extension></xs:complexContent></xs:complexType>"
    "<xs:complexType name=\"Ext2\"><xs:complexContent><xs:extension base=\"t:Ext\"><xs:sequence><xs:element name=\"e2\" type=\"xs:string\"/></xs:sequence></xs:extension></xs:complexContent></xs:complexType>"
    "<xs:complexType name=\"MixEmpty\"><xs:complexContent><xs:extension base=\"t:MixBase\"/></xs:complexContent></xs:complexType>"
    "<xs:complexType name=\"MixBase\" mixed=\"true\"><xs:sequence><xs:element name=\"m\" type=\"xs:string\" minOccurs=\"0\"/></xs:sequence></xs:complexType>"
    "<xs:element name=\"R1\" type=\"t:Ext2\"/><xs:element name=\"R2\" type=\"t:Wild\"/><xs:element name=\"R3\" type=\"t:MixEmpty\"/>");
  for (long n = 0;; n++) {
    xsd_compile_opts_t o = {1, 0, 0, NULL, 0, 0};
    const xsd_compiled_t *c = NULL;
    xsd_set_t *s = xsd_set_new();
    xml_doc_t *p = NULL;
    int hit;
    ck_assert_int_eq(xml_doc_parse(doc, strlen(doc), &p, NULL), 0);
    ck_assert_int_eq(xsd_set_add_doc(s, p, "c.xsd"), 0);
    ck_assert_int_eq(xsd_set_resolve(s), 0);
    alloc_fail_at(n);
    (void)xsd_compile(s, &o, &c);
    hit = alloc_fail_end();
    xsd_set_free(s);
    if (!hit) break;
  }
}
END_TEST

static char *chain_doc(int n, int elems) {
  size_t cap = (size_t)n * 400 + 1024;
  char *out = calloc(cap, 1);
  ck_assert_ptr_nonnull(out);
  strcat(out, "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:t=\"urn:t\" targetNamespace=\"urn:t\" elementFormDefault=\"qualified\">");
  if (elems) {
    strcat(out, "<xs:element name=\"H0\" type=\"xs:string\"/>");
    for (int i = 1; i < n; i++) {
      char one[160];
      snprintf(one, sizeof one, "<xs:element name=\"H%d\" type=\"xs:string\" substitutionGroup=\"t:H%d\"/>", i, i - 1);
      strcat(out, one);
    }
  } else {
    strcat(out, "<xs:complexType name=\"T0\"><xs:sequence/></xs:complexType>");
    for (int i = 1; i < n; i++) {
      char one[240];
      snprintf(one, sizeof one, "<xs:complexType name=\"T%d\"><xs:complexContent><xs:extension base=\"t:T%d\"/></xs:complexContent></xs:complexType>", i, i - 1);
      strcat(out, one);
    }
  }
  strcat(out, "</xs:schema>");
  return out;
}

START_TEST(content_codes_depth_and_hierarchy) {
  static const char shallow[] = WRAP(
    "<xs:attribute name=\"ga\" type=\"xs:string\"/><xs:attribute name=\"gb\" type=\"xs:int\"/>"
    "<xs:complexType name=\"A\"><xs:sequence/></xs:complexType>"
    "<xs:complexType name=\"B\"><xs:complexContent><xs:extension base=\"t:A\"/></xs:complexContent></xs:complexType>"
    "<xs:complexType name=\"C\"><xs:complexContent><xs:extension base=\"t:A\"/></xs:complexContent></xs:complexType>"
    "<xs:complexType name=\"D\"><xs:complexContent><xs:extension base=\"t:B\"/></xs:complexContent></xs:complexType>"
    "<xs:element name=\"Hd\" type=\"t:A\"/>"
    "<xs:element name=\"M1\" type=\"t:B\" substitutionGroup=\"t:Hd\"/>"
    "<xs:element name=\"M2\" type=\"t:C\" substitutionGroup=\"t:Hd\"/>"
    "<xs:element name=\"M3\" type=\"t:D\" substitutionGroup=\"t:M1\"/>");
  for (int hier = 0; hier < 2; hier++) {
    xsd_compile_opts_t o = {1, 0, hier, NULL, 0, 0};
    cx_t x;
    xml_doc_t *doc = NULL;
    x.s = xsd_set_new();
    ck_assert_ptr_nonnull(x.s);
    ck_assert_int_eq(xml_doc_parse(shallow, strlen(shallow), &doc, NULL), 0);
    ck_assert_int_eq(xsd_set_add_doc(x.s, doc, "c.xsd"), 0);
    ck_assert_int_eq(xsd_set_resolve(x.s), 0);
    ck_assert_int_eq(xsd_compile(x.s, &o, &x.c), 0);
    ck_assert_int_eq(x.c->schema.nattrs, 2);
    ck_assert_int_gt(x.c->schema.nsubs, 0);
    xsd_set_free(x.s);
  }
  for (int elems = 0; elems < 2; elems++) {
    char *text = chain_doc(300, elems);
    cx_t x;
    char code[16];
    ck_assert_int_ne(compile_text(text, &x, code), 0);
    ck_assert_str_eq(code, "LS400129");
    xsd_set_free(x.s);
    free(text);
  }
}
END_TEST

START_TEST(content_codes_allocation_failures) {
  static const char doc[] = WRAP(
    "<xs:attribute name=\"ga\" type=\"xs:string\"/><xs:attribute name=\"gb\" type=\"xs:int\"/>"
    "<xs:complexType name=\"A\"><xs:sequence/></xs:complexType>"
    "<xs:complexType name=\"B\"><xs:complexContent><xs:extension base=\"t:A\"/></xs:complexContent></xs:complexType>"
    "<xs:complexType name=\"C\"><xs:complexContent><xs:extension base=\"t:A\"/></xs:complexContent></xs:complexType>"
    "<xs:complexType name=\"D\"><xs:complexContent><xs:extension base=\"t:B\"/></xs:complexContent></xs:complexType>"
    "<xs:element name=\"Hd\" type=\"t:A\"/>"
    "<xs:element name=\"M1\" type=\"t:B\" substitutionGroup=\"t:Hd\"/>"
    "<xs:element name=\"M2\" type=\"t:C\" substitutionGroup=\"t:Hd\"/>"
    "<xs:element name=\"M3\" type=\"t:D\" substitutionGroup=\"t:M1\"/>");
  for (int hier = 0; hier < 2; hier++)
    for (long n = 0;; n++) {
      xsd_compile_opts_t o = {1, 0, hier, NULL, 0, 0};
      const xsd_compiled_t *c = NULL;
      xsd_set_t *s = xsd_set_new();
      xml_doc_t *p = NULL;
      int hit;
      ck_assert_int_eq(xml_doc_parse(doc, strlen(doc), &p, NULL), 0);
      ck_assert_int_eq(xsd_set_add_doc(s, p, "c.xsd"), 0);
      ck_assert_int_eq(xsd_set_resolve(s), 0);
      alloc_fail_at(n);
      (void)xsd_compile(s, &o, &c);
      hit = alloc_fail_end();
      xsd_set_free(s);
      if (!hit) break;
    }
}
END_TEST

#define M7 "urn:mpeg:mpeg7:schema:2001"

static const char m7_doc[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:m=\"" M7 "\" targetNamespace=\"" M7 "\" elementFormDefault=\"qualified\">"
  "<xs:simpleType name=\"basicTimePointType\"><xs:restriction base=\"xs:string\"/></xs:simpleType>"
  "<xs:simpleType name=\"basicDurationType\"><xs:restriction base=\"xs:string\"/></xs:simpleType>"
  "<xs:simpleType name=\"timePointType\"><xs:restriction base=\"m:basicTimePointType\"/></xs:simpleType>"
  "<xs:simpleType name=\"mediaTimePointType\"><xs:restriction base=\"m:basicTimePointType\"/></xs:simpleType>"
  "<xs:simpleType name=\"durationType\"><xs:restriction base=\"m:basicDurationType\"/></xs:simpleType>"
  "<xs:simpleType name=\"mediaDurationType\"><xs:restriction base=\"m:basicDurationType\"/></xs:simpleType>"
  "<xs:complexType name=\"HolderType\"><xs:sequence><xs:element name=\"T\" type=\"m:timePointType\"/><xs:element name=\"D\" type=\"m:mediaDurationType\"/></xs:sequence>"
  "<xs:attribute name=\"at\" type=\"m:basicTimePointType\"/></xs:complexType>"
  "<xs:element name=\"Holder\" type=\"m:HolderType\"/></xs:schema>";

START_TEST(content_compile_modes_and_tree_helpers) {
  static const unsigned compats[] = {0, BIM_COMPAT_M7_FLAGS};
  const xsd_compiled_t *c[2] = {NULL, NULL};
  xsd_set_t *sets[2];
  bim_node_t *root;
  bim_node_t *kid;
  const char *bad = NULL;
  const char *ns[2];
  int n;
  for (int i = 0; i < 2; i++) {
    xsd_compile_opts_t o = {0, 0, 0, NULL, 0, compats[i]};
    xml_doc_t *doc = NULL;
    sets[i] = xsd_set_new();
    ck_assert_ptr_nonnull(sets[i]);
    ck_assert_int_eq(xml_doc_parse(m7_doc, strlen(m7_doc), &doc, NULL), 0);
    ck_assert_int_eq(xsd_set_add_doc(sets[i], doc, "m.xsd"), 0);
    ck_assert_int_eq(xsd_set_resolve(sets[i]), 0);
    ck_assert_int_eq(xsd_compile(sets[i], &o, &c[i]), 0);
  }
  ck_assert_ptr_nonnull(xsd_compiled_type(c[0], M7, "HolderType"));
  ck_assert_ptr_null(xsd_compiled_type(c[0], "urn:nowhere", "HolderType"));
  ck_assert_ptr_null(xsd_compiled_type(c[0], M7, "Nope"));
  ck_assert_ptr_nonnull(xsd_compiled_elem(c[0], M7, "Holder"));
  ck_assert_ptr_null(xsd_compiled_elem(c[0], M7, "Nope"));
  ck_assert_int_ne(xsd_compiled_has_ns(c[0], M7), 0);
  ck_assert_int_eq(xsd_compiled_has_ns(c[0], "urn:nowhere"), 0);

  root = bim_node_new("Holder");
  root->ns = M7;
  ck_assert_int_eq(bim_node_add_attr_ns(root, XSI_NS, "type", strdup("m:HolderType")), 0);
  root->nsdecls = calloc(1, sizeof *root->nsdecls);
  ck_assert_ptr_nonnull(root->nsdecls);
  root->nsdecls[0].prefix = "m";
  root->nsdecls[0].uri = M7;
  root->nnsdecls = 1;
  kid = bim_node_add_new(root, "T", "x");
  kid->ns = M7;
  ck_assert_int_eq(bim_node_add_attr_ns(kid, XSI_NS, "type", strdup("m:timePointType")), 0);
  ck_assert_int_eq(bim_node_add_attr_ns(kid, XML_NS, "lang", strdup("en")), 0);
  ck_assert_int_eq(xsd_cast_bind(c, 2, root, &bad), 0);
  ck_assert_ptr_nonnull(root->cast);
  ck_assert_ptr_nonnull(kid->cast);
  bim_node_add_new(root, NULL, "text node");
  ck_assert_int_eq(xsd_tree_extra_ns(c[0], root, ns, 2), 0);

  kid = bim_node_add_new(root, "Foreign", NULL);
  kid->ns = "urn:foreign:a";
  ck_assert_int_eq(bim_node_add_attr_ns(kid, "urn:foreign:b", "x", strdup("1")), 0);
  ck_assert_int_eq(bim_node_add_attr_ns(kid, "urn:foreign:b", "y", strdup("1")), 0);
  ck_assert_int_eq(bim_node_add_attr_ns(kid, NULL, "z", strdup("1")), 0);
  ck_assert_int_eq(bim_node_add_attr_ns(kid, XSI_NS, "type", strdup("bad:Prefix:x")), 0);
  n = xsd_tree_extra_ns(c[0], root, ns, 2);
  ck_assert_int_eq(n, 2);
  ck_assert_int_eq(xsd_tree_extra_ns(c[0], root, ns, 1), -1);
  ck_assert_int_eq(xsd_tree_extra_ns(c[0], root, ns, 0), -1);

  ck_assert_int_eq(xsd_cast_bind(c, 2, root, &bad), -1);
  ck_assert_str_eq(bad, "bad:Prefix:x");
  bim_node_free(root);

  root = bim_node_new("Holder");
  ck_assert_int_eq(bim_node_add_attr_ns(root, XSI_NS, "type", strdup("m:Unknown")), 0);
  root->nsdecls = calloc(1, sizeof *root->nsdecls);
  ck_assert_ptr_nonnull(root->nsdecls);
  root->nsdecls[0].prefix = "m";
  root->nsdecls[0].uri = M7;
  root->nnsdecls = 1;
  ck_assert_int_eq(xsd_cast_bind(c, 2, root, &bad), -1);
  ck_assert_str_eq(bad, "m:Unknown");
  ck_assert_int_eq(xsd_cast_bind(c, 0, root, &bad), -1);
  bim_node_free(root);
  root = bim_node_new("Holder");
  ck_assert_int_eq(bim_node_add_attr_ns(root, XSI_NS, "type", strdup("nopfx:T")), 0);
  ck_assert_int_eq(xsd_cast_bind(c, 2, root, &bad), -1);
  bim_node_free(root);
  xsd_set_free(sets[0]);
  xsd_set_free(sets[1]);
}
END_TEST

START_TEST(content_compile_modes_allocation_failures) {
  for (int i = 0; i < 2; i++)
    for (long n = 0;; n++) {
      xsd_compile_opts_t o = {0, 0, 0, NULL, 0, i ? BIM_COMPAT_M7_FLAGS : 0};
      const xsd_compiled_t *c = NULL;
      xsd_set_t *s = xsd_set_new();
      xml_doc_t *p = NULL;
      int hit;
      ck_assert_int_eq(xml_doc_parse(m7_doc, strlen(m7_doc), &p, NULL), 0);
      ck_assert_int_eq(xsd_set_add_doc(s, p, "m.xsd"), 0);
      ck_assert_int_eq(xsd_set_resolve(s), 0);
      alloc_fail_at(n);
      (void)xsd_compile(s, &o, &c);
      hit = alloc_fail_end();
      xsd_set_free(s);
      if (!hit) break;
    }
}
END_TEST

#define SBC_DOC WRAP( \
  "<xs:group name=\"G\"><xs:sequence><xs:element name=\"g1\" type=\"xs:string\"/><xs:element name=\"g2\" type=\"xs:string\"/></xs:sequence></xs:group>" \
  "<xs:attributeGroup name=\"AG\"><xs:attribute name=\"ag1\" type=\"xs:string\"/><xs:anyAttribute namespace=\"##other\" processContents=\"skip\"/></xs:attributeGroup>" \
  "<xs:complexType name=\"A\"><xs:sequence><xs:element name=\"a\" type=\"xs:string\"/><xs:any namespace=\"##other\" processContents=\"lax\" minOccurs=\"0\"/></xs:sequence>" \
  "<xs:attribute name=\"z\" type=\"xs:string\"/><xs:attribute name=\"a\" type=\"xs:int\" use=\"required\"/><xs:attribute name=\"m\" type=\"xs:string\" use=\"prohibited\"/>" \
  "<xs:attribute name=\"k\" type=\"xs:string\"/><xs:anyAttribute processContents=\"lax\"/></xs:complexType>" \
  "<xs:complexType name=\"B\"><xs:complexContent><xs:extension base=\"t:A\"><xs:choice><xs:choice><xs:element name=\"x\" type=\"xs:string\"/><xs:element name=\"y\" type=\"xs:int\"/></xs:choice>" \
  "<xs:element name=\"w\" type=\"xs:string\"/><xs:group ref=\"t:G\"/><xs:choice><xs:element name=\"u\" type=\"xs:string\"/><xs:choice><xs:element name=\"v\" type=\"xs:string\"/></xs:choice></xs:choice></xs:choice>" \
  "<xs:attribute name=\"b\" type=\"xs:string\"/><xs:attributeGroup ref=\"t:AG\"/><xs:anyAttribute namespace=\"urn:q urn:r\" processContents=\"strict\"/></xs:extension></xs:complexContent></xs:complexType>" \
  "<xs:complexType name=\"C\"><xs:complexContent><xs:restriction base=\"t:B\"><xs:sequence><xs:element name=\"a\" type=\"xs:string\"/></xs:sequence>" \
  "<xs:attribute name=\"b\" type=\"xs:string\" use=\"prohibited\"/></xs:restriction></xs:complexContent></xs:complexType>" \
  "<xs:complexType name=\"D\"><xs:simpleContent><xs:extension base=\"xs:string\"><xs:attribute name=\"q\" type=\"xs:string\"/></xs:extension></xs:simpleContent></xs:complexType>" \
  "<xs:complexType name=\"E\"><xs:simpleContent><xs:extension base=\"t:D\"><xs:attribute name=\"r\" type=\"xs:string\"/><xs:attribute name=\"a\" type=\"xs:string\"/></xs:extension></xs:simpleContent></xs:complexType>" \
  "<xs:complexType name=\"F\"><xs:complexContent><xs:extension base=\"t:A\"><xs:anyAttribute namespace=\"##local\" processContents=\"skip\"/></xs:extension></xs:complexContent></xs:complexType>" \
  "<xs:complexType name=\"H\" mixed=\"true\"><xs:all><xs:element name=\"h1\" type=\"xs:string\" minOccurs=\"0\"/><xs:element name=\"h2\" type=\"xs:int\" minOccurs=\"0\"/></xs:all></xs:complexType>" \
  "<xs:complexType name=\"Em\"/><xs:complexType name=\"EmD\"><xs:complexContent><xs:extension base=\"t:Em\"><xs:attribute name=\"e\" type=\"xs:string\"/></xs:extension></xs:complexContent></xs:complexType>" \
  "<xs:element name=\"R\" type=\"t:B\"/><xs:element name=\"R2\" type=\"t:C\"/><xs:element name=\"R3\" type=\"t:E\"/>")

START_TEST(content_sbc_lists_are_built_in_signature_order) {
  cx_t x;
  const bim_type_t *b;
  const bim_type_t *e;
  const bim_type_t *c;
  char code[16];
  ck_assert_int_eq(compile_text(SBC_DOC, &x, code), 0);
  b = type_of(&x, "B");
  e = type_of(&x, "E");
  c = type_of(&x, "C");
  ck_assert_int_gt(b->nsbc, 4);
  ck_assert_int_gt(e->nsbc, 2);
  ck_assert_int_gt(c->nsbc, 0);
  xsd_set_free(x.s);
}
END_TEST

START_TEST(content_sbc_rejects_deep_derivation) {
  char *text = chain_doc(300, 0);
  cx_t x;
  char code[16];
  ck_assert_int_ne(compile_text(text, &x, code), 0);
  ck_assert_str_eq(code, "LS400129");
  xsd_set_free(x.s);
  free(text);
}
END_TEST

START_TEST(content_sbc_survives_allocation_failures) {
  for (int mpegb = 0; mpegb < 2; mpegb++)
    for (long n = 0;; n++) {
      xsd_compile_opts_t o = {mpegb, 0, 0, NULL, 0, 0};
      const xsd_compiled_t *c = NULL;
      xsd_set_t *s = xsd_set_new();
      xml_doc_t *p = NULL;
      int hit;
      ck_assert_int_eq(xml_doc_parse(SBC_DOC, strlen(SBC_DOC), &p, NULL), 0);
      ck_assert_int_eq(xsd_set_add_doc(s, p, "c.xsd"), 0);
      ck_assert_int_eq(xsd_set_resolve(s), 0);
      alloc_fail_at(n);
      (void)xsd_compile(s, &o, &c);
      hit = alloc_fail_end();
      xsd_set_free(s);
      if (!hit) break;
    }
}
END_TEST

START_TEST(content_compile_labels_its_failures) {
  fails(WRAP("<xs:simpleType name=\"Big\"><xs:restriction base=\"xs:integer\"><xs:minInclusive value=\"-99999999999999999999\"/></xs:restriction></xs:simpleType>"
             "<xs:element name=\"e\" type=\"t:Big\"/>"), "LS400283");
  fails(WRAP("<xs:simpleType name=\"Big\"><xs:restriction base=\"xs:integer\"><xs:minExclusive value=\"99999999999999999999\"/></xs:restriction></xs:simpleType>"
             "<xs:element name=\"e\" type=\"t:Big\"/>"), "LS400283");
  fails(WRAP("<xs:simpleType name=\"Big\"><xs:restriction base=\"xs:integer\"><xs:maxExclusive value=\"-99999999999999999999\"/></xs:restriction></xs:simpleType>"
             "<xs:element name=\"e\" type=\"t:Big\"/>"), "LS400283");
  fails(WRAP("<xs:simpleType name=\"Empty\"><xs:restriction base=\"xs:integer\"><xs:minInclusive value=\"5\"/><xs:maxInclusive value=\"1\"/></xs:restriction></xs:simpleType>"
             "<xs:element name=\"e\" type=\"t:Empty\"/>"), "LS400284");
  fails(WRAP("<xs:simpleType name=\"L\"><xs:restriction><xs:simpleType><xs:list itemType=\"xs:int\"/></xs:simpleType><xs:length value=\"-1\"/></xs:restriction></xs:simpleType>"
             "<xs:element name=\"e\" type=\"t:L\"/>"), "LS400285");
  fails(WRAP("<xs:simpleType name=\"L\"><xs:restriction><xs:simpleType><xs:list itemType=\"xs:int\"/></xs:simpleType><xs:minLength value=\"5\"/><xs:maxLength value=\"2\"/></xs:restriction></xs:simpleType>"
             "<xs:element name=\"e\" type=\"t:L\"/>"), "LS400285");
  fails(WRAP("<xs:complexType name=\"C\"><xs:choice><xs:element name=\"a\" type=\"xs:string\" minOccurs=\"0\" maxOccurs=\"0\"/><xs:element name=\"b\" type=\"xs:string\"/></xs:choice></xs:complexType>"
             "<xs:element name=\"e\" type=\"t:C\"/>"), "LS400289");
  fails(WRAP("<xs:complexType name=\"C\"><xs:sequence maxOccurs=\"4294967296\"><xs:element name=\"a\" type=\"xs:string\" maxOccurs=\"4294967296\"/></xs:sequence></xs:complexType>"
             "<xs:element name=\"e\" type=\"t:C\"/>"), "LS400288");
  fails(WRAP("<xs:simpleType name=\"P\"><xs:restriction base=\"xs:string\"><xs:pattern value=\"(?&lt;=a)b\"/></xs:restriction></xs:simpleType><xs:element name=\"e\" type=\"t:P\"/>"), "LS400290");
}
END_TEST

START_TEST(content_compile_handles_many_types) {
  char *text = chain_doc(120, 0);
  cx_t x;
  char code[16];
  ck_assert_int_eq(compile_text(text, &x, code), 0);
  xsd_set_free(x.s);
  free(text);
  text = chain_doc(300, 1);
  ck_assert_int_ne(compile_text(text, &x, code), 0);
  xsd_set_free(x.s);
  free(text);
}
END_TEST

static Suite *content_suite(void) {
  Suite *s = suite_create("bim_xsd_content");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 120);
  tcase_add_test(tc, content_model_rewrites);
  tcase_add_test(tc, content_model_limits);
  tcase_add_test(tc, content_depth_limits);
  tcase_add_test(tc, content_compile_allocation_failures);
  tcase_add_test(tc, content_codes_depth_and_hierarchy);
  tcase_add_test(tc, content_codes_allocation_failures);
  tcase_add_test(tc, content_compile_modes_and_tree_helpers);
  tcase_add_test(tc, content_compile_modes_allocation_failures);
  tcase_add_test(tc, content_sbc_lists_are_built_in_signature_order);
  tcase_add_test(tc, content_sbc_rejects_deep_derivation);
  tcase_add_test(tc, content_sbc_survives_allocation_failures);
  tcase_add_test(tc, content_compile_labels_its_failures);
  tcase_add_test(tc, content_compile_handles_many_types);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(content_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
