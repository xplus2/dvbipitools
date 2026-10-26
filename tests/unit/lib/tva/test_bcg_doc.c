/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/tva/bcg_doc.h"
#include "lib/sys/ioutil.h"

START_TEST(bcg_add_channel_grows_past_initial_capacity) {
  bcg_doc_t d;
  int i;
  bcg_doc_init(&d);

  for (i = 0; i < 40; i++) { /* initial cap is 16, forces grow() at least twice */
    bcg_channel_t *c = bcg_add_channel(&d);
    ck_assert_ptr_nonnull(c);
    snprintf(c->id, sizeof c->id, "ch%d", i);
  }
  ck_assert_int_eq(d.channel_count, 40);
  for (i = 0; i < 40; i++) {
    char id[16];
    snprintf(id, sizeof id, "ch%d", i);
    ck_assert_str_eq(d.channels[i].id, id);
  }

  bcg_doc_free(&d);
}
END_TEST

START_TEST(bcg_find_channel_by_id) {
  bcg_doc_t d;
  bcg_channel_t *c;
  bcg_doc_init(&d);
  c = bcg_add_channel(&d);
  bufcpy(c->id, sizeof c->id, "bbc1");
  c = bcg_add_channel(&d);
  bufcpy(c->id, sizeof c->id, "bbc2");

  ck_assert_ptr_nonnull(bcg_find_channel(&d, "bbc1"));
  ck_assert_str_eq(bcg_find_channel(&d, "bbc2")->id, "bbc2");
  ck_assert_ptr_null(bcg_find_channel(&d, "nope"));

  bcg_doc_free(&d);
}
END_TEST

START_TEST(bcg_channel_add_name_caps_at_max_and_reports_drop) {
  bcg_doc_t d;
  bcg_channel_t *c;
  bcg_doc_init(&d);
  c = bcg_add_channel(&d);

  for (int i = 0; i < BCG_MAX_NAMES + 3; i++) {
    char name[16];
    snprintf(name, sizeof name, "name%d", i);
    ck_assert_int_eq(bcg_channel_add_name(c, name), i < BCG_MAX_NAMES ? 0 : -1);
  }
  ck_assert_int_eq(c->name_count, BCG_MAX_NAMES);
  ck_assert_str_eq(c->names[0], "name0");
  ck_assert_str_eq(c->names[BCG_MAX_NAMES - 1], "name7");

  bcg_doc_free(&d);
}
END_TEST

START_TEST(bcg_add_programme_zeroes_new_entries) {
  bcg_doc_t d;
  bcg_programme_t *pr;
  bcg_doc_init(&d);
  pr = bcg_add_programme(&d);
  ck_assert_ptr_nonnull(pr);
  ck_assert_ptr_null(pr->title);
  ck_assert_uint_eq(strlen(pr->channel_id), 0u);
  ck_assert_int_eq(d.programme_count, 1);

  bcg_doc_free(&d);
}
END_TEST

START_TEST(bcg_set_text_copies_replaces_and_maps_empty_to_null) {
  char *t = NULL;
  char src[] = "abc";
  ck_assert_int_eq(bcg_set_text(&t, src), 0);
  src[0] = 'x';
  ck_assert_str_eq(t, "abc");
  ck_assert_int_eq(bcg_set_text(&t, "def"), 0);
  ck_assert_str_eq(t, "def");
  ck_assert_int_eq(bcg_set_text(&t, ""), 0);
  ck_assert_ptr_null(t);
  ck_assert_int_eq(bcg_set_text(&t, NULL), 0);
  ck_assert_ptr_null(t);
}
END_TEST

START_TEST(bcg_programme_copy_is_deep) {
  bcg_programme_t a;
  bcg_programme_t b;
  memset(&a, 0, sizeof a);
  ck_assert_int_eq(bcg_set_text(&a.title, "T"), 0);
  ck_assert_int_eq(bcg_set_text(&a.desc, "D"), 0);
  ck_assert_int_eq(bcg_programme_copy(&b, &a), 0);
  ck_assert_ptr_ne(a.title, b.title);
  ck_assert_str_eq(b.title, "T");
  ck_assert_str_eq(b.desc, "D");
  ck_assert_ptr_null(b.category);
  bcg_programme_free_text(&a);
  ck_assert_str_eq(b.title, "T");
  bcg_programme_free_text(&b);
}
END_TEST

START_TEST(bcg_elem_text_extracts_or_clears) {
  static const char xml[] = "<a><t>Hello &amp; bye</t><e></e><n/></a>";
  const char *end = xml + sizeof xml - 1;
  char *out = NULL;
  int rc;
  ck_assert_int_eq(bcg_elem_text(xml, end, "t", &out), 0);
  ck_assert_str_eq(out, "Hello & bye");
  ck_assert_int_eq(bcg_elem_text(xml, end, "e", &out), 0);
  ck_assert_ptr_null(out);
  ck_assert_int_eq(bcg_elem_text(xml, end, "t", &out), 0);
  ck_assert_int_eq(bcg_elem_text(xml, end, "missing", &out), 0);
  free(out);
  out = NULL;
  alloc_fail_at(0);
  rc = bcg_elem_text(xml, end, "t", &out);
  alloc_fail_end();
  ck_assert_int_eq(rc, -1);
  ck_assert_ptr_null(out);
}
END_TEST

