/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../alloc_fail.h"
#include "../bcg_schema_fixture.h"
#include "dipibim/job.h"
#include "lib/bim/tvainit.h"

#define EXTRA_NS "urn:test:schema:extra"

static const char extra_xsd[] =
  "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" targetNamespace=\"" EXTRA_NS "\"><xs:element name=\"E\" type=\"xs:string\"/></xs:schema>";

typedef struct {
  char dir[64];
  config_t cfg;
  job_t job;
} sj_t;

static void sj_init(sj_t *s, int profile) {
  memset(s, 0, sizeof *s);
  bcg_fixture_make(s->dir);
  s->cfg.profile = profile;
  s->cfg.schemas[0] = s->dir;
  s->cfg.nschemas = 1;
  s->job.cfg = &s->cfg;
  s->job.ns.tva = BCG_FIXTURE_NS;
}

static void sj_free(sj_t *s) {
  job_free(&s->job);
  bcg_fixture_remove(s->dir);
}

START_TEST(schema_profile_predicates) {
  config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  for (int p = PROFILE_DVB_TVA; p <= PROFILE_GENERIC; p++) {
    cfg.profile = p;
    cfg.syntax = SYNTAX_23001;
    ck_assert_int_eq(job_mpegb(&cfg), 1);
    cfg.syntax = SYNTAX_15938;
    ck_assert_int_eq(job_mpegb(&cfg), p != PROFILE_GENERIC);
    ck_assert_int_eq(job_uses_zlib(&cfg), p == PROFILE_TVA || p == PROFILE_SDNS);
  }
}
END_TEST

START_TEST(schema_loads_and_compiles_every_profile) {
  for (int p = PROFILE_DVB_TVA; p <= PROFILE_GENERIC; p++)
    for (int syntax = 0; syntax < 2; syntax++) {
      sj_t s;
      sj_init(&s, p);
      s.cfg.syntax = syntax;
      s.cfg.dvb_locator = s.cfg.dvb_duration = s.cfg.dvb_pubtime = 1;
      s.cfg.len_mode = 1;
      s.cfg.deferred = 1;
      s.cfg.cast = 1;
      ck_assert_int_eq(job_schema_load(&s.job, BCG_FIXTURE_NS), 0);
      ck_assert_int_eq(job_schema_compile(&s.job), 0);
      ck_assert_ptr_nonnull(s.job.c);
      ck_assert_int_eq(s.job.eo.nschemas, 1);
      ck_assert_int_eq(s.job.eo.dvb_locator, p == PROFILE_DVB_TVA);
      job_count_fragment(&s.job, 1);
      job_count_fragment(&s.job, FRAG_CODES + 5);
      s.cfg.verbose = 1;
      job_log_fragments(&s.job);
      s.cfg.verbose = 0;
      job_log_fragments(&s.job);
      sj_free(&s);
    }
}
END_TEST

START_TEST(schema_reports_missing_and_uncompilable_schemas) {
  sj_t s;
  char empty[] = "/tmp/dvbipitools_test_schema_emptyXXXXXX";
  char *chain;
  size_t cap = 300 * 240 + 400;
  sj_init(&s, PROFILE_GENERIC);
  ck_assert_ptr_nonnull(mkdtemp(empty));
  s.cfg.schemas[0] = empty;
  ck_assert_int_eq(job_schema_load(&s.job, BCG_FIXTURE_NS), -1);
  rmdir(empty);
  job_free(&s.job);
  bcg_fixture_remove(s.dir);

  sj_init(&s, PROFILE_GENERIC);
  chain = calloc(cap, 1);
  ck_assert_ptr_nonnull(chain);
  strcat(chain, "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" xmlns:t=\"" BCG_FIXTURE_NS "\" targetNamespace=\"" BCG_FIXTURE_NS "\">");
  strcat(chain, "<xs:complexType name=\"T0\"><xs:sequence/></xs:complexType>");
  for (int i = 1; i < 300; i++) {
    char one[240];
    snprintf(one, sizeof one, "<xs:complexType name=\"T%d\"><xs:complexContent><xs:extension base=\"t:T%d\"/></xs:complexContent></xs:complexType>", i, i - 1);
    strcat(chain, one);
  }
  strcat(chain, "</xs:schema>");
  bcg_fixture_put(s.dir, "tva.xsd", chain);
  free(chain);
  ck_assert_int_eq(job_schema_load(&s.job, BCG_FIXTURE_NS), 0);
  ck_assert_int_eq(job_schema_compile(&s.job), -1);
  sj_free(&s);
}
END_TEST

