/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "lib/config/yamlcfg.h"
#include "lib/helper/log.h"

#include "dipitvhead/cli/priv.h"

#include "args_fixture.h"

static const opt_case_t BAD_VALUE[] = {
  {"-b", "0"},
  {"-b", "1000001"},
  {"--pcr-mode", "bogus"},
  {"--pcr-lead-ms", "0"},
  {"--pcr-lead-ms", "1001"},
  {"-e", "x"},
  {"--tsid", "0"},
  {"--tsid", "65536"},
  {"--onid", "0"},
  {"--color", "bogus"},
  {"--metrics-interval", "x"},
  {"--metrics-inspect-ts", "bogus"},
  {"--metrics-inspect-ts-pids", "zz"},
};

static const opt_case_t OVERSIZED_VALUE[] = {
  {"-n", NULL},
  {"--default-provider", NULL},
};

START_TEST(value_errors_are_rejected) {
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_with(&cfg, NULL, &BAD_VALUE[_i]), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(oversized_text_is_rejected) {
  char big[600];
  opt_case_t oc = {OVERSIZED_VALUE[_i].opt, big};
  config_t cfg = {0};

  memset(big, 'a', sizeof big - 1);
  big[sizeof big - 1] = '\0';
  ck_assert_int_eq(args_fixture_parse_with(&cfg, NULL, &oc), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(unknown_option_is_left_to_the_caller) {
  config_t cfg = {0};
  tvh_opt_t p = {.cfg = &cfg};

  ck_assert_int_eq(tvh_opt_general(&p, 'x'), OPT_UNHANDLED);
}
END_TEST

START_TEST(nit_dash_drops_the_table) {
  const char *extra[] = {"-n", "-", NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_OK);
  ck_assert_int_eq(cfg.nit_mode, TABLE_DROP);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(general_options_fill_config) {
  const char *extra[] = {
    "-n", "Network",
    "--default-provider", "Fallback",
    "-b", "5000",
    "-S",
    "-B",
    "--pcr-mode", "regenerate",
    "--pcr-lead-ms", "500",
    "-e", "30",
    "-k",
    "--tsid", "10",
    "--onid", "20",
    "--color", "always",
    "-v",
    "-d",
    NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_OK);
  ck_assert_int_eq(cfg.nit_mode, TABLE_OVERRIDE);
  ck_assert_str_eq(cfg.nit_text, "Network");
  ck_assert_str_eq(cfg.default_provider_text, "Fallback");
  ck_assert_uint_eq(cfg.bitrate_kbps, 5000u);
  ck_assert_int_eq(cfg.stuff, 1);
  ck_assert_int_eq(cfg.burst_limit, 1);
  ck_assert_int_eq(cfg.pcr_mode, PCR_MODE_REGENERATE);
  ck_assert_uint_eq(cfg.pcr_lead_ms, 500u);
  ck_assert_int_eq(cfg.error_retry_s, 30);
  ck_assert_int_eq(cfg.insecure_tls, 1);
  ck_assert_uint_eq(cfg.tsid, 10u);
  ck_assert_uint_eq(cfg.onid, 20u);
  ck_assert_int_eq(cfg.color_mode, LOG_COLOR_ALWAYS);
  ck_assert_int_eq(cfg.verbose, 1);
  ck_assert_int_eq(cfg.daemonize, 1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(metrics_options_fill_config) {
  const char *extra[] = {
    "--metrics", "/tmp/dipi.sock",
    "--metrics-id", "tv1",
    "--metrics-interval", "5",
    "--metrics-inspect-ts", "full",
    "--metrics-inspect-ts-pids", "0x100,0x101",
    NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_OK);
  ck_assert_str_eq(cfg.metrics_sock, "/tmp/dipi.sock");
  ck_assert_str_eq(cfg.metrics_id, "tv1");
  ck_assert_uint_eq(cfg.metrics_interval_s, 5u);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_FULL);
  ck_assert_uint_eq(cfg.metrics_n_known_pids, 2u);
  ck_assert_uint_eq(cfg.metrics_known_pids[0], 0x100u);
  ck_assert_uint_eq(cfg.metrics_known_pids[1], 0x101u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

static Suite *args_general_suite(void) {
  Suite *s = suite_create("dipitvhead_args_general");
  TCase *tc = tcase_create("core");

  tcase_add_loop_test(tc, value_errors_are_rejected, 0, (int)ARRAY_LEN(BAD_VALUE));
  tcase_add_loop_test(tc, oversized_text_is_rejected, 0, (int)ARRAY_LEN(OVERSIZED_VALUE));
  tcase_add_test(tc, unknown_option_is_left_to_the_caller);
  tcase_add_test(tc, nit_dash_drops_the_table);
  tcase_add_test(tc, general_options_fill_config);
  tcase_add_test(tc, metrics_options_fill_config);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(args_general_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
