/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "lib/helper/argutil.h"

START_TEST(argutil_numbers_and_ports) {
  unsigned u = 0;
  int i = 0;
  ck_assert_int_eq(argutil_port_parse("80", &u), 0);
  ck_assert_uint_eq(u, 80u);
  ck_assert_int_eq(argutil_port_parse("", &u), -1);
  ck_assert_int_eq(argutil_port_parse("0", &u), -1);
  ck_assert_int_eq(argutil_port_parse("65536", &u), -1);
  ck_assert_int_eq(argutil_port_parse("12x", &u), -1);
  ck_assert_int_eq(argutil_port_parse("99999999999999999999", &u), -1);
  ck_assert_int_eq(argutil_uint_range("5", 1, 9, &u), 0);
  ck_assert_int_eq(argutil_uint_range("", 1, 9, &u), -1);
  ck_assert_int_eq(argutil_uint_range("0", 1, 9, &u), -1);
  ck_assert_int_eq(argutil_uint_range("10", 1, 9, &u), -1);
  ck_assert_int_eq(argutil_uint_range(" 5", 1, 9, &u), -1);
  ck_assert_int_eq(argutil_uint_range("+5", 1, 9, &u), -1);
  ck_assert_int_eq(argutil_uint_range("-1", 0, 0xFFFFFFFFu, &u), -1);
  ck_assert_int_eq(argutil_port_parse(" 80", &u), -1);
  ck_assert_int_eq(argutil_port_parse("+80", &u), -1);
  ck_assert_int_eq(argutil_uint_range("5x", 1, 9, &u), -1);
  ck_assert_int_eq(argutil_uint_range("99999999999999999999", 1, 9, &u), -1);
  ck_assert_int_eq(argutil_rist_key_size("128", &i), 0);
  ck_assert_int_eq(i, 128);
  ck_assert_int_eq(argutil_rist_key_size("256", &i), 0);
  ck_assert_int_eq(argutil_rist_key_size("192", &i), -1);
  ck_assert_int_eq(argutil_rist_key_size("64", &i), -1);
  ck_assert_int_eq(argutil_srt_pbkeylen("16", &i), 0);
  ck_assert_int_eq(argutil_srt_pbkeylen("24", &i), 0);
  ck_assert_int_eq(argutil_srt_pbkeylen("32", &i), 0);
  ck_assert_int_eq(argutil_srt_pbkeylen("20", &i), -1);
  ck_assert_int_eq(argutil_srt_pbkeylen("8", &i), -1);
}
END_TEST