START_TEST(bcg_publisher_and_lang_validation) {
  static const char *const pub_ok[] = {"example.com/Guide", "a-b.c/x", "example.com/My%20Guide", "a/b", "ex.ample/a-._~!$&'()*+,;=:@%41"};
  static const char *const pub_bad[] = {"", "/x", "nodomain", "ex ample.com/g", ".a/b", "a./b", "example.com/", "example.com/sp ace", "example.com/%zz", "exa_mple.com/g", "ex/\xc3\xa4"};
  static const char *const lang_ok[] = {"en", "en-GB", "de-CH-1996", "zh-Hant-TW", "x-private"};
  static const char *const lang_bad[] = {"", "-en", "en-", "en--GB", "1n", "en-GB-toolongsubtag", "e n", "en_GB", "abcdefghi"};
  char name[BCG_ID_LEN];
  char big[BCG_ID_LEN + 8];
  char small[4];
  for (size_t i = 0; i < sizeof pub_ok / sizeof pub_ok[0]; i++) {
    ck_assert_msg(bcg_publisher_valid(pub_ok[i]), "valid %s", pub_ok[i]);
    ck_assert_int_eq(bcg_publisher_name(pub_ok[i], name, sizeof name), 0);
  }
  for (size_t i = 0; i < sizeof pub_bad / sizeof pub_bad[0]; i++) ck_assert_msg(!bcg_publisher_valid(pub_bad[i]), "invalid %s", pub_bad[i]);
  ck_assert_int_eq(bcg_publisher_name("example.com/My%20Guide", name, sizeof name), 0);
  ck_assert_str_eq(name, "My Guide");
  ck_assert_int_eq(bcg_publisher_name("example.com/Guide", small, sizeof small), -1);
  memset(big, 'a', sizeof big - 1);
  big[sizeof big - 1] = 0;
  big[5] = '/';
  ck_assert_int_eq(bcg_publisher_valid(big), 0);
  for (size_t i = 0; i < sizeof lang_ok / sizeof lang_ok[0]; i++) ck_assert_msg(bcg_lang_valid(lang_ok[i]), "valid %s", lang_ok[i]);
  for (size_t i = 0; i < sizeof lang_bad / sizeof lang_bad[0]; i++) ck_assert_msg(!bcg_lang_valid(lang_bad[i]), "invalid %s", lang_bad[i]);
  memset(big, 'a', 40);
  big[40] = 0;
  ck_assert_int_eq(bcg_lang_valid(big), 0);
}
END_TEST

START_TEST(bcg_index_groups_programmes_by_channel) {
  bcg_doc_t d;
  bcg_doc_index_t ix;
  bcg_channel_t *c;
  bcg_programme_t *p;
  bcg_doc_init(&d);
  ck_assert_int_eq(bcg_doc_index_build(&ix, &d), 0);
  bcg_doc_index_free(&ix);
  c = bcg_add_channel(&d);
  snprintf(c->id, sizeof c->id, "zeta");
  c = bcg_add_channel(&d);
  snprintf(c->id, sizeof c->id, "alpha");
  ck_assert_int_eq(bcg_doc_index_build(&ix, &d), 0);
  ck_assert_int_eq(ix.nprog[0], 0);
  bcg_doc_index_free(&ix);
  for (int i = 0; i < 5; i++) {
    p = bcg_add_programme(&d);
    const char *chan = i % 2 ? "alpha" : "zeta";
    if (i == 3) chan = "unknown";
    snprintf(p->channel_id, sizeof p->channel_id, "%s", chan);
  }
  ck_assert_int_eq(bcg_doc_index_build(&ix, &d), 0);
  ck_assert_int_eq(ix.nprog[0], 3);
  ck_assert_int_eq(ix.nprog[1], 1);
  ck_assert_int_eq(ix.first[0], 0);
  ck_assert_int_eq(ix.next[0], 2);
  ck_assert_int_eq(ix.next[2], 4);
  ck_assert_int_eq(ix.next[4], -1);
  ck_assert_int_eq(ix.first[1], 1);
  ck_assert_int_eq(ix.next[1], -1);
  ck_assert_int_eq(ix.next[3], -1);
  bcg_doc_index_free(&ix);
  for (long n = 0;; n++) {
    int hit;
    int rc;
    alloc_fail_at(n);
    rc = bcg_doc_index_build(&ix, &d);
    hit = alloc_fail_end();
    if (!rc) bcg_doc_index_free(&ix);
    if (!hit) break;
  }
  bcg_doc_free(&d);
}
END_TEST