START_TEST(schema_survives_allocation_failures) {
  for (int p = PROFILE_DVB_TVA; p <= PROFILE_GENERIC; p++)
    for (long n = 0;; n++) {
      sj_t s;
      int hit;
      sj_init(&s, p);
      alloc_fail_at(n);
      if (!job_schema_load(&s.job, BCG_FIXTURE_NS)) (void)job_schema_compile(&s.job);
      hit = alloc_fail_end();
      sj_free(&s);
      if (!hit) break;
    }
  {
    sj_t s;
    int rc;
    sj_init(&s, PROFILE_GENERIC);
    alloc_fail_at(0);
    rc = job_schema_load(&s.job, BCG_FIXTURE_NS);
    alloc_fail_end();
    ck_assert_int_eq(rc, -1);
    sj_free(&s);
  }
}
END_TEST

static xml_doc_t *parse_doc(const char *text) {
  xml_doc_t *d = NULL;
  ck_assert_int_eq(xml_doc_parse(text, strlen(text), &d, NULL), 0);
  return d;
}

START_TEST(schema_extras_open_encode_and_cast) {
  static const char with_extra[] = "<R xmlns=\"" BCG_FIXTURE_NS "\" xmlns:x=\"" EXTRA_NS "\"><x:E/></R>";
  static const char unknown_extra[] = "<R xmlns=\"" BCG_FIXTURE_NS "\" xmlns:y=\"urn:test:schema:none\"><y:E/></R>";
  char big[3000];
  char *w;
  sj_t s;
  xml_doc_t *d;
  bim_node_t *root;
  sj_init(&s, PROFILE_DVB_TVA);
  bcg_fixture_put(s.dir, "extra.xsd", extra_xsd);
  ck_assert_int_eq(job_schema_load(&s.job, BCG_FIXTURE_NS), 0);
  ck_assert_int_eq(job_schema_compile(&s.job), 0);

  s.job.nschemas = DVBTVA_SCHEMAS + 1;
  ck_assert_int_eq(job_schemas_open(&s.job), -1);
  s.job.nschemas = 1;
  ck_assert_int_eq(job_schemas_open(&s.job), 0);
  s.job.nschemas = 3;
  s.job.uris[0] = BCG_FIXTURE_NS;
  s.job.uris[1] = EXTRA_NS;
  s.job.uris[2] = "urn:test:schema:absent";
  ck_assert_int_eq(job_schemas_open(&s.job), 0);
  ck_assert_int_eq(s.job.sch.n, 3);
  ck_assert_ptr_nonnull(s.job.xc[1]);
  ck_assert_ptr_null(s.job.xc[2]);
  for (size_t k = 1; k < DVBTVA_SCHEMAS; k++) {
    xsd_set_free(s.job.xsets[k]);
    s.job.xsets[k] = NULL;
    s.job.xc[k] = NULL;
  }

  d = parse_doc(with_extra);
  s.job.nschemas = 0;
  s.job.uris[0] = BCG_FIXTURE_NS;
  ck_assert_int_eq(job_schemas_encode(&s.job, xml_doc_root(d)), 0);
  ck_assert_int_eq(s.job.nschemas, 2);
  ck_assert_int_eq(s.job.eo.multi, 1);
  xml_doc_free(d);
  for (size_t k = 1; k < DVBTVA_SCHEMAS; k++) {
    xsd_set_free(s.job.xsets[k]);
    s.job.xsets[k] = NULL;
    s.job.xc[k] = NULL;
  }
  d = parse_doc(unknown_extra);
  s.job.nschemas = 0;
  ck_assert_int_eq(job_schemas_encode(&s.job, xml_doc_root(d)), 0);
  ck_assert_int_eq(s.job.nschemas, 0);
  xml_doc_free(d);
  d = parse_doc("<R xmlns=\"" BCG_FIXTURE_NS "\"/>");
  ck_assert_int_eq(job_schemas_encode(&s.job, xml_doc_root(d)), 0);
  xml_doc_free(d);

  w = big;
  w += snprintf(w, 100, "<R xmlns=\"%s\"", BCG_FIXTURE_NS);
  for (int i = 0; i < DVBTVA_SCHEMAS + 2; i++) w += snprintf(w, 80, " xmlns:n%d=\"urn:many:%d\"", i, i);
  w += snprintf(w, 20, ">");
  for (int i = 0; i < DVBTVA_SCHEMAS + 2; i++) w += snprintf(w, 40, "<n%d:E/>", i);
  snprintf(w, 20, "</R>");
  d = parse_doc(big);
  ck_assert_int_eq(job_schemas_encode(&s.job, xml_doc_root(d)), -1);
  xml_doc_free(d);

  root = bim_node_new("R");
  root->ns = BCG_FIXTURE_NS;
  ck_assert_int_eq(job_cast_bind(&s.job, root), 0);
  ck_assert_int_eq(bim_node_add_attr_ns(root, XSI_NS, "type", strdup("p:Nope")), 0);
  root->nsdecls = calloc(1, sizeof *root->nsdecls);
  ck_assert_ptr_nonnull(root->nsdecls);
  root->nsdecls[0].prefix = "p";
  root->nsdecls[0].uri = BCG_FIXTURE_NS;
  root->nnsdecls = 1;
  ck_assert_int_eq(job_cast_bind(&s.job, root), -1);
  bim_node_free(root);
  sj_free(&s);
}
END_TEST

