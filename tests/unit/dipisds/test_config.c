/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "../cfg_fixture.h"
#include "dipisds/config.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/log.h"
#include "lib/net/netconnect.h"

static const cfg_field_case_t field_cases[] = {
  CFG_FIELD(config_t, "announce: true\n", CFG_INT, fl.have_a, 1, NULL),
  CFG_FIELD(config_t, "listen: yes\n", CFG_INT, fl.have_l, 1, NULL),
  CFG_FIELD(config_t, "input: /dev/null\n", CFG_STRPTR, input_path, 0, "/dev/null"),
  CFG_FIELD(config_t, "provider: example.org\n", CFG_STRPTR, provider, 0, "example.org"),
  CFG_FIELD(config_t, "offering: My Offer\n", CFG_STRPTR, offering, 0, "My Offer"),
  CFG_FIELD(config_t, "lang: fra\n", CFG_CHARARR, lang, 0, "fra"),
  CFG_FIELD(config_t, "mcast: 239.1.2.3:5000\n", CFG_CHARARR, mcast_group, 0, "239.1.2.3"),
  CFG_FIELD(config_t, "mcast: 239.1.2.3:5000\n", CFG_UINT, mcast_port, 5000, NULL),
  CFG_FIELD(config_t, "mcast: \"[ff15::1]:5000\"\n", CFG_INT, family, AF_INET6, NULL),
  CFG_FIELD(config_t, "mcast: 239.1.2.3:5000\n", CFG_INT, fl.have_mcast, 1, NULL),
  CFG_FIELD(config_t, "iface: eth7\n", CFG_STRPTR, iface, 0, "eth7"),
  CFG_FIELD(config_t, "dscp: video-high\n", CFG_INT, dscp, NET_DSCP_VIDEO_HIGH, NULL),
  CFG_FIELD(config_t, "dscp: 10\n", CFG_INT, dscp, 40, NULL),
  CFG_FIELD(config_t, "interval: 9\n", CFG_LONG, interval_s, 9, NULL),
  CFG_FIELD(config_t, "timeout: 77\n", CFG_LONG, timeout_s, 77, NULL),
  CFG_FIELD(config_t, "output: /tmp/out.m3u\n", CFG_STRPTR, output_path, 0, "/tmp/out.m3u"),
  CFG_FIELD(config_t, "format: xspf\n", CFG_INT, format, OUT_XSPF, NULL),
  CFG_FIELD(config_t, "format: xspf\n", CFG_INT, fl.have_format, 1, NULL),
  CFG_FIELD(config_t, "verbose: on\n", CFG_INT, verbose, 1, NULL),
  CFG_FIELD(config_t, "color: never\n", CFG_INT, color_mode, LOG_COLOR_NEVER, NULL),
  CFG_FIELD(config_t, "daemonize: 1\n", CFG_INT, daemonize, 1, NULL),
  CFG_FIELD(config_t, "ret:\n  addr: 10.0.0.1:6000\n", CFG_INT, ret_enabled, 1, NULL),
  CFG_FIELD(config_t, "ret:\n  addr: 10.0.0.1:6000\n", CFG_CHARARR, ret_addr, 0, "10.0.0.1"),
  CFG_FIELD(config_t, "ret:\n  addr: 10.0.0.1:6000\n", CFG_UINT, ret_port, 6000, NULL),
  CFG_FIELD(config_t, "ret:\n  rtx-time: 1500\n", CFG_UINT, ret_rtx_time, 1500, NULL),
  CFG_FIELD(config_t, "ret:\n  rtx-pt: 100\n", CFG_UCHAR, ret_rtx_pt, 100, NULL),
  CFG_FIELD(config_t, "ret:\n  mc: true\n", CFG_INT, ret_mc, 1, NULL),
  CFG_FIELD(config_t, "ret:\n  mc-port: 6002\n", CFG_UINT, ret_mc_port, 6002, NULL),
  CFG_FIELD(config_t, "ret:\n  rsi-mc-ret: true\n", CFG_INT, ret_rsi_mc_ret, 1, NULL),
  CFG_FIELD(config_t, "fcc:\n  addr: 10.0.0.2:6001\n", CFG_INT, fcc_enabled, 1, NULL),
  CFG_FIELD(config_t, "fcc:\n  addr: 10.0.0.2:6001\n", CFG_CHARARR, fcc_addr, 0, "10.0.0.2"),
  CFG_FIELD(config_t, "fcc:\n  addr: 10.0.0.2:6001\n", CFG_UINT, fcc_port, 6001, NULL),
  CFG_FIELD(config_t, "fcc:\n  rtx-time: 1600\n", CFG_UINT, fcc_rtx_time, 1600, NULL),
  CFG_FIELD(config_t, "fcc:\n  rtx-pt: 101\n", CFG_UCHAR, fcc_rtx_pt, 101, NULL),
  CFG_FIELD(config_t, "fcc:\n  resolve-by-port: true\n", CFG_INT, fcc_resolve_by_port, 1, NULL),
  CFG_FIELD(config_t, "fcc:\n  resolve-base-port: 30000\n", CFG_UINT, fcc_resolve_base_port, 30000, NULL),
  CFG_FIELD(config_t, "fcc:\n  resolve-max-channels: 123\n", CFG_SIZE, fcc_resolve_max_channels, 123, NULL),
  CFG_FIELD(config_t, "al-fec:\n  addr: 10.0.0.3:6004\n", CFG_INT, al_fec_enabled, 1, NULL),
  CFG_FIELD(config_t, "al-fec:\n  addr: 10.0.0.3:6004\n", CFG_CHARARR, al_fec_addr, 0, "10.0.0.3"),
  CFG_FIELD(config_t, "al-fec:\n  addr: 10.0.0.3:6004\n", CFG_UINT, al_fec_port, 6004, NULL),
  CFG_FIELD(config_t, "al-fec:\n  pt: 97\n", CFG_UCHAR, al_fec_pt, 97, NULL),
  CFG_FIELD(config_t, "metrics:\n  sock: /tmp/m.sock\n", CFG_STRPTR, metrics_sock, 0, "/tmp/m.sock"),
  CFG_FIELD(config_t, "metrics:\n  id: sds1\n", CFG_STRPTR, metrics_id, 0, "sds1"),
  CFG_FIELD(config_t, "metrics:\n  interval: 42\n", CFG_UINT, metrics_interval_s, 42, NULL),
  CFG_FIELD(config_t, "packages: /dev/null\n", CFG_STRPTR, packages_path, 0, "/dev/null"),
  CFG_FIELD(config_t, "cells: /dev/null\n", CFG_STRPTR, cells_path, 0, "/dev/null"),
  CFG_FIELD(config_t, "rms:\n  name: Regional\n", CFG_STRPTR, rms_name, 0, "Regional"),
  CFG_FIELD(config_t, "rms:\n  name: Regional\n", CFG_INT, rms_enabled, 1, NULL),
  CFG_FIELD(config_t, "rms:\n  lang: fra\n", CFG_CHARARR, rms_lang, 0, "fra"),
  CFG_FIELD(config_t, "rms:\n  location: Berlin\n", CFG_STRPTR, rms_location, 0, "Berlin"),
  CFG_FIELD(config_t, "rms:\n  logo: http://example.org/l.png\n", CFG_STRPTR, rms_logo, 0, "http://example.org/l.png"),
  CFG_FIELD(config_t, "fus:\n  name: Follow\n", CFG_STRPTR, fus_name, 0, "Follow"),
  CFG_FIELD(config_t, "fus:\n  name: Follow\n", CFG_INT, fus_enabled, 1, NULL),
  CFG_FIELD(config_t, "fus:\n  lang: fra\n", CFG_CHARARR, fus_lang, 0, "fra"),
  CFG_FIELD(config_t, "fus:\n  id: 4711\n", CFG_SIZE, fus_id, 4711, NULL),
  CFG_FIELD(config_t, "fus:\n  announce: 239.9.9.9:5000\n", CFG_CHARARR, fus_announce_addr, 0, "239.9.9.9"),
  CFG_FIELD(config_t, "fus:\n  announce: 239.9.9.9:5000\n", CFG_UINT, fus_announce_port, 5000, NULL),
  CFG_FIELD(config_t, "fus:\n  logo: http://example.org/f.png\n", CFG_STRPTR, fus_logo, 0, "http://example.org/f.png"),
};

