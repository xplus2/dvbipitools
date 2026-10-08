/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "lib/cas/cas_args.h"
#include "lib/config/yamlcfg.h"

#include "args_fixture.h"

static const char *const VENDOR[] = {"--cas-algo", "csa2", "--cas-ecmg", "ecmg.example:1234", "--cas-super-id", "0x1234", "--cas-ecm-id", "7", NULL};

static const opt_case_t BAD_VENDOR_VALUE[] = {
  {"--cas-ecmg-version", "4"},
  {"--cas-super-id", "0"},
  {"--cas-super-id", "zz"},
  {"--cas-ecm-id", "0"},
  {"--cas-ecm-id", "65536"},
  {"--cas-ecm-pid", "0"},
  {"--cas-ecm-pid", "0x2000"},
  {"--cas-emmg-port", "x"},
  {"--cas-emmg-reverse", "nocolon"},
  {"--cas-emmg-version", "5"},
  {"--cas-emmg-max-conns", "0"},
  {"--cas-emm-pid", "0"},
  {"--cas-emm-pid", "0x2000"},
  {"--cas-resilience", "bogus"},
  {"--cas-cwenc-algo", "bogus"},
  {"--cas-cwenc-aes-mode", "bogus"},
};

static const opt_case_t BAD_STANDALONE_VALUE[] = {
  {"--cas-algo", "bogus"},
  {"--cas-ecmg", "nocolon"},
  {"--cas-cp-duration", "0"},
  {"--cas-cp-duration", "86400001"},
  {"--biss2-sw", "0011"},
  {"--biss2-emit-esw", "0011"},
  {"--biss1-sw", "0011"},
  {"--biss2-ca-session-id", "0x10000"},
  {"--biss2-ca-session-id", "zz"},
};

static const opt_case_t NEEDS_ECMG[] = {
  {"--cas-ecmg-version", "2"},
  {"--cas-super-id", "1"},
  {"--cas-ecm-id", "1"},
  {"--cas-ecm-pid", "0x0100"},
  {"--cas-emmg-port", "9000"},
  {"--cas-emmg-reverse", "h:1"},
  {"--cas-emmg-version", "2"},
  {"--cas-emmg-max-conns", "2"},
  {"--cas-emm-pid", "0x0101"},
  {"--cas-resilience", "silent"},
  {"--cas-required", NULL},
  {"--cas-cwenc-algo", "des56"},
  {"--cas-cwenc-aes-mode", "ecb"},
  {"--cas-cwenc-fixed-key", "00"},
  {"--cas-cwenc-key-list-a", "a.txt"},
  {"--cas-cwenc-key-list-b", "b.txt"},
};