START_TEST(argutil_option_validators_report) {
  char buf[8];
  unsigned u = 0;
  unsigned pids[METRICS_KNOWN_PIDS_MAX + 2];
  unsigned n = 0;
  metrics_inspect_ts_t lvl;
  int c = 0;
  ck_assert_int_eq(argutil_bufcpy_opt("t", buf, sizeof buf, "short", "--x"), 0);
  ck_assert_int_eq(argutil_bufcpy_opt("t", buf, sizeof buf, "far too long", "--x"), -1);
  ck_assert_int_eq(argutil_srt_passphrase_valid(""), 1);
  ck_assert_int_eq(argutil_srt_passphrase_valid("short"), 0);
  ck_assert_int_eq(argutil_srt_passphrase_valid("exactly-10"), 1);
  ck_assert_int_eq(argutil_srt_passphrase_opt("t", "", "--p"), 0);
  ck_assert_int_eq(argutil_srt_passphrase_opt("t", "tiny", "--p"), -1);
  ck_assert_int_eq(argutil_metrics_interval_opt("t", "10", &u), 0);
  ck_assert_int_eq(argutil_metrics_interval_opt("t", "0", &u), -1);
  ck_assert_int_eq(argutil_metrics_opts_validate("t", NULL, NULL, 0), 0);
  ck_assert_int_eq(argutil_metrics_opts_validate("t", "sock", "id", 0), 0);
  ck_assert_int_eq(argutil_metrics_opts_validate("t", "sock", NULL, 0), -1);
  ck_assert_int_eq(argutil_metrics_opts_validate("t", NULL, NULL, 5), -1);
  ck_assert_int_eq(argutil_metrics_known_pids_opt("t", "0x100,200", pids, &n), 0);
  ck_assert_uint_eq(n, 2u);
  ck_assert_uint_eq(pids[0], 256u);
  ck_assert_int_eq(argutil_metrics_known_pids_opt("t", "", pids, &n), -1);
  ck_assert_int_eq(argutil_metrics_known_pids_opt("t", "9000", pids, &n), -1);
  ck_assert_int_eq(argutil_metrics_known_pids_opt("t", "1;2", pids, &n), -1);
  ck_assert_int_eq(argutil_metrics_known_pids_opt("t", "x", pids, &n), -1);
  ck_assert_int_eq(metrics_known_pids_parse("1,2,", pids, &n), 0);
  {
    char many[600];
    many[0] = 0;
    for (int k = 0; k < METRICS_KNOWN_PIDS_MAX + 1; k++) strcat(many, "1,");
    ck_assert_int_eq(metrics_known_pids_parse(many, pids, &n), -1);
  }
  ck_assert_int_eq(argutil_metrics_inspect_ts_opt("t", "full", &lvl), 0);
  ck_assert_int_eq(lvl, METRICS_INSPECT_TS_FULL);
  ck_assert_int_eq(argutil_metrics_inspect_ts_opt("t", "off", &lvl), 0);
  ck_assert_int_eq(argutil_metrics_inspect_ts_opt("t", "basic", &lvl), 0);
  ck_assert_int_eq(argutil_metrics_inspect_ts_opt("t", "medium", &lvl), 0);
  ck_assert_int_eq(argutil_metrics_inspect_ts_opt("t", "nope", &lvl), -1);
  ck_assert_int_eq(argutil_metrics_inspect_ts_validate("t", NULL, METRICS_INSPECT_TS_OFF), 0);
  ck_assert_int_eq(argutil_metrics_inspect_ts_validate("t", NULL, METRICS_INSPECT_TS_BASIC), -1);
  ck_assert_int_eq(argutil_metrics_inspect_ts_validate("t", "id", METRICS_INSPECT_TS_FULL), 0);
  ck_assert_int_eq(metrics_queue_level(METRICS_INSPECT_TS_OFF), SRT_QUEUE_METRICS_OFF);
  ck_assert_int_eq(metrics_queue_level(METRICS_INSPECT_TS_BASIC), SRT_QUEUE_METRICS_BASIC);
  ck_assert_int_eq(metrics_queue_level(METRICS_INSPECT_TS_MEDIUM), SRT_QUEUE_METRICS_FULL);
  ck_assert_int_eq(metrics_queue_level(METRICS_INSPECT_TS_FULL), SRT_QUEUE_METRICS_FULL);
  ck_assert_int_eq(argutil_color_opt("t", "LS1", "always", &c), 0);
  ck_assert_int_eq(argutil_color_opt("t", "LS1", "rainbow", &c), -1);
}
END_TEST

static int parse_seven(const char *s, int *out) {
  if (strcmp(s, "seven")) return -1;
  *out = 7;
  return 0;
}