static void drop_extras(sj_t *s) {
  for (size_t k = 1; k < DVBTVA_SCHEMAS; k++) {
    xsd_set_free(s->job.xsets[k]);
    s->job.xsets[k] = NULL;
    s->job.xc[k] = NULL;
  }
}

START_TEST(schema_extras_survive_allocation_and_load_failures) {
  static const char with_extra[] = "<R xmlns=\"" BCG_FIXTURE_NS "\" xmlns:x=\"" EXTRA_NS "\"><x:E/></R>";
  static const char broken_xsd[] =
    "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" targetNamespace=\"" EXTRA_NS "\"><xs:element name=\"E\" type=\"missing\"/></xs:schema>";
  sj_t s;
  xml_doc_t *d;
  sj_init(&s, PROFILE_DVB_TVA);
  bcg_fixture_put(s.dir, "extra.xsd", broken_xsd);
  ck_assert_int_eq(job_schema_load(&s.job, BCG_FIXTURE_NS), 0);
  ck_assert_int_eq(job_schema_compile(&s.job), 0);
  d = parse_doc(with_extra);
  s.job.nschemas = 0;
  ck_assert_int_eq(job_schemas_encode(&s.job, xml_doc_root(d)), -1);
  xml_doc_free(d);
  drop_extras(&s);
  bcg_fixture_put(s.dir, "extra.xsd", extra_xsd);
  for (long n = 0;; n++) {
    int hit;
    d = parse_doc(with_extra);
    s.job.nschemas = 0;
    alloc_fail_at(n);
    (void)job_schemas_encode(&s.job, xml_doc_root(d));
    hit = alloc_fail_end();
    xml_doc_free(d);
    drop_extras(&s);
    if (!hit) break;
  }
  sj_free(&s);
}
END_TEST

START_TEST(schema_job_free_handles_each_decoder_init_origin) {
  for (int variant = 0; variant < 3; variant++) {
    sj_t s;
    sj_init(&s, variant == 1 ? PROFILE_SDNS : PROFILE_GENERIC);
    s.job.di_parsed = variant == 2;
    if (variant == 0) ck_assert_int_eq(bim_tvainit_decoderinit(BCG_FIXTURE_NS, NULL, 0, &s.job.di), 0);
    if (variant == 1) memset(&s.job.di, 0, sizeof s.job.di);
    sj_free(&s);
  }
}
END_TEST

static Suite *schema_suite(void) {
  Suite *s = suite_create("dipibim_schema");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 120);
  tcase_add_test(tc, schema_profile_predicates);
  tcase_add_test(tc, schema_loads_and_compiles_every_profile);
  tcase_add_test(tc, schema_reports_missing_and_uncompilable_schemas);
  tcase_add_test(tc, schema_survives_allocation_failures);
  tcase_add_test(tc, schema_extras_open_encode_and_cast);
  tcase_add_test(tc, schema_extras_survive_allocation_and_load_failures);
  tcase_add_test(tc, schema_job_free_handles_each_decoder_init_origin);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(schema_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
