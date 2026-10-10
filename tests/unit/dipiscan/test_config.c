/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "../cfg_fixture.h"
#include "dipiscan/config.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/log.h"

static const cfg_field_case_t field_cases[] = {
  CFG_FIELD(config_t, "mcast: 239.1.2.0/30\n", CFG_UINT, total, 4, NULL),
  CFG_FIELD(config_t, "mcast: ff0e::1-ff0e::5\n", CFG_INT, family, AF_INET6, NULL),
  CFG_FIELD(config_t, "port: 8000-8002\n", CFG_UINT, port_lo, 8000, NULL),
  CFG_FIELD(config_t, "port: 8000-8002\n", CFG_UINT, port_hi, 8002, NULL),
  CFG_FIELD(config_t, "format: xspf\n", CFG_INT, format, OUT_XSPF, NULL),
  CFG_FIELD(config_t, "provider: example.org\n", CFG_STRPTR, provider, 0, "example.org"),
  CFG_FIELD(config_t, "out: /tmp/scan.m3u\n", CFG_STRPTR, out_path, 0, "/tmp/scan.m3u"),
  CFG_FIELD(config_t, "timeout: 7\n", CFG_INT, timeout_ms, 7000, NULL),
  CFG_FIELD(config_t, "jets: 12\n", CFG_UINT, jets, 12, NULL),
  CFG_FIELD(config_t, "mpts: yes\n", CFG_INT, mpts, 1, NULL),
  CFG_FIELD(config_t, "http-proxy: proxy.example:8080\n", CFG_INT, http_proxy, 1, NULL),
  CFG_FIELD(config_t, "http-proxy: proxy.example:8080\n", CFG_CHARARR, http_proxy_host, 0, "proxy.example"),
  CFG_FIELD(config_t, "http-path: /s/%g/%p/\n", CFG_STRPTR, http_path_tmpl, 0, "/s/%g/%p/"),
  CFG_FIELD(config_t, "iface: eth7\n", CFG_STRPTR, iface, 0, "eth7"),
  CFG_FIELD(config_t, "verbose: on\n", CFG_INT, verbose, 1, NULL),
  CFG_FIELD(config_t, "color: never\n", CFG_INT, color_mode, LOG_COLOR_NEVER, NULL),
};

static const char *const bad_cases[] = {
  "mcast: bogus\n",
  "mcast: 239.1.2.9-239.1.2.1\n",
  "port: 0\n",
  "port: 9-3\n",
  "format: pdf\n",
  "timeout: 0\n",
  "timeout: 3601\n",
  "jets: 0\n",
  "jets: 257\n",
  "mpts: maybe\n",
  "http-proxy: [::1\n",
  "http-path: /a/%z\n",
  "verbose: perhaps\n",
  "color: rainbow\n",
};

static cfg_fixture_t g_fx;

static int load(const char *text, config_t *cfg) {
  int rc;

  cfg_fixture_write(&g_fx, text);
  scan_cfg_defaults(cfg);
  rc = scan_cfg_load(cfg, g_fx.path, 1);
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

  scan_cfg_defaults(&cfg);
  ck_assert_int_eq(cfg.family, AF_INET);
  ck_assert_uint_eq(cfg.total, 256u);
  ck_assert_uint_eq(cfg.port_lo, 8700u);
  ck_assert_uint_eq(cfg.port_hi, 8700u);
  ck_assert_int_eq(cfg.format, OUT_M3U);
  ck_assert_int_eq(cfg.timeout_ms, 1000);
  ck_assert_uint_eq(cfg.jets, 1u);
  ck_assert_int_eq(cfg.mpts, 0);
  ck_assert_int_eq(cfg.http_proxy, 0);
}
END_TEST

START_TEST(unknown_key_is_rejected_in_strict_mode_only) {
  config_t cfg;

  cfg_fixture_write(&g_fx, "bogus-key: 1\n");
  scan_cfg_defaults(&cfg);
  ck_assert_int_eq(scan_cfg_load(&cfg, g_fx.path, 1), -1);
  scan_cfg_defaults(&cfg);
  ck_assert_int_eq(scan_cfg_load(&cfg, g_fx.path, 0), 0);
  cfg_fixture_remove(&g_fx);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(missing_explicit_file_fails) {
  config_t cfg;

  scan_cfg_defaults(&cfg);
  ck_assert_int_eq(scan_cfg_load(&cfg, "/nonexistent/dipiscan.yaml", 0), -1);
}
END_TEST

typedef struct {
  const char *yaml;
  int strict_result;
} cfgtest_case_t;

static const cfgtest_case_t cfgtest_cases[] = {
  {"mcast: 239.1.2.3\nport: 8700\n", 0},
  {"format: xml\n", -1},
  {"format: xml\nprovider: example.org\n", 0},
  {"http-path: /a/%g/\n", -1},
  {"http-path: /a/%g/\nhttp-proxy: p:80\n", 0},
  {"bogus: 1\n", -1},
};

START_TEST(config_test_reports_warnings_and_fails_only_in_strict_mode) {
  cfg_fixture_write(&g_fx, cfgtest_cases[_i].yaml);
  ck_assert_int_eq(scan_cfg_test(g_fx.path, 0), 0);
  ck_assert_int_eq(scan_cfg_test(g_fx.path, 1), cfgtest_cases[_i].strict_result);
  cfg_fixture_remove(&g_fx);
}
END_TEST

START_TEST(config_test_missing_file_fails) {
  ck_assert_int_eq(scan_cfg_test("/nonexistent/dipiscan.yaml", 0), -1);
}
END_TEST

static Suite *config_suite(void) {
  Suite *s = suite_create("dipiscan_config");
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
