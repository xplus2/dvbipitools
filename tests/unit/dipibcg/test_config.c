/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "../cfg_fixture.h"
#include "dipibcg/config.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/log.h"
#include "lib/net/netconnect.h"

static const cfg_field_case_t field_cases[] = {
  CFG_FIELD(config_t, "announce: true\n", CFG_INT, fl.have_a, 1, NULL),
  CFG_FIELD(config_t, "listen: yes\n", CFG_INT, fl.have_l, 1, NULL),
  CFG_FIELD(config_t, "input: /dev/null\n", CFG_STRPTR, input_path, 0, "/dev/null"),
  CFG_FIELD(config_t, "map: /dev/null\n", CFG_STRPTR, map_path, 0, "/dev/null"),
  CFG_FIELD(config_t, "window: 48\n", CFG_LONG, window_hours, 48, NULL),
  CFG_FIELD(config_t, "mcast: 239.1.2.3:5000\n", CFG_CHARARR, mcast_group, 0, "239.1.2.3"),
  CFG_FIELD(config_t, "mcast: 239.1.2.3:5000\n", CFG_UINT, mcast_port, 5000, NULL),
  CFG_FIELD(config_t, "mcast: \"[ff15::1]:5000\"\n", CFG_INT, family, AF_INET6, NULL),
  CFG_FIELD(config_t, "iface: eth7\n", CFG_STRPTR, iface, 0, "eth7"),
  CFG_FIELD(config_t, "dscp: video-high\n", CFG_INT, dscp, NET_DSCP_VIDEO_HIGH, NULL),
  CFG_FIELD(config_t, "interval: 9\n", CFG_LONG, interval_s, 9, NULL),
  CFG_FIELD(config_t, "timeout: 77\n", CFG_LONG, timeout_s, 77, NULL),
  CFG_FIELD(config_t, "output: /tmp/out.xml\n", CFG_STRPTR, output_path, 0, "/tmp/out.xml"),
  CFG_FIELD(config_t, "csv-map: /tmp/map.csv\n", CFG_STRPTR, csvmap_path, 0, "/tmp/map.csv"),
  CFG_FIELD(config_t, "compress: true\n", CFG_INT, compress, 1, NULL),
  CFG_FIELD(config_t, "verbose: on\n", CFG_INT, verbose, 1, NULL),
  CFG_FIELD(config_t, "color: never\n", CFG_INT, color_mode, LOG_COLOR_NEVER, NULL),
  CFG_FIELD(config_t, "daemonize: 1\n", CFG_INT, daemonize, 1, NULL),
  CFG_FIELD(config_t, "metrics:\n  sock: /tmp/m.sock\n", CFG_STRPTR, metrics_sock, 0, "/tmp/m.sock"),
  CFG_FIELD(config_t, "metrics:\n  id: bcg1\n", CFG_STRPTR, metrics_id, 0, "bcg1"),
  CFG_FIELD(config_t, "metrics:\n  interval: 42\n", CFG_UINT, metrics_interval_s, 42, NULL),
};

static const char *const bad_cases[] = {
  "announce: maybe\n",
  "listen: maybe\n",
  "window: 0\n",
  "window: forever\n",
  "mcast: bogus\n",
  "mcast: 127.0.0.1:5000\n",
  "dscp: bogus\n",
  "interval: -1\n",
  "timeout: soon\n",
  "compress: maybe\n",
  "verbose: perhaps\n",
  "color: rainbow\n",
  "daemonize: maybe\n",
  "metrics:\n  interval: 0\n",
  "metrics:\n  interval: 86401\n",
};

static cfg_fixture_t g_fx;

static int load(const char *text, config_t *cfg) {
  int rc;

  cfg_fixture_write(&g_fx, text);
  bcg_cfg_defaults(cfg);
  rc = bcg_cfg_load(cfg, g_fx.path, 1);
  cfg_fixture_remove(&g_fx);
  return rc;
}