START_TEST(bcg_progtext_apply_and_free) {
  bcg_programme_t pr;
  bcg_progtext_t pt;
  memset(&pr, 0, sizeof pr);
  memset(&pt, 0, sizeof pt);
  pt.crid = strdup("crid://x/1");
  pt.title = strdup("T");
  pt.desc = strdup("D");
  pt.category = NULL;
  ck_assert_int_eq(bcg_progtext_apply(&pr, &pt), 0);
  ck_assert_str_eq(pr.title, "T");
  ck_assert_str_eq(pr.desc, "D");
  ck_assert_ptr_null(pr.category);
  for (int field = 0; field < 3; field++) {
    bcg_programme_t q;
    int rc;
    memset(&q, 0, sizeof q);
    alloc_fail_at(field);
    rc = bcg_progtext_apply(&q, &pt);
    alloc_fail_end();
    ck_assert_int_eq(rc, field < 2 ? -1 : 0);
    bcg_programme_free_text(&q);
  }
  pr.crid = strdup("c");
  bcg_programme_free_text(&pr);
  ck_assert_ptr_null(pr.crid);
  bcg_progtext_free(&pt);
  ck_assert_ptr_null(pt.crid);
}
END_TEST

START_TEST(bcg_channel_dtt_round_trips) {
  bcg_channel_t c;
  bcg_channel_t d;
  char buf[64];
  char small[8];
  memset(&c, 0, sizeof c);
  memset(&d, 0, sizeof d);
  bcg_channel_dtt_format(&c, buf, sizeof buf);
  ck_assert_str_eq(buf, "");
  c.onid = 0x233a;
  c.tsid = 0x1000;
  c.sid = 0xab;
  bcg_channel_dtt_format(&c, buf, sizeof buf);
  ck_assert_str_eq(buf, "dvb://233a.1000.ab");
  ck_assert_int_eq(bcg_channel_dtt_parse(&d, buf), 0);
  ck_assert_uint_eq(d.onid, 0x233au);
  ck_assert_uint_eq(d.tsid, 0x1000u);
  ck_assert_uint_eq(d.sid, 0xabu);
  ck_assert_int_eq(bcg_channel_dtt_parse(&d, "dvb://1.2"), -1);
  ck_assert_int_eq(bcg_channel_dtt_parse(&d, "http://x"), -1);
  bcg_channel_dtt_format(&c, small, sizeof small);
  ck_assert_int_lt((int)strlen(small), 8);
}
END_TEST

START_TEST(bcg_adders_and_copy_survive_allocation_failure) {
  bcg_doc_t d;
  bcg_programme_t a;
  bcg_programme_t b;
  char *t = NULL;
  bcg_channel_t *ch;
  bcg_programme_t *pg;
  int rc1;
  bcg_doc_init(&d);
  alloc_fail_at(0);
  ch = bcg_add_channel(&d);
  alloc_fail_end();
  ck_assert_ptr_null(ch);
  alloc_fail_at(0);
  pg = bcg_add_programme(&d);
  alloc_fail_end();
  ck_assert_ptr_null(pg);
  alloc_fail_at(0);
  rc1 = bcg_set_text(&t, "x");
  alloc_fail_end();
  ck_assert_int_eq(rc1, -1);
  memset(&a, 0, sizeof a);
  a.title = strdup("T");
  a.desc = strdup("D");
  a.category = strdup("C");
  for (int k = 0; k < 3; k++) {
    int rc;
    alloc_fail_at(k);
    rc = bcg_programme_copy(&b, &a);
    alloc_fail_end();
    ck_assert_int_eq(rc, -1);
    ck_assert_ptr_null(b.title);
  }
  bcg_programme_free_text(&a);
  bcg_doc_free(&d);
}
END_TEST

static Suite *bcg_doc_suite(void) {
  Suite *s = suite_create("bcg_doc");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, bcg_add_channel_grows_past_initial_capacity);
  tcase_add_test(tc, bcg_find_channel_by_id);
  tcase_add_test(tc, bcg_channel_add_name_caps_at_max_and_reports_drop);
  tcase_add_test(tc, bcg_add_programme_zeroes_new_entries);
  tcase_add_test(tc, bcg_set_text_copies_replaces_and_maps_empty_to_null);
  tcase_add_test(tc, bcg_programme_copy_is_deep);
  tcase_add_test(tc, bcg_elem_text_extracts_or_clears);
  tcase_add_test(tc, bcg_publisher_and_lang_validation);
  tcase_add_test(tc, bcg_index_groups_programmes_by_channel);
  tcase_add_test(tc, bcg_progtext_apply_and_free);
  tcase_add_test(tc, bcg_channel_dtt_round_trips);
  tcase_add_test(tc, bcg_adders_and_copy_survive_allocation_failure);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(bcg_doc_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
