/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../alloc_fail.h"
#include "lib/bim/bcgip.h"
#include "lib/bim/container.h"
#include "lib/bim/dvbtva.h"
#include "lib/bim/tvainit.h"
#include "lib/helper/xml_util.h"
#include "../../fx_bim.h"

static void write_init(const bim_tvainit_t *i, bitwriter_t *bw) {
  bim_err_t err;
  bitwriter_init(bw);
  if (bim_tvainit_write(i, bw, &err)) ck_abort_msg("%s %s", err.code, err.msg);
}

/* TS 102 323 table 54: EncodingVersion F0, no flags, reserved bits set, DecoderInitptr 5, no BufferSize, UTF-8 */
START_TEST(tvainit_dvb_minimal_bytes) {
  static const unsigned char want[] = {0xF0, 0x7F, 0x05, 0x3F, 0x01};
  bim_tvainit_t i = {.encoding_version = BIM_TVAINIT_DVB, .char_encoding = 1};
  bitwriter_t bw;
  size_t n;
  const unsigned char *p;
  write_init(&i, &bw);
  p = bitwriter_data(&bw, &n);
  ck_assert_uint_eq(n, sizeof want);
  ck_assert_mem_eq(p, want, n);
  bitwriter_free(&bw);
}
END_TEST

START_TEST(tvainit_round_trips_all_fields) {
  bim_tvainit_t i = {.encoding_version = BIM_TVAINIT_TVA, .indexing = 1, .indexing_version = 1, .has_buffer_size = 1, .buffer_size = 0xABCDEF, .position_code = 1, .char_encoding = 0xE5};
  bim_tvainit_t g;
  bim_err_t err;
  bitwriter_t bw;
  const unsigned char *p;
  size_t n;
  size_t used;
  write_init(&i, &bw);
  p = bitwriter_data(&bw, &n);
  ck_assert_uint_eq(n, 9);
  ck_assert_int_eq(bim_tvainit_parse(p, n, &g, &used, &err), 0);
  ck_assert_uint_eq(used, 9);
  ck_assert_mem_eq(&g, &i, sizeof i);
  ck_assert_uint_eq(bim_tvainit_buffer_size(&g), 0xABCDEF);
  g.has_buffer_size = 0;
  ck_assert_uint_eq(bim_tvainit_buffer_size(&g), 1000);
  bitwriter_free(&bw);
}
END_TEST

/* DecoderInitptr skips reserved space, truncation and reserved values are refused */
START_TEST(tvainit_parse_rules) {
  static const unsigned char padded[] = {0xF0, 0x00, 0x07, 0x00, 0x01, 0x00, 0x00, 0xAA};
  static const unsigned char dvb_pos[] = {0xF0, 0x00, 0x05, 0x40, 0x01};
  static const unsigned char bad_version[] = {0x02, 0x00, 0x05, 0x00, 0x01};
  static const unsigned char bad_enc[] = {0xF0, 0x00, 0x05, 0x00, 0x07};
  static const unsigned char bad_ptr[] = {0xF0, 0x00, 0x02, 0x00, 0x01};
  static const unsigned char bad_idx[] = {0xF0, 0x80, 0x06, 0x00, 0x01, 0x02};
  bim_tvainit_t g;
  bim_err_t err;
  size_t used;
  ck_assert_int_eq(bim_tvainit_parse(padded, sizeof padded, &g, &used, &err), 0);
  ck_assert_uint_eq(used, 7);
  ck_assert_int_eq(bim_tvainit_parse(padded, 3, &g, &used, &err), -1);
  ck_assert_str_eq(err.code, BIM_TVAINIT_TRUNC);
  ck_assert_int_eq(bim_tvainit_parse(dvb_pos, sizeof dvb_pos, &g, &used, &err), -1);
  ck_assert_str_eq(err.code, BIM_TVAINIT_RANGE);
  ck_assert_int_eq(bim_tvainit_parse(bad_version, sizeof bad_version, &g, &used, &err), -1);
  ck_assert_str_eq(err.code, BIM_TVAINIT_RANGE);
  ck_assert_int_eq(bim_tvainit_parse(bad_enc, sizeof bad_enc, &g, &used, &err), -1);
  ck_assert_str_eq(err.code, BIM_TVAINIT_RANGE);
  ck_assert_int_eq(bim_tvainit_parse(bad_ptr, sizeof bad_ptr, &g, &used, &err), -1);
  ck_assert_str_eq(err.code, BIM_TVAINIT_RANGE);
  ck_assert_int_eq(bim_tvainit_parse(bad_idx, sizeof bad_idx, &g, &used, &err), -1);
  ck_assert_str_eq(err.code, BIM_TVAINIT_RANGE);
}
END_TEST

START_TEST(tvainit_write_refuses_invalid) {
  bim_tvainit_t i = {.encoding_version = BIM_TVAINIT_DVB, .position_code = 1};
  bim_err_t err;
  bitwriter_t bw;
  bitwriter_init(&bw);
  ck_assert_int_eq(bim_tvainit_write(&i, &bw, &err), -1);
  ck_assert_str_eq(err.code, BIM_TVAINIT_RANGE);
  i.position_code = 0;
  i.indexing = 1;
  i.indexing_version = 0;
  ck_assert_int_eq(bim_tvainit_write(&i, &bw, &err), -1);
  i.encoding_version = 0x55;
  i.indexing = 0;
  ck_assert_int_eq(bim_tvainit_write(&i, &bw, &err), -1);
  bitwriter_free(&bw);
}
END_TEST

#define TVA "urn:tva:metadata:2004"
#define EXT "urn:tva:metadata:extended:test"