START_TEST(each_key_sets_its_config_field) {
  const cfg_field_case_t *c = &field_cases[_i];
  config_t cfg;

  ck_assert_int_eq(load(c->yaml, &cfg), 0);
  cfg_field_check(&cfg, c);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(invalid_values_are_rejected) {
  config_t cfg;

  ck_assert_int_eq(load(bad_cases[_i], &cfg), -1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(defaults_are_the_documented_values) {
  config_t cfg;

  bcg_cfg_defaults(&cfg);
  ck_assert_int_eq(cfg.dscp, NET_DSCP_SIGNALLING);
  ck_assert_int_eq((int)cfg.window_hours, 24);
  ck_assert_int_eq((int)cfg.interval_s, 5);
  ck_assert_int_eq((int)cfg.timeout_s, 35);
}
END_TEST

START_TEST(unknown_key_is_rejected_in_strict_mode_only) {
  config_t cfg;

  cfg_fixture_write(&g_fx, "bogus-key: 1\n");
  bcg_cfg_defaults(&cfg);
  ck_assert_int_eq(bcg_cfg_load(&cfg, g_fx.path, 1), -1);
  bcg_cfg_defaults(&cfg);
  ck_assert_int_eq(bcg_cfg_load(&cfg, g_fx.path, 0), 0);
  cfg_fixture_remove(&g_fx);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(missing_explicit_file_fails) {
  config_t cfg;

  bcg_cfg_defaults(&cfg);
  ck_assert_int_eq(bcg_cfg_load(&cfg, "/nonexistent/dipibcg.yaml", 0), -1);
}
END_TEST

#define MC "mcast: 239.1.2.3:5000\n"
#define ANN "announce: true\n" MC "input: /dev/null\nmap: /dev/null\n"

typedef struct {
  const char *yaml;
  int strict_result;
} cfgtest_case_t;

static const cfgtest_case_t cfgtest_cases[] = {
  {ANN, 0},
  {"listen: true\n" MC, 0},
  {MC, -1},
  {"announce: true\ninput: /dev/null\nmap: /dev/null\n", -1},
  {"announce: true\n" MC "map: /dev/null\n", -1},
  {"announce: true\n" MC "input: /dev/null\n", -1},
  {"announce: true\nlisten: true\n" MC, -1},
  {"listen: true\n" MC "metrics:\n  id: m1\n", -1},
  {"listen: true\n" MC "compress: true\n", -1},
  {ANN "metrics:\n  sock: /tmp/x.sock\n", -1},
  {ANN "metrics:\n  interval: 10\n", -1},
};

START_TEST(config_test_reports_warnings_and_fails_only_in_strict_mode) {
  cfg_fixture_write(&g_fx, cfgtest_cases[_i].yaml);
  ck_assert_int_eq(bcg_cfg_test(g_fx.path, 0), 0);
  ck_assert_int_eq(bcg_cfg_test(g_fx.path, 1), cfgtest_cases[_i].strict_result);
  cfg_fixture_remove(&g_fx);
}
END_TEST

START_TEST(config_test_missing_file_fails) {
  ck_assert_int_eq(bcg_cfg_test("/nonexistent/dipibcg.yaml", 0), -1);
}
END_TEST

static Suite *config_suite(void) {
  Suite *s = suite_create("dipibcg_config");
  TCase *tc = tcase_create("core");

  tcase_add_loop_test(tc, each_key_sets_its_config_field, 0, (int)(sizeof field_cases / sizeof field_cases[0]));
  tcase_add_loop_test(tc, invalid_values_are_rejected, 0, (int)(sizeof bad_cases / sizeof bad_cases[0]));
  tcase_add_test(tc, defaults_are_the_documented_values);
  tcase_add_test(tc, unknown_key_is_rejected_in_strict_mode_only);
  tcase_add_test(tc, missing_explicit_file_fails);
  tcase_add_loop_test(tc, config_test_reports_warnings_and_fails_only_in_strict_mode, 0, (int)(sizeof cfgtest_cases / sizeof cfgtest_cases[0]));
  tcase_add_test(tc, config_test_missing_file_fails);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(config_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
