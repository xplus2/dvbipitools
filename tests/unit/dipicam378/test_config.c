/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../cfg_fixture.h"
#include "dipicam378/config.h"
#include "dipicam378/version.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/log.h"

#define A64 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define A256 A64 A64 A64 A64

static const cfg_field_case_t field_cases[] = {
  CFG_FIELD(config_t, "key: /dev/null\n", CFG_STRPTR, key_path, 0, "/dev/null"),
  CFG_FIELD(config_t, "serial: SER-9\n", CFG_STRPTR, serial, 0, "SER-9"),
  CFG_FIELD(config_t, "port: 15378\n", CFG_UINT, port, 15378, NULL),
  CFG_FIELD(config_t, "auth: alice:s3cret\n", CFG_STRPTR, username, 0, "alice"),
  CFG_FIELD(config_t, "auth: alice:s3cret\n", CFG_STRPTR, password, 0, "s3cret"),
  CFG_FIELD(config_t, "auth: onlypw\n", CFG_STRPTR, password, 0, "onlypw"),
  CFG_FIELD(config_t, "caid: 2602\n", CFG_UINT, caid, 0x2602, NULL),
  CFG_FIELD(config_t, "caid: FFFF\n", CFG_UINT, caid, 0xFFFF, NULL),
  CFG_FIELD(config_t, "algo: csa2\n", CFG_INT, cw_len, 8, NULL),
  CFG_FIELD(config_t, "algo: cissa\n", CFG_INT, cw_len, 16, NULL),
  CFG_FIELD(config_t, "verbose: on\n", CFG_INT, verbose, 1, NULL),
  CFG_FIELD(config_t, "color: never\n", CFG_INT, color_mode, LOG_COLOR_NEVER, NULL),
  CFG_FIELD(config_t, "metrics: /tmp/m.sock\n", CFG_STRPTR, metrics_sock, 0, "/tmp/m.sock"),
  CFG_FIELD(config_t, "metrics:\n  id: cam1\n", CFG_STRPTR, metrics_id, 0, "cam1"),
  CFG_FIELD(config_t, "metrics:\n  interval: 42\n", CFG_UINT, metrics_interval_s, 42, NULL),
  CFG_FIELD(config_t, "daemonize: 1\n", CFG_INT, daemonize, 1, NULL),
};

static const char *const bad_cases[] = {
  "key: /nonexistent/key.pem\n",
  "port: 0\n",
  "port: 65536\n",
  "auth: " A256 "\n",
  "caid: 0\n",
  "caid: 10000\n",
  "caid: xyz\n",
  "algo: aes\n",
  "verbose: perhaps\n",
  "color: rainbow\n",
  "metrics:\n  interval: 0\n",
  "metrics:\n  interval: 86401\n",
  "daemonize: maybe\n",
};

static cfg_fixture_t g_fx;

static int load(const char *text, config_t *cfg) {
  int rc;

  cfg_fixture_write(&g_fx, text);
  cam378_cfg_defaults(cfg);
  rc = cam378_cfg_load(cfg, g_fx.path, 1);
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

  cam378_cfg_defaults(&cfg);
  ck_assert_uint_eq(cfg.port, ARGS_DEFAULT_PORT);
  ck_assert_str_eq(cfg.password, ARGS_DEFAULT_PASSWORD);
  ck_assert_int_eq(cfg.cw_len, 16);
  ck_assert_ptr_null(cfg.username);
}
END_TEST

typedef struct {
  const char *s;
  int ret;
  unsigned caid;
} caid_case_t;

static const caid_case_t caid_cases[] = {
  {"2602", 0, 0x2602},
  {"0x2602", 0, 0x2602},
  {"ffff", 0, 0xFFFF},
  {"1", 0, 1},
  {"", -1, 0},
  {"0", -1, 0},
  {"10000", -1, 0},
  {"12zz", -1, 0},
};

START_TEST(caid_parsing) {
  unsigned v = 0;

  ck_assert_int_eq(cam378_cfg_caid(caid_cases[_i].s, &v), caid_cases[_i].ret);
  if (!caid_cases[_i].ret) ck_assert_uint_eq(v, caid_cases[_i].caid);
}
END_TEST

START_TEST(unknown_key_is_rejected_in_strict_mode_only) {
  config_t cfg;

  cfg_fixture_write(&g_fx, "bogus-key: 1\n");
  cam378_cfg_defaults(&cfg);
  ck_assert_int_eq(cam378_cfg_load(&cfg, g_fx.path, 1), -1);
  cam378_cfg_defaults(&cfg);
  ck_assert_int_eq(cam378_cfg_load(&cfg, g_fx.path, 0), 0);
  cfg_fixture_remove(&g_fx);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(missing_explicit_file_fails) {
  config_t cfg;

  cam378_cfg_defaults(&cfg);
  ck_assert_int_eq(cam378_cfg_load(&cfg, "/nonexistent/dipicam378.yaml", 0), -1);
}
END_TEST

typedef struct {
  const char *yaml;
  int strict_result;
} cfgtest_case_t;

static const cfgtest_case_t cfgtest_cases[] = {
  {"key: /dev/null\n", 0},
  {"port: 15378\n", -1},
  {"key: /dev/null\nmetrics: /tmp/x.sock\n", -1},
  {"key: /dev/null\nmetrics:\n  interval: 10\n", -1},
  {"key: /dev/null\nmetrics:\n  id: cam1\n  interval: 10\n", 0},
};

START_TEST(config_test_reports_warnings_and_fails_only_in_strict_mode) {
  cfg_fixture_write(&g_fx, cfgtest_cases[_i].yaml);
  ck_assert_int_eq(cam378_cfg_test(g_fx.path, 0), 0);
  ck_assert_int_eq(cam378_cfg_test(g_fx.path, 1), cfgtest_cases[_i].strict_result);
  cfg_fixture_remove(&g_fx);
}
END_TEST

START_TEST(config_test_missing_file_fails) {
  ck_assert_int_eq(cam378_cfg_test("/nonexistent/dipicam378.yaml", 0), -1);
}
END_TEST

static Suite *config_suite(void) {
  Suite *s = suite_create("dipicam378_config");
  TCase *tc = tcase_create("core");

  tcase_add_loop_test(tc, each_key_sets_its_config_field, 0, (int)(sizeof field_cases / sizeof field_cases[0]));
  tcase_add_loop_test(tc, invalid_values_are_rejected, 0, (int)(sizeof bad_cases / sizeof bad_cases[0]));
  tcase_add_test(tc, defaults_are_the_documented_values);
  tcase_add_loop_test(tc, caid_parsing, 0, (int)(sizeof caid_cases / sizeof caid_cases[0]));
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
