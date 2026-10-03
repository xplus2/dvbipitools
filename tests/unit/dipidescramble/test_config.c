/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dipidescramble/config.h"
#include "lib/config/yamlcfg.h"

#define HEX32 "00112233445566778899aabbccddeeff"
#define A64 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define A256 A64 A64 A64 A64

typedef enum { K_INT, K_UINT, K_STR, K_ARR } kind_t;

typedef struct {
  const char *yaml;
  kind_t kind;
  size_t off;
  long num;
  const char *str;
} field_case_t;

#define F(y, k, field, n, s) {y, k, offsetof(config_t, field), n, s}

static const field_case_t field_cases[] = {
  F("input: rtp://@239.1.1.1:5000\n", K_INT, input.kind, INPUT_RTP, NULL),
  F("input: udp://@[ff15::1]:5000\n", K_INT, input.family, AF_INET6, NULL),
  F("input: srt://@127.0.0.1:9000\n", K_INT, input.srt_listen, 1, NULL),
  F("serial: SER-9\n", K_STR, serial, 0, "SER-9"),
  F("emm-file: /tmp/emm.bin\n", K_STR, emm_file, 0, "/tmp/emm.bin"),
  F("unicast-emm: https://t@h/emm\n", K_STR, unicast_emm_uri, 0, "https://t@h/emm"),
  F("insecure: true\n", K_INT, insecure_tls, 1, NULL),
  F("token-header: X-Tok\n", K_STR, unicast_emm_token_header, 0, "X-Tok"),
  F("output:\n  - a.ts\n  - srt://127.0.0.1:9001\n", K_INT, n_out, 2, NULL),
  F("output:\n  - a.ts\n  - srt://127.0.0.1:9001\n", K_INT, out[1].kind, OUT_SRT, NULL),
  F("format: mkv\n", K_INT, format, FMT_MKV, NULL),
  F("strip-lcevc: yes\n", K_INT, strip_lcevc, 1, NULL),
  F("pmt-pid: 0x200\n", K_UINT, pmt_pid, 512, NULL),
  F("pmt-pid: all\n", K_INT, pmt_sel, PMT_SEL_ALL, NULL),
  F("iface: eth7\n", K_STR, iface_in, 0, "eth7"),
  F("verbose: on\n", K_INT, verbose, 1, NULL),
  F("color: never\n", K_INT, color_mode, 2, NULL),
  F("daemonize: 1\n", K_INT, daemonize, 1, NULL),
  F("ecm-profile: cipher=aes256-ecb\n", K_INT, ecm_profile.set, 1, NULL),
  F("max-services: 17\n", K_UINT, max_services, 17, NULL),
  F("rist:\n  profile: main\n", K_INT, rist_profile_main, 1, NULL),
  F("rist:\n  encryption-type: 256\n", K_INT, rist_key_size, 256, NULL),
  F("biss1:\n  sw: 0123456789ab\n", K_INT, biss1_sw_given, 1, NULL),
  F("biss2:\n  sw: " HEX32 "\n", K_INT, biss2_sw_given, 1, NULL),
  F("biss2:\n  esw: " HEX32 "\n", K_INT, biss2_esw_given, 1, NULL),
  F("biss2:\n  id: " HEX32 "\n", K_INT, biss2_id_given, 1, NULL),
  F("metrics:\n  sock: /tmp/m.sock\n", K_STR, metrics_sock, 0, "/tmp/m.sock"),
  F("metrics:\n  id: inst3\n", K_STR, metrics_id, 0, "inst3"),
  F("metrics:\n  interval: 33\n", K_UINT, metrics_interval_s, 33, NULL),
  F("metrics:\n  inspect-ts: full\n", K_INT, metrics_inspect_ts, METRICS_INSPECT_TS_FULL, NULL),
  F("metrics:\n  inspect-ts-pids: 256,512\n", K_UINT, metrics_n_known_pids, 2, NULL),
  F("srt:\n  passphrase-in: 0123456789ab\n", K_ARR, srt_passphrase_in, 0, "0123456789ab"),
  F("srt:\n  pbkeylen-in: 24\n", K_INT, srt_pbkeylen_in, 24, NULL),
  F("srt:\n  streamid-in: sid1\n", K_ARR, srt_streamid_in, 0, "sid1"),
  F("srt:\n  packetfilter-in: fec,cols:4\n", K_ARR, srt_packetfilter_in, 0, "fec,cols:4"),
  F("srt:\n  latency-in: 123\n", K_UINT, srt_latency_in_ms, 123, NULL),
  F("srt:\n  passphrase: 0123456789cd\n", K_ARR, srt_passphrase, 0, "0123456789cd"),
  F("srt:\n  pbkeylen: 32\n", K_INT, srt_pbkeylen, 32, NULL),
  F("srt:\n  streamid: sid2\n", K_ARR, srt_streamid, 0, "sid2"),
  F("srt:\n  packetfilter: fec,rows:3\n", K_ARR, srt_packetfilter, 0, "fec,rows:3"),
  F("srt:\n  latency: 321\n", K_UINT, srt_latency_ms, 321, NULL),
};

