/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipifccret/args.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(required_options_apply_defaults) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.range_count, 1u);
  ck_assert_str_eq(cfg.ranges[0], "239.0.0.0/8");
  ck_assert_str_eq(cfg.listen_addr, "10.0.0.1");
  ck_assert_uint_eq(cfg.listen_port, 6000u);
  ck_assert_str_eq(cfg.iface, "eth0");
  ck_assert_uint_eq(cfg.buffer_ms, 2000u);
  ck_assert_uint_eq(cfg.rtx_pt, 99u);
  ck_assert_uint_eq(cfg.gop_cap_ms, 8000u);
  ck_assert_uint_eq(cfg.max_bursts, 4096u);
  ck_assert_double_eq_tol(cfg.burst_multiplier, 1.5, 1e-9);
  ck_assert_uint_eq(cfg.duration_cap_ms, 10000u);
  ck_assert_uint_eq(cfg.channel_idle_timeout_s, 120u);
  ck_assert_uint_eq(cfg.max_ret_clients, 16384u);
  ck_assert_uint_eq(cfg.ret_client_idle_timeout_s, 300u);
  ck_assert_uint_gt(cfg.workers, 0u); /* 0 -> defaults to online CPU cores */
}
END_TEST

START_TEST(range_accepts_multiple_cidrs_v4_and_v6) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8,ff15::/16", "-l", "10.0.0.1:6000", "-I", "eth0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.range_count, 2u);
  ck_assert_str_eq(cfg.ranges[0], "239.0.0.0/8");
  ck_assert_str_eq(cfg.ranges[1], "ff15::/16");
}
END_TEST

START_TEST(range_rejects_missing_prefix) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0", "-l", "10.0.0.1:6000", "-I", "eth0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(range_rejects_prefix_out_of_range_for_v4) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/33", "-l", "10.0.0.1:6000", "-I", "eth0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(range_rejects_bad_address) {
  char *argv[] = {"dipifccret", "-g", "not-an-ip/8", "-l", "10.0.0.1:6000", "-I", "eth0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(range_is_required) {
  char *argv[] = {"dipifccret", "-l", "10.0.0.1:6000", "-I", "eth0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(listen_is_required) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-I", "eth0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(iface_is_required) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(no_ret_and_no_fcc_together_is_rejected) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0",
                  "--no-ret", "--no-fcc", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(no_ret_alone_is_accepted) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0", "--no-ret", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.no_ret, 1);
}
END_TEST

START_TEST(rtx_pt_out_of_range_is_rejected) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0", "-R", "200", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(buffer_zero_is_rejected) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0", "-B", "0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(burst_multiplier_must_exceed_one) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0", "-X", "1.0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(burst_multiplier_is_overridable) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0", "-X", "2.5", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_double_eq_tol(cfg.burst_multiplier, 2.5, 1e-9);
}
END_TEST

START_TEST(workers_is_overridable) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0", "-w", "4", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.workers, 4u);
}
END_TEST

START_TEST(invalid_color_mode_is_rejected) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0", "--color", "sometimes", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(unexpected_positional_argument_is_rejected) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0", "extra", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rsi_interval_defaults_to_five) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.rsi_interval_s, 5u);
  ck_assert_int_eq(cfg.no_rsi, 0);
}
END_TEST

START_TEST(rsi_interval_is_overridable) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0",
                  "--rsi-interval", "10", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.rsi_interval_s, 10u);
}
END_TEST

START_TEST(rsi_interval_zero_is_rejected) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0",
                  "--rsi-interval", "0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(no_rsi_is_accepted) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0", "--no-rsi", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.no_rsi, 1);
}
END_TEST

START_TEST(rsi_mc_ret_requires_mc_ret) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0",
                  "--no-mc-ret", "--rsi-mc-ret", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rsi_mc_ret_requires_ret) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0",
                  "--no-ret", "--rsi-mc-ret", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rsi_mc_ret_is_accepted) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0", "--rsi-mc-ret", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rsi_mc_ret, 1);
}
END_TEST

START_TEST(rsi_hostname_is_accepted) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0",
                  "--rsi-hostname", "ret.example.com", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.rsi_hostname, "ret.example.com");
}
END_TEST

START_TEST(rsi_hostname_empty_by_default) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.rsi_hostname[0], 0u);
}
END_TEST

START_TEST(rsi_hostname_too_long_is_rejected) {
  char longname[300];
  char *argv[10];
  config_t cfg;
  memset(longname, 'a', sizeof longname - 1);
  longname[sizeof longname - 1] = '\0';
  {
    char *base[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0", "--rsi-hostname"};
    for (int i = 0; i < 8; i++)
      argv[i] = base[i];
  }
  argv[8] = longname;
  argv[9] = NULL;
  ck_assert_int_eq(args_parse(9, argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(max_buffer_fill_bound_defaults_to_30000) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.max_buffer_fill_bound_ms, 30000u);
}
END_TEST

START_TEST(max_buffer_fill_bound_zero_disables) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0",
                  "--max-buffer-fill-bound", "0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.max_buffer_fill_bound_ms, 0u);
}
END_TEST

START_TEST(max_buffer_fill_bound_rejects_garbage) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0",
                  "--max-buffer-fill-bound", "abc", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(max_ret_clients_is_overridable) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0",
                  "--max-ret-clients", "512", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.max_ret_clients, 512u);
}
END_TEST

