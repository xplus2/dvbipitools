/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../../alloc_fail.h"
#include "../../log_capture.h"
#include "lib/bim/xsd/xsd.h"

#define OPEN "<xs:schema xmlns:xs=\"http://www.w3.org/2001/XMLSchema\" "

static void put_file(const char *dir, const char *name, const char *body) {
  char p[600];
  FILE *f;
  int w = snprintf(p, sizeof p, "%s/%s", dir, name);
  ck_assert(w > 0 && (size_t)w < sizeof p);
  f = fopen(p, "w");
  if (!f) abort();
  fputs(body, f);
  fclose(f);
}

static void rm_tree(const char *dir) {
  char cmd[600];
  snprintf(cmd, sizeof cmd, "rm -rf -- '%s'", dir);
  ck_assert_int_eq(system(cmd), 0);
}

static int load(const char *dir, const char *main_name, char *code) {
  char path[600];
  const char *dirs[1] = {dir};
  xsd_set_t *s = xsd_set_new();
  int rc;
  snprintf(path, sizeof path, "%s/%s", dir, main_name);
  rc = xsd_set_load(s, NULL, path, dirs, 1);
  if (code) snprintf(code, 16, "%s", rc ? xsd_set_error_code(s) : "");
  xsd_set_free(s);
  return rc;
}

static void expect_load(const char *dir, const char *main_name, const char *want) {
  char code[16];
  int rc = load(dir, main_name, code);
  if (want) {
    ck_assert_msg(rc != 0, "%s loaded", main_name);
    ck_assert_msg(!strcmp(code, want), "%s: wanted %s got %s", main_name, want, code);
  } else {
    ck_assert_msg(rc == 0, "%s failed with %s", main_name, code);
  }
}

