/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "lib/config/yamlcfg.h"

#include "dipitvhead/cli/priv.h"

#include "args_fixture.h"

static const opt_case_t BAD_VALUE[] = {
  {"-i", "bogus://x"},
  {"-p", "0"},
  {"-p", "0x2000"},
  {"--strip", "bogus"},
  {"--hbbtv-org-id", "zz"},
  {"--hbbtv-app-id", "0"},
  {"--sid", "0"},
  {"--rist-encryption-type-in", "64"},
  {"--rist-profile-in", "bogus"},
  {"--srt-pbkeylen-in", "20"},
  {"--srt-latency-in", "0"},
  {"--jitter-ms", "0"},
};

static const opt_case_t OVERSIZED_VALUE[] = {
  {"-s", NULL},
  {"--provider", NULL},
  {"--srt-passphrase-in", NULL},
  {"--srt-streamid-in", NULL},
  {"--srt-packetfilter-in", NULL},
};

static const opt_case_t BEFORE_INPUT[] = {
  {"-p", "0x100"},
  {"-I", "eth0"},
  {"-s", "name"},
  {"--provider", "name"},
  {"--strip-eit", NULL},
  {"--strip", "none"},
  {"--hbbtv", "http://app.example/"},
  {"--hbbtv-org-id", "1"},
  {"--hbbtv-app-id", "1"},
  {"--sid", "1"},
  {"--rist-encryption-type-in", "128"},
  {"--rist-profile-in", "main"},
  {"--srt-passphrase-in", "0123456789"},
  {"--srt-pbkeylen-in", "16"},
  {"--srt-streamid-in", "id"},
  {"--srt-packetfilter-in", "fec"},
  {"--srt-latency-in", "100"},
  {"--jitter-ms", "100"},
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

START_TEST(input_scoped_option_before_first_input_is_rejected) {
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_before_input(&cfg, &BEFORE_INPUT[_i]), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(too_many_inputs_are_rejected) {
  char *argv[2 * (ARGS_MAX_INPUTS + 1) + 2];
  int n = 0;
  config_t cfg = {0};

  argv[n++] = "dipitvhead";
  for (int i = 0; i <= ARGS_MAX_INPUTS; i++) {
    argv[n++] = "-i";
    argv[n++] = "udp://@239.1.1.1:5000";
  }
  argv[n] = NULL;
  ck_assert_int_eq(args_parse(n, argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(unknown_option_is_left_to_the_caller) {
  config_t cfg = {0};
  tvh_opt_t p = {.cfg = &cfg};

  ck_assert_int_eq(tvh_opt_input(&p, 'x'), OPT_UNHANDLED);
}
END_TEST

START_TEST(input_options_fill_the_current_input) {
  const char *extra[] = {
    "-p", "0x0100",
    "-I", "eth0",
    "-s", "Station",
    "--provider", "Provider",
    "--strip-eit",
    "--strip", "DATA,ECM",
    "--hbbtv", "http://app.example/",
    "--hbbtv-org-id", "12",
    "--hbbtv-app-id", "34",
    "--sid", "56",
    "--jitter-ms", "250",
    NULL};
  config_t cfg = {0};
  const dipitvhead_input_t *in;

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_OK);
  in = &cfg.inputs[0];
  ck_assert_uint_eq(in->pmt_pid, 0x0100u);
  ck_assert_str_eq(in->iface_in, "eth0");
  ck_assert_int_eq(in->sdt_mode, TABLE_OVERRIDE);
  ck_assert_str_eq(in->sdt_text, "Station");
  ck_assert_str_eq(in->provider_text, "Provider");
  ck_assert_int_eq(in->strip_eit, 1);
  ck_assert_uint_ne(in->strip_mask, 0u);
  ck_assert_str_eq(in->hbbtv_url, "http://app.example/");
  ck_assert_uint_eq(in->hbbtv_org_id, 12u);
  ck_assert_uint_eq(in->hbbtv_app_id, 34u);
  ck_assert_uint_eq(in->sid, 56u);
  ck_assert_uint_eq(in->jitter_ms, 250u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(sdt_dash_drops_the_table) {
  const char *extra[] = {"-s", "-", NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_OK);
  ck_assert_int_eq(cfg.inputs[0].sdt_mode, TABLE_DROP);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_input_options_fill_the_current_input) {
  char *argv[] = {"dipitvhead", "-i", "srt://127.0.0.1:9000",
                  "--srt-passphrase-in", "0123456789",
                  "--srt-pbkeylen-in", "32",
                  "--srt-streamid-in", "radio",
                  "--srt-packetfilter-in", "fec,cols:5",
                  "--srt-latency-in", "250",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  const dipitvhead_input_t *in;

  ck_assert_int_eq(args_parse(ARGS_FIXTURE_ARGC(argv), argv, &cfg), ARGS_OK);
  in = &cfg.inputs[0];
  ck_assert_str_eq(in->srt_passphrase_in, "0123456789");
  ck_assert_int_eq(in->srt_pbkeylen_in, 32);
  ck_assert_str_eq(in->srt_streamid_in, "radio");
  ck_assert_str_eq(in->srt_packetfilter_in, "fec,cols:5");
  ck_assert_uint_eq(in->srt_latency_in_ms, 250u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_input_options_fill_the_current_input) {
  char *argv[] = {"dipitvhead", "-i", "rist://@127.0.0.1:6000",
                  "--rist-profile-in", "main",
                  "--rist-encryption-type-in", "256",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  const dipitvhead_input_t *in;

  ck_assert_int_eq(args_parse(ARGS_FIXTURE_ARGC(argv), argv, &cfg), ARGS_OK);
  in = &cfg.inputs[0];
  ck_assert_int_eq(in->rist_profile_main, 1);
  ck_assert_int_eq(in->rist_key_size_in, 256);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

static Suite *args_input_suite(void) {
  Suite *s = suite_create("dipitvhead_args_input");
  TCase *tc = tcase_create("core");

  tcase_add_loop_test(tc, value_errors_are_rejected, 0, (int)ARRAY_LEN(BAD_VALUE));
  tcase_add_loop_test(tc, oversized_text_is_rejected, 0, (int)ARRAY_LEN(OVERSIZED_VALUE));
  tcase_add_loop_test(tc, input_scoped_option_before_first_input_is_rejected, 0, (int)ARRAY_LEN(BEFORE_INPUT));
  tcase_add_test(tc, too_many_inputs_are_rejected);
  tcase_add_test(tc, unknown_option_is_left_to_the_caller);
  tcase_add_test(tc, input_options_fill_the_current_input);
  tcase_add_test(tc, sdt_dash_drops_the_table);
  tcase_add_test(tc, srt_input_options_fill_the_current_input);
  tcase_add_test(tc, rist_input_options_fill_the_current_input);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(args_input_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
