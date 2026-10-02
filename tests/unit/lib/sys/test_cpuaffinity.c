/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#define _GNU_SOURCE
#include <check.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/sys/cpuaffinity.h"

static int first_allowed(void) {
  cpu_set_t set;
  if (sched_getaffinity(0, sizeof set, &set)) return -1;
  for (int i = 0; i < CPU_SETSIZE; i++) if (CPU_ISSET(i, &set)) return i;
  return -1;
}

static int unallowed_cpu(void) {
  cpu_set_t set;
  if (sched_getaffinity(0, sizeof set, &set)) return -1;
  for (int i = 0; i < CPU_SETSIZE; i++) if (!CPU_ISSET(i, &set)) return i;
  return -1;
}

START_TEST(parse_off_and_auto) {
  cpuaff_t a;
  memset(&a, 0xff, sizeof a);
  ck_assert_int_eq(cpuaff_parse(&a, "off"), 0);
  ck_assert_int_eq(a.mode, CPUAFF_OFF);
  ck_assert_uint_eq(a.n, 0);
  ck_assert_int_eq(cpuaff_parse(&a, "auto"), 0);
  ck_assert_int_eq(a.mode, CPUAFF_AUTO);
}
END_TEST

START_TEST(parse_list) {
  cpuaff_t a;
  ck_assert_int_eq(cpuaff_parse(&a, "2-5,8"), 0);
  ck_assert_int_eq(a.mode, CPUAFF_LIST);
  ck_assert_uint_eq(a.n, 5);
  ck_assert_uint_eq(a.cpus[0], 2);
  ck_assert_uint_eq(a.cpus[3], 5);
  ck_assert_uint_eq(a.cpus[4], 8);
  ck_assert_int_eq(cpuaff_parse(&a, "7,3,5"), 0);
  ck_assert_uint_eq(a.n, 3);
  ck_assert_uint_eq(a.cpus[0], 7);
  ck_assert_uint_eq(a.cpus[2], 5);
  ck_assert_int_eq(cpuaff_parse(&a, "0"), 0);
  ck_assert_uint_eq(a.n, 1);
  ck_assert_int_eq(cpuaff_parse(&a, "4-4"), 0);
  ck_assert_uint_eq(a.n, 1);
}
END_TEST

START_TEST(parse_rejects_junk) {
  static const char *const bad[] = {"", ",", "1,", ",1", "1,,2", "-1", "1-", "5-2", "a", "1-b", "1 2", "1;2", "AUTO", "Off", "1024", "99999999999999999999", "1,1", "1-3,2", "+1", "0x1"};
  cpuaff_t a;
  ck_assert_int_eq(cpuaff_parse(&a, "3"), 0);
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    ck_assert_msg(cpuaff_parse(&a, bad[i]) == -1, "accepted '%s'", bad[i]);
    ck_assert_int_eq(a.mode, CPUAFF_LIST);
    ck_assert_uint_eq(a.cpus[0], 3);
  }
}
END_TEST

START_TEST(parse_full_range) {
  cpuaff_t a;
  ck_assert_int_eq(cpuaff_parse(&a, "0-1023"), 0);
  ck_assert_uint_eq(a.n, CPUAFF_MAX);
  ck_assert_uint_eq(a.cpus[CPUAFF_MAX - 1], CPUAFF_MAX - 1);
}
END_TEST

START_TEST(pin_off_is_noop) {
  cpu_set_t before;
  cpu_set_t after;
  cpuaff_t a;
  memset(&a, 0, sizeof a);
  ck_assert_int_eq(sched_getaffinity(0, sizeof before, &before), 0);
  ck_assert_int_eq(cpuaff_pin(&a, 0, "t"), 0);
  ck_assert_int_eq(cpuaff_pin(NULL, 0, "t"), 0);
  ck_assert_int_eq(sched_getaffinity(0, sizeof after, &after), 0);
  ck_assert(CPU_EQUAL(&before, &after));
}
END_TEST