START_TEST(argutil_maps_and_addresses) {
  static const enum_map_t m[] = {{"a", 1}, {"b", 2}};
  int v = 0;
  int fam = 0;
  char out[64];
  char small[4];
  unsigned port = 0;
  ck_assert_int_eq(map_lookup(m, 2, "b", &v), 0);
  ck_assert_int_eq(v, 2);
  ck_assert_int_eq(map_lookup(m, 2, "z", &v), -1);
  ck_assert_int_eq(argutil_int_opt("t", "LS1", "x", "seven", parse_seven, "hint", &v), 0);
  ck_assert_int_eq(v, 7);
  ck_assert_int_eq(argutil_int_opt("t", NULL, "x", "other", parse_seven, "hint", &v), -1);

  ck_assert_int_eq(argutil_addrport_parse("239.1.2.3:5000", &fam, out, sizeof out, &port), 0);
  ck_assert_int_eq(fam, AF_INET);
  ck_assert_str_eq(out, "239.1.2.3");
  ck_assert_int_eq(argutil_addrport_parse("[ff15::1]:5000", &fam, out, sizeof out, &port), 0);
  ck_assert_int_eq(fam, AF_INET6);
  ck_assert_int_eq(argutil_addrport_parse("[ff15::1", &fam, out, sizeof out, &port), -1);
  ck_assert_int_eq(argutil_addrport_parse("[]:5000", &fam, out, sizeof out, &port), -1);
  ck_assert_int_eq(argutil_addrport_parse("[ff15::1]5000", &fam, out, sizeof out, &port), -1);
  ck_assert_int_eq(argutil_addrport_parse("[ff15::1]:0", &fam, out, sizeof out, &port), -1);
  ck_assert_int_eq(argutil_addrport_parse("[not-an-address]:5000", &fam, out, sizeof out, &port), -1);
  ck_assert_int_eq(argutil_addrport_parse("239.1.2.3", &fam, out, sizeof out, &port), -1);
  ck_assert_int_eq(argutil_addrport_parse(":5000", &fam, out, sizeof out, &port), -1);
  ck_assert_int_eq(argutil_addrport_parse("239.1.2.3:x", &fam, out, sizeof out, &port), -1);
  ck_assert_int_eq(argutil_addrport_parse("999.1.2.3:5000", &fam, out, sizeof out, &port), -1);
  ck_assert_int_eq(argutil_addrport_parse("239.1.2.3:5000", &fam, small, sizeof small, &port), -1);
  {
    char longaddr[120];
    memset(longaddr, '1', sizeof longaddr);
    memcpy(longaddr + 100, ":80", 4);
    ck_assert_int_eq(argutil_addrport_parse(longaddr, &fam, out, sizeof out, &port), -1);
  }

  ck_assert_int_eq(argutil_hostport_parse("host.example:80", &fam, out, sizeof out, &port), 0);
  ck_assert_int_eq(fam, AF_UNSPEC);
  ck_assert_str_eq(out, "host.example");
  ck_assert_int_eq(argutil_hostport_parse("1.2.3.4:80", &fam, out, sizeof out, &port), 0);
  ck_assert_int_eq(fam, AF_INET);
  ck_assert_int_eq(argutil_hostport_parse("[::1]:80", &fam, out, sizeof out, &port), 0);
  ck_assert_int_eq(argutil_hostport_parse("nohostport", &fam, out, sizeof out, &port), -1);
  ck_assert_int_eq(argutil_hostport_parse(":80", &fam, out, sizeof out, &port), -1);
  ck_assert_int_eq(argutil_hostport_parse("bad host:80", &fam, out, sizeof out, &port), -1);
  ck_assert_int_eq(argutil_hostport_parse(".host:80", &fam, out, sizeof out, &port), -1);
  ck_assert_int_eq(argutil_hostport_parse("-host:80", &fam, out, sizeof out, &port), -1);
  ck_assert_int_eq(argutil_hostport_parse("host-:80", &fam, out, sizeof out, &port), -1);
  ck_assert_int_eq(argutil_hostport_parse("host:0", &fam, out, sizeof out, &port), -1);
  ck_assert_int_eq(argutil_hostport_parse("host.example:80", &fam, small, sizeof small, &port), -1);
  ck_assert_int_eq(argutil_hostport_parse("under_score-1.example:80", &fam, out, sizeof out, &port), 0);
  {
    char longhost[300];
    memset(longhost, 'a', sizeof longhost);
    memcpy(longhost + 260, ":80", 4);
    ck_assert_int_eq(argutil_hostport_parse(longhost, &fam, out, sizeof out, &port), -1);
  }
}
END_TEST

START_TEST(argutil_prescan_reads_config_and_flags) {
  static const struct option longopts[] = {{"config", 1, NULL, 'c'}, {"configtest", 0, NULL, 1000}, {"strict", 0, NULL, 1001}, {"help", 0, NULL, 'h'}, {NULL, 0, NULL, 0}};
  char *a1[] = {"t", "-c", "x.yaml", "--configtest", "--strict", "-q"};
  char *a2[] = {"t", "-h", "-c", "later.yaml"};
  const char *cfg = NULL;
  int ct = 0;
  int st = 0;
  ck_assert_int_eq(argutil_prescan(6, a1, "c:hq", longopts, 1000, 1001, &cfg, &ct, &st), 0);
  ck_assert_str_eq(cfg, "x.yaml");
  ck_assert_int_eq(ct, 1);
  ck_assert_int_eq(st, 1);
  cfg = NULL;
  ct = st = 0;
  ck_assert_int_eq(argutil_prescan(4, a2, "c:hq", longopts, 1000, 1001, &cfg, &ct, &st), 1);
  ck_assert_ptr_null(cfg);
}
END_TEST

static Suite *argutil_suite(void) {
  Suite *s = suite_create("argutil");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, argutil_numbers_and_ports);
  tcase_add_test(tc, argutil_option_validators_report);
  tcase_add_test(tc, argutil_maps_and_addresses);
  tcase_add_test(tc, argutil_prescan_reads_config_and_flags);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(argutil_suite());
  int nf;
  srunner_run_all(sr, CK_NORMAL);
  nf = srunner_ntests_failed(sr);
  srunner_free(sr);
  return nf == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
