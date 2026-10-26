/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../cfg_fixture.h"
#include "dipibim/config.h"
#include "lib/bim/codec.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/log.h"

static const cfg_field_case_t field_cases[] = {
  CFG_FIELD(config_t, "input: /dev/null\n", CFG_STRPTR, input_path, 0, "/dev/null"),
  CFG_FIELD(config_t, "output: /tmp/out.bim\n", CFG_STRPTR, output_path, 0, "/tmp/out.bim"),
  CFG_FIELD(config_t, "format: bim\n", CFG_INT, format, FMT_BIM, NULL),
  CFG_FIELD(config_t, "format: xml\n", CFG_INT, have_format, 1, NULL),
  CFG_FIELD(config_t, "profile: tva\n", CFG_INT, profile, PROFILE_TVA, NULL),
  CFG_FIELD(config_t, "profile: sdns\n", CFG_INT, profile, PROFILE_SDNS, NULL),
  CFG_FIELD(config_t, "profile: generic\n", CFG_INT, profile, PROFILE_GENERIC, NULL),
  CFG_FIELD(config_t, "schema:\n  - /dev/null\n  - /tmp\n", CFG_INT, nschemas, 2, NULL),
  CFG_FIELD(config_t, "schema:\n  - /dev/null\n  - /tmp\n", CFG_STRPTR, schemas[1], 0, "/tmp"),
  CFG_FIELD(config_t, "syntax: 15938-1\n", CFG_INT, syntax, SYNTAX_15938, NULL),
  CFG_FIELD(config_t, "compat: time-flags,fixed-attr\n", CFG_UINT, compat, BIM_COMPAT_M7_FLAGS | BIM_COMPAT_FIXED_ATTR, NULL),
  CFG_FIELD(config_t, "len-mode: 2\n", CFG_UINT, len_mode, 2, NULL),
  CFG_FIELD(config_t, "deferred: yes\n", CFG_INT, deferred, 1, NULL),
  CFG_FIELD(config_t, "cast: on\n", CFG_INT, cast, 1, NULL),
  CFG_FIELD(config_t, "dvb-locator: true\n", CFG_INT, dvb_locator, 1, NULL),
  CFG_FIELD(config_t, "dvb-duration: true\n", CFG_INT, dvb_duration, 1, NULL),
  CFG_FIELD(config_t, "dvb-pubtime: true\n", CFG_INT, dvb_pubtime, 1, NULL),
  CFG_FIELD(config_t, "zbuf-size: 16777215\n", CFG_UINT, zbuf_size, 16777215, NULL),
  CFG_FIELD(config_t, "zgather: 1\n", CFG_INT, zgather, 1, NULL),
  CFG_FIELD(config_t, "verbose: on\n", CFG_INT, verbose, 1, NULL),
  CFG_FIELD(config_t, "color: never\n", CFG_INT, color_mode, LOG_COLOR_NEVER, NULL),
};

static const char *const bad_cases[] = {
  "format: json\n",
  "profile: mpeg7\n",
  "syntax: 2002\n",
  "compat: bogus\n",
  "compat: attr-local,\n",
  "len-mode: 3\n",
  "len-mode: -1\n",
  "deferred: maybe\n",
  "cast: maybe\n",
  "dvb-locator: maybe\n",
  "dvb-duration: maybe\n",
  "dvb-pubtime: maybe\n",
  "zbuf-size: 16777216\n",
  "zbuf-size: lots\n",
  "zgather: maybe\n",
  "verbose: perhaps\n",
  "color: rainbow\n",
};

static cfg_fixture_t g_fx;

static int load(const char *text, config_t *cfg) {
  int rc;

  cfg_fixture_write(&g_fx, text);
  bim_cfg_defaults(cfg);
  rc = bim_cfg_load(cfg, g_fx.path, 1);
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

START_TEST(schema_list_is_capped) {
  char text[1024] = "schema:\n";
  config_t cfg;

  for (int i = 0; i <= SCHEMA_MAX; i++) strcat(text, "  - /dev/null\n");
  ck_assert_int_eq(load(text, &cfg), -1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(defaults_are_the_documented_values) {
  config_t cfg;

  bim_cfg_defaults(&cfg);
  ck_assert_int_eq(cfg.profile, PROFILE_DVB_TVA);
  ck_assert_int_eq(cfg.nschemas, 0);
  ck_assert_uint_eq(cfg.len_mode, 0);
  ck_assert_uint_eq(cfg.zbuf_size, 0);
}
END_TEST

START_TEST(unknown_key_is_rejected_in_strict_mode_only) {
  config_t cfg;

  cfg_fixture_write(&g_fx, "bogus-key: 1\n");
  bim_cfg_defaults(&cfg);
  ck_assert_int_eq(bim_cfg_load(&cfg, g_fx.path, 1), -1);
  bim_cfg_defaults(&cfg);
  ck_assert_int_eq(bim_cfg_load(&cfg, g_fx.path, 0), 0);
  cfg_fixture_remove(&g_fx);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(missing_explicit_file_fails) {
  config_t cfg;

  bim_cfg_defaults(&cfg);
  ck_assert_int_eq(bim_cfg_load(&cfg, "/nonexistent/dipibim.yaml", 0), -1);
}
END_TEST

START_TEST(config_test_accepts_a_valid_file) {
  cfg_fixture_write(&g_fx, "profile: tva\nschema:\n  - /tmp\nzbuf-size: 8\n");
  ck_assert_int_eq(bim_cfg_test(g_fx.path, 1), 0);
  cfg_fixture_remove(&g_fx);
}
END_TEST

START_TEST(config_test_missing_file_fails) {
  ck_assert_int_eq(bim_cfg_test("/nonexistent/dipibim.yaml", 0), -1);
}
END_TEST

static Suite *config_suite(void) {
  Suite *s = suite_create("dipibim_config");
  TCase *tc = tcase_create("core");

  tcase_add_loop_test(tc, each_key_sets_its_config_field, 0, (int)(sizeof field_cases / sizeof field_cases[0]));
  tcase_add_loop_test(tc, invalid_values_are_rejected, 0, (int)(sizeof bad_cases / sizeof bad_cases[0]));
  tcase_add_test(tc, schema_list_is_capped);
  tcase_add_test(tc, defaults_are_the_documented_values);
  tcase_add_test(tc, unknown_key_is_rejected_in_strict_mode_only);
  tcase_add_test(tc, missing_explicit_file_fails);
  tcase_add_test(tc, config_test_accepts_a_valid_file);
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
