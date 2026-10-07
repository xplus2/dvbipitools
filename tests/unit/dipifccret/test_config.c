/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../cfg_fixture.h"
#include "dipifccret/config.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/log.h"

static const cfg_field_case_t field_cases[] = {
  CFG_FIELD(config_t, "iface: veth7\n", CFG_STRPTR, iface, 0, "veth7"),
  CFG_FIELD(config_t, "max-channels: 77\n", CFG_SIZE, max_channels, 77, NULL),
  CFG_FIELD(config_t, "channel-idle-timeout: 33\n", CFG_UINT, channel_idle_timeout_s, 33, NULL),
  CFG_FIELD(config_t, "rtx-pt: 97\n", CFG_UCHAR, rtx_pt, 97, NULL),
  CFG_FIELD(config_t, "workers: 5\n", CFG_UINT, workers, 5, NULL),
  CFG_FIELD(config_t, "user: nobody\n", CFG_STRPTR, user, 0, "nobody"),
  CFG_FIELD(config_t, "verbose: true\n", CFG_INT, verbose, 1, NULL),
  CFG_FIELD(config_t, "daemonize: yes\n", CFG_INT, daemonize, 1, NULL),
  CFG_FIELD(config_t, "no-ret: on\n", CFG_INT, no_ret, 1, NULL),
  CFG_FIELD(config_t, "buffer: 4321\n", CFG_UINT, buffer_ms, 4321, NULL),
  CFG_FIELD(config_t, "ff-port: 40001\n", CFG_UINT, ff_port, 40001, NULL),
  CFG_FIELD(config_t, "no-mc-ret: 1\n", CFG_INT, no_mc_ret, 1, NULL),
  CFG_FIELD(config_t, "max-ret-clients: 99\n", CFG_SIZE, max_ret_clients, 99, NULL),
  CFG_FIELD(config_t, "ret:\n  client-idle-timeout: 61\n", CFG_UINT, ret_client_idle_timeout_s, 61, NULL),
  CFG_FIELD(config_t, "no-rsi: true\n", CFG_INT, no_rsi, 1, NULL),
  CFG_FIELD(config_t, "rsi:\n  interval: 17\n", CFG_UINT, rsi_interval_s, 17, NULL),
  CFG_FIELD(config_t, "rsi:\n  mc-ret: true\n", CFG_INT, rsi_mc_ret, 1, NULL),
  CFG_FIELD(config_t, "rsi:\n  hostname: fcc.example.net\n", CFG_CHARARR, rsi_hostname, 0, "fcc.example.net"),
  CFG_FIELD(config_t, "no-fcc: true\n", CFG_INT, no_fcc, 1, NULL),
  CFG_FIELD(config_t, "gop-cap: 1234\n", CFG_UINT, gop_cap_ms, 1234, NULL),
  CFG_FIELD(config_t, "max-bursts: 321\n", CFG_SIZE, max_bursts, 321, NULL),
  CFG_FIELD(config_t, "burst-multiplier: 2.5\n", CFG_DOUBLE, burst_multiplier, 2.5, NULL),
  CFG_FIELD(config_t, "burst-duration-cap: 4567\n", CFG_UINT, duration_cap_ms, 4567, NULL),
  CFG_FIELD(config_t, "max-buffer-fill-bound: 0\n", CFG_UINT, max_buffer_fill_bound_ms, 0, NULL),
  CFG_FIELD(config_t, "congestion-nack-threshold: 9\n", CFG_UINT, congestion_nack_threshold, 9, NULL),
  CFG_FIELD(config_t, "fcc:\n  resolve-by-port: true\n", CFG_INT, fcc_resolve_by_port, 1, NULL),
  CFG_FIELD(config_t, "fcc:\n  resolve-base-port: 30000\n", CFG_UINT, fcc_resolve_base_port, 30000, NULL),
  CFG_FIELD(config_t, "metrics:\n  sock: /tmp/m.sock\n", CFG_STRPTR, metrics_sock, 0, "/tmp/m.sock"),
  CFG_FIELD(config_t, "metrics:\n  id: inst9\n", CFG_STRPTR, metrics_id, 0, "inst9"),
  CFG_FIELD(config_t, "metrics:\n  interval: 42\n", CFG_UINT, metrics_interval_s, 42, NULL),
  CFG_FIELD(config_t, "metrics:\n  inspect-ts: full\n", CFG_INT, metrics_inspect_ts, METRICS_INSPECT_TS_FULL, NULL),
  CFG_FIELD(config_t, "color: never\n", CFG_INT, color_mode, LOG_COLOR_NEVER, NULL),
};

static cfg_fixture_t g_fx;