START_TEST(load_reports_every_reference_failure) {
  char dir[] = "/tmp/xsdloadtestXXXXXX";
  char deep[512];
  char sub[600];
  ck_assert_ptr_nonnull(mkdtemp(dir));
  snprintf(sub, sizeof sub, "%s/sub", dir);
  ck_assert_int_eq(mkdir(sub, 0700), 0);
  put_file(dir, "ok.xsd", OPEN "targetNamespace=\"urn:ok\"><xs:include schemaLocation=\"inc.xsd\"/><xs:include schemaLocation=\"ok.xsd\"/></xs:schema>");
  put_file(dir, "inc.xsd", OPEN "><xs:simpleType name=\"S\"><xs:restriction base=\"xs:string\"/></xs:simpleType></xs:schema>");
  put_file(dir, "imp.xsd", OPEN "xmlns:o=\"urn:other\" targetNamespace=\"urn:imp\"><xs:import namespace=\"urn:other\" schemaLocation=\"other.xsd\"/><xs:element name=\"E\" type=\"o:T\"/></xs:schema>");
  put_file(dir, "other.xsd", OPEN "targetNamespace=\"urn:other\"><xs:simpleType name=\"T\"><xs:restriction base=\"xs:string\"/></xs:simpleType></xs:schema>");
  put_file(dir, "byns.xsd", OPEN "xmlns:o=\"urn:other\" targetNamespace=\"urn:byns\"><xs:import namespace=\"urn:other\"/><xs:element name=\"E\" type=\"o:T\"/></xs:schema>");
  put_file(dir, "nons.xsd", OPEN "targetNamespace=\"urn:nons\"><xs:import namespace=\"urn:missing\"/></xs:schema>");
  put_file(dir, "urlimp.xsd", OPEN "targetNamespace=\"urn:urlimp\"><xs:import namespace=\"urn:other\" schemaLocation=\"http://example.invalid/o.xsd\"/></xs:schema>");
  put_file(dir, "badimp.xsd", OPEN "targetNamespace=\"urn:badimp\"><xs:import namespace=\"urn:wrong\" schemaLocation=\"other.xsd\"/></xs:schema>");
  put_file(dir, "noloc.xsd", OPEN "targetNamespace=\"urn:noloc\"><xs:include/></xs:schema>");
  put_file(dir, "redefnoloc.xsd", OPEN "targetNamespace=\"urn:noloc\"><xs:redefine/></xs:schema>");
  put_file(dir, "missinginc.xsd", OPEN "targetNamespace=\"urn:m\"><xs:include schemaLocation=\"nothere.xsd\"/></xs:schema>");
  put_file(dir, "urlinc.xsd", OPEN "targetNamespace=\"urn:m\"><xs:include schemaLocation=\"urn:x:y\"/></xs:schema>");
  put_file(dir, "bad.xsd", "<xs:schema");
  put_file(dir, "enc.xsd", "<?xml version=\"1.0\" encoding=\"klingon\"?><schema/>");
  put_file(dir, "dupa.xsd", OPEN "targetNamespace=\"urn:dup\"/>");
  put_file(sub, "dupa.xsd", OPEN "targetNamespace=\"urn:dup2\"/>");
  {
    char sub2[600];
    snprintf(sub2, sizeof sub2, "%s/sub2", dir);
    ck_assert_int_eq(mkdir(sub2, 0700), 0);
    put_file(sub, "dupc.xsd", OPEN "targetNamespace=\"urn:bydup\"/>");
    put_file(sub2, "dupc.xsd", OPEN "targetNamespace=\"urn:bydup\"/>");
  }
  put_file(dir, "bydup.xsd", OPEN "targetNamespace=\"urn:bydup\"><xs:include schemaLocation=\"dupc.xsd\"/></xs:schema>");
  put_file(dir, "ambiguous.xsd", OPEN "targetNamespace=\"urn:amb\"><xs:import namespace=\"urn:dup\"/></xs:schema>");
  put_file(sub, "dupb.xsd", OPEN "targetNamespace=\"urn:dup\"/>");
  put_file(dir, ".hidden.xsd", OPEN "targetNamespace=\"urn:hidden\"/>");
  put_file(dir, "redef.xsd", OPEN "xmlns:r=\"urn:redef\" targetNamespace=\"urn:redef\"><xs:redefine schemaLocation=\"inc.xsd\"><xs:simpleType name=\"S\"><xs:restriction base=\"r:S\"/></xs:simpleType></xs:redefine></xs:schema>");
  expect_load(dir, "ok.xsd", NULL);
  expect_load(dir, "imp.xsd", NULL);
  expect_load(dir, "byns.xsd", NULL);
  expect_load(dir, "nons.xsd", "LS000746");
  expect_load(dir, "urlimp.xsd", NULL);
  expect_load(dir, "badimp.xsd", "LS400276");
  expect_load(dir, "noloc.xsd", "LS000748");
  expect_load(dir, "redefnoloc.xsd", "LS000748");
  expect_load(dir, "missinginc.xsd", "LS000744");
  expect_load(dir, "urlinc.xsd", "LS000744");
  expect_load(dir, "bad.xsd", "LS400248");
  expect_load(dir, "enc.xsd", "LS400249");
  expect_load(dir, "nothere.xsd", "LS000100");
  expect_load(dir, "ambiguous.xsd", "LS000747");
  expect_load(dir, "bydup.xsd", "LS000745");
  expect_load(dir, "redef.xsd", NULL);
  snprintf(deep, sizeof deep, "%s", dir);
  for (int i = 0; i < 20; i++) {
    size_t at = strlen(deep);
    snprintf(deep + at, sizeof deep - at, "/d%d", i);
    ck_assert_int_eq(mkdir(deep, 0700), 0);
  }
  put_file(deep, "deepmost.xsd", OPEN "targetNamespace=\"urn:deepmost\"/>");
  {
    char out[600];
    const char *dirs[1] = {dir};
    xsd_set_t *s = xsd_set_new();
    ck_assert_int_eq(xsd_find_schema(s, NULL, dirs, 1, "urn:deepmost", out, sizeof out), 1);
    ck_assert_int_eq(xsd_find_schema(s, NULL, dirs, 1, "urn:ok", out, sizeof out), 0);
    xsd_set_free(s);
  }
  rm_tree(dir);
}
END_TEST

