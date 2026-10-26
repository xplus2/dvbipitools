/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../alloc_fail.h"
#include "lib/bim/dvbtva.h"

#define TVA "urn:tva:metadata:2004"

static bim_node_t *nn(bim_node_t *parent, const char *name, const char *text) {
  bim_node_t *n = parent ? bim_node_add_new(parent, name, text) : bim_node_new(name);
  ck_assert_ptr_nonnull(n);
  n->ns = TVA;
  if (!parent && text) ck_assert_int_eq(bim_node_set_text(n, text), 0);
  return n;
}

static void at(bim_node_t *n, const char *name, const char *v) { ck_assert_int_eq(bim_node_set_attr(n, name, v), 0); }

static int check(unsigned code, bim_node_t *root) {
  bim_tvans_t ns = {TVA, NULL, NULL};
  dvbtva_fuu_t f = {code, root};
  bim_err_t err;
  int rc = dvbtva_check_fragment(&ns, &f, &err);
  bim_node_free(root);
  return rc;
}

static bim_node_t *event(bim_node_t *sched, const char *crid, const char *start, const char *dur, int with_end) {
  bim_node_t *ev = nn(sched, "ScheduleEvent", NULL);
  bim_node_t *p = nn(ev, "Program", NULL);
  if (crid) at(p, "crid", crid);
  if (start) nn(ev, "PublishedStartTime", start);
  if (dur) nn(ev, "PublishedDuration", dur);
  if (with_end) nn(ev, "PublishedEndTime", "2030-01-01T13:00:00Z");
  return ev;
}

