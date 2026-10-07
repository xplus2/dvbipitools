/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "../cfg_fixture.h"
#include "dipisrt/config.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/log.h"

#define A64 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define A128 A64 A64
#define A256 A128 A128

static const cfg_field_case_t field_cases[] = {
  CFG_FIELD(config_t, "in: srt://@127.0.0.1:9000\n", CFG_INT, n_in, 1, NULL),
  CFG_FIELD(config_t, "in: srt://@127.0.0.1:9000\n", CFG_INT, in.is_srt, 1, NULL),
  CFG_FIELD(config_t, "in: srt://@127.0.0.1:9000\n", CFG_INT, in.listen, 1, NULL),
  CFG_FIELD(config_t, "in: srt://@127.0.0.1:9000\n", CFG_UINT, in.srt_port[0], 9000, NULL),
  CFG_FIELD(config_t, "in: srt://@127.0.0.1:9000\n", CFG_CHARARR, in.srt_host[0], 0, "127.0.0.1"),
  CFG_FIELD(config_t, "in: rtp://@239.1.2.3:5000\n", CFG_INT, in.nonsrt.kind, PLAIN_EP_RTP, NULL),
  CFG_FIELD(config_t, "in: http://example.org/live.ts\n", CFG_INT, in.nonsrt.kind, PLAIN_EP_HTTP, NULL),
  CFG_FIELD(config_t, "out: srt://127.0.0.1:9001\n", CFG_INT, n_out, 1, NULL),
  CFG_FIELD(config_t, "out: srt://127.0.0.1:9001\n", CFG_INT, out.listen, 0, NULL),
  CFG_FIELD(config_t, "out: srt://127.0.0.1:9001\n", CFG_UINT, out.srt_port[0], 9001, NULL),
  CFG_FIELD(config_t, "out: udp://239.1.2.3:5000\n", CFG_INT, out.nonsrt.kind, PLAIN_EP_UDP, NULL),
  CFG_FIELD(config_t, "iface: eth7\n", CFG_STRPTR, iface, 0, "eth7"),
  CFG_FIELD(config_t, "insecure: true\n", CFG_INT, insecure_tls, 1, NULL),
#ifdef DIPISRT_HAVE_BONDING
  CFG_FIELD(config_t, "group-mode: broadcast\n", CFG_INT, group_mode, SRTGROUP_BROADCAST, NULL),
  CFG_FIELD(config_t, "group-mode: backup\n", CFG_INT, group_mode, SRTGROUP_BACKUP, NULL),
#endif
  CFG_FIELD(config_t, "rendezvous: true\n", CFG_INT, rendezvous, 1, NULL),
  CFG_FIELD(config_t, "local: 127.0.0.1:9100\n", CFG_CHARARR, local_host, 0, "127.0.0.1"),
  CFG_FIELD(config_t, "local: 127.0.0.1:9100\n", CFG_UINT, local_port, 9100, NULL),
  CFG_FIELD(config_t, "passphrase: 0123456789ab\n", CFG_CHARARR, passphrase, 0, "0123456789ab"),
  CFG_FIELD(config_t, "pbkeylen: 24\n", CFG_INT, pbkeylen, 24, NULL),
  CFG_FIELD(config_t, "streamid: sid1\n", CFG_CHARARR, streamid, 0, "sid1"),
  CFG_FIELD(config_t, "packetfilter: fec,cols:4\n", CFG_CHARARR, packetfilter, 0, "fec,cols:4"),
  CFG_FIELD(config_t, "latency: 123\n", CFG_UINT, latency_ms, 123, NULL),
  CFG_FIELD(config_t, "send-buffer-mult: 8\n", CFG_UINT, send_buffer_mult, 8, NULL),
  CFG_FIELD(config_t, "al-fec: 10:5\n", CFG_UINT, al_fec_l, 10, NULL),
  CFG_FIELD(config_t, "al-fec: 10:5\n", CFG_UINT, al_fec_d, 5, NULL),
  CFG_FIELD(config_t, "al-fec-port: 6004\n", CFG_UINT, al_fec_port, 6004, NULL),
  CFG_FIELD(config_t, "metrics:\n  sock: /tmp/m.sock\n", CFG_STRPTR, metrics_sock, 0, "/tmp/m.sock"),
  CFG_FIELD(config_t, "metrics:\n  id: srt1\n", CFG_STRPTR, metrics_id, 0, "srt1"),
  CFG_FIELD(config_t, "metrics:\n  interval: 42\n", CFG_UINT, metrics_interval_s, 42, NULL),
  CFG_FIELD(config_t, "metrics:\n  inspect-ts: full\n", CFG_INT, metrics_inspect_ts, METRICS_INSPECT_TS_FULL, NULL),
  CFG_FIELD(config_t, "color: never\n", CFG_INT, color_mode, LOG_COLOR_NEVER, NULL),
  CFG_FIELD(config_t, "verbose: on\n", CFG_INT, verbose, 1, NULL),
  CFG_FIELD(config_t, "daemonize: 1\n", CFG_INT, daemonize, 1, NULL),
};