START_TEST(vendor_value_errors_are_rejected) {
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_with(&cfg, VENDOR, &BAD_VENDOR_VALUE[_i]), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(oversized_cwenc_values_are_rejected) {
  static const char *const opts[] = {"--cas-cwenc-fixed-key", "--cas-cwenc-key-list-a", "--cas-cwenc-key-list-b"};
  char big[300];
  opt_case_t oc = {opts[_i], big};
  config_t cfg = {0};

  memset(big, 'a', sizeof big - 1);
  big[sizeof big - 1] = '\0';
  ck_assert_int_eq(args_fixture_parse_with(&cfg, VENDOR, &oc), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(standalone_value_errors_are_rejected) {
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_with(&cfg, NULL, &BAD_STANDALONE_VALUE[_i]), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(vendor_option_without_ecmg_is_rejected) {
  static const char *const algo[] = {"--cas-algo", "csa2", NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_with(&cfg, algo, &NEEDS_ECMG[_i]), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(cas_algo_values_are_mapped) {
  static const struct {
    const char *name;
    cas_algo_t algo;
  } map[] = {{"cissa", CAS_ALGO_CISSA}, {"csa2", CAS_ALGO_CSA2}, {"csa1", CAS_ALGO_CSA1}};
  const char *prefix[] = {"--cas-algo", NULL, "--cas-ecmg", "ecmg.example:1234", "--cas-super-id", "1", "--cas-ecm-id", "1", NULL};
  config_t cfg = {0};

  prefix[1] = map[_i].name;
  ck_assert_int_eq(args_fixture_parse_list(&cfg, prefix), ARGS_OK);
  ck_assert_int_eq(cfg.cas_algo, map[_i].algo);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(vendor_options_fill_the_current_vendor) {
  const char *extra[] = {
    "--cas-algo", "csa2",
    "--cas-ecmg", "tcp://ecmg.example:1234",
    "--cas-ecmg-version", "3",
    "--cas-super-id", "0x12345678",
    "--cas-ecm-id", "7",
    "--cas-ecm-pid", "0x0100",
    "--cas-emmg-port", "9000",
    "--cas-emmg-version", "2",
    "--cas-emmg-max-conns", "4",
    "--cas-emm-pid", "0x0101",
    "--cas-resilience", "cycling",
    "--cas-required",
    "--cas-cp-duration", "5000",
    "--cas-fallback-clear",
    NULL};
  config_t cfg = {0};
  const cas_vendor_t *v;

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_OK);
  ck_assert_uint_eq(cfg.n_cas_vendors, 1u);
  v = &cfg.cas_vendors[0];
  ck_assert_str_eq(v->ecmg_host, "ecmg.example");
  ck_assert_uint_eq(v->ecmg_port, 1234u);
  ck_assert_uint_eq(v->ecmg_version, 3u);
  ck_assert_uint_eq(v->super_cas_id, 0x12345678u);
  ck_assert_uint_eq(v->ecm_id, 7u);
  ck_assert_uint_eq(v->ecm_pid, 0x0100u);
  ck_assert_uint_eq(v->emmg_port, 9000u);
  ck_assert_int_eq(v->emmg_port_given, 1);
  ck_assert_uint_eq(v->emmg_version, 2u);
  ck_assert_uint_eq(v->emmg_max_conns, 4u);
  ck_assert_uint_eq(v->emm_pid, 0x0101u);
  ck_assert_int_eq(v->resilience, CAS_OUTAGE_CYCLING);
  ck_assert_int_eq(v->required, 1);
  ck_assert_uint_eq(cfg.cas_cp_duration_ms, 5000u);
  ck_assert_int_eq(cfg.cas_fallback_clear, 1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(emmg_reverse_fills_host_and_port) {
  const char *extra[] = {
    "--cas-algo", "csa2",
    "--cas-ecmg", "ecmg.example:1234",
    "--cas-super-id", "1",
    "--cas-ecm-id", "1",
    "--cas-emmg-reverse", "emmg.example:7000",
    NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_OK);
  ck_assert_str_eq(cfg.cas_vendors[0].emmg_reverse_host, "emmg.example");
  ck_assert_uint_eq(cfg.cas_vendors[0].emmg_reverse_port, 7000u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(cwenc_options_are_recorded_before_validation) {
  const char *extra[] = {
    "--cas-algo", "csa2",
    "--cas-ecmg", "ecmg.example:1234",
    "--cas-super-id", "1",
    "--cas-ecm-id", "1",
    "--cas-cwenc-algo", "des56",
    "--cas-cwenc-aes-mode", "ecb",
    "--cas-cwenc-fixed-key", "4DA19FF0AF6B8F",
    NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_ERR);
  ck_assert_str_eq(cfg.cas_vendors[0].cwenc_algorithm, "des56");
  ck_assert_str_eq(cfg.cas_vendors[0].cwenc_aes_mode, "ecb");
  ck_assert_str_eq(cfg.cas_vendors[0].cwenc_fixed_key_hex, "4DA19FF0AF6B8F");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(cwenc_key_list_paths_are_recorded) {
  const char *extra[] = {
    "--cas-algo", "csa2",
    "--cas-ecmg", "ecmg.example:1234",
    "--cas-super-id", "1",
    "--cas-ecm-id", "1",
    "--cas-cwenc-algo", "des56",
    "--cas-cwenc-key-list-a", "/nonexistent/a.keys",
    "--cas-cwenc-key-list-b", "/nonexistent/b.keys",
    NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_ERR);
  ck_assert_str_eq(cfg.cas_vendors[0].cwenc_key_list_a_path, "/nonexistent/a.keys");
  ck_assert_str_eq(cfg.cas_vendors[0].cwenc_key_list_b_path, "/nonexistent/b.keys");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(too_many_vendors_are_rejected) {
  static const char *const hosts[] = {"h:1", "h:2", "h:3", "h:4", "h:5", "h:6", "h:7", "h:8", "h:9"};
  const char *extra[2 * ARRAY_LEN(hosts) + 1];
  size_t n = 0;
  config_t cfg = {0};

  for (size_t i = 0; i < ARRAY_LEN(hosts); i++) {
    extra[n++] = "--cas-ecmg";
    extra[n++] = hosts[i];
  }
  extra[n] = NULL;
  ck_assert_uint_gt(ARRAY_LEN(hosts), (size_t)ARGS_MAX_CAS_VENDORS);
  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(biss2_sw_enables_and_stores_key) {
  const char *extra[] = {"--biss2-sw", "00112233445566778899aabbccddeeff", NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_OK);
  ck_assert_int_eq(cfg.biss2_enabled, 1);
  ck_assert_uint_eq(cfg.biss2_sw[0], 0x00u);
  ck_assert_uint_eq(cfg.biss2_sw[15], 0xffu);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(biss2_emit_esw_stores_id_with_sw) {
  const char *extra[] = {"--biss2-sw", "00112233445566778899aabbccddeeff", "--biss2-emit-esw", "ffeeddccbbaa99887766554433221100", NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_OK);
  ck_assert_int_eq(cfg.biss2_emit_esw, 1);
  ck_assert_uint_eq(cfg.biss2_esw_id[0], 0xffu);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(biss1_sw_enables_and_stores_key) {
  const char *extra[] = {"--biss1-sw", "001122334455", NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_OK);
  ck_assert_int_eq(cfg.biss1_enabled, 1);
  ck_assert_uint_eq(cfg.biss1_cw[0], 0x00u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

static Suite *args_cas_suite(void) {
  Suite *s = suite_create("dipiradiohead_args_cas");
  TCase *tc = tcase_create("core");

  tcase_add_loop_test(tc, vendor_value_errors_are_rejected, 0, (int)ARRAY_LEN(BAD_VENDOR_VALUE));
  tcase_add_loop_test(tc, oversized_cwenc_values_are_rejected, 0, 3);
  tcase_add_loop_test(tc, standalone_value_errors_are_rejected, 0, (int)ARRAY_LEN(BAD_STANDALONE_VALUE));
  tcase_add_loop_test(tc, vendor_option_without_ecmg_is_rejected, 0, (int)ARRAY_LEN(NEEDS_ECMG));
  tcase_add_loop_test(tc, cas_algo_values_are_mapped, 0, 3);
  tcase_add_test(tc, vendor_options_fill_the_current_vendor);
  tcase_add_test(tc, emmg_reverse_fills_host_and_port);
  tcase_add_test(tc, cwenc_options_are_recorded_before_validation);
  tcase_add_test(tc, cwenc_key_list_paths_are_recorded);
  tcase_add_test(tc, too_many_vendors_are_rejected);
  tcase_add_test(tc, biss2_sw_enables_and_stores_key);
  tcase_add_test(tc, biss2_emit_esw_stores_id_with_sw);
  tcase_add_test(tc, biss1_sw_enables_and_stores_key);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(args_cas_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