START_TEST(rules_check_each_fragment_type) {
  bim_node_t *r;
  bim_node_t *bd;
  r = nn(NULL, "ProgramInformation", NULL);
  at(r, "programId", "crid://x/a");
  ck_assert_int_eq(check(0x0001, r), 0);
  r = nn(NULL, "ProgramInformation", NULL);
  at(r, "programId", "urn:a");
  ck_assert_int_eq(check(0x0001, r), -1);
  r = nn(NULL, "ProgramInformation", NULL);
  at(r, "programId", "crid://");
  ck_assert_int_eq(check(0x0001, r), -1);
  r = nn(NULL, "ProgramInformation", NULL);
  ck_assert_int_eq(check(0x0001, r), -1);
  r = nn(NULL, "ProgramInformation", NULL);
  at(r, "programId", "crid://x/a");
  bd = nn(r, "BasicDescription", NULL);
  at(nn(bd, "Synopsis", "s"), "length", "short");
  ck_assert_int_eq(check(0x0001, r), 0);
  r = nn(NULL, "ProgramInformation", NULL);
  at(r, "programId", "crid://x/a");
  bd = nn(r, "BasicDescription", NULL);
  nn(bd, "Synopsis", "s");
  nn(bd, NULL, "text");
  ck_assert_int_eq(check(0x0001, r), -1);

  r = nn(NULL, "GroupInformation", NULL);
  at(r, "groupId", "crid://x/g");
  bd = nn(r, "BasicDescription", NULL);
  nn(bd, "Title", "t");
  nn(bd, "Synopsis", "only one without length");
  ck_assert_int_eq(check(0x0002, r), 0);
  r = nn(NULL, "GroupInformation", NULL);
  at(r, "groupId", "crid://x/g");
  bd = nn(r, "BasicDescription", NULL);
  nn(bd, "Title", "t");
  at(nn(bd, "Synopsis", "a"), "length", "short");
  at(nn(bd, "Synopsis", "b"), "length", "long");
  ck_assert_int_eq(check(0x0002, r), 0);
  r = nn(NULL, "GroupInformation", NULL);
  at(r, "groupId", "crid://x/g");
  bd = nn(r, "BasicDescription", NULL);
  nn(bd, "Title", "t");
  at(nn(bd, "Synopsis", "a"), "length", "short");
  nn(bd, "Synopsis", "b");
  ck_assert_int_eq(check(0x0002, r), -1);
  r = nn(NULL, "GroupInformation", NULL);
  at(r, "groupId", "crid://x/g");
  nn(r, "BasicDescription", NULL);
  ck_assert_int_eq(check(0x0002, r), -1);
  r = nn(NULL, "GroupInformation", NULL);
  at(r, "groupId", "crid://x/g");
  ck_assert_int_eq(check(0x0002, r), -1);
  r = nn(NULL, "GroupInformation", NULL);
  ck_assert_int_eq(check(0x0002, r), -1);

  r = nn(NULL, "BroadcastEvent", NULL);
  at(nn(r, "Program", NULL), "crid", "crid://x/a");
  ck_assert_int_eq(check(0x0004, r), 0);
  r = nn(NULL, "BroadcastEvent", NULL);
  at(nn(r, "Program", NULL), "crid", "crid://x/a");
  nn(r, "PublishedEndTime", "2030-01-01T13:00:00Z");
  ck_assert_int_eq(check(0x0004, r), -1);
  r = nn(NULL, "BroadcastEvent", NULL);
  ck_assert_int_eq(check(0x0004, r), -1);
  r = nn(NULL, "BroadcastEvent", NULL);
  at(nn(r, "Program", NULL), "crid", "bad");
  ck_assert_int_eq(check(0x0004, r), -1);

  r = nn(NULL, "ServiceInformation", NULL);
  nn(r, "ServiceURL", "dvb://1.2.3");
  ck_assert_int_eq(check(0x0006, r), 0);
  r = nn(NULL, "ServiceInformation", NULL);
  nn(r, "ServiceURL", "rtp://x");
  ck_assert_int_eq(check(0x0006, r), -1);
  r = nn(NULL, "ServiceInformation", NULL);
  ck_assert_int_eq(check(0x0006, r), -1);
  r = nn(NULL, "ServiceInformation", NULL);
  nn(r, "ServiceURL", NULL);
  ck_assert_int_eq(check(0x0006, r), -1);

  for (unsigned code = 0x000C; code <= 0x000D; code++) {
    r = nn(NULL, "SegmentInformation", NULL);
    ck_assert_int_eq(check(code, r), 0);
    r = nn(NULL, "SegmentInformation", NULL);
    at(r, "mediaTimeUnit", "PT1N25F");
    at(nn(r, "Nested", NULL), "mediaTimeUnit", "PT1001N30000F");
    ck_assert_int_eq(check(code, r), 0);
    r = nn(NULL, "SegmentInformation", NULL);
    at(r, "mediaTimeUnit", "PT7N7F");
    ck_assert_int_eq(check(code, r), -1);
    r = nn(NULL, "SegmentInformation", NULL);
    at(nn(r, "Nested", NULL), "mediaTimeUnit", "bogus");
    ck_assert_int_eq(check(code, r), -1);
  }

  r = nn(NULL, "TVAMain", NULL);
  ck_assert_int_eq(check(0x000E, r), 0);
  {
    static const char *const good[] = {"example.com/My%20Guide", "a.b/c", "example.com/a-b_c.d~e!f$g&h'i(j)k*l+m,n;o=p:q@r"};
    static const char *const bad[] = {"nodomain", "/name", "example.com/", "exa%20mple.com/x", "example.com/bad%zz", "example.com/bad%2", "example.com/sp ace", "ex ample/x", "example.com/\xC3\xA9"};
    for (size_t i = 0; i < sizeof good / sizeof *good; i++) {
      r = nn(NULL, "TVAMain", NULL);
      at(r, "publisher", good[i]);
      ck_assert_msg(check(0x000E, r) == 0, "publisher %s", good[i]);
    }
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++) {
      r = nn(NULL, "TVAMain", NULL);
      at(r, "publisher", bad[i]);
      ck_assert_msg(check(0x000E, r) == -1, "publisher %s", bad[i]);
    }
  }
  r = nn(NULL, "Other", NULL);
  ck_assert_int_eq(check(0x0003, r), 0);
}
END_TEST