static char g_dir[64];
static char g_path[128];

static void write_cfg(const char *text) {
  FILE *f;

  snprintf(g_dir, sizeof g_dir, "/tmp/dscr_cfg_XXXXXX");
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
  dscr_cfg_defaults(cfg);
  rc = dscr_cfg_load(cfg, g_path, 1);
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
    case K_INT: ck_assert_int_eq(*(const int *)base, (int)c->num); break;
    case K_UINT: ck_assert_uint_eq(*(const unsigned *)base, (unsigned)c->num); break;
    case K_STR: ck_assert_str_eq(*(const char *const *)base, c->str); break;
    case K_ARR: ck_assert_str_eq((const char *)base, c->str); break;
  }
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(key_and_ca_key_paths_are_stored) {
  config_t cfg;

  write_cfg("key: /etc/dev.pem\nbiss2:\n  ca-key: /etc/ca.pem\n");
  dscr_cfg_defaults(&cfg);
  ck_assert_int_eq(dscr_cfg_load(&cfg, g_path, 0), 0);
  remove_cfg();
  ck_assert_str_eq(cfg.key_path, "/etc/dev.pem");
  ck_assert_int_eq(cfg.n_biss2_ca_key, 1);
  ck_assert_str_eq(cfg.biss2_ca_key[0], "/etc/ca.pem");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(ca_key_accepts_a_list) {
  config_t cfg;

  write_cfg("biss2:\n  ca-key:\n    - /etc/a.pem\n    - /etc/b.pem\n");
  dscr_cfg_defaults(&cfg);
  ck_assert_int_eq(dscr_cfg_load(&cfg, g_path, 0), 0);
  remove_cfg();
  ck_assert_int_eq(cfg.n_biss2_ca_key, 2);
  ck_assert_str_eq(cfg.biss2_ca_key[0], "/etc/a.pem");
  ck_assert_str_eq(cfg.biss2_ca_key[1], "/etc/b.pem");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(too_many_ca_keys_are_rejected) {
  config_t cfg;
  char text[1536] = "biss2:\n  ca-key:\n";
  int i;
  for (i = 0; i < DIPIDESCRAMBLE_MAX_CA_KEYS + 1; i++) strcat(text, "    - /etc/ca.pem\n");
  ck_assert_int_eq(load(text, &cfg), -1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

static const char *const bad_yaml[] = {
  "input: bogus://x\n",
  "token-header: a b\n",
  "output: srt://@127.0.0.1:9001\n",
  "format: avi\n",
  "pmt-pid: 0x5\n",
  "verbose: maybe\n",
  "color: rainbow\n",
  "biss2:\n  sw: zz\n",
  "biss2:\n  esw: zz\n",
  "biss2:\n  id: zz\n",
  "biss1:\n  sw: abc\n",
  "ecm-profile: bogus\n",
  "metrics:\n  interval: 0\n",
  "metrics:\n  inspect-ts: loud\n",
  "metrics:\n  inspect-ts-pids: 9000\n",
  "max-services: 0\n",
  "max-services: 999\n",
  "rist:\n  profile: ultra\n",
  "rist:\n  encryption-type: 100\n",
  "srt:\n  pbkeylen-in: 20\n",
  "srt:\n  pbkeylen: 20\n",
  "srt:\n  latency-in: 0\n",
  "srt:\n  latency: 70000\n",
  "srt:\n  passphrase-in: " A256 "\n",
  "srt:\n  streamid-in: " A256 "\n",
  "srt:\n  packetfilter-in: " A256 A256 "\n",
  "srt:\n  passphrase: " A256 "\n",
  "srt:\n  streamid: " A256 "\n",
  "srt:\n  packetfilter: " A256 A256 "\n",
};

START_TEST(invalid_values_are_rejected) {
  config_t cfg;

  ck_assert_int_eq(load(bad_yaml[_i], &cfg), -1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(too_many_outputs_are_rejected) {
  config_t cfg;
  char text[512] = "output:\n";
  int i;

  for (i = 0; i < DIPIDESCRAMBLE_MAX_OUT + 1; i++) {
    char line[32];

    snprintf(line, sizeof line, "  - o%d.ts\n", i);
    strcat(text, line);
  }
  ck_assert_int_eq(load(text, &cfg), -1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(unknown_key_is_rejected_in_strict_mode_only) {
  config_t cfg;

  write_cfg("bogus-key: 1\n");
  dscr_cfg_defaults(&cfg);
  ck_assert_int_eq(dscr_cfg_load(&cfg, g_path, 1), -1);
  dscr_cfg_defaults(&cfg);
  ck_assert_int_eq(dscr_cfg_load(&cfg, g_path, 0), 0);
  remove_cfg();
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

typedef struct {
  const char *yaml;
  int strict_result;
} test_case_t;

static const test_case_t cfgtest_cases[] = {
  {"input: udp://@239.1.1.1:5000\noutput:\n  - a.ts\n", 0},
  {"output:\n  - a.ts\n", -1},
  {"input: udp://@239.1.1.1:5000\n", -1},
  {"input: udp://@239.1.1.1:5000\noutput:\n  - a.ts\n  - b.ts\nformat: mkv\n", -1},
  {"input: udp://@239.1.1.1:5000\noutput:\n  - a.ts\ninsecure: true\n", -1},
  {"input: udp://@239.1.1.1:5000\noutput:\n  - a.ts\nstrip-lcevc: true\n", -1},
  {"input: udp://@239.1.1.1:5000\noutput:\n  - a.ts\nbiss2:\n  sw: " HEX32 "\n  esw: " HEX32 "\n  id: " HEX32 "\n", -1},
  {"input: udp://@239.1.1.1:5000\noutput:\n  - a.ts\nbiss2:\n  esw: " HEX32 "\n", -1},
  {"input: udp://@239.1.1.1:5000\noutput:\n  - a.ts\nbiss2:\n  id: " HEX32 "\n", -1},
  {"input: udp://@239.1.1.1:5000\noutput:\n  - a.ts\nbiss1:\n  sw: 0123456789ab\nbiss2:\n  sw: " HEX32 "\n", -1},
  {"input: udp://@239.1.1.1:5000\noutput:\n  - a.ts\nmetrics:\n  sock: /tmp/x.sock\n", -1},
  {"input: udp://@239.1.1.1:5000\noutput:\n  - a.ts\nmetrics:\n  inspect-ts: basic\n", -1},
  {"input: udp://@239.1.1.1:5000\noutput:\n  - a.ts\nrist:\n  profile: main\n", -1},
  {"input: udp://@239.1.1.1:5000\noutput:\n  - a.ts\nrist:\n  encryption-type: 128\n", -1},
  {"input: rist://@127.0.0.1:6000\noutput:\n  - a.ts\nrist:\n  encryption-type: 128\n", -1},
  {"input: srt://@127.0.0.1:9000\noutput:\n  - a.ts\nsrt:\n  passphrase-in: short\n", -1},
  {"input: srt://@127.0.0.1:9000\noutput:\n  - a.ts\nsrt:\n  pbkeylen-in: 16\n", -1},
  {"input: udp://@239.1.1.1:5000\noutput:\n  - a.ts\nsrt:\n  latency-in: 100\n", -1},
  {"input: udp://@239.1.1.1:5000\noutput:\n  - srt://127.0.0.1:9001\nsrt:\n  passphrase: short\n", -1},
  {"input: udp://@239.1.1.1:5000\noutput:\n  - srt://127.0.0.1:9001\nsrt:\n  pbkeylen: 16\n", -1},
  {"input: udp://@239.1.1.1:5000\noutput:\n  - a.ts\nsrt:\n  latency: 100\n", -1},
  {"input: udp://@239.1.1.1:5000\noutput:\n  - a.ts\nkey: /nonexistent-dir-dscr/k.pem\n", -1},
};

START_TEST(config_test_flags_inconsistent_settings_in_strict_mode) {
  write_cfg(cfgtest_cases[_i].yaml);
  ck_assert_int_eq(dscr_cfg_test(g_path, 1), cfgtest_cases[_i].strict_result);
  ck_assert_int_eq(dscr_cfg_test(g_path, 0), 0);
  remove_cfg();
}
END_TEST

START_TEST(config_test_fails_for_a_missing_file) {
  ck_assert_int_eq(dscr_cfg_test("/nonexistent/dscr.yaml", 0), -1);
}
END_TEST

START_TEST(defaults_zero_the_whole_config) {
  config_t cfg;

  memset(&cfg, 0xAB, sizeof cfg);
  dscr_cfg_defaults(&cfg);
  ck_assert_int_eq(cfg.n_out, 0);
  ck_assert_int_eq(cfg.have_input, 0);
  ck_assert_ptr_null(cfg.key_path);
}
END_TEST

static Suite *config_suite(void) {
  Suite *s = suite_create("dipidescramble_config");
  TCase *tc = tcase_create("core");
  tcase_add_loop_test(tc, each_key_sets_its_config_field, 0, sizeof field_cases / sizeof field_cases[0]);
  tcase_add_test(tc, key_and_ca_key_paths_are_stored);
  tcase_add_test(tc, ca_key_accepts_a_list);
  tcase_add_test(tc, too_many_ca_keys_are_rejected);
  tcase_add_loop_test(tc, invalid_values_are_rejected, 0, sizeof bad_yaml / sizeof bad_yaml[0]);
  tcase_add_test(tc, too_many_outputs_are_rejected);
  tcase_add_test(tc, unknown_key_is_rejected_in_strict_mode_only);
  tcase_add_loop_test(tc, config_test_flags_inconsistent_settings_in_strict_mode, 0, sizeof cfgtest_cases / sizeof cfgtest_cases[0]);
  tcase_add_test(tc, config_test_fails_for_a_missing_file);
  tcase_add_test(tc, defaults_zero_the_whole_config);
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