static int load(const char *text, config_t *cfg) {
  int rc;

  cfg_fixture_write(&g_fx, text);
  fccret_cfg_defaults(cfg);
  rc = fccret_cfg_load(cfg, g_fx.path, 1);
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

START_TEST(range_listen_and_cidr_lists_are_parsed) {
  config_t cfg;
  const char *text =
    "range: 239.0.0.0/8,ff15::/16\n"
    "listen: 10.1.2.3:6001\n"
    "fcc:\n"
    "  range: 239.1.0.0/16\n"
    "  client-range: 10.0.0.0/8,192.168.0.0/16\n";

  ck_assert_int_eq(load(text, &cfg), 0);
  ck_assert_uint_eq(cfg.range_count, 2u);
  ck_assert_str_eq(cfg.ranges[1], "ff15::/16");
  ck_assert_int_eq(cfg.listen_family, AF_INET);
  ck_assert_str_eq(cfg.listen_addr, "10.1.2.3");
  ck_assert_uint_eq(cfg.listen_port, 6001u);
  ck_assert_uint_eq(cfg.fcc_range_count, 1u);
  ck_assert_uint_eq(cfg.fcc_client_range_count, 2u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(cpu_affinity_values_are_parsed) {
  config_t cfg;

  ck_assert_int_eq(load("cpu-affinity: 2-3,6\n", &cfg), 0);
  ck_assert_int_eq(cfg.cpu_affinity.mode, CPUAFF_LIST);
  ck_assert_uint_eq(cfg.cpu_affinity.n, 3u);
  ck_assert_uint_eq(cfg.cpu_affinity.cpus[2], 6u);
  ck_assert_int_eq(load("cpu-affinity: auto\n", &cfg), 0);
  ck_assert_int_eq(cfg.cpu_affinity.mode, CPUAFF_AUTO);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

typedef struct {
  const char *yaml;
} bad_case_t;

static const bad_case_t bad_cases[] = {
  {"range: not-a-range\n"},
  {"listen: nonsense\n"},
  {"rtx-pt: 200\n"},
  {"cpu-affinity: 5-2\n"},
  {"verbose: maybe\n"},
  {"color: rainbow\n"},
  {"buffer: 0\n"},
  {"ff-port: 70000\n"},
  {"max-ret-clients: 0\n"},
  {"rsi:\n  interval: 0\n"},
  {"rsi:\n  hostname: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"},
  {"gop-cap: 0\n"},
  {"max-bursts: 0\n"},
  {"burst-multiplier: 0.5\n"},
  {"burst-duration-cap: 0\n"},
  {"fcc:\n  resolve-base-port: 70000\n"},
  {"fcc:\n  range: bogus\n"},
  {"fcc:\n  client-range: bogus\n"},
  {"metrics:\n  interval: 0\n"},
  {"metrics:\n  inspect-ts: loud\n"},
};

START_TEST(invalid_values_are_rejected) {
  config_t cfg;

  ck_assert_int_eq(load(bad_cases[_i].yaml, &cfg), -1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(unknown_key_is_rejected_in_strict_mode_only) {
  config_t cfg;

  cfg_fixture_write(&g_fx, "bogus-key: 1\n");
  fccret_cfg_defaults(&cfg);
  ck_assert_int_eq(fccret_cfg_load(&cfg, g_fx.path, 1), -1);
  fccret_cfg_defaults(&cfg);
  ck_assert_int_eq(fccret_cfg_load(&cfg, g_fx.path, 0), 0);
  cfg_fixture_remove(&g_fx);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(missing_explicit_file_fails) {
  config_t cfg;

  fccret_cfg_defaults(&cfg);
  ck_assert_int_eq(fccret_cfg_load(&cfg, "/nonexistent/fccret.yaml", 0), -1);
}
END_TEST

START_TEST(defaults_are_the_documented_values) {
  config_t cfg;

  fccret_cfg_defaults(&cfg);
  ck_assert_uint_eq(cfg.buffer_ms, 2000u);
  ck_assert_uint_eq(cfg.rtx_pt, 99u);
  ck_assert_uint_eq(cfg.max_bursts, 4096u);
  ck_assert_uint_eq(cfg.congestion_nack_threshold, 5u);
  ck_assert_uint_eq(cfg.rsi_interval_s, 5u);
  ck_assert_uint_eq(cfg.max_ret_clients, 16384u);
}
END_TEST

START_TEST(config_test_reports_warnings_and_fails_only_in_strict_mode) {
  cfg_fixture_write(&g_fx, "no-ret: true\nno-fcc: true\n");
  ck_assert_int_eq(fccret_cfg_test(g_fx.path, 0), 0);
  ck_assert_int_eq(fccret_cfg_test(g_fx.path, 1), -1);
  cfg_fixture_remove(&g_fx);

  cfg_fixture_write(&g_fx, "rsi:\n  mc-ret: true\nno-mc-ret: true\nmetrics:\n  sock: /tmp/x.sock\n  inspect-ts: basic\n");
  ck_assert_int_eq(fccret_cfg_test(g_fx.path, 1), -1);
  cfg_fixture_remove(&g_fx);

  cfg_fixture_write(&g_fx, "range: 239.0.0.0/8\nlisten: 10.0.0.1:6000\niface: eth0\nmetrics:\n  id: m1\n");
  ck_assert_int_eq(fccret_cfg_test(g_fx.path, 1), 0);
  cfg_fixture_remove(&g_fx);

  ck_assert_int_eq(fccret_cfg_test("/nonexistent/fccret.yaml", 0), -1);
}
END_TEST

static Suite *config_suite(void) {
  Suite *s = suite_create("dipifccret_config");
  TCase *tc = tcase_create("core");
  tcase_add_loop_test(tc, each_key_sets_its_config_field, 0, sizeof field_cases / sizeof field_cases[0]);
  tcase_add_test(tc, range_listen_and_cidr_lists_are_parsed);
  tcase_add_test(tc, cpu_affinity_values_are_parsed);
  tcase_add_loop_test(tc, invalid_values_are_rejected, 0, sizeof bad_cases / sizeof bad_cases[0]);
  tcase_add_test(tc, unknown_key_is_rejected_in_strict_mode_only);
  tcase_add_test(tc, missing_explicit_file_fails);
  tcase_add_test(tc, defaults_are_the_documented_values);
  tcase_add_test(tc, config_test_reports_warnings_and_fails_only_in_strict_mode);
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