START_TEST(load_rejects_an_unreadable_file) {
  char dir[] = "/tmp/xsdloadtestXXXXXX";
  char p[600];
  ck_assert_ptr_nonnull(mkdtemp(dir));
  put_file(dir, "locked.xsd", OPEN "targetNamespace=\"urn:l\"/>");
  snprintf(p, sizeof p, "%s/locked.xsd", dir);
  ck_assert_int_eq(chmod(p, 0), 0);
  if (access(p, R_OK)) expect_load(dir, "locked.xsd", "LS000100");
  chmod(p, 0600);
  rm_tree(dir);
}
END_TEST

START_TEST(load_report_prints_every_outcome) {
  char msg[4096];
  xsd_set_t *s = xsd_set_new();
  log_capture_begin();
  xsd_load_report("AE", "AW", XSD_LOAD_NONE, NULL, "urn:a", s);
  xsd_load_report("AE", "AW", XSD_LOAD_SEVERAL, NULL, "urn:a", s);
  xsd_load_report("AE", "AW", XSD_LOAD_FILES, "/x/b.xsd", NULL, s);
  xsd_load_report("AE", "AW", XSD_LOAD_STAT, "/x/c.xsd", NULL, s);
  xsd_load_report("AE", "AW", -2, NULL, NULL, s);
  xsd_load_report("AE", "AW", -1, NULL, NULL, s);
  log_capture_end(msg, sizeof msg);
  ck_assert_ptr_nonnull(strstr(msg, "no schema file for namespace urn:a"));
  ck_assert_ptr_nonnull(strstr(msg, "several schema files"));
  ck_assert_ptr_nonnull(strstr(msg, "more than one schema file given: /x/b.xsd"));
  ck_assert_ptr_nonnull(strstr(msg, "cannot open /x/c.xsd"));
  ck_assert_ptr_nonnull(strstr(msg, "out of memory"));
  xsd_set_free(s);
}
END_TEST

START_TEST(load_survives_allocation_failures) {
  char dir[] = "/tmp/xsdloadtestXXXXXX";
  char sub[600];
  ck_assert_ptr_nonnull(mkdtemp(dir));
  snprintf(sub, sizeof sub, "%s/sub", dir);
  ck_assert_int_eq(mkdir(sub, 0700), 0);
  put_file(dir, "imp.xsd", OPEN "xmlns:o=\"urn:other\" targetNamespace=\"urn:imp\"><xs:include schemaLocation=\"inc.xsd\"/><xs:import namespace=\"urn:other\"/><xs:element name=\"E\" type=\"o:T\"/></xs:schema>");
  put_file(dir, "inc.xsd", OPEN "><xs:simpleType name=\"S\"><xs:restriction base=\"xs:string\"/></xs:simpleType></xs:schema>");
  put_file(sub, "other.xsd", OPEN "targetNamespace=\"urn:other\"><xs:simpleType name=\"T\"><xs:restriction base=\"xs:string\"/></xs:simpleType></xs:schema>");
  for (long n = 0;; n++) {
    char path[600];
    const char *dirs[1] = {dir};
    xsd_set_t *s = xsd_set_new();
    int hit;
    snprintf(path, sizeof path, "%s/imp.xsd", dir);
    alloc_fail_at(n);
    (void)xsd_set_load(s, NULL, path, dirs, 1);
    hit = alloc_fail_end();
    xsd_set_free(s);
    if (!hit) break;
  }
  for (long n = 0;; n++) {
    char out[600];
    const char *dirs[1] = {dir};
    xsd_set_t *s = xsd_set_new();
    const char *paths[2];
    const char *culprit = NULL;
    char tns[64];
    int hit;
    paths[0] = dir;
    alloc_fail_at(n);
    (void)xsd_find_schema(s, NULL, dirs, 1, "urn:other", out, sizeof out);
    (void)xsd_load_for_ns(s, NULL, paths, 1, "urn:other", &culprit, tns, sizeof tns);
    hit = alloc_fail_end();
    xsd_set_free(s);
    if (!hit) break;
  }
  rm_tree(dir);
}
END_TEST

static Suite *load_suite(void) {
  Suite *s = suite_create("bim_xsd_load");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 120);
  tcase_add_test(tc, load_reports_every_reference_failure);
  tcase_add_test(tc, load_rejects_an_unreadable_file);
  tcase_add_test(tc, load_report_prints_every_outcome);
  tcase_add_test(tc, load_survives_allocation_failures);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(load_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