static const char schema_tva[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns=\"" TVA "\" xmlns:x=\"" EXT "\" targetNamespace=\"" TVA "\" elementFormDefault=\"qualified\">"
  "<xs:import namespace=\"" EXT "\"/>"
  "<xs:simpleType name=\"termReferenceType\"><xs:restriction base=\"xs:string\"/></xs:simpleType>"
  "<xs:complexType name=\"ProgramInformationType\"><xs:sequence><xs:element name=\"Title\" type=\"xs:string\" minOccurs=\"0\"/>"
  "<xs:element name=\"Genre\" type=\"termReferenceType\" minOccurs=\"0\"/></xs:sequence>"
  "<xs:attribute name=\"programId\" type=\"xs:string\" use=\"required\"/></xs:complexType>"
  "<xs:complexType name=\"ProgramInformationTableType\"><xs:sequence><xs:element name=\"ProgramInformation\" type=\"ProgramInformationType\" minOccurs=\"0\" maxOccurs=\"unbounded\"/></xs:sequence></xs:complexType>"
  "<xs:complexType name=\"ScheduleEventType\"><xs:sequence><xs:element name=\"PublishedDuration\" type=\"xs:duration\" minOccurs=\"0\"/></xs:sequence>"
  "<xs:attribute name=\"crid\" type=\"xs:anyURI\"/><xs:attribute name=\"start\" type=\"xs:dateTime\"/><xs:attribute name=\"url\" type=\"xs:anyURI\"/></xs:complexType>"
  "<xs:complexType name=\"ScheduleType\"><xs:sequence><xs:element name=\"ScheduleEvent\" type=\"ScheduleEventType\" minOccurs=\"0\" maxOccurs=\"unbounded\"/></xs:sequence>"
  "<xs:attribute name=\"serviceIDRef\" type=\"xs:string\"/></xs:complexType>"
  "<xs:complexType name=\"ProgramLocationTableType\"><xs:sequence><xs:element name=\"Schedule\" type=\"ScheduleType\" minOccurs=\"0\" maxOccurs=\"unbounded\"/></xs:sequence></xs:complexType>"
  "<xs:complexType name=\"ProgramDescriptionType\"><xs:sequence><xs:element name=\"ProgramInformationTable\" type=\"ProgramInformationTableType\" minOccurs=\"0\"/>"
  "<xs:element name=\"ProgramLocationTable\" type=\"ProgramLocationTableType\" minOccurs=\"0\"/></xs:sequence></xs:complexType>"
  "<xs:complexType name=\"TVAMainType\"><xs:sequence><xs:element name=\"ProgramDescription\" type=\"ProgramDescriptionType\" minOccurs=\"0\"/>"
  "<xs:element ref=\"x:PackageTable\" minOccurs=\"0\"/></xs:sequence><xs:attribute name=\"publisher\" type=\"xs:string\"/></xs:complexType>"
  "<xs:element name=\"TVAMain\" type=\"TVAMainType\"/></xs:schema>";

static const char schema_ext[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns=\"" EXT "\" targetNamespace=\"" EXT "\" elementFormDefault=\"qualified\">"
  "<xs:complexType name=\"PackageType\"><xs:attribute name=\"id\" type=\"xs:string\"/></xs:complexType>"
  "<xs:complexType name=\"PackageTableType\"><xs:sequence><xs:element name=\"Package\" type=\"PackageType\" minOccurs=\"0\" maxOccurs=\"unbounded\"/></xs:sequence></xs:complexType>"
  "<xs:element name=\"PackageTable\" type=\"PackageTableType\"/></xs:schema>";

typedef struct {
  xsd_set_t *set;
  const xsd_compiled_t *c;
  bim_tvans_t ns;
} tv_t;

#define ROOTS(t) dvbtva_tva_root, &(dvbtva_roots_t){(t).c, &(t).ns}

static void tv_init(tv_t *t) {
  xsd_codec_map_t codecs[DVBTVA_CODECS];
  xsd_compile_opts_t o = {0, 0, 0, codecs, 0, 0};
  const char *docs[] = {schema_tva, schema_ext};
  t->ns = (bim_tvans_t){TVA, EXT, NULL};
  o.ncodecs = dvbtva_codecs(&t->ns, codecs);
  t->set = xsd_set_new();
  ck_assert_ptr_nonnull(t->set);
  for (int i = 0; i < 2; i++) {
    xml_doc_t *doc = NULL;
    ck_assert_int_eq(xml_doc_parse(docs[i], strlen(docs[i]), &doc, NULL), 0);
    if (xsd_set_add_doc(t->set, doc, "t.xsd")) ck_abort_msg("%s %s", xsd_set_error_code(t->set), xsd_set_error(t->set));
  }
  if (xsd_set_resolve(t->set) || xsd_compile(t->set, &o, &t->c)) ck_abort_msg("%s %s", xsd_set_error_code(t->set), xsd_set_error(t->set));
}

/* TS 102 323 table 58, TS 102 822-3-2 table 7 */
START_TEST(dvbtva_context_paths_resolve) {
  tv_t t;
  bim_tvaroot_t r;
  tv_init(&t);
  ck_assert_int_eq(bim_tvafrag_root(t.c, &t.ns, 0x0001, &r), 0);
  ck_assert_str_eq(r.name, "ProgramInformation");
  ck_assert_ptr_eq(r.type, xsd_compiled_type(t.c, TVA, "ProgramInformationType"));
  ck_assert_int_eq(bim_tvafrag_root(t.c, &t.ns, 0x000E, &r), 0);
  ck_assert_str_eq(r.name, "TVAMain");
  ck_assert_int_eq(bim_tvafrag_root(t.c, &t.ns, 0x0012, &r), 0);
  ck_assert_str_eq(r.name, "Package");
  ck_assert_str_eq(r.ns, EXT);
  ck_assert_ptr_eq(r.type, xsd_compiled_type(t.c, EXT, "PackageType"));
  ck_assert_int_eq(bim_tvafrag_root(t.c, &t.ns, 0x0000, &r), -1);
  ck_assert_int_eq(bim_tvafrag_root(t.c, &t.ns, 0x0023, &r), -1);
  ck_assert_int_eq(bim_tvafrag_root(t.c, &t.ns, 0x0003, &r), -1);
  ck_assert_int_eq(bim_tvafrag_root(t.c, &t.ns, 0x0017, &r), -1);
  xsd_set_free(t.set);
}
END_TEST

static bim_node_t *node_text(bim_node_t *parent, const char *name, const char *text) {
  bim_node_t *n = bim_node_add_new(parent, name, text);
  ck_assert_ptr_nonnull(n);
  n->ns = TVA;
  return n;
}

static bim_node_t *program(const char *id, const char *title) {
  bim_node_t *p = bim_node_new("ProgramInformation");
  ck_assert_ptr_nonnull(p);
  p->ns = TVA;
  ck_assert_int_eq(bim_node_add_attr_ns(p, NULL, "programId", strdup(id)), 0);
  node_text(p, "Title", title);
  node_text(p, "Genre", "urn:tva:metadata:cs:ContentCS:2004:3.1");
  return p;
}

/* table 57 DVBBiMAccessUnit: FUULength, DVBContextPath, payload, strings in the string repository */
START_TEST(dvbtva_access_unit_round_trips) {
  tv_t t;
  bim_encode_opts_t eo;
  bim_decode_opts_t dopts;
  dvbtva_fuu_t in[3];
  dvbtva_fuu_t *got;
  bim_fragref_t refs[3];
  bitwriter_t bw;
  strrepo_writer_t sw;
  bitreader_t br;
  strrepo_reader_t sr;
  bim_err_t err;
  const unsigned char *b;
  const unsigned char *s;
  size_t bl;
  size_t sl;
  size_t n;
  size_t sizes[2];
  bim_node_t *sched;
  bim_node_t *ev;
  tv_init(&t);
  memset(&eo, 0, sizeof eo);
  eo.schemas = &t.c->schema;
  eo.nschemas = 1;
  eo.lax_schema = -1;
  eo.frag_format = 1;
  sched = bim_node_new("Schedule");
  sched->ns = TVA;
  ck_assert_int_eq(bim_node_add_attr_ns(sched, NULL, "serviceIDRef", strdup("dvb://1.2.3")), 0);
  ev = node_text(sched, "ScheduleEvent", NULL);
  ck_assert_int_eq(bim_node_add_attr_ns(ev, NULL, "crid", strdup("crid://example.com/a")), 0);
  ck_assert_int_eq(bim_node_add_attr_ns(ev, NULL, "start", strdup("2024-05-01T10:00:00Z")), 0);
  ck_assert_int_eq(bim_node_add_attr_ns(ev, NULL, "url", strdup("dvb://233a.1004.1b8f.1&2$ff;3f4~20240501T100000Z--PT01H30M00S/p/q")), 0);
  node_text(ev, "PublishedDuration", "PT1H30M");
  ev = node_text(sched, "ScheduleEvent", NULL);
  ck_assert_int_eq(bim_node_add_attr_ns(ev, NULL, "crid", strdup("crid://example.com/b")), 0);
  ck_assert_int_eq(bim_node_add_attr_ns(ev, NULL, "start", strdup("2024-05-01T11:30:00Z")), 0);
  ck_assert_int_eq(bim_node_add_attr_ns(ev, NULL, "url", strdup("dvb://233a.1004.1b8f;;12~20240501T113000Z--PT00H30M00S")), 0);
  node_text(ev, "PublishedDuration", "PT30M");
  in[0] = (dvbtva_fuu_t){0x0001, program("crid://example.com/a", "First")};
  in[1] = (dvbtva_fuu_t){0x0005, sched};
  in[2] = (dvbtva_fuu_t){0x0001, program("crid://example.com/b", "Second")};
  for (int opt = 0; opt < 2; opt++) {
    eo.dvb_locator = eo.dvb_duration = eo.dvb_pubtime = opt;
    bitwriter_init(&bw);
    ck_assert_int_eq(strrepo_writer_init(&sw), 0);
    if (dvbtva_au_write(ROOTS(t), &eo, in, 3, &bw, &sw, refs, &err)) ck_abort_msg("%s %s", err.code, err.msg);
    ck_assert_uint_eq(refs[0].au_off, 1);
    ck_assert_uint_gt(refs[1].au_off, refs[0].au_off);
    ck_assert_uint_gt(refs[1].str_off, refs[0].str_off);
    b = bitwriter_data(&bw, &bl);
    s = strrepo_writer_data(&sw, &sl);
    ck_assert_uint_eq(b[0], 3);
    memset(&dopts, 0, sizeof dopts);
    dopts.schemas = &t.c->schema;
    dopts.nschemas = 1;
    dopts.lax_schema = -1;
    for (int use_refs = 0; use_refs < 2; use_refs++) {
      bitreader_init(&br, b, bl);
      ck_assert_int_eq(strrepo_reader_init(&sr, s, sl), 0);
      if (dvbtva_au_parse(ROOTS(t), &dopts, &br, &sr, use_refs ? refs : NULL, use_refs ? 3 : 0, &got, &n, &err)) ck_abort_msg("%s %s", err.code, err.msg);
      ck_assert_uint_eq(n, 3);
      for (size_t i = 0; i < 3; i++) {
        ck_assert_uint_eq(got[i].code, in[i].code);
        ck_assert_msg(tree_same(in[i].root, got[i].root), "fragment %zu differs", i);
        ck_assert_str_eq(got[i].root->ns, TVA);
      }
      dvbtva_au_free(got, n);
    }
    sizes[opt] = bl + sl;
    bitwriter_free(&bw);
    strrepo_writer_free(&sw);
  }
  ck_assert_uint_lt(sizes[1], sizes[0]);
  in[1].code = 0x00F5;
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(dvbtva_au_write(ROOTS(t), &eo, in, 3, &bw, &sw, NULL, &err), -1);
  ck_assert_str_eq(err.code, BIM_DVBTVA_ERR);
  bim_node_free(in[0].root);
  bim_node_free(in[1].root);
  bim_node_free(in[2].root);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  xsd_set_free(t.set);
}
END_TEST

static int child_codec(const bim_type_t *t, const char *name) {
  for (int i = 0; i < t->nsbc; i++)
    if (t->sbc[i].kind == BIM_SBC_ELEM && !strcmp(t->sbc[i].name, name)) return (int)t->sbc[i].type->simple->codec;
  return -1;
}

/* 9.4.3: default codecs by type, 9.4.2.2: DecoderInit with SchemaURI[0] */
START_TEST(dvbtva_default_codecs_and_decoderinit) {
  static const unsigned char want[] = {0x00, 0x1F, 0x01, 0x15, 'u', 'r', 'n', ':', 't', 'v', 'a', ':', 'm', 'e', 't', 'a', 'd', 'a', 't', 'a', ':', '2', '0', '0', '4', 0x00, 0x00, 0x00};
  tv_t t;
  const bim_simple_t *s;
  bim_decoderinit_t di;
  bim_decoderinit_t g;
  bitwriter_t bw;
  bim_err_t err;
  const unsigned char *p;
  size_t n;
  size_t used;
  tv_init(&t);
  ck_assert_int_eq(xsd_compiled_type(t.c, TVA, "ProgramInformationType")->attrs[0].simple->codec, BIM_STRING);
  s = xsd_compiled_type(t.c, TVA, "ScheduleEventType")->attrs[0].simple;
  ck_assert_int_eq(s->codec, BIM_LOCATOR);
  ck_assert_int_eq(xsd_compiled_type(t.c, TVA, "ScheduleEventType")->attrs[1].simple->codec, BIM_DATETIME);
  ck_assert_int_eq(child_codec(xsd_compiled_type(t.c, TVA, "ScheduleEventType"), "PublishedDuration"), BIM_DURATION);
  ck_assert_int_eq(child_codec(xsd_compiled_type(t.c, TVA, "ProgramInformationType"), "Genre"), BIM_TERMREF);
  ck_assert_int_eq(bim_tvainit_decoderinit(TVA, NULL, 0, &di), 0);
  bitwriter_init(&bw);
  ck_assert_int_eq(bim_decoderinit_write(&di, NULL, NULL, &bw, &err), 0);
  p = bitwriter_data(&bw, &n);
  ck_assert_uint_eq(n, sizeof want);
  ck_assert_mem_eq(p, want, n);
  ck_assert_int_eq(bim_decoderinit_parse(p, n, 0, NULL, NULL, &g, &used, &err), 0);
  ck_assert_str_eq(g.schemas[0].uri, TVA);
  bim_decoderinit_free(&g);
  bitwriter_free(&bw);
  bim_tvainit_decoderinit_free(&di);
  xsd_set_free(t.set);
}
END_TEST

static const char *const terms[] = {"3", "3.1", "3.1.1", "3.2"};

static int cs_rank(void *ctx, const char *uri, const char *term, uint64_t *rank) {
  (void)ctx;
  if (strstr(uri, "ContentCS") == NULL) return -1;
  for (uint64_t k = 0; k < sizeof terms / sizeof *terms; k++)
    if (!strcmp(terms[k], term)) {
      *rank = k;
      return 0;
    }
  return -1;
}

static int cs_term(void *ctx, const char *uri, uint64_t rank, char *out, size_t cap) {
  (void)ctx;
  if (strstr(uri, "ContentCS") == NULL || rank >= sizeof terms / sizeof *terms || strlen(terms[rank]) >= cap) return -1;
  strcpy(out, terms[rank]);
  return 0;
}

static void term_round(const dvb_cs_t *cs, const char *href, unsigned drop_check) {
  bitwriter_t bw;
  strrepo_writer_t sw;
  bitreader_t br;
  strrepo_reader_t sr;
  const unsigned char *b;
  const unsigned char *s;
  size_t bl;
  size_t sl;
  char out[256];
  (void)drop_check;
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(dvb_controlledterm_encode_cs(&bw, &sw, cs, href), 0);
  b = bitwriter_data(&bw, &bl);
  s = strrepo_writer_data(&sw, &sl);
  bitreader_init(&br, b, bl);
  ck_assert_int_eq(strrepo_reader_init(&sr, s, sl), 0);
  ck_assert_int_eq(dvb_controlledterm_decode_cs(&br, &sr, cs, out, sizeof out), 0);
  ck_assert_str_eq(out, href);
  ck_assert_uint_eq(bl, drop_check ? 3 : bl);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}

/* TS 102 323 table 69: scheme id 7 bits, termID vluimsbf8 where the scheme is of table 70 and the term known, else string */
START_TEST(dvbtva_controlled_terms) {
  static const char *const list[] = {"urn:example:cs:Mine:2024"};
  dvb_cs_t cs = {cs_rank, cs_term, NULL, list, 1, 1};
  bitwriter_t bw;
  bitreader_t br;
  strrepo_writer_t sw;
  strrepo_reader_t sr;
  const unsigned char *b;
  const unsigned char *s;
  size_t bl;
  size_t sl;
  char out[256];
  term_round(&cs, "urn:tva:metadata:cs:ContentCS:2004:3.1.1", 1);
  term_round(&cs, "urn:tva:metadata:cs:ContentCS:2004:9.9", 0);
  term_round(&cs, "urn:example:cs:Other:2004:1", 0);
  cs.compact = 0;
  term_round(&cs, "urn:tva:metadata:cs:ContentCS:2004:3.1", 0);
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(bitwriter_put(&bw, 3, 2) || bitwriter_put_vluimsbf5(&bw, 2) || bitwriter_put_vluimsbf8(&bw, 4) || bitwriter_put_vluimsbf8(&bw, 3), 0);
  b = bitwriter_data(&bw, &bl);
  s = strrepo_writer_data(&sw, &sl);
  bitreader_init(&br, b, bl);
  ck_assert_int_eq(strrepo_reader_init(&sr, s, sl), 0);
  ck_assert_int_eq(dvb_controlledterm_decode_cs(&br, &sr, &cs, out, sizeof out), 0);
  ck_assert_str_eq(out, "urn:tva:metadata:cs:ContentCS:2005:3.2");
  bitwriter_free(&bw);
  bitwriter_init(&bw);
  ck_assert_int_eq(bitwriter_put(&bw, 3, 2) || bitwriter_put_vluimsbf5(&bw, 0) || bitwriter_put_vluimsbf8(&bw, 0) || bitwriter_put_vluimsbf8(&bw, 0), 0);
  b = bitwriter_data(&bw, &bl);
  bitreader_init(&br, b, bl);
  ck_assert_int_eq(dvb_controlledterm_decode_cs(&br, &sr, &cs, out, sizeof out), -1);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
}
END_TEST

/* TS 102 323 9.3: Encapsulation structure with DVB BiM fragment references, binary and string repositories */
START_TEST(dvbtva_container_carries_access_unit) {
  tv_t t;
  bim_encode_opts_t eo;
  bim_decode_opts_t dopts;
  dvbtva_fuu_t in[2];
  dvbtva_fuu_t *got;
  bim_fragref_t refs[2];
  bim_fragref_t *prefs = NULL;
  bitwriter_t bw;
  strrepo_writer_t sw;
  bitreader_t br;
  strrepo_reader_t sr;
  bim_err_t err;
  unsigned char *cont = NULL;
  size_t cl;
  const unsigned char *au;
  const unsigned char *str;
  size_t aul;
  size_t srl;
  int np = 0;
  unsigned fmt;
  size_t n;
  tv_init(&t);
  memset(&eo, 0, sizeof eo);
  eo.schemas = &t.c->schema;
  eo.nschemas = 1;
  eo.lax_schema = -1;
  eo.frag_format = 1;
  in[0] = (dvbtva_fuu_t){0x0001, program("crid://example.com/a", "First")};
  in[1] = (dvbtva_fuu_t){0x0001, program("crid://example.com/b", "Second")};
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  if (dvbtva_au_write(ROOTS(t), &eo, in, 2, &bw, &sw, refs, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  for (unsigned i = 0; i < 2; i++) {
    refs[i].frag_id = 0x100 + i;
    refs[i].version = (uint8_t)(3 + i);
  }
  {
    size_t bl;
    size_t sl;
    const unsigned char *b = bitwriter_data(&bw, &bl);
    const unsigned char *s = strrepo_writer_data(&sw, &sl);
    ck_assert_int_eq(bim_container_build(BIM_FRAGREF_DVB_BIM, b, bl, s, sl, refs, 2, &cont, &cl), 0);
  }
  ck_assert_int_eq(bim_container_parse(cont, cl, &au, &aul, &str, &srl), 0);
  ck_assert_int_eq(bim_container_parse_refs(cont, cl, &fmt, &prefs, &np), 0);
  ck_assert_int_eq(np, 2);
  for (int i = 0; i < 2; i++) {
    ck_assert_uint_eq(prefs[i].au_off, refs[i].au_off);
    ck_assert_uint_eq(prefs[i].str_off, refs[i].str_off);
    ck_assert_uint_eq(prefs[i].frag_id, refs[i].frag_id);
    ck_assert_uint_eq(prefs[i].version, refs[i].version);
  }
  memset(&dopts, 0, sizeof dopts);
  dopts.schemas = &t.c->schema;
  dopts.nschemas = 1;
  dopts.lax_schema = -1;
  bitreader_init(&br, au, aul);
  ck_assert_int_eq(strrepo_reader_init(&sr, str, srl), 0);
  if (dvbtva_au_parse(ROOTS(t), &dopts, &br, &sr, prefs, (size_t)np, &got, &n, &err)) ck_abort_msg("%s %s", err.code, err.msg);
  ck_assert_uint_eq(n, 2);
  ck_assert(tree_same(in[1].root, got[1].root));
  dvbtva_au_free(got, n);
  free(prefs);
  prefs = NULL;
  {
    unsigned char *plain = NULL;
    size_t pl;
    ck_assert_int_eq(bim_container_build(BIM_FRAGREF_DVB_BIM, au, aul, str, srl, NULL, 0, &plain, &pl), 0);
    ck_assert_int_eq(bim_container_parse_refs(plain, pl, &fmt, &prefs, &np), 0);
    ck_assert_int_eq(np, 0);
    ck_assert_ptr_null(prefs);
    free(plain);
  }
  free(cont);
  bim_node_free(in[0].root);
  bim_node_free(in[1].root);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  xsd_set_free(t.set);
}
END_TEST

static bim_node_t *tnode(bim_node_t *parent, const char *name, const char *text) {
  bim_node_t *n = parent ? bim_node_add_new(parent, name, text) : bim_node_new(name);
  ck_assert_ptr_nonnull(n);
  n->ns = TVA;
  if (!parent && text) ck_assert_int_eq(bim_node_set_text(n, text), 0);
  return n;
}

static void tattr(bim_node_t *n, const char *name, const char *v) { ck_assert_int_eq(bim_node_add_attr_ns(n, NULL, name, strdup(v)), 0); }

static bim_node_t *sched_event(bim_node_t *sc, const char *crid, const char *start, const char *dur, int inst, int end) {
  bim_node_t *ev = tnode(sc, "ScheduleEvent", NULL);
  tattr(tnode(ev, "Program", NULL), "crid", crid);
  if (inst) tnode(ev, "InstanceDescription", NULL);
  if (start) tnode(ev, "PublishedStartTime", start);
  if (dur) tnode(ev, "PublishedDuration", dur);
  if (end) tnode(ev, "PublishedEndTime", "2024-05-01T12:00:00Z");
  return ev;
}

static bim_node_t *schedule(const char *svc, const char *start, const char *end) {
  bim_node_t *sc = tnode(NULL, "Schedule", NULL);
  tattr(sc, "serviceIDRef", svc);
  tattr(sc, "start", start);
  tattr(sc, "end", end);
  return sc;
}

static int frag_rule(unsigned code, bim_node_t *root) {
  tv_t t = {NULL, NULL, {TVA, EXT, NULL}};
  dvbtva_fuu_t f = {code, root};
  bim_err_t err;
  int rc = dvbtva_check_fragment(&t.ns, &f, &err);
  if (rc) ck_assert_str_eq(err.code, BIM_DVBTVA_RULE);
  bim_node_free(root);
  return rc;
}

/* TS 102 323 clause 8.3 to 8.9 */
START_TEST(dvbtva_rules_per_fragment) {
  bim_node_t *p = tnode(NULL, "ProgramInformation", NULL);
  bim_node_t *bd;
  bim_node_t *g;
  bim_node_t *sc;
  bim_node_t *m;
  tattr(p, "programId", "crid://a/1");
  bd = tnode(p, "BasicDescription", NULL);
  tattr(tnode(bd, "Synopsis", "s"), "length", "short");
  ck_assert_int_eq(frag_rule(0x0001, p), 0);
  p = tnode(NULL, "ProgramInformation", NULL);
  tattr(p, "programId", "http://a/1");
  ck_assert_int_eq(frag_rule(0x0001, p), -1);
  p = tnode(NULL, "ProgramInformation", NULL);
  tattr(p, "programId", "crid://a/1");
  tnode(tnode(p, "BasicDescription", NULL), "Synopsis", "no length");
  ck_assert_int_eq(frag_rule(0x0001, p), -1);

  g = tnode(NULL, "GroupInformation", NULL);
  tattr(g, "groupId", "crid://a/g");
  ck_assert_int_eq(frag_rule(0x0002, g), -1);
  g = tnode(NULL, "GroupInformation", NULL);
  tattr(g, "groupId", "crid://a/g");
  tnode(tnode(g, "BasicDescription", NULL), "Title", "T");
  ck_assert_int_eq(frag_rule(0x0002, g), 0);

  sc = schedule("dvb://1.2.3", "2024-05-01T10:00:00Z", "2024-05-01T12:00:00Z");
  sched_event(sc, "crid://a/1", "2024-05-01T10:00:00Z", "PT1H", 0, 0);
  sched_event(sc, "crid://a/2", "2024-05-01T11:00:00Z", "PT1H", 0, 0);
  ck_assert_int_eq(frag_rule(0x0005, sc), 0);
  sc = schedule("dvb://1.2.3", "2024-05-01T10:00:00Z", "2024-05-01T12:00:00Z");
  sched_event(sc, "crid://a/2", "2024-05-01T11:00:00Z", "PT1H", 0, 0);
  sched_event(sc, "crid://a/1", "2024-05-01T10:00:00Z", "PT1H", 0, 0);
  ck_assert_int_eq(frag_rule(0x0005, sc), -1);
  sc = schedule("dvb://1.2.3", "2024-05-01T10:30:00Z", "2024-05-01T12:00:00Z");
  sched_event(sc, "crid://a/1", "2024-05-01T10:00:00Z", "PT1H", 0, 0);
  ck_assert_int_eq(frag_rule(0x0005, sc), -1);
  sc = schedule("dvb://1.2.3", "2024-05-01T10:00:00Z", "2024-05-01T10:30:00Z");
  sched_event(sc, "crid://a/1", "2024-05-01T10:00:00Z", "PT1H", 0, 0);
  ck_assert_int_eq(frag_rule(0x0005, sc), -1);
  sc = schedule("dvb://1.2.3", "2024-05-01T10:00:00Z", "2024-05-01T12:00:00Z");
  sched_event(sc, "crid://a/1", "2024-05-01T10:00:00Z", "PT1H", 0, 1);
  ck_assert_int_eq(frag_rule(0x0005, sc), -1);
  sc = schedule("dvb://1.2.3", "2024-05-01T10:00:00Z", "2024-05-01T12:00:00Z");
  sched_event(sc, "crid://a/1", "2024-05-01T10:00:00Z", NULL, 0, 0);
  ck_assert_int_eq(frag_rule(0x0005, sc), -1);
  sc = schedule("dvb://1.2.3", "2024-05-01T10:00:00Z", "2024-05-01T12:00:00Z");
  sched_event(sc, "http://a/1", "2024-05-01T10:00:00Z", "PT1H", 0, 0);
  ck_assert_int_eq(frag_rule(0x0005, sc), -1);

  m = tnode(NULL, "ServiceInformation", NULL);
  ck_assert_int_eq(frag_rule(0x0006, m), -1);
  m = tnode(NULL, "ServiceInformation", NULL);
  tnode(m, "ServiceURL", "dvb://1.2.3");
  ck_assert_int_eq(frag_rule(0x0006, m), 0);

  m = tnode(NULL, "TVAMain", NULL);
  tattr(m, "publisher", "tvlistings.tv/TV%20Listings");
  ck_assert_int_eq(frag_rule(0x000E, m), 0);
  m = tnode(NULL, "TVAMain", NULL);
  tattr(m, "publisher", "tvlistings.tv/TV Listings");
  ck_assert_int_eq(frag_rule(0x000E, m), -1);
  m = tnode(NULL, "TVAMain", NULL);
  tattr(m, "publisher", "nodomain");
  ck_assert_int_eq(frag_rule(0x000E, m), -1);
  m = tnode(NULL, "TVAMain", NULL);
  tattr(m, "publisher", "a.tv/bad%2");
  ck_assert_int_eq(frag_rule(0x000E, m), -1);

  m = tnode(NULL, "SegmentInformation", NULL);
  tattr(tnode(m, "SegmentLocator", NULL), "mediaTimeUnit", "PT1N25F");
  ck_assert_int_eq(frag_rule(0x000C, m), 0);
  m = tnode(NULL, "SegmentInformation", NULL);
  tattr(tnode(m, "SegmentLocator", NULL), "mediaTimeUnit", "PT1N26F");
  ck_assert_int_eq(frag_rule(0x000C, m), -1);
  m = tnode(NULL, "BroadcastEvent", NULL);
  tattr(tnode(m, "Program", NULL), "crid", "crid://a/1");
  ck_assert_int_eq(frag_rule(0x0004, m), 0);
  m = tnode(NULL, "BroadcastEvent", NULL);
  tattr(tnode(m, "Program", NULL), "crid", "crid://a/1");
  tnode(m, "PublishedEndTime", "2024-05-01T12:00:00Z");
  ck_assert_int_eq(frag_rule(0x0004, m), -1);
}
END_TEST

static int set_rule(dvbtva_fuu_t *f, size_t n) {
  bim_tvans_t ns = {TVA, EXT, NULL};
  bim_err_t err;
  int rc = dvbtva_check_set(&ns, f, n, &err);
  if (rc) ck_assert_str_eq(err.code, BIM_DVBTVA_RULE);
  return rc;
}

/* 8.2 table 45 references, 8.5 contiguous Schedules of a service */
START_TEST(dvbtva_rules_across_fragments) {
  bim_node_t *s1 = schedule("dvb://1.2.3", "2024-05-01T10:00:00Z", "2024-05-01T11:00:00Z");
  bim_node_t *s2 = schedule("dvb://1.2.3", "2024-05-01T11:00:00Z", "2024-05-01T12:00:00Z");
  bim_node_t *pi = tnode(NULL, "ProgramInformation", NULL);
  bim_node_t *si = tnode(NULL, "ServiceInformation", NULL);
  bim_node_t *gi = tnode(NULL, "GroupInformation", NULL);
  dvbtva_fuu_t f[5];
  sched_event(s1, "crid://a/1", "2024-05-01T10:00:00Z", "PT1H", 0, 0);
  tattr(pi, "programId", "crid://a/1");
  tattr(tnode(pi, "MemberOf", NULL), "crid", "crid://a/g");
  tattr(si, "serviceId", "dvb://1.2.3");
  tattr(gi, "groupId", "crid://a/g");
  f[0] = (dvbtva_fuu_t){0x0005, s1};
  f[1] = (dvbtva_fuu_t){0x0001, pi};
  f[2] = (dvbtva_fuu_t){0x0006, si};
  f[3] = (dvbtva_fuu_t){0x0002, gi};
  ck_assert_int_eq(set_rule(f, 4), 0);
  ck_assert_int_eq(set_rule(f, 1), -1);
  f[4] = (dvbtva_fuu_t){0x0005, s2};
  ck_assert_int_eq(set_rule(f, 5), 0);
  ck_assert_int_eq(set_rule((dvbtva_fuu_t[]){f[0], f[2], f[1], f[3], f[4]}, 3), -1);
  bim_node_free(s2);
  s2 = schedule("dvb://1.2.3", "2024-05-01T11:30:00Z", "2024-05-01T12:00:00Z");
  f[4].root = s2;
  ck_assert_int_eq(set_rule(f, 5), -1);
  f[4].code = 0x00F0;
  ck_assert_int_eq(set_rule(f, 5), 0);
  bim_node_free(s1);
  bim_node_free(s2);
  bim_node_free(pi);
  bim_node_free(si);
  bim_node_free(gi);
}
END_TEST

static int bident(const unsigned char *in, size_t n, unsigned char *out, size_t *outlen) {
  if (n > *outlen) return -1;
  memcpy(out, in, n);
  *outlen = n;
  return 0;
}

/* TS 102 323 table 21: method 0 container, method 1 original size and Zlib structure */
START_TEST(bcg_compression_wrapper) {
  static const unsigned char cont[] = {1, 2, 3, 4, 5};
  unsigned char *w;
  unsigned char *u;
  size_t wl;
  size_t ul;
  ck_assert_int_eq(bim_bcg_wrap(cont, sizeof cont, NULL, &w, &wl), 0);
  ck_assert_uint_eq(w[0], 0);
  ck_assert_uint_eq(wl, sizeof cont + 1);
  ck_assert_int_eq(bim_bcg_unwrap(w, wl, NULL, &u, &ul), 0);
  ck_assert_uint_eq(ul, sizeof cont);
  ck_assert_mem_eq(u, cont, ul);
  free(w);
  free(u);
  ck_assert_int_eq(bim_bcg_wrap(cont, sizeof cont, bident, &w, &wl), 0);
  ck_assert_uint_eq(w[0], 1);
  ck_assert_uint_eq(w[3], sizeof cont);
  ck_assert_int_eq(bim_bcg_unwrap(w, wl, NULL, &u, &ul), -1);
  ck_assert_int_eq(bim_bcg_unwrap(w, wl, bident, &u, &ul), 0);
  ck_assert_mem_eq(u, cont, ul);
  free(u);
  w[3]++;
  ck_assert_int_eq(bim_bcg_unwrap(w, wl, bident, &u, &ul), -1);
  w[0] = 2;
  ck_assert_int_eq(bim_bcg_unwrap(w, wl, bident, &u, &ul), -1);
  ck_assert_int_eq(bim_bcg_unwrap(w, 0, bident, &u, &ul), -1);
  free(w);
}
END_TEST

/* TS 102 539 7.3, 7.4: payload 0xA1 DVB-TVA-init and DecoderInit, payload 0xA2 TVAMain container */
START_TEST(bcg_payloads_0xa1_and_0xa2) {
  tv_t t;
  bim_tvainit_t ti = {.encoding_version = BIM_TVAINIT_DVB, .char_encoding = 1};
  bim_tvainit_t gi;
  bim_decoderinit_t di;
  bim_decoderinit_t gd;
  bitwriter_t bw;
  bim_err_t err;
  const unsigned char *p;
  size_t n;
  bim_encode_opts_t eo;
  bim_decode_opts_t dopts;
  dvbtva_roots_t roots;
  bim_node_t *main = tnode(NULL, "TVAMain", NULL);
  bim_node_t *got = NULL;
  unsigned char *pay;
  size_t pl;
  tv_init(&t);
  roots = (dvbtva_roots_t){t.c, &t.ns};
  ck_assert_int_eq(bim_tvainit_decoderinit(TVA, NULL, 0, &di), 0);
  bitwriter_init(&bw);
  ck_assert_int_eq(bim_bcg_tvainit_write(&ti, &di, &bw, &err), 0);
  p = bitwriter_data(&bw, &n);
  ck_assert_int_eq(bim_bcg_tvainit_parse(p, n, &gi, &gd, &err), 0);
  ck_assert_str_eq(gd.schemas[0].uri, TVA);
  bim_decoderinit_free(&gd);
  ck_assert_int_eq(bim_bcg_tvainit_parse(p, n - 1, &gi, &gd, &err), -1);
  ti.encoding_version = BIM_TVAINIT_TVA;
  bitwriter_free(&bw);
  bitwriter_init(&bw);
  ck_assert_int_eq(bim_bcg_tvainit_write(&ti, &di, &bw, &err), -1);
  ck_assert_str_eq(err.code, BIM_BCG_ERR);
  bitwriter_free(&bw);

  tattr(main, "publisher", "tvlistings.tv/TV%20Listings");
  memset(&eo, 0, sizeof eo);
  eo.schemas = &t.c->schema;
  eo.nschemas = 1;
  eo.lax_schema = -1;
  eo.frag_format = 1;
  memset(&dopts, 0, sizeof dopts);
  dopts.schemas = &t.c->schema;
  dopts.nschemas = 1;
  dopts.lax_schema = -1;
  dopts.inflate = bident;
  eo.deflate = bident;
  ck_assert_int_eq(bim_bcg_tvamain_write(&roots, &eo, main, 7, bident, &pay, &pl, &err), 0);
  ck_assert_uint_eq(pay[0], 1);
  ck_assert_int_eq(bim_bcg_tvamain_parse(&roots, &dopts, pay, pl, &got, &err), 0);
  ck_assert_str_eq(bim_node_attr(got, "publisher"), "tvlistings.tv/TV%20Listings");
  bim_node_free(got);
  free(pay);
  ck_assert_int_eq(bim_bcg_tvamain_write(&roots, &eo, main, 7, NULL, &pay, &pl, &err), 0);
  ck_assert_uint_eq(pay[0], 0);
  {
    bim_fragref_t *refs;
    unsigned fmt;
    int nrefs;
    ck_assert_int_eq(bim_container_parse_refs(pay + 1, pl - 1, &fmt, &refs, &nrefs), 0);
    ck_assert_int_eq(nrefs, 1);
    ck_assert_uint_eq(refs[0].frag_id, 7);
    free(refs);
  }
  ck_assert_int_eq(bim_bcg_tvamain_parse(&roots, &dopts, pay, pl, &got, &err), 0);
  bim_node_free(got);
  pay[pl / 2] ^= 0xFF;
  pay[pl - 1] ^= 0xFF;
  got = NULL;
  (void)bim_bcg_tvamain_parse(&roots, &dopts, pay, pl, &got, &err);
  bim_node_free(got);
  free(pay);
  ck_assert_int_eq(bim_bcg_tvamain_parse(&roots, &dopts, (const unsigned char *)"\x00\x00", 2, &got, &err), -1);
  ck_assert_str_eq(err.code, BIM_BCG_ERR);
  bim_node_free(main);
  bim_tvainit_decoderinit_free(&di);
  xsd_set_free(t.set);
}
END_TEST

static int bfail(const unsigned char *in, size_t n, unsigned char *out, size_t *outlen) {
  (void)in;
  (void)n;
  (void)out;
  (void)outlen;
  return -1;
}

START_TEST(bcg_wrapper_edge_cases) {
  static const unsigned char cont[] = {1, 2, 3, 4, 5};
  unsigned char *w;
  unsigned char *u;
  unsigned char *big;
  size_t wl;
  size_t ul;
  int rc;
  ck_assert_int_eq(bim_bcg_wrap(cont, sizeof cont, bfail, &w, &wl), 0);
  ck_assert_uint_eq(w[0], 0);
  free(w);
  ck_assert_int_eq(bim_bcg_wrap(cont, 0, NULL, &w, &wl), 0);
  ck_assert_uint_eq(wl, 1u);
  ck_assert_int_eq(bim_bcg_unwrap(w, wl, NULL, &u, &ul), 0);
  ck_assert_uint_eq(ul, 0u);
  free(u);
  free(w);
  big = calloc(0x1000001, 1);
  ck_assert_ptr_nonnull(big);
  ck_assert_int_eq(bim_bcg_wrap(big, 0x1000001, bident, &w, &wl), 0);
  ck_assert_uint_eq(w[0], 0);
  free(w);
  free(big);
  ck_assert_int_eq(bim_bcg_wrap(cont, sizeof cont, bident, &w, &wl), 0);
  ck_assert_int_eq(bim_bcg_unwrap(w, 3, bident, &u, &ul), -1);
  ck_assert_int_eq(bim_bcg_unwrap(w, wl, bfail, &u, &ul), -1);
  alloc_fail_at(0);
  rc = bim_bcg_unwrap(w, wl, bident, &u, &ul);
  alloc_fail_end();
  ck_assert_int_eq(rc, -1);
  free(w);
  ck_assert_int_eq(bim_bcg_wrap(cont, sizeof cont, NULL, &w, &wl), 0);
  alloc_fail_at(0);
  rc = bim_bcg_unwrap(w, wl, NULL, &u, &ul);
  alloc_fail_end();
  ck_assert_int_eq(rc, -1);
  free(w);
  for (int k = 0; k < 2; k++) {
    alloc_fail_at(k);
    rc = bim_bcg_wrap(cont, sizeof cont, bident, &w, &wl);
    alloc_fail_end();
    if (!rc) free(w);
  }
}
END_TEST

START_TEST(bcg_payloads_survive_damage_and_allocation_failure) {
  tv_t t;
  bim_tvainit_t ti = {.encoding_version = BIM_TVAINIT_DVB, .char_encoding = 1};
  bim_tvainit_t gi;
  bim_decoderinit_t di;
  bim_decoderinit_t gd;
  bitwriter_t bw;
  bim_err_t err;
  const unsigned char *p;
  unsigned char *copy;
  size_t n;
  bim_encode_opts_t eo;
  bim_decode_opts_t dopts;
  dvbtva_roots_t roots;
  bim_node_t *main = tnode(NULL, "TVAMain", NULL);
  bim_node_t *got = NULL;
  unsigned char *pay;
  size_t pl;
  tv_init(&t);
  roots = (dvbtva_roots_t){t.c, &t.ns};
  ck_assert_int_eq(bim_tvainit_decoderinit(TVA, NULL, 0, &di), 0);
  bitwriter_init(&bw);
  ck_assert_int_eq(bim_bcg_tvainit_write(&ti, &di, &bw, &err), 0);
  p = bitwriter_data(&bw, &n);
  copy = malloc(n + 1);
  ck_assert_ptr_nonnull(copy);
  memcpy(copy, p, n);
  copy[n] = 0xAA;
  ck_assert_int_eq(bim_bcg_tvainit_parse(copy, n + 1, &gi, &gd, &err), -1);
  ck_assert_str_eq(err.code, BIM_BCG_ERR);
  for (size_t cut = 0; cut < n; cut++)
    if (!bim_bcg_tvainit_parse(copy, cut, &gi, &gd, &err)) bim_decoderinit_free(&gd);
  for (size_t bit = 0; bit < n * 8; bit++) {
    copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
    if (!bim_bcg_tvainit_parse(copy, n, &gi, &gd, &err)) bim_decoderinit_free(&gd);
    copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
  }
  for (long k = 0;; k++) {
    int hit;
    int rc;
    alloc_fail_at(k);
    rc = bim_bcg_tvainit_parse(copy, n, &gi, &gd, &err);
    hit = alloc_fail_end();
    if (!rc) bim_decoderinit_free(&gd);
    if (!hit) break;
  }
  free(copy);
  bitwriter_free(&bw);
  for (long k = 0;; k++) {
    int hit;
    bitwriter_init(&bw);
    alloc_fail_at(k);
    (void)bim_bcg_tvainit_write(&ti, &di, &bw, &err);
    hit = alloc_fail_end();
    bitwriter_free(&bw);
    if (!hit) break;
  }

  tattr(main, "publisher", "tvlistings.tv/TV%20Listings");
  memset(&eo, 0, sizeof eo);
  eo.schemas = &t.c->schema;
  eo.nschemas = 1;
  eo.lax_schema = -1;
  eo.frag_format = 1;
  eo.deflate = bident;
  memset(&dopts, 0, sizeof dopts);
  dopts.schemas = &t.c->schema;
  dopts.nschemas = 1;
  dopts.lax_schema = -1;
  dopts.inflate = bident;
  for (int zl = 0; zl < 2; zl++) {
    ck_assert_int_eq(bim_bcg_tvamain_write(&roots, &eo, main, 7, zl ? bident : NULL, &pay, &pl, &err), 0);
    for (size_t cut = 0; cut < pl; cut++)
      if (!bim_bcg_tvamain_parse(&roots, &dopts, pay, cut, &got, &err)) bim_node_free(got);
    for (size_t bit = 0; bit < pl * 8; bit++) {
      pay[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
      if (!bim_bcg_tvamain_parse(&roots, &dopts, pay, pl, &got, &err)) bim_node_free(got);
      pay[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
    }
    for (long k = 0;; k++) {
      int hit;
      int rc;
      got = NULL;
      alloc_fail_at(k);
      rc = bim_bcg_tvamain_parse(&roots, &dopts, pay, pl, &got, &err);
      hit = alloc_fail_end();
      if (!rc) bim_node_free(got);
      if (!hit) break;
    }
    free(pay);
    for (long k = 0;; k++) {
      int hit;
      int rc;
      alloc_fail_at(k);
      rc = bim_bcg_tvamain_write(&roots, &eo, main, 7, zl ? bident : NULL, &pay, &pl, &err);
      hit = alloc_fail_end();
      if (!rc) free(pay);
      if (!hit) break;
    }
  }
  bim_node_free(main);
  bim_tvainit_decoderinit_free(&di);
  xsd_set_free(t.set);
}
END_TEST

static int post_fail(void *ctx, size_t k, const dvbtva_fuu_t *f, strrepo_reader_t *sr) {
  (void)ctx;
  (void)f;
  (void)sr;
  return k == 1 ? -1 : 0;
}

static bim_node_t *package_node(void) {
  bim_node_t *p = bim_node_new("Package");
  ck_assert_ptr_nonnull(p);
  p->ns = EXT;
  ck_assert_int_eq(bim_node_add_attr_ns(p, NULL, "id", strdup("pkg")), 0);
  return p;
}

START_TEST(dvbtva_parse_reports_string_offsets_post_failures_and_oom) {
  tv_t t;
  bim_encode_opts_t eo;
  bim_decode_opts_t dop;
  dvbtva_fuu_t in[2];
  dvbtva_fuu_t *got = NULL;
  bim_fragref_t refs[2];
  uint32_t saved_off;
  bitwriter_t bw;
  strrepo_writer_t sw;
  bitreader_t br;
  strrepo_reader_t sr;
  const unsigned char *a;
  const unsigned char *st;
  size_t al;
  size_t sl;
  size_t n = 0;
  bim_err_t err;
  tv_init(&t);
  memset(&eo, 0, sizeof eo);
  eo.schemas = &t.c->schema;
  eo.nschemas = 1;
  eo.lax_schema = -1;
  eo.frag_format = 1;
  memset(&dop, 0, sizeof dop);
  dop.schemas = &t.c->schema;
  dop.nschemas = 1;
  dop.lax_schema = -1;
  in[0] = (dvbtva_fuu_t){0x0001, program("crid://example.com/a", "A")};
  in[1] = (dvbtva_fuu_t){0x0012, package_node()};
  bitwriter_init(&bw);
  ck_assert_int_eq(strrepo_writer_init(&sw), 0);
  ck_assert_int_eq(dvbtva_au_write(ROOTS(t), &eo, in, 2, &bw, &sw, refs, &err), 0);
  a = bitwriter_data(&bw, &al);
  st = strrepo_writer_data(&sw, &sl);

  bitreader_init(&br, a, al);
  ck_assert_int_eq(strrepo_reader_init(&sr, st, sl), 0);
  ck_assert_int_eq(dvbtva_au_parse(ROOTS(t), &dop, &br, &sr, refs, 2, &got, &n, &err), 0);
  ck_assert_uint_eq(n, 2);
  ck_assert_str_eq(got[1].root->ns, EXT);
  dvbtva_au_free(got, n);

  saved_off = refs[0].str_off;
  refs[0].str_off = 0x7FFFFFF;
  bitreader_init(&br, a, al);
  ck_assert_int_eq(strrepo_reader_init(&sr, st, sl), 0);
  ck_assert_int_eq(dvbtva_au_parse(ROOTS(t), &dop, &br, &sr, refs, 2, &got, &n, &err), -1);
  ck_assert_ptr_nonnull(strstr(err.msg, "string offset"));
  refs[0].str_off = saved_off;

  bitreader_init(&br, a, al);
  ck_assert_int_eq(strrepo_reader_init(&sr, st, sl), 0);
  ck_assert_int_eq(dvbtva_au_parse_ex(dvbtva_tva_root, &(dvbtva_roots_t){t.c, &t.ns}, &dop, &br, &sr, refs, 2, post_fail, NULL, &got, &n, &err), -1);
  ck_assert_msg(strstr(err.msg, "strings malformed"), "%s", err.msg);

  for (long k = 0;; k++) {
    int hit;
    int rc;
    bitreader_init(&br, a, al);
    ck_assert_int_eq(strrepo_reader_init(&sr, st, sl), 0);
    got = NULL;
    alloc_fail_at(k);
    rc = dvbtva_au_parse(ROOTS(t), &dop, &br, &sr, refs, 2, &got, &n, &err);
    hit = alloc_fail_end();
    if (!rc) dvbtva_au_free(got, n);
    if (!hit) break;
  }
  for (long k = 0;; k++) {
    int hit;
    bitwriter_t b2;
    strrepo_writer_t s2;
    bim_fragref_t r2[2];
    bitwriter_init(&b2);
    ck_assert_int_eq(strrepo_writer_init(&s2), 0);
    alloc_fail_at(k);
    (void)dvbtva_au_write(ROOTS(t), &eo, in, 2, &b2, &s2, r2, &err);
    hit = alloc_fail_end();
    bitwriter_free(&b2);
    strrepo_writer_free(&s2);
    if (!hit) break;
  }
  bim_node_free(in[0].root);
  bim_node_free(in[1].root);
  bitwriter_free(&bw);
  strrepo_writer_free(&sw);
  xsd_set_free(t.set);
}
END_TEST

static int fail_compile(xsd_set_t *set, const xsd_compiled_t **out, void *arg) {
  (void)set;
  (void)out;
  (void)arg;
  return -1;
}

static int ok_compile(xsd_set_t *set, const xsd_compiled_t **out, void *arg) { return dvbtva_compile(set, arg, out); }

START_TEST(dvbtva_extras_load_reports_each_outcome) {
  static const char ext_xsd[] =
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" targetNamespace=\"urn:test:extra\"><xs:element name=\"E\" type=\"xs:string\"/></xs:schema>";
  static const char bad_xsd[] =
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" targetNamespace=\"urn:test:broken\"><xs:element name=\"E\" type=\"missing\"/></xs:schema>";
  char dir[] = "/tmp/dvbipitools_test_extras_XXXXXX";
  char path[200];
  const char *paths[1];
  const char *uris[3] = {TVA, "urn:test:extra", "urn:test:absent"};
  const char *buris[2] = {TVA, "urn:test:broken"};
  const xsd_compiled_t *known[3];
  xsd_set_t *sets[3];
  dvbtva_extra_codes_t codes = {"GS", "GE", "GS500101", "GW000770"};
  FILE *f;
  ck_assert_ptr_nonnull(mkdtemp(dir));
  paths[0] = dir;
  snprintf(path, sizeof path, "%s/extra.xsd", dir);
  f = fopen(path, "w");
  ck_assert_ptr_nonnull(f);
  fputs(ext_xsd, f);
  fclose(f);
  snprintf(path, sizeof path, "%s/broken.xsd", dir);
  f = fopen(path, "w");
  ck_assert_ptr_nonnull(f);
  fputs(bad_xsd, f);
  fclose(f);

  for (int enc = 0; enc < 2; enc++) {
    memset(sets, 0, sizeof sets);
    memset(known, 0, sizeof known);
    ck_assert_int_eq(dvbtva_extras_load(sets, known, uris, 3, paths, 1, NULL, enc, &codes, ok_compile, (void *)TVA), 0);
    ck_assert_ptr_nonnull(known[1]);
    ck_assert_ptr_null(known[2]);
    for (int k = 1; k < 3; k++) xsd_set_free(sets[k]);
  }
  memset(sets, 0, sizeof sets);
  memset(known, 0, sizeof known);
  ck_assert_int_eq(dvbtva_extras_load(sets, known, uris, 3, paths, 1, NULL, 0, &codes, fail_compile, NULL), -1);
  for (int k = 1; k < 3; k++) xsd_set_free(sets[k]);
  memset(sets, 0, sizeof sets);
  memset(known, 0, sizeof known);
  ck_assert_int_eq(dvbtva_extras_load(sets, known, buris, 2, paths, 1, NULL, 0, &codes, ok_compile, (void *)TVA), -1);
  xsd_set_free(sets[1]);
  for (long k = 0;; k++) {
    int hit;
    memset(sets, 0, sizeof sets);
    memset(known, 0, sizeof known);
    alloc_fail_at(k);
    (void)dvbtva_extras_load(sets, known, uris, 3, paths, 1, NULL, 0, &codes, ok_compile, (void *)TVA);
    hit = alloc_fail_end();
    for (int j = 1; j < 3; j++) xsd_set_free(sets[j]);
    if (!hit) break;
  }
  snprintf(path, sizeof path, "%s/extra.xsd", dir);
  unlink(path);
  snprintf(path, sizeof path, "%s/broken.xsd", dir);
  unlink(path);
  rmdir(dir);
}
END_TEST

START_TEST(container_build_and_parse_cover_every_limit) {
  static const unsigned char au[] = {1, 2, 3};
  static const unsigned char sr[] = {9, 8};
  bim_fragref_t refs[2] = {{.au_off = 0, .str_off = 1, .version = 2, .frag_id = 0x010203}, {.au_off = 5, .str_off = 6, .version = 7, .frag_id = 8}};
  bim_fragref_t big[1] = {{.au_off = 70000, .str_off = 0, .version = 0, .frag_id = 0}};
  bim_fragref_t bigs[1] = {{.au_off = 0, .str_off = 70000, .version = 0, .frag_id = 0}};
  bim_struct_t many[256];
  unsigned char *out = NULL;
  size_t ol = 0;
  const unsigned char *a;
  const unsigned char *s2;
  size_t al;
  size_t sl;
  bim_fragref_t *got = NULL;
  int ng = 0;
  unsigned fmt = 0;
  unsigned char *copy;
  int rc;
  ck_assert_int_eq(bim_container_build(99, au, sizeof au, sr, sizeof sr, refs, 2, &out, &ol), -1);
  ck_assert_int_eq(bim_container_build(BIM_FRAGREF_DVB_BIM, au, sizeof au, sr, sizeof sr, big, 1, &out, &ol), -1);
  ck_assert_int_eq(bim_container_build(BIM_FRAGREF_DVB_BIM, au, sizeof au, sr, sizeof sr, bigs, 1, &out, &ol), -1);
  ck_assert_int_eq(bim_container_build(BIM_FRAGREF_BIM, au, sizeof au, NULL, 0, bigs, 1, &out, &ol), 0);
  free(out);
  for (int f = 0; f < 2; f++) {
    unsigned format = f ? BIM_FRAGREF_BIM : BIM_FRAGREF_DVB_BIM;
    ck_assert_int_eq(bim_container_build(format, au, sizeof au, sr, sizeof sr, refs, 2, &out, &ol), 0);
    ck_assert_int_eq(bim_container_parse(out, ol, &a, &al, &s2, &sl), 0);
    ck_assert_uint_eq(al, sizeof au);
    ck_assert_uint_eq(sl, sizeof sr);
    ck_assert_int_eq(bim_container_parse_refs(out, ol, &fmt, &got, &ng), 0);
    ck_assert_uint_eq(fmt, format);
    ck_assert_int_eq(ng, 2);
    ck_assert_uint_eq(got[1].frag_id, 8u);
    ck_assert_uint_eq(got[0].str_off, f ? 0u : 1u);
    free(got);
    copy = malloc(ol);
    ck_assert_ptr_nonnull(copy);
    memcpy(copy, out, ol);
    for (size_t cut = 0; cut <= ol; cut++) {
      if (!bim_container_parse(copy, cut, &a, &al, &s2, &sl)) (void)a;
      got = NULL;
      if (!bim_container_parse_refs(copy, cut, &fmt, &got, &ng)) free(got);
    }
    for (size_t bit = 0; bit < ol * 8; bit++) {
      copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
      (void)bim_container_parse(copy, ol, &a, &al, &s2, &sl);
      got = NULL;
      if (!bim_container_parse_refs(copy, ol, &fmt, &got, &ng)) free(got);
      copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
    }
    free(copy);
    free(out);
  }
  ck_assert_int_eq(bim_container_build(BIM_FRAGREF_BIM, au, sizeof au, NULL, 0, NULL, 0, &out, &ol), 0);
  ck_assert_int_eq(bim_container_parse_refs(out, ol, &fmt, &got, &ng), 0);
  ck_assert_int_eq(ng, 0);
  ck_assert_uint_eq(fmt, 0u);
  free(out);

  memset(many, 0, sizeof many);
  for (int i = 0; i < 256; i++) many[i] = (bim_struct_t){BIM_STRUCT_DATA_REPOSITORY, (unsigned char)i, (unsigned char *)au, sizeof au};
  ck_assert_int_eq(bim_container_build_structs(many, 256, &out, &ol), -1);
  ck_assert_int_eq(bim_container_build_structs(many, 0, &out, &ol), -1);
  many[0].len = 0x1000000;
  ck_assert_int_eq(bim_container_build_structs(many, 1, &out, &ol), -1);
  many[0].len = 0xFFFFF0;
  many[1].len = 0xFFFFF0;
  ck_assert_int_eq(bim_container_build_structs(many, 2, &out, &ol), -1);

  for (int variant = 0; variant < 2; variant++)
    for (long k = 0;; k++) {
      int hit;
      alloc_fail_at(k);
      rc = bim_container_build(variant ? BIM_FRAGREF_BIM : BIM_FRAGREF_DVB_BIM, au, sizeof au, sr, sizeof sr, refs, 2, &out, &ol);
      hit = alloc_fail_end();
      if (!rc) free(out);
      if (!hit) break;
    }
  ck_assert_int_eq(bim_container_build(BIM_FRAGREF_DVB_BIM, au, sizeof au, sr, sizeof sr, refs, 2, &out, &ol), 0);
  for (long k = 0;; k++) {
    int hit;
    alloc_fail_at(k);
    rc = bim_container_parse_refs(out, ol, &fmt, &got, &ng);
    hit = alloc_fail_end();
    if (!rc) free(got);
    if (!hit) break;
  }
  free(out);
}
END_TEST

static void enc_container(const unsigned char *body, size_t n, unsigned char **out, size_t *ol) {
  bim_struct_t st[1];
  st[0] = (bim_struct_t){BIM_STRUCT_ENCAPSULATION, BIM_STRUCT_ID_UNUSED, (unsigned char *)body, n};
  ck_assert_int_eq(bim_container_build_structs(st, 1, out, ol), 0);
}

START_TEST(container_parse_refs_rejects_malformed_encapsulation) {
  static const unsigned char too_short[] = {0xFF};
  static const unsigned char unknown_format[] = {0xFF, 3, 1, 2, 3};
  static const unsigned char empty_bim[] = {0xFF, BIM_FRAGREF_BIM};
  static const unsigned char odd_bim[] = {0xFF, BIM_FRAGREF_BIM, 1, 2, 3};
  unsigned char *out = NULL;
  size_t ol = 0;
  bim_fragref_t *got = NULL;
  int ng = 0;
  unsigned fmt = 0;
  const unsigned char *a;
  const unsigned char *s2;
  size_t al;
  size_t sl;
  enc_container(too_short, sizeof too_short, &out, &ol);
  ck_assert_int_eq(bim_container_parse_refs(out, ol, &fmt, &got, &ng), -1);
  free(out);
  enc_container(unknown_format, sizeof unknown_format, &out, &ol);
  ck_assert_int_eq(bim_container_parse_refs(out, ol, &fmt, &got, &ng), 0);
  ck_assert_uint_eq(fmt, 3u);
  ck_assert_int_eq(ng, 0);
  free(out);
  enc_container(empty_bim, sizeof empty_bim, &out, &ol);
  ck_assert_int_eq(bim_container_parse_refs(out, ol, &fmt, &got, &ng), 0);
  ck_assert_int_eq(ng, 0);
  ck_assert_ptr_null(got);
  free(out);
  enc_container(odd_bim, sizeof odd_bim, &out, &ol);
  ck_assert_int_eq(bim_container_parse_refs(out, ol, &fmt, &got, &ng), -1);
  ck_assert_int_eq(bim_container_parse(out, ol, &a, &al, &s2, &sl), -1);
  out[0] = 0;
  ck_assert_int_eq(bim_container_parse(out, ol, &a, &al, &s2, &sl), -1);
  out[0] = 200;
  ck_assert_int_eq(bim_container_parse(out, ol, &a, &al, &s2, &sl), -1);
  ck_assert_int_eq(bim_container_parse_refs(out, ol, &fmt, &got, &ng), -1);
  ck_assert_int_eq(bim_container_parse(out, 0, &a, &al, &s2, &sl), -1);
  free(out);
}
END_TEST

START_TEST(tvainit_checks_every_field_and_survives_damage) {
  bim_tvainit_t bad[] = {
    {.encoding_version = 0x55},
    {.encoding_version = BIM_TVAINIT_DVB, .position_code = 1},
    {.encoding_version = BIM_TVAINIT_TVA, .char_encoding = 0x07},
    {.encoding_version = BIM_TVAINIT_TVA, .char_encoding = 0xE0},
    {.encoding_version = BIM_TVAINIT_TVA, .char_encoding = 0x100},
    {.encoding_version = BIM_TVAINIT_TVA, .has_buffer_size = 1, .buffer_size = 0x1000000},
    {.encoding_version = BIM_TVAINIT_TVA, .indexing = 1, .indexing_version = 0x02},
    {.encoding_version = BIM_TVAINIT_TVA, .indexing = 1, .indexing_version = 0xEF},
  };
  bim_tvainit_t good[] = {
    {.encoding_version = BIM_TVAINIT_TVA, .char_encoding = 0x06},
    {.encoding_version = BIM_TVAINIT_TVA, .char_encoding = 0xE1},
    {.encoding_version = BIM_TVAINIT_TVA, .char_encoding = 0xFF, .indexing = 1, .indexing_version = 0xF0},
    {.encoding_version = BIM_TVAINIT_TVA, .indexing = 1, .indexing_version = 0xFF, .has_buffer_size = 1, .buffer_size = 0xFFFFFF, .position_code = 1},
  };
  bim_err_t err;
  bitwriter_t bw;
  for (size_t k = 0; k < sizeof bad / sizeof *bad; k++) {
    bitwriter_init(&bw);
    ck_assert_int_eq(bim_tvainit_write(&bad[k], &bw, &err), -1);
    ck_assert_str_eq(err.code, BIM_TVAINIT_RANGE);
    bitwriter_free(&bw);
  }
  for (size_t k = 0; k < sizeof good / sizeof *good; k++) {
    const unsigned char *p;
    unsigned char *copy;
    size_t n;
    size_t used;
    bim_tvainit_t g;
    bitwriter_init(&bw);
    ck_assert_int_eq(bim_tvainit_write(&good[k], &bw, &err), 0);
    p = bitwriter_data(&bw, &n);
    copy = malloc(n + 1);
    ck_assert_ptr_nonnull(copy);
    memcpy(copy, p, n);
    ck_assert_int_eq(bim_tvainit_parse(copy, n, &g, &used, &err), 0);
    for (size_t cut = 0; cut < n; cut++) (void)bim_tvainit_parse(copy, cut, &g, &used, &err);
    for (size_t bit = 0; bit < n * 8; bit++) {
      copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
      (void)bim_tvainit_parse(copy, n, &g, &used, &err);
      copy[bit / 8] ^= (unsigned char)(0x80u >> (bit % 8));
    }
    copy[2] = 1;
    ck_assert_int_eq(bim_tvainit_parse(copy, n, &g, &used, &err), -1);
    copy[2] = 0xFF;
    ck_assert_int_eq(bim_tvainit_parse(copy, n, &g, &used, &err), -1);
    free(copy);
    bitwriter_free(&bw);
    for (long a = 0;; a++) {
      int hit;
      bitwriter_init(&bw);
      for (int j = 0; j < a % 30; j++) ck_assert_int_eq(bitwriter_put(&bw, 0xA5, 8), 0);
      alloc_fail_at(a / 30);
      (void)bim_tvainit_write(&good[k], &bw, &err);
      hit = alloc_fail_end();
      bitwriter_free(&bw);
      if (!hit && a % 30 == 29) break;
    }
  }
  {
    bim_decoderinit_t di;
    int rc;
    alloc_fail_at(0);
    rc = bim_tvainit_decoderinit(TVA, NULL, 0, &di);
    alloc_fail_end();
    ck_assert_int_eq(rc, -1);
  }
}
END_TEST

static Suite *dvbtva_suite(void) {
  Suite *s = suite_create("bim_dvbtva");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, tvainit_dvb_minimal_bytes);
  tcase_add_test(tc, tvainit_round_trips_all_fields);
  tcase_add_test(tc, tvainit_parse_rules);
  tcase_add_test(tc, tvainit_write_refuses_invalid);
  tcase_add_test(tc, dvbtva_context_paths_resolve);
  tcase_add_test(tc, dvbtva_access_unit_round_trips);
  tcase_add_test(tc, dvbtva_default_codecs_and_decoderinit);
  tcase_add_test(tc, dvbtva_controlled_terms);
  tcase_add_test(tc, dvbtva_container_carries_access_unit);
  tcase_add_test(tc, dvbtva_rules_per_fragment);
  tcase_add_test(tc, dvbtva_rules_across_fragments);
  tcase_add_test(tc, bcg_compression_wrapper);
  tcase_add_test(tc, bcg_payloads_0xa1_and_0xa2);
  tcase_add_test(tc, bcg_wrapper_edge_cases);
  tcase_add_test(tc, bcg_payloads_survive_damage_and_allocation_failure);
  tcase_add_test(tc, dvbtva_parse_reports_string_offsets_post_failures_and_oom);
  tcase_add_test(tc, dvbtva_extras_load_reports_each_outcome);
  tcase_add_test(tc, container_build_and_parse_cover_every_limit);
  tcase_add_test(tc, container_parse_refs_rejects_malformed_encapsulation);
  tcase_add_test(tc, tvainit_checks_every_field_and_survives_damage);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(dvbtva_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