static const char *const bad_cases[] = {
  "in: srt://:9000\n",
  "in: srt://@127.0.0.1\n",
  "out: rtp://not-an-address:5000\n",
  "out: http://example.org/live.ts\n",
  "insecure: maybe\n",
  "group-mode: sideways\n",
  "rendezvous: maybe\n",
  "local: bogus\n",
  "passphrase: " A128 "\n",
  "pbkeylen: 20\n",
  "streamid: " A128 "\n",
  "packetfilter: " A256 "\n",
  "latency: 0\n",
  "latency: 60001\n",
  "send-buffer-mult: 0\n",
  "send-buffer-mult: 33\n",
  "al-fec: 41:1\n",
  "al-fec-port: 0\n",
  "metrics:\n  interval: 0\n",
  "metrics:\n  inspect-ts: loud\n",
  "color: rainbow\n",
  "verbose: perhaps\n",
  "daemonize: maybe\n",
};

static cfg_fixture_t g_fx;

static int load(const char *text, config_t *cfg) {
  int rc;

  cfg_fixture_write(&g_fx, text);
  srt_cfg_defaults(cfg);
  rc = srt_cfg_load(cfg, g_fx.path, 1);
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

START_TEST(bonded_srt_peers_are_collected) {
  config_t cfg;

  ck_assert_int_eq(load("out:\n  - srt://127.0.0.1:9001\n  - srt://127.0.0.2:9002\n", &cfg), 0);
  ck_assert_int_eq(cfg.out.n_srt, 2);
  ck_assert_uint_eq(cfg.out.srt_port[1], 9002u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(defaults_are_the_documented_values) {
  config_t cfg;

  srt_cfg_defaults(&cfg);
  ck_assert_int_eq(cfg.group_mode, SRTGROUP_NONE);
  ck_assert_int_eq(cfg.n_in, 0);
  ck_assert_int_eq(cfg.n_out, 0);
}
END_TEST

START_TEST(unknown_key_is_rejected_in_strict_mode_only) {
  config_t cfg;

  cfg_fixture_write(&g_fx, "bogus-key: 1\n");
  srt_cfg_defaults(&cfg);
  ck_assert_int_eq(srt_cfg_load(&cfg, g_fx.path, 1), -1);
  srt_cfg_defaults(&cfg);
  ck_assert_int_eq(srt_cfg_load(&cfg, g_fx.path, 0), 0);
  cfg_fixture_remove(&g_fx);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(missing_explicit_file_fails) {
  config_t cfg;

  srt_cfg_defaults(&cfg);
  ck_assert_int_eq(srt_cfg_load(&cfg, "/nonexistent/dipisrt.yaml", 0), -1);
}
END_TEST

#define SEND "in: udp://@239.1.2.3:5000\nout: srt://127.0.0.1:9001\n"

typedef struct {
  const char *yaml;
  int strict_result;
} cfgtest_case_t;

static const cfgtest_case_t cfgtest_cases[] = {
  {SEND, 0},
  {"in: srt://@127.0.0.1:9000\nout: udp://239.1.2.3:5000\n", 0},
  {"out: srt://127.0.0.1:9001\n", -1},
  {"in: udp://@239.1.2.3:5000\n", -1},
  {"in: srt://@127.0.0.1:9000\nout: srt://127.0.0.1:9001\n", -1},
  {"in: udp://@239.1.2.3:5000\nout: capture.ts\n", -1},
  {"in: udp://@239.1.2.3:5000\nout:\n  - srt://127.0.0.1:9001\n  - srt://127.0.0.2:9002\n", -1},
  {SEND "rendezvous: true\n", -1},
  {SEND "rendezvous: true\nlocal: 127.0.0.1:9100\n", 0},
  {"in: srt://@127.0.0.1:9000\nout: udp://239.1.2.3:5000\nrendezvous: true\nlocal: 127.0.0.1:9100\n", -1},
  {SEND "passphrase: short\n", -1},
  {SEND "passphrase: 0123456789ab\n", 0},
  {SEND "pbkeylen: 16\n", -1},
  {SEND "metrics:\n  sock: /tmp/x.sock\n", -1},
  {SEND "metrics:\n  interval: 10\n", -1},
  {SEND "metrics:\n  inspect-ts: basic\n", -1},
  {SEND "al-fec: 10:5\n", -1},
  {SEND "al-fec-port: 6004\n", -1},
#ifdef DIPISRT_HAVE_BONDING
  {SEND "group-mode: broadcast\n", -1},
  {"in: udp://@239.1.2.3:5000\nout:\n  - srt://127.0.0.1:9001\n  - srt://127.0.0.2:9002\ngroup-mode: backup\n", 0},
  {SEND "group-mode: broadcast\nrendezvous: true\nlocal: 127.0.0.1:9100\n", -1},
#endif
};

START_TEST(config_test_reports_warnings_and_fails_only_in_strict_mode) {
  cfg_fixture_write(&g_fx, cfgtest_cases[_i].yaml);
  ck_assert_int_eq(srt_cfg_test(g_fx.path, 0), 0);
  ck_assert_int_eq(srt_cfg_test(g_fx.path, 1), cfgtest_cases[_i].strict_result);
  cfg_fixture_remove(&g_fx);
}
END_TEST

START_TEST(config_test_missing_file_fails) {
  ck_assert_int_eq(srt_cfg_test("/nonexistent/dipisrt.yaml", 0), -1);
}
END_TEST

static Suite *config_suite(void) {
  Suite *s = suite_create("dipisrt_config");
  TCase *tc = tcase_create("core");

  tcase_add_loop_test(tc, each_key_sets_its_config_field, 0, (int)(sizeof field_cases / sizeof field_cases[0]));
  tcase_add_loop_test(tc, invalid_values_are_rejected, 0, (int)(sizeof bad_cases / sizeof bad_cases[0]));
  tcase_add_test(tc, bonded_srt_peers_are_collected);
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