static const char *const bad_cases[] = {
  "announce: maybe\n",
  "listen: maybe\n",
  "lang: xx\n",
  "mcast: bogus\n",
  "mcast: 127.0.0.1:5000\n",
  "dscp: bogus\n",
  "dscp: 64\n",
  "interval: -1\n",
  "interval: soon\n",
  "timeout: -1\n",
  "format: pdf\n",
  "verbose: perhaps\n",
  "color: rainbow\n",
  "daemonize: maybe\n",
  "ret:\n  addr: bogus\n",
  "ret:\n  rtx-time: 0\n",
  "ret:\n  rtx-pt: 128\n",
  "ret:\n  mc: maybe\n",
  "ret:\n  mc-port: 0\n",
  "ret:\n  rsi-mc-ret: maybe\n",
  "fcc:\n  addr: bogus\n",
  "fcc:\n  rtx-time: 0\n",
  "fcc:\n  rtx-pt: 128\n",
  "fcc:\n  resolve-by-port: maybe\n",
  "fcc:\n  resolve-base-port: 0\n",
  "fcc:\n  resolve-max-channels: 0\n",
  "al-fec:\n  addr: bogus\n",
  "al-fec:\n  pt: 128\n",
  "metrics:\n  interval: 0\n",
  "metrics:\n  interval: 86401\n",
  "rms:\n  lang: xx\n",
  "fus:\n  lang: xx\n",
  "fus:\n  id: abc\n",
  "fus:\n  id: \"\"\n",
  "fus:\n  announce: bogus\n",
};

