/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "lib/cas/cas_args.h"
#include "lib/config/yamlcfg.h"

#include "dipitvhead/cli/priv.h"

#include "args_fixture.h"

static const char *const VENDOR[] = {"--cas-algo", "csa2", "--cas-ecmg", "ecmg.example:1234", "--cas-super-id", "0x1234", "--cas-ecm-id", "7", NULL};

static const opt_case_t BAD_VENDOR_VALUE[] = {
  {"--cas-ecmg-version", "4"},
  {"--cas-super-id", "0"},
  {"--cas-ecm-id", "0"},
  {"--cas-ecm-pid", "0x2000"},
  {"--cas-emmg-listen", "x"},
  {"--cas-emmg-reverse", "nocolon"},
  {"--cas-emmg-version", "5"},
  {"--cas-emmg-max-conns", "0"},
  {"--cas-emm-pid", "0x2000"},
  {"--cas-resilience", "bogus"},
  {"--cas-cwenc-algo", "bogus"},
  {"--cas-cwenc-aes-mode", "bogus"},
};

static const opt_case_t BAD_STANDALONE_VALUE[] = {
  {"--cas-algo", "bogus"},
  {"--cas-ecmg", "nocolon"},
  {"--cas-pids", "zz"},
  {"--cas-pids", "0x2000"},
  {"--cas-cp-duration", "0"},
  {"--biss2-sw", "0011"},
  {"--biss2-emit-esw", "0011"},
  {"--biss1-sw", "0011"},
  {"--biss2-ca-session-id", "0x10000"},
};

static const opt_case_t NEEDS_ECMG[] = {
  {"--cas-ecmg-version", "2"},
  {"--cas-super-id", "1"},
  {"--cas-ecm-id", "1"},
  {"--cas-ecm-pid", "0x0100"},
  {"--cas-emmg-listen", "9000"},
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

START_TEST(unknown_option_is_left_to_the_caller) {
  config_t cfg = {0};
  tvh_opt_t p = {.cfg = &cfg};

  ck_assert_int_eq(tvh_opt_cas(&p, 'x'), OPT_UNHANDLED);
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
    "--cas-emmg-listen", "9000",
    "--cas-emmg-version", "2",
    "--cas-emmg-max-conns", "4",
    "--cas-emm-pid", "0x0101",
    "--cas-resilience", "cycling",
    "--cas-required",
    "--cas-cp-duration", "5000",
    "--cas-fallback-clear",
    "--cas-pids", "video,audio,lcevc,0x0200",
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
  ck_assert_uint_eq(v->emmg_version, 2u);
  ck_assert_uint_eq(v->emmg_max_conns, 4u);
  ck_assert_uint_eq(v->emm_pid, 0x0101u);
  ck_assert_int_eq(v->resilience, CAS_OUTAGE_CYCLING);
  ck_assert_int_eq(v->required, 1);
  ck_assert_uint_eq(cfg.cas_cp_duration_ms, 5000u);
  ck_assert_int_eq(cfg.cas_fallback_clear, 1);
  ck_assert_int_eq(cfg.cas_pids_video, 1);
  ck_assert_int_eq(cfg.cas_pids_audio, 1);
  ck_assert_int_eq(cfg.cas_pids_lcevc, 1);
  ck_assert_uint_eq(cfg.cas_pid_count, 1u);
  ck_assert_uint_eq(cfg.cas_pids[0], 0x0200u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(emmg_listen_fills_host_and_port) {
  const char *extra[] = {
    "--cas-algo", "csa2",
    "--cas-ecmg", "ecmg.example:1234",
    "--cas-super-id", "1",
    "--cas-ecm-id", "1",
    "--cas-emmg-listen", "[::1]:7000",
    NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_OK);
  ck_assert_str_eq(cfg.cas_vendors[0].emmg_listen_host, "::1");
  ck_assert_uint_eq(cfg.cas_vendors[0].emmg_port, 7000u);
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
    "--cas-cwenc-key-list-a", "/nonexistent/a.keys",
    "--cas-cwenc-key-list-b", "/nonexistent/b.keys",
    NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_ERR);
  ck_assert_str_eq(cfg.cas_vendors[0].cwenc_algorithm, "des56");
  ck_assert_str_eq(cfg.cas_vendors[0].cwenc_aes_mode, "ecb");
  ck_assert_str_eq(cfg.cas_vendors[0].cwenc_fixed_key_hex, "4DA19FF0AF6B8F");
  ck_assert_str_eq(cfg.cas_vendors[0].cwenc_key_list_a_path, "/nonexistent/a.keys");
  ck_assert_str_eq(cfg.cas_vendors[0].cwenc_key_list_b_path, "/nonexistent/b.keys");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(biss_options_enable_and_store_keys) {
  const char *extra[] = {
    "--biss2-sw", "00112233445566778899aabbccddeeff",
    "--biss2-emit-esw", "ffeeddccbbaa99887766554433221100",
    NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_OK);
  ck_assert_int_eq(cfg.biss2_enabled, 1);
  ck_assert_uint_eq(cfg.biss2_sw[15], 0xffu);
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
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(biss2_ca_options_fill_config) {
  const char *extra[] = {
    "--biss2-ca-receivers", "/etc/biss-ca/receivers",
    "--biss2-ca-session-id", "0x1234",
    "--cas-cp-duration", "2000",
    NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_OK);
  ck_assert_int_eq(cfg.biss2_ca_enabled, 1);
  ck_assert_str_eq(cfg.biss2_ca_receivers_dir, "/etc/biss-ca/receivers");
  ck_assert_int_eq(cfg.biss2_ca_session_id_given, 1);
  ck_assert_uint_eq(cfg.biss2_ca_session_id, 0x1234u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

static Suite *args_cas_suite(void) {
  Suite *s = suite_create("dipitvhead_args_cas");
  TCase *tc = tcase_create("core");

  tcase_add_loop_test(tc, vendor_value_errors_are_rejected, 0, (int)ARRAY_LEN(BAD_VENDOR_VALUE));
  tcase_add_loop_test(tc, standalone_value_errors_are_rejected, 0, (int)ARRAY_LEN(BAD_STANDALONE_VALUE));
  tcase_add_loop_test(tc, vendor_option_without_ecmg_is_rejected, 0, (int)ARRAY_LEN(NEEDS_ECMG));
  tcase_add_test(tc, unknown_option_is_left_to_the_caller);
  tcase_add_test(tc, vendor_options_fill_the_current_vendor);
  tcase_add_test(tc, emmg_listen_fills_host_and_port);
  tcase_add_test(tc, emmg_reverse_fills_host_and_port);
  tcase_add_test(tc, cwenc_options_are_recorded_before_validation);
  tcase_add_test(tc, biss_options_enable_and_store_keys);
  tcase_add_test(tc, biss1_sw_enables_and_stores_key);
  tcase_add_test(tc, biss2_ca_options_fill_config);
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