START_TEST(rules_check_schedules) {
  bim_node_t *r;
  r = nn(NULL, "Schedule", NULL);
  ck_assert_int_eq(check(0x0005, r), 0);
  r = nn(NULL, "Schedule", NULL);
  nn(r, NULL, "text");
  nn(r, "Other", NULL);
  event(r, "crid://x/a", "2030-01-01T12:00:00Z", "PT30M", 0);
  event(r, "crid://x/b", "2030-01-01T12:30:00Z", "PT30M", 0);
  at(r, "start", "2030-01-01T12:00:00Z");
  at(r, "end", "2030-01-01T13:00:00Z");
  ck_assert_int_eq(check(0x0005, r), 0);
  r = nn(NULL, "Schedule", NULL);
  event(r, "crid://x/a", "2030-01-01T12:00:00Z", "PT30M", 1);
  ck_assert_int_eq(check(0x0005, r), -1);
  r = nn(NULL, "Schedule", NULL);
  event(r, "crid://x/a", NULL, "PT30M", 0);
  ck_assert_int_eq(check(0x0005, r), -1);
  r = nn(NULL, "Schedule", NULL);
  event(r, "crid://x/a", "2030-01-01T12:00:00Z", NULL, 0);
  ck_assert_int_eq(check(0x0005, r), -1);
  r = nn(NULL, "Schedule", NULL);
  event(r, "crid://x/a", "bogus", "PT30M", 0);
  ck_assert_int_eq(check(0x0005, r), -1);
  r = nn(NULL, "Schedule", NULL);
  event(r, "crid://x/a", "2030-01-01T12:00:00Z", "bogus", 0);
  ck_assert_int_eq(check(0x0005, r), -1);
  r = nn(NULL, "Schedule", NULL);
  event(r, NULL, "2030-01-01T12:00:00Z", "PT30M", 0);
  ck_assert_int_eq(check(0x0005, r), -1);
  r = nn(NULL, "Schedule", NULL);
  event(r, "bad", "2030-01-01T12:00:00Z", "PT30M", 0);
  ck_assert_int_eq(check(0x0005, r), -1);
  r = nn(NULL, "Schedule", NULL);
  nn(nn(r, "ScheduleEvent", NULL), "PublishedStartTime", "2030-01-01T12:00:00Z");
  ck_assert_int_eq(check(0x0005, r), -1);
  r = nn(NULL, "Schedule", NULL);
  event(r, "crid://x/b", "2030-01-01T12:30:00Z", "PT30M", 0);
  event(r, "crid://x/a", "2030-01-01T12:00:00Z", "PT30M", 0);
  ck_assert_int_eq(check(0x0005, r), -1);
  r = nn(NULL, "Schedule", NULL);
  event(r, "crid://x/a", "2030-01-01T12:00:00Z", "PT30M", 0);
  at(r, "start", "2030-01-01T12:30:00Z");
  ck_assert_int_eq(check(0x0005, r), -1);
  r = nn(NULL, "Schedule", NULL);
  event(r, "crid://x/a", "2030-01-01T12:00:00Z", "PT30M", 0);
  at(r, "start", "bogus");
  ck_assert_int_eq(check(0x0005, r), -1);
  r = nn(NULL, "Schedule", NULL);
  event(r, "crid://x/a", "2030-01-01T12:00:00Z", "PT30M", 0);
  at(r, "end", "2030-01-01T12:10:00Z");
  ck_assert_int_eq(check(0x0005, r), -1);
  r = nn(NULL, "Schedule", NULL);
  event(r, "crid://x/a", "2030-01-01T12:00:00Z", "PT30M", 0);
  at(r, "end", "bogus");
  ck_assert_int_eq(check(0x0005, r), -1);
}
END_TEST

static bim_node_t *frag(const char *name, const char *attr_name, const char *attr_value) {
  bim_node_t *r = nn(NULL, name, NULL);
  if (attr_name) at(r, attr_name, attr_value);
  return r;
}

static int check_all(dvbtva_fuu_t *f, size_t n) {
  bim_tvans_t ns = {TVA, NULL, NULL};
  bim_err_t err;
  int rc = dvbtva_check_set(&ns, f, n, &err);
  for (size_t i = 0; i < n; i++) bim_node_free(f[i].root);
  return rc;
}

