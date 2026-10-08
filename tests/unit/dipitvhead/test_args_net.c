/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "lib/config/yamlcfg.h"
#include "lib/net/netconnect.h"

#include "dipitvhead/cli/priv.h"

#include "args_fixture.h"

static const opt_case_t BAD_VALUE[] = {
  {"-m", "nocolon"},
  {"-T", "0"},
  {"-T", "256"},
  {"--dscp", "bogus"},
  {"--al-fec", "0:0"},
  {"--al-fec-port", "x"},
  {"-R", "http://host:1"},
  {"-R", "srt://@host:1"},
  {"-R", "srt://nocolon"},
  {"--rist-profile", "bogus"},
  {"--rist-encryption-type", "64"},
  {"--rist-buffer", "0"},
  {"--srt-group-mode", "bogus"},
  {"--srt-pbkeylen", "20"},
  {"--srt-latency", "0"},
  {"--srt-latency", "60001"},
};

static const opt_case_t OVERSIZED_VALUE[] = {
  {"--rist-secret", NULL},
  {"--rist-cname", NULL},
  {"--srt-passphrase", NULL},
  {"--srt-streamid", NULL},
  {"--srt-packetfilter", NULL},
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

START_TEST(rist_and_srt_peers_in_either_order_are_rejected) {
  static const char *const rist_first[] = {"-R", "rist://h:1", "-R", "srt://127.0.0.1:2", NULL};
  static const char *const srt_first[] = {"-R", "srt://127.0.0.1:2", "-R", "rist://h:1", NULL};
  const char *const *extra = _i ? srt_first : rist_first;
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(too_many_peers_are_rejected) {
  static const char *const uris[] = {"rist://h:1", "srt://127.0.0.1:1"};
  const char *extra[2 * (ARGS_MAX_RIST_PEERS + 1) + 1];
  size_t n = 0;
  config_t cfg = {0};

  for (int i = 0; i <= ARGS_MAX_RIST_PEERS; i++) {
    extra[n++] = "-R";
    extra[n++] = uris[_i];
  }
  extra[n] = NULL;
  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(unknown_option_is_left_to_the_caller) {
  config_t cfg = {0};
  tvh_opt_t p = {.cfg = &cfg};

  ck_assert_int_eq(tvh_opt_net(&p, 'x'), OPT_UNHANDLED);
}
END_TEST

START_TEST(output_options_fill_config) {
  const char *extra[] = {"-O", "eth0", "-T", "16", "--dscp", "voice", "--al-fec", "5:4", "--al-fec-port", "5002", NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_OK);
  ck_assert_str_eq(cfg.iface_out, "eth0");
  ck_assert_uint_eq(cfg.ttl, 16u);
  ck_assert_int_eq(cfg.dscp, NET_DSCP_VOICE_BEARER);
  ck_assert_uint_eq(cfg.al_fec_l, 5u);
  ck_assert_uint_eq(cfg.al_fec_d, 4u);
  ck_assert_uint_eq(cfg.al_fec_port, 5002u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(udp_flag_disables_rtp) {
  const char *extra[] = {"-u", NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_OK);
  ck_assert_int_eq(cfg.rtp, 0);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_options_fill_config) {
  const char *extra[] = {
    "-R", "rist://peer.example:6000",
    "--rist-profile", "main",
    "--rist-secret", "0123456789",
    "--rist-encryption-type", "256",
    "--rist-cname", "tv",
    "--rist-buffer", "800",
    NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_OK);
  ck_assert_uint_eq(cfg.n_rist, 1u);
  ck_assert_str_eq(cfg.rist_uri[0], "rist://peer.example:6000");
  ck_assert_int_eq(cfg.rist_profile, RIST_PROF_MAIN);
  ck_assert_str_eq(cfg.rist_secret, "0123456789");
  ck_assert_int_eq(cfg.rist_key_size, 256);
  ck_assert_str_eq(cfg.rist_cname, "tv");
  ck_assert_uint_eq(cfg.rist_buffer_ms, 800u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_options_fill_config) {
  const char *extra[] = {
    "-R", "srt://127.0.0.1:7000",
    "--srt-passphrase", "0123456789",
    "--srt-pbkeylen", "32",
    "--srt-streamid", "tv",
    "--srt-packetfilter", "fec,cols:5",
    "--srt-latency", "250",
    NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_OK);
  ck_assert_uint_eq(cfg.n_srt, 1u);
  ck_assert_str_eq(cfg.srt_host[0], "127.0.0.1");
  ck_assert_uint_eq(cfg.srt_port[0], 7000u);
  ck_assert_str_eq(cfg.srt_passphrase, "0123456789");
  ck_assert_int_eq(cfg.srt_pbkeylen, 32);
  ck_assert_str_eq(cfg.srt_streamid, "tv");
  ck_assert_str_eq(cfg.srt_packetfilter, "fec,cols:5");
  ck_assert_uint_eq(cfg.srt_latency_ms, 250u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_group_mode_is_recorded) {
  const char *extra[] = {"-R", "srt://127.0.0.1:7000", "-R", "srt://127.0.0.1:7001", "--srt-group-mode", "backup", NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_fixture_parse_list(&cfg, extra), ARGS_OK);
  ck_assert_int_eq(cfg.srt_group_mode, SRT_BOND_BACKUP);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

static Suite *args_net_suite(void) {
  Suite *s = suite_create("dipitvhead_args_net");
  TCase *tc = tcase_create("core");

  tcase_add_loop_test(tc, value_errors_are_rejected, 0, (int)ARRAY_LEN(BAD_VALUE));
  tcase_add_loop_test(tc, oversized_text_is_rejected, 0, (int)ARRAY_LEN(OVERSIZED_VALUE));
  tcase_add_loop_test(tc, rist_and_srt_peers_in_either_order_are_rejected, 0, 2);
  tcase_add_loop_test(tc, too_many_peers_are_rejected, 0, 2);
  tcase_add_test(tc, unknown_option_is_left_to_the_caller);
  tcase_add_test(tc, output_options_fill_config);
  tcase_add_test(tc, udp_flag_disables_rtp);
  tcase_add_test(tc, rist_options_fill_config);
  tcase_add_test(tc, srt_options_fill_config);
  tcase_add_test(tc, srt_group_mode_is_recorded);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(args_net_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