START_TEST(pin_auto_single_cpu) {
  cpu_set_t now;
  cpuaff_t a;
  ck_assert_int_eq(cpuaff_parse(&a, "auto"), 0);
  ck_assert_int_eq(cpuaff_pin(&a, 0, "t"), 0);
  ck_assert_int_eq(sched_getaffinity(0, sizeof now, &now), 0);
  ck_assert_int_eq(CPU_COUNT(&now), 1);
  ck_assert_int_eq(CPU_ISSET(first_allowed(), &now), 1);
}
END_TEST

START_TEST(pin_auto_wraps_modulo) {
  cpu_set_t allowed;
  cpu_set_t now;
  cpuaff_t a;
  unsigned n;
  int first;
  ck_assert_int_eq(sched_getaffinity(0, sizeof allowed, &allowed), 0);
  n = (unsigned)CPU_COUNT(&allowed);
  first = first_allowed();
  ck_assert_int_eq(cpuaff_parse(&a, "auto"), 0);
  ck_assert_int_eq(cpuaff_pin(&a, n, "t"), 0);
  ck_assert_int_eq(sched_getaffinity(0, sizeof now, &now), 0);
  ck_assert_int_eq(CPU_COUNT(&now), 1);
  ck_assert_int_eq(CPU_ISSET(first, &now), 1);
}
END_TEST

START_TEST(pin_list_entry) {
  cpu_set_t now;
  cpuaff_t a;
  char spec[16];
  int first = first_allowed();
  ck_assert_int_ge(first, 0);
  snprintf(spec, sizeof spec, "%d", first);
  ck_assert_int_eq(cpuaff_parse(&a, spec), 0);
  ck_assert_int_eq(cpuaff_pin(&a, 0, "t"), 0);
  ck_assert_int_eq(sched_getaffinity(0, sizeof now, &now), 0);
  ck_assert_int_eq(CPU_COUNT(&now), 1);
  ck_assert_int_eq(CPU_ISSET(first, &now), 1);
}
END_TEST

START_TEST(pin_list_short_floats) {
  cpu_set_t before;
  cpu_set_t after;
  cpuaff_t a;
  char spec[16];
  snprintf(spec, sizeof spec, "%d", first_allowed());
  ck_assert_int_eq(cpuaff_parse(&a, spec), 0);
  ck_assert_int_eq(sched_getaffinity(0, sizeof before, &before), 0);
  ck_assert_int_eq(cpuaff_pin(&a, 1, "t"), 1);
  ck_assert_int_eq(sched_getaffinity(0, sizeof after, &after), 0);
  ck_assert(CPU_EQUAL(&before, &after));
}
END_TEST

START_TEST(pin_list_disallowed_cpu_fails) {
  cpu_set_t before;
  cpu_set_t after;
  cpuaff_t a;
  char spec[16];
  int bad = unallowed_cpu();
  if (bad < 0) return;
  snprintf(spec, sizeof spec, "%d", bad);
  ck_assert_int_eq(cpuaff_parse(&a, spec), 0);
  ck_assert_int_eq(sched_getaffinity(0, sizeof before, &before), 0);
  ck_assert_int_eq(cpuaff_pin(&a, 0, "t"), -1);
  ck_assert_int_eq(sched_getaffinity(0, sizeof after, &after), 0);
  ck_assert(CPU_EQUAL(&before, &after));
}
END_TEST

static Suite *cpuaff_suite(void) {
  Suite *s = suite_create("cpuaffinity");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, parse_off_and_auto);
  tcase_add_test(tc, parse_list);
  tcase_add_test(tc, parse_rejects_junk);
  tcase_add_test(tc, parse_full_range);
  tcase_add_test(tc, pin_off_is_noop);
  tcase_add_test(tc, pin_auto_single_cpu);
  tcase_add_test(tc, pin_auto_wraps_modulo);
  tcase_add_test(tc, pin_list_entry);
  tcase_add_test(tc, pin_list_short_floats);
  tcase_add_test(tc, pin_list_disallowed_cpu_fails);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(cpuaff_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