static cfg_fixture_t g_fx;

static int load(const char *text, config_t *cfg) {
  int rc;

  cfg_fixture_write(&g_fx, text);
  sds_cfg_defaults(cfg);
  rc = sds_cfg_load(cfg, g_fx.path, 1);
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

  sds_cfg_defaults(&cfg);
  ck_assert_int_eq(cfg.format, OUT_M3U);
  ck_assert_int_eq(cfg.dscp, NET_DSCP_SIGNALLING);
  ck_assert_int_eq((int)cfg.interval_s, 5);
  ck_assert_int_eq((int)cfg.timeout_s, 35);
}
END_TEST

START_TEST(unknown_key_is_rejected_in_strict_mode_only) {
  config_t cfg;

  cfg_fixture_write(&g_fx, "bogus-key: 1\n");
  sds_cfg_defaults(&cfg);
  ck_assert_int_eq(sds_cfg_load(&cfg, g_fx.path, 1), -1);
  sds_cfg_defaults(&cfg);
  ck_assert_int_eq(sds_cfg_load(&cfg, g_fx.path, 0), 0);
  cfg_fixture_remove(&g_fx);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(missing_explicit_file_fails) {
  config_t cfg;

  sds_cfg_defaults(&cfg);
  ck_assert_int_eq(sds_cfg_load(&cfg, "/nonexistent/dipisds.yaml", 0), -1);
}
END_TEST

#define ANN "announce: true\nmcast: 239.1.2.3:5000\n"
#define ANN_XML ANN "input: @F@\n"
#define LST "listen: true\nmcast: 239.1.2.3:5000\n"

typedef struct {
  const char *yaml;
  const char *file;
  int strict_result;
} cfgtest_case_t;

static const cfgtest_case_t cfgtest_cases[] = {
  {ANN_XML, "a.xml", 0},
  {LST, "a.xml", 0},
  {"mcast: 239.1.2.3:5000\n", "a.xml", -1},
  {"announce: true\ninput: @F@\n", "a.xml", -1},
  {ANN, "a.xml", -1},
  {ANN "input: @F@\n", "a.csv", -1},
  {ANN "input: @F@\nprovider: example.org\n", "a.csv", -1},
  {ANN "input: @F@\nprovider: example.org\noffering: Offer\n", "a.csv", 0},
  {"announce: true\nlisten: true\nmcast: 239.1.2.3:5000\n", "a.xml", -1},
  {LST "ret:\n  addr: 10.0.0.1:6000\n", "a.xml", -1},
  {ANN_XML "rms:\n  name: R\n  location: L\nfus:\n  name: F\n  id: 1\n", "a.xml", -1},
  {ANN_XML "rms:\n  name: R\n", "a.xml", -1},
  {ANN_XML "rms:\n  lang: fra\n", "a.xml", -1},
  {ANN_XML "fus:\n  name: F\n", "a.xml", -1},
  {ANN_XML "fus:\n  lang: fra\n", "a.xml", -1},
  {ANN_XML "ret:\n  rtx-time: 100\n", "a.xml", -1},
  {ANN_XML "ret:\n  addr: 10.0.0.1:6000\n  rsi-mc-ret: true\n", "a.xml", -1},
  {ANN_XML "fcc:\n  rtx-time: 100\n", "a.xml", -1},
  {ANN_XML "al-fec:\n  pt: 97\n", "a.xml", -1},
  {ANN_XML "metrics:\n  sock: /tmp/x.sock\n", "a.xml", -1},
  {ANN_XML "packages: @F@\n", "a.xml", -1},
};

START_TEST(config_test_reports_warnings_and_fails_only_in_strict_mode) {
  cfg_fixture_write_with_file(&g_fx, cfgtest_cases[_i].yaml, "@F@", cfgtest_cases[_i].file);
  ck_assert_int_eq(sds_cfg_test(g_fx.path, 0), 0);
  ck_assert_int_eq(sds_cfg_test(g_fx.path, 1), cfgtest_cases[_i].strict_result);
  cfg_fixture_remove(&g_fx);
}
END_TEST

START_TEST(config_test_missing_file_fails) {
  ck_assert_int_eq(sds_cfg_test("/nonexistent/dipisds.yaml", 0), -1);
}
END_TEST

static Suite *config_suite(void) {
  Suite *s = suite_create("dipisds_config");
  TCase *tc = tcase_create("core");

  tcase_add_loop_test(tc, each_key_sets_its_config_field, 0, (int)(sizeof field_cases / sizeof field_cases[0]));
  tcase_add_loop_test(tc, invalid_values_are_rejected, 0, (int)(sizeof bad_cases / sizeof bad_cases[0]));
  tcase_add_test(tc, defaults_are_the_documented_values);
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
