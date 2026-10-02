/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dipifccret/config.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/log.h"

typedef enum { K_UINT, K_SIZE, K_UCHAR, K_INT, K_DOUBLE, K_STRPTR, K_CHARARR } kind_t;

typedef struct {
  const char *yaml;
  kind_t kind;
  size_t off;
  double num;
  const char *str;
} field_case_t;

#define F(y, k, field, n, s) {y, k, offsetof(config_t, field), n, s}

static const field_case_t field_cases[] = {
  F("iface: veth7\n", K_STRPTR, iface, 0, "veth7"),
  F("max-channels: 77\n", K_SIZE, max_channels, 77, NULL),
  F("channel-idle-timeout: 33\n", K_UINT, channel_idle_timeout_s, 33, NULL),
  F("rtx-pt: 97\n", K_UCHAR, rtx_pt, 97, NULL),
  F("workers: 5\n", K_UINT, workers, 5, NULL),
  F("user: nobody\n", K_STRPTR, user, 0, "nobody"),
  F("verbose: true\n", K_INT, verbose, 1, NULL),
  F("daemonize: yes\n", K_INT, daemonize, 1, NULL),
  F("no-ret: on\n", K_INT, no_ret, 1, NULL),
  F("buffer: 4321\n", K_UINT, buffer_ms, 4321, NULL),
  F("ff-port: 40001\n", K_UINT, ff_port, 40001, NULL),
  F("no-mc-ret: 1\n", K_INT, no_mc_ret, 1, NULL),
  F("max-ret-clients: 99\n", K_SIZE, max_ret_clients, 99, NULL),
  F("ret:\n  client-idle-timeout: 61\n", K_UINT, ret_client_idle_timeout_s, 61, NULL),
  F("no-rsi: true\n", K_INT, no_rsi, 1, NULL),
  F("rsi:\n  interval: 17\n", K_UINT, rsi_interval_s, 17, NULL),
  F("rsi:\n  mc-ret: true\n", K_INT, rsi_mc_ret, 1, NULL),
  F("rsi:\n  hostname: fcc.example.net\n", K_CHARARR, rsi_hostname, 0, "fcc.example.net"),
  F("no-fcc: true\n", K_INT, no_fcc, 1, NULL),
  F("gop-cap: 1234\n", K_UINT, gop_cap_ms, 1234, NULL),
  F("max-bursts: 321\n", K_SIZE, max_bursts, 321, NULL),
  F("burst-multiplier: 2.5\n", K_DOUBLE, burst_multiplier, 2.5, NULL),
  F("burst-duration-cap: 4567\n", K_UINT, duration_cap_ms, 4567, NULL),
  F("max-buffer-fill-bound: 0\n", K_UINT, max_buffer_fill_bound_ms, 0, NULL),
  F("congestion-nack-threshold: 9\n", K_UINT, congestion_nack_threshold, 9, NULL),
  F("fcc:\n  resolve-by-port: true\n", K_INT, fcc_resolve_by_port, 1, NULL),
  F("fcc:\n  resolve-base-port: 30000\n", K_UINT, fcc_resolve_base_port, 30000, NULL),
  F("metrics:\n  sock: /tmp/m.sock\n", K_STRPTR, metrics_sock, 0, "/tmp/m.sock"),
  F("metrics:\n  id: inst9\n", K_STRPTR, metrics_id, 0, "inst9"),
  F("metrics:\n  interval: 42\n", K_UINT, metrics_interval_s, 42, NULL),
  F("metrics:\n  inspect-ts: full\n", K_INT, metrics_inspect_ts, METRICS_INSPECT_TS_FULL, NULL),
  F("color: never\n", K_INT, color_mode, LOG_COLOR_NEVER, NULL),
};

static char g_dir[64];
static char g_path[128];

static void write_cfg(const char *text) {
  FILE *f;

  snprintf(g_dir, sizeof g_dir, "/tmp/fccret_cfg_XXXXXX");
  ck_assert_ptr_nonnull(mkdtemp(g_dir));
  snprintf(g_path, sizeof g_path, "%s/c.yaml", g_dir);
  f = fopen(g_path, "w");
  ck_assert_ptr_nonnull(f);
  ck_assert_uint_eq(fwrite(text, 1, strlen(text), f), strlen(text));
  fclose(f);
}

static void remove_cfg(void) {
  unlink(g_path);
  rmdir(g_dir);
}

static int load(const char *text, config_t *cfg) {
  int rc;

  write_cfg(text);
  fccret_cfg_defaults(cfg);
  rc = fccret_cfg_load(cfg, g_path, 1);
  remove_cfg();
  return rc;
}

START_TEST(each_key_sets_its_config_field) {
  const field_case_t *c = &field_cases[_i];
  config_t cfg;
  const unsigned char *base;

  ck_assert_int_eq(load(c->yaml, &cfg), 0);
  base = (const unsigned char *)&cfg + c->off;
  switch (c->kind) {
    case K_UINT: ck_assert_uint_eq(*(const unsigned *)base, (unsigned)c->num); break;
    case K_SIZE: ck_assert_uint_eq(*(const size_t *)base, (size_t)c->num); break;
    case K_UCHAR: ck_assert_uint_eq(*base, (unsigned)c->num); break;
    case K_INT: ck_assert_int_eq(*(const int *)base, (int)c->num); break;
    case K_DOUBLE: {
      double v;
      memcpy(&v, base, sizeof v);
      ck_assert_double_eq(v, c->num);
      break;
    }
    case K_STRPTR: ck_assert_str_eq(*(const char *const *)base, c->str); break;
    case K_CHARARR: ck_assert_str_eq((const char *)base, c->str); break;
  }
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

  write_cfg("bogus-key: 1\n");
  fccret_cfg_defaults(&cfg);
  ck_assert_int_eq(fccret_cfg_load(&cfg, g_path, 1), -1);
  fccret_cfg_defaults(&cfg);
  ck_assert_int_eq(fccret_cfg_load(&cfg, g_path, 0), 0);
  remove_cfg();
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
  write_cfg("no-ret: true\nno-fcc: true\n");
  ck_assert_int_eq(fccret_cfg_test(g_path, 0), 0);
  ck_assert_int_eq(fccret_cfg_test(g_path, 1), -1);
  remove_cfg();

  write_cfg("rsi:\n  mc-ret: true\nno-mc-ret: true\nmetrics:\n  sock: /tmp/x.sock\n  inspect-ts: basic\n");
  ck_assert_int_eq(fccret_cfg_test(g_path, 1), -1);
  remove_cfg();

  write_cfg("range: 239.0.0.0/8\nlisten: 10.0.0.1:6000\niface: eth0\nmetrics:\n  id: m1\n");
  ck_assert_int_eq(fccret_cfg_test(g_path, 1), 0);
  remove_cfg();

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