START_TEST(rules_check_fragment_sets) {
  dvbtva_fuu_t f[6];
  bim_node_t *sc;
  bim_node_t *pi;

  pi = frag("ProgramInformation", "programId", "crid://x/a");
  at(nn(nn(pi, "BasicDescription", NULL), "MemberOf", NULL), "crid", "crid://x/g");
  f[0] = (dvbtva_fuu_t){0x0001, pi};
  f[1] = (dvbtva_fuu_t){0x0002, frag("GroupInformation", "groupId", "crid://x/g")};
  ck_assert_int_eq(check_all(f, 2), 0);

  pi = frag("ProgramInformation", "programId", "crid://x/a");
  at(nn(nn(pi, "BasicDescription", NULL), "EpisodeOf", NULL), "crid", "crid://x/missing");
  f[0] = (dvbtva_fuu_t){0x0001, pi};
  ck_assert_int_eq(check_all(f, 1), -1);

  pi = frag("ProgramInformation", "programId", "crid://x/a");
  at(nn(nn(pi, "BasicDescription", NULL), "MemberOf", NULL), "crid", "crid://x/missing");
  f[0] = (dvbtva_fuu_t){0x0001, pi};
  ck_assert_int_eq(check_all(f, 1), -1);

  sc = frag("Schedule", "serviceIDRef", "svc1");
  event(sc, "crid://x/a", "2030-01-01T12:00:00Z", "PT30M", 0);
  f[0] = (dvbtva_fuu_t){0x0005, sc};
  f[1] = (dvbtva_fuu_t){0x0006, frag("ServiceInformation", "serviceId", "svc1")};
  f[2] = (dvbtva_fuu_t){0x0001, frag("ProgramInformation", "programId", "crid://x/a")};
  ck_assert_int_eq(check_all(f, 3), 0);

  sc = frag("Schedule", "serviceIDRef", "svc1");
  event(sc, "crid://x/a", "2030-01-01T12:00:00Z", "PT30M", 0);
  f[0] = (dvbtva_fuu_t){0x0005, sc};
  f[1] = (dvbtva_fuu_t){0x0006, frag("ServiceInformation", "serviceId", "svc1")};
  ck_assert_int_eq(check_all(f, 2), -1);

  sc = frag("Schedule", "serviceIDRef", "svc1");
  nn(nn(sc, "ScheduleEvent", NULL), "Other", NULL);
  nn(nn(sc, "ScheduleEvent", NULL), "Program", NULL);
  {
    bim_node_t *ev = event(sc, "crid://x/a", "2030-01-01T12:00:00Z", "PT30M", 0);
    nn(ev, "InstanceDescription", NULL);
  }
  f[0] = (dvbtva_fuu_t){0x0005, sc};
  f[1] = (dvbtva_fuu_t){0x0006, frag("ServiceInformation", "serviceId", "svc1")};
  ck_assert_int_eq(check_all(f, 2), 0);

  sc = frag("Schedule", "serviceIDRef", "svc-missing");
  f[0] = (dvbtva_fuu_t){0x0005, sc};
  ck_assert_int_eq(check_all(f, 1), -1);
  f[0] = (dvbtva_fuu_t){0x0004, frag("BroadcastEvent", "serviceIDRef", "svc-missing")};
  ck_assert_int_eq(check_all(f, 1), -1);
  f[0] = (dvbtva_fuu_t){0x0004, frag("BroadcastEvent", NULL, NULL)};
  ck_assert_int_eq(check_all(f, 1), 0);

  for (int variant = 0; variant < 3; variant++) {
    sc = frag("Schedule", "serviceIDRef", "svc1");
    at(sc, "start", "2030-01-01T12:00:00Z");
    at(sc, "end", "2030-01-01T13:00:00Z");
    f[0] = (dvbtva_fuu_t){0x0005, sc};
    sc = frag("Schedule", "serviceIDRef", "svc1");
    at(sc, "start", variant == 0 ? "2030-01-01T13:00:00Z" : variant == 1 ? "2030-01-01T13:30:00Z" : "bogus");
    at(sc, "end", "2030-01-01T14:00:00Z");
    f[1] = (dvbtva_fuu_t){0x0005, sc};
    f[2] = (dvbtva_fuu_t){0x0006, frag("ServiceInformation", "serviceId", "svc1")};
    ck_assert_int_eq(check_all(f, 3), variant == 1 ? -1 : 0);
  }
}
END_TEST

START_TEST(rules_check_set_allocation_failure) {
  bim_tvans_t ns = {TVA, NULL, NULL};
  bim_err_t err;
  dvbtva_fuu_t f[1];
  int rc;
  f[0] = (dvbtva_fuu_t){0x0003, frag("Other", NULL, NULL)};
  alloc_fail_at(0);
  rc = dvbtva_check_set(&ns, f, 1, &err);
  alloc_fail_end();
  ck_assert_int_eq(rc, -1);
  bim_node_free(f[0].root);
}
END_TEST

static Suite *rules_suite(void) {
  Suite *s = suite_create("bim_dvbtva_rules");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, rules_check_each_fragment_type);
  tcase_add_test(tc, rules_check_schedules);
  tcase_add_test(tc, rules_check_fragment_sets);
  tcase_add_test(tc, rules_check_set_allocation_failure);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(rules_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