START_TEST(max_ret_clients_zero_is_rejected) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0",
                  "--max-ret-clients", "0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(ret_client_idle_timeout_zero_disables) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0",
                  "--ret-client-idle-timeout", "0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.ret_client_idle_timeout_s, 0u);
}
END_TEST

START_TEST(ret_client_idle_timeout_rejects_garbage) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0",
                  "--ret-client-idle-timeout", "abc", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(fcc_resolve_by_port_defaults_off) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.fcc_resolve_by_port, 0);
  ck_assert_uint_eq(cfg.fcc_resolve_base_port, 0u);
}
END_TEST

START_TEST(fcc_resolve_by_port_and_base_port_are_settable) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0",
                  "--fcc-resolve-by-port", "--fcc-resolve-base-port", "7000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.fcc_resolve_by_port, 1);
  ck_assert_uint_eq(cfg.fcc_resolve_base_port, 7000u);
}
END_TEST

START_TEST(fcc_resolve_base_port_rejects_out_of_range) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0",
                  "--fcc-resolve-base-port", "99999", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(congestion_nack_threshold_defaults_to_five) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.congestion_nack_threshold, 5u);
}
END_TEST

START_TEST(congestion_nack_threshold_zero_disables) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0",
                  "--congestion-nack-threshold", "0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.congestion_nack_threshold, 0u);
}
END_TEST

START_TEST(fcc_range_accepts_cidr_list) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0",
                  "--fcc-range", "239.1.0.0/16,239.2.0.0/16", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.fcc_range_count, 2u);
}
END_TEST

START_TEST(fcc_range_rejects_malformed_cidr) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0",
                  "--fcc-range", "not-a-cidr", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(fcc_client_range_accepts_cidr_list) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0",
                  "--fcc-client-range", "10.0.0.0/24", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.fcc_client_range_count, 1u);
}
END_TEST

START_TEST(fcc_client_range_rejects_malformed_cidr) {
  char *argv[] = {"dipifccret", "-g", "239.0.0.0/8", "-l", "10.0.0.1:6000", "-I", "eth0",
                  "--fcc-client-range", "10.0.0.0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(help_returns_help_status) {
  char *argv[] = {"dipifccret", "-h", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
}
END_TEST

static Suite *args_suite(void) {
  Suite *s = suite_create("dipifccret_args");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, required_options_apply_defaults);
  tcase_add_test(tc, range_accepts_multiple_cidrs_v4_and_v6);
  tcase_add_test(tc, range_rejects_missing_prefix);
  tcase_add_test(tc, range_rejects_prefix_out_of_range_for_v4);
  tcase_add_test(tc, range_rejects_bad_address);
  tcase_add_test(tc, range_is_required);
  tcase_add_test(tc, listen_is_required);
  tcase_add_test(tc, iface_is_required);
  tcase_add_test(tc, no_ret_and_no_fcc_together_is_rejected);
  tcase_add_test(tc, no_ret_alone_is_accepted);
  tcase_add_test(tc, rtx_pt_out_of_range_is_rejected);
  tcase_add_test(tc, buffer_zero_is_rejected);
  tcase_add_test(tc, burst_multiplier_must_exceed_one);
  tcase_add_test(tc, burst_multiplier_is_overridable);
  tcase_add_test(tc, workers_is_overridable);
  tcase_add_test(tc, invalid_color_mode_is_rejected);
  tcase_add_test(tc, unexpected_positional_argument_is_rejected);
  tcase_add_test(tc, rsi_interval_defaults_to_five);
  tcase_add_test(tc, rsi_interval_is_overridable);
  tcase_add_test(tc, rsi_interval_zero_is_rejected);
  tcase_add_test(tc, no_rsi_is_accepted);
  tcase_add_test(tc, rsi_mc_ret_requires_mc_ret);
  tcase_add_test(tc, rsi_mc_ret_requires_ret);
  tcase_add_test(tc, rsi_mc_ret_is_accepted);
  tcase_add_test(tc, rsi_hostname_is_accepted);
  tcase_add_test(tc, rsi_hostname_empty_by_default);
  tcase_add_test(tc, rsi_hostname_too_long_is_rejected);
  tcase_add_test(tc, max_buffer_fill_bound_defaults_to_30000);
  tcase_add_test(tc, max_buffer_fill_bound_zero_disables);
  tcase_add_test(tc, max_buffer_fill_bound_rejects_garbage);
  tcase_add_test(tc, max_ret_clients_is_overridable);
  tcase_add_test(tc, max_ret_clients_zero_is_rejected);
  tcase_add_test(tc, ret_client_idle_timeout_zero_disables);
  tcase_add_test(tc, ret_client_idle_timeout_rejects_garbage);
  tcase_add_test(tc, fcc_resolve_by_port_defaults_off);
  tcase_add_test(tc, fcc_resolve_by_port_and_base_port_are_settable);
  tcase_add_test(tc, fcc_resolve_base_port_rejects_out_of_range);
  tcase_add_test(tc, congestion_nack_threshold_defaults_to_five);
  tcase_add_test(tc, congestion_nack_threshold_zero_disables);
  tcase_add_test(tc, fcc_range_accepts_cidr_list);
  tcase_add_test(tc, fcc_range_rejects_malformed_cidr);
  tcase_add_test(tc, fcc_client_range_accepts_cidr_list);
  tcase_add_test(tc, fcc_client_range_rejects_malformed_cidr);
  tcase_add_test(tc, help_returns_help_status);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(args_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
