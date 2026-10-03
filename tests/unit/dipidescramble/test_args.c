/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stddef.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/config/yamlcfg.h"

#include "dipidescramble/cli/args.h"

typedef enum { K_INT, K_UINT, K_STR, K_ARR } kind_t;

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(missing_input_is_rejected) {
  char *argv[] = {"dipidescramble", "-o", "out.ts", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(missing_output_is_rejected) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(minimal_valid_args_ok) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "out.ts", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.input.kind, INPUT_UDP);
  ck_assert_int_eq(cfg.n_out, 1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(strip_lcevc_flag_is_parsed) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "out.mka", "-f", "mka", "--strip-lcevc", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.strip_lcevc, 1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(strip_lcevc_defaults_off) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "out.ts", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.strip_lcevc, 0);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(metrics_options_require_metrics_id) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "out.ts",
                  "--metrics", "/tmp/x.sock", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(metrics_id_alone_is_accepted) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "out.ts", "--metrics-id", "inst1", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.metrics_id, "inst1");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_input_with_at_is_accepted) {
  char *argv[] = {"dipidescramble", "-i", "rist://@127.0.0.1:6000", "-o", "out.ts", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.input.kind, INPUT_RIST);
  ck_assert_str_eq(cfg.input.rist_uri, "rist://@127.0.0.1:6000");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_input_without_at_is_rejected) {
  char *argv[] = {"dipidescramble", "-i", "rist://127.0.0.1:6000", "-o", "out.ts", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_profile_main_is_parsed) {
  char *argv[] = {"dipidescramble", "-i", "rist://@127.0.0.1:6000", "-o", "out.ts", "--rist-profile", "main", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rist_profile_main, 1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(invalid_rist_profile_is_rejected) {
  char *argv[] = {"dipidescramble", "-i", "rist://@127.0.0.1:6000", "-o", "out.ts", "--rist-profile", "bogus", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_encryption_type_with_profile_main_is_parsed) {
  char *argv[] = {"dipidescramble", "-i", "rist://@127.0.0.1:6000", "-o", "out.ts", "--rist-profile", "main", "--rist-encryption-type", "256", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rist_key_size, 256);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_encryption_type_without_profile_main_is_rejected) {
  char *argv[] = {"dipidescramble", "-i", "rist://@127.0.0.1:6000", "-o", "out.ts", "--rist-encryption-type", "128", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_encryption_type_invalid_is_rejected) {
  char *argv[] = {"dipidescramble", "-i", "rist://@127.0.0.1:6000", "-o", "out.ts", "--rist-profile", "main", "--rist-encryption-type", "192", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_encryption_type_without_rist_input_is_harmless) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "out.ts", "--rist-encryption-type", "128", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_input_listen_is_accepted) {
  char *argv[] = {"dipidescramble", "-i", "srt://@127.0.0.1:6000", "-o", "out.ts", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.input.kind, INPUT_SRT);
  ck_assert_int_eq(cfg.input.srt_listen, 1);
  ck_assert_str_eq(cfg.input.srt_host, "127.0.0.1");
  ck_assert_uint_eq(cfg.input.srt_port, 6000);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_input_caller_is_accepted) {
  char *argv[] = {"dipidescramble", "-i", "srt://127.0.0.1:6000", "-o", "out.ts", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.input.kind, INPUT_SRT);
  ck_assert_int_eq(cfg.input.srt_listen, 0);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_passphrase_in_length_is_validated) {
  char *argv[] = {"dipidescramble", "-i", "srt://@127.0.0.1:6000", "-o", "out.ts", "--srt-passphrase-in", "short", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_pbkeylen_in_requires_passphrase_in) {
  char *argv[] = {"dipidescramble", "-i", "srt://@127.0.0.1:6000", "-o", "out.ts", "--srt-pbkeylen-in", "16", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_pbkeylen_in_bad_value_is_rejected) {
  char *argv[] = {"dipidescramble", "-i", "srt://@127.0.0.1:6000", "-o", "out.ts", "--srt-passphrase-in", "0123456789", "--srt-pbkeylen-in", "20", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_output_caller_is_accepted) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "srt://127.0.0.1:7000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.n_out, 1);
  ck_assert_int_eq(cfg.out[0].kind, OUT_SRT);
  ck_assert_str_eq(cfg.out[0].srt_host, "127.0.0.1");
  ck_assert_uint_eq(cfg.out[0].srt_port, 7000);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_output_listen_is_rejected) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "srt://@127.0.0.1:7000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_output_repeatable_independent_targets) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "srt://127.0.0.1:7000", "-o", "srt://127.0.0.1:7001", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.n_out, 2);
  ck_assert_int_eq(cfg.out[0].kind, OUT_SRT);
  ck_assert_int_eq(cfg.out[1].kind, OUT_SRT);
  ck_assert_uint_eq(cfg.out[0].srt_port, 7000);
  ck_assert_uint_eq(cfg.out[1].srt_port, 7001);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_passphrase_length_is_validated) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "srt://127.0.0.1:7000", "--srt-passphrase", "short", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_pbkeylen_requires_passphrase) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "srt://127.0.0.1:7000", "--srt-pbkeylen", "16", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

static void write_cfg(char *path, const char *text) {
  int fd = mkstemp(path);
  ck_assert_int_ge(fd, 0);
  ck_assert_int_eq((int)write(fd, text, strlen(text)), (int)strlen(text));
  close(fd);
}

START_TEST(config_file_provides_rist_settings) {
  char path[] = "/tmp/dipidescramble_cfg_XXXXXX";
  char *argv[] = {"dipidescramble", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "input: rist://@127.0.0.1:6000\noutput: a.ts\nrist:\n  profile: main\n  encryption-type: 256\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.rist_profile_main, 1);
  ck_assert_int_eq(cfg.rist_key_size, 256);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_file_provides_settings) {
  char path[] = "/tmp/dipidescramble_cfg_XXXXXX";
  char *argv[] = {"dipidescramble", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "input: udp://@239.1.1.1:5000\noutput:\n  - a.ts\n  - b.ts\nformat: ts\nserial: e2e-01\nmax-services: 64\nbiss2:\n  sw: 00112233445566778899aabbccddeeff\nsrt:\n  latency-in: 100\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.n_out, 2);
  ck_assert_int_eq(cfg.input.kind, INPUT_UDP);
  ck_assert_str_eq(cfg.serial, "e2e-01");
  ck_assert_uint_eq(cfg.max_services, 64u);
  ck_assert_int_eq(cfg.biss2_sw_given, 1);
  ck_assert_uint_eq(cfg.srt_latency_in_ms, 100u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(cmdline_wins_over_config) {
  char path[] = "/tmp/dipidescramble_cfg_XXXXXX";
  char *argv[] = {"dipidescramble", "-c", path, "-o", "c.ts", "-s", "cli", NULL};
  config_t cfg = {0};
  write_cfg(path, "input: udp://@239.1.1.1:5000\noutput:\n  - a.ts\n  - b.ts\nserial: cfg\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.n_out, 1);
  ck_assert_str_eq(cfg.out[0].file_path, "c.ts");
  ck_assert_str_eq(cfg.serial, "cli");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_missing_file_is_error) {
  char *argv[] = {"dipidescramble", "-c", "/nonexistent/dipidescramble.yaml", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_invalid_value_is_error) {
  char path[] = "/tmp/dipidescramble_cfg_XXXXXX";
  char *argv[] = {"dipidescramble", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "input: udp://@239.1.1.1:5000\noutput: a.ts\nmax-services: 300\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_biss_conflict_is_rejected) {
  char path[] = "/tmp/dipidescramble_cfg_XXXXXX";
  char *argv[] = {"dipidescramble", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "input: udp://@239.1.1.1:5000\noutput: a.ts\nbiss2:\n  sw: 00112233445566778899aabbccddeeff\n  esw: 00112233445566778899aabbccddeeff\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(configtest_reports_by_exit_status) {
  char path[] = "/tmp/dipidescramble_cfg_XXXXXX";
  char *argv[] = {"dipidescramble", "--configtest", "-c", path, NULL};
  char *argv2[] = {"dipidescramble", "--configtest", "-c", "/nonexistent/dipidescramble.yaml", NULL};
  config_t cfg = {0};
  write_cfg(path, "bogus: 1\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
  ck_assert_int_eq(args_parse(ARGC(argv2), argv2, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_level_is_recorded) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "out.ts", "--metrics-id", "inst1", "--metrics-inspect-ts", "medium", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_MEDIUM);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_defaults_to_off) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "out.ts", "--metrics-id", "inst1", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_OFF);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_requires_metrics_id) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "out.ts", "--metrics-inspect-ts", "basic", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_rejects_unknown_level) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "out.ts", "--metrics-id", "inst1", "--metrics-inspect-ts", "bogus", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_from_yaml) {
  char path[] = "/tmp/dipidescramble_inspect_XXXXXX";
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "out.ts", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "metrics:\n  id: a\n  inspect-ts: full\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_FULL);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_pids_parsed) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "out.ts", "--metrics-id", "inst1", "--metrics-inspect-ts", "full", "--metrics-inspect-ts-pids", "0x100,300", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.metrics_n_known_pids, 2u);
  ck_assert_uint_eq(cfg.metrics_known_pids[0], 256u);
  ck_assert_uint_eq(cfg.metrics_known_pids[1], 300u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_pids_need_full_level) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "out.ts", "--metrics-id", "inst1", "--metrics-inspect-ts", "basic", "--metrics-inspect-ts-pids", "0x100", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_pids_reject_bad_list) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "out.ts", "--metrics-id", "inst1", "--metrics-inspect-ts", "full", "--metrics-inspect-ts-pids", "0x100,9000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

#define MAX_EXTRA 8
#define HEX32 "00112233445566778899aabbccddeeff"
#define A64 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define A256 A64 A64 A64 A64

typedef struct {
  const char *extra[MAX_EXTRA];
  kind_t kind;
  size_t off;
  long num;
  const char *str;
} opt_case_t;

#define OC(kind, field, num, str, ...) {{__VA_ARGS__}, kind, offsetof(config_t, field), num, str}

static const opt_case_t good_opts[] = {
  OC(K_INT, input.kind, INPUT_RTP, NULL, "-i", "rtp://@239.1.1.1:5000"),
  OC(K_INT, input.kind, INPUT_STDIN, NULL, "-i", "-"),
  OC(K_INT, input.family, AF_INET6, NULL, "-i", "udp://@[ff15::1]:5000"),
  OC(K_INT, input.srt_listen, 1, NULL, "-i", "srt://@127.0.0.1:9000"),
  OC(K_STR, key_path, 0, "/etc/key.pem", "-k", "/etc/key.pem"),
  OC(K_STR, serial, 0, "SERIAL-1", "-s", "SERIAL-1"),
  OC(K_STR, emm_file, 0, "/tmp/emm.bin", "-e", "/tmp/emm.bin"),
  OC(K_STR, unicast_emm_uri, 0, "http://tok@h.example/emm", "-u", "http://tok@h.example/emm"),
  OC(K_INT, insecure_tls, 1, NULL, "-u", "https://t@h/x", "--insecure"),
  OC(K_STR, unicast_emm_token_header, 0, "X-Token", "--token-header", "X-Token"),
  OC(K_INT, format, FMT_MKV, NULL, "-f", "mkv"),
  OC(K_INT, format, FMT_MKA, NULL, "-f", "mka"),
  OC(K_INT, pmt_sel, PMT_SEL_PID, NULL, "-p", "0x100"),
  OC(K_UINT, pmt_pid, 256, NULL, "-p", "0x100"),
  OC(K_INT, pmt_sel, PMT_SEL_ALL, NULL, "-p", "all"),
  OC(K_STR, iface_in, 0, "eth9", "-I", "eth9"),
  OC(K_INT, verbose, 1, NULL, "-v"),
  OC(K_INT, daemonize, 1, NULL, "-d"),
  OC(K_INT, color_mode, 2, NULL, "--color", "never"),
  OC(K_INT, biss2_sw_given, 1, NULL, "--biss2-sw", HEX32),
  OC(K_INT, biss2_esw_given, 1, NULL, "--biss2-esw", HEX32, "--biss2-id", HEX32),
  OC(K_INT, biss2_id_given, 1, NULL, "--biss2-esw", HEX32, "--biss2-id", HEX32),
  OC(K_INT, biss1_sw_given, 1, NULL, "--biss1-sw", "0123456789ab"),
  OC(K_STR, biss2_ca_key[0], 0, "/etc/ca.pem", "--biss2-ca-key", "/etc/ca.pem"),
  OC(K_INT, ecm_profile.set, 1, NULL, "--ecm-profile", "cipher=aes256-ecb"),
  OC(K_STR, metrics_sock, 0, "/tmp/m.sock", "--metrics-id", "i1", "--metrics", "/tmp/m.sock"),
  OC(K_UINT, metrics_interval_s, 7, NULL, "--metrics-id", "i1", "--metrics-interval", "7"),
  OC(K_UINT, max_services, 12, NULL, "--max-services", "12"),
  OC(K_UINT, max_services, 256, NULL, "--max-services", "256"),
  OC(K_UINT, srt_latency_ms, 250, NULL, "-o", "srt://127.0.0.1:9001", "--srt-latency", "250"),
  OC(K_ARR, srt_streamid, 0, "stream-1", "-o", "srt://127.0.0.1:9001", "--srt-streamid", "stream-1"),
  OC(K_ARR, srt_packetfilter, 0, "fec,cols:5", "-o", "srt://127.0.0.1:9001", "--srt-packetfilter", "fec,cols:5"),
  OC(K_INT, srt_pbkeylen, 24, NULL, "-o", "srt://127.0.0.1:9001", "--srt-passphrase", "0123456789ab", "--srt-pbkeylen", "24"),
  OC(K_ARR, srt_passphrase, 0, "0123456789ab", "-o", "srt://127.0.0.1:9001", "--srt-passphrase", "0123456789ab"),
  OC(K_ARR, srt_streamid_in, 0, "sid-in", "-i", "srt://@127.0.0.1:9000", "--srt-streamid-in", "sid-in"),
  OC(K_ARR, srt_packetfilter_in, 0, "fec,rows:2", "-i", "srt://@127.0.0.1:9000", "--srt-packetfilter-in", "fec,rows:2"),
  OC(K_UINT, srt_latency_in_ms, 500, NULL, "-i", "srt://@127.0.0.1:9000", "--srt-latency-in", "500"),
  OC(K_ARR, srt_passphrase_in, 0, "0123456789ab", "-i", "srt://@127.0.0.1:9000", "--srt-passphrase-in", "0123456789ab"),
  OC(K_INT, srt_pbkeylen_in, 32, NULL, "-i", "srt://@127.0.0.1:9000", "--srt-passphrase-in", "0123456789ab", "--srt-pbkeylen-in", "32"),
  OC(K_UINT, srt_latency_ms, 100, NULL, "--srt-latency", "100"),
};

START_TEST(options_set_their_config_fields) {
  const opt_case_t *c = &good_opts[_i];
  char *argv[32];
  int n = 0;
  size_t i;
  config_t cfg = {0};
  const unsigned char *base;
  int has_input = 0;
  int has_output = 0;

  argv[n++] = "dipidescramble";
  for (i = 0; i < MAX_EXTRA && c->extra[i]; i++) {
    if (strcmp(c->extra[i], "-i") == 0)
      has_input = 1;
    if (strcmp(c->extra[i], "-o") == 0)
      has_output = 1;
  }
  if (!has_input) {
    argv[n++] = "-i";
    argv[n++] = "udp://@239.1.1.1:5000";
  }
  if (!has_output) {
    argv[n++] = "-o";
    argv[n++] = "out.ts";
  }
  for (i = 0; i < MAX_EXTRA && c->extra[i]; i++)
    argv[n++] = (char *)c->extra[i];
  argv[n] = NULL;
  ck_assert_int_eq(args_parse(n, argv, &cfg), ARGS_OK);
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

typedef struct {
  const char *extra[MAX_EXTRA];
} bad_opt_t;

static const bad_opt_t bad_opts[] = {
  {{"-i", "bogus://x"}},
  {{"-i", "rtp://not-an-address"}},
  {{"-f", "avi"}},
  {{"-p", "0x5"}},
  {{"-p", "zzz"}},
  {{"--color", "rainbow"}},
  {{"--biss2-sw", "zz"}},
  {{"--biss2-esw", "zz", "--biss2-id", HEX32}},
  {{"--biss2-esw", HEX32, "--biss2-id", "zz"}},
  {{"--biss1-sw", "abc"}},
  {{"--ecm-profile", "bogus"}},
  {{"--max-services", "0"}},
  {{"--max-services", "9999"}},
  {{"--metrics-id", "i", "--metrics-interval", "0"}},
  {{"--metrics-id", "i", "--metrics-inspect-ts", "loud"}},
  {{"--token-header", "a b"}},
  {{"--token-header", "a:b"}},
  {{"--token-header", ""}},
  {{"--rist-encryption-type", "100"}},
  {{"--rist-profile", "ultra"}},
  {{"-i", "srt://@127.0.0.1:9000", "--srt-latency-in", "0"}},
  {{"-i", "srt://@127.0.0.1:9000", "--srt-pbkeylen-in", "20", "--srt-passphrase-in", "0123456789ab"}},
  {{"-o", "srt://127.0.0.1:9001", "--srt-latency", "70000"}},
  {{"-o", "srt://127.0.0.1:9001", "--srt-pbkeylen", "20", "--srt-passphrase", "0123456789ab"}},
  {{"-o", "srt://127.0.0.1:9001", "--srt-passphrase", "short"}},
  {{"--biss2-sw", HEX32, "--biss2-esw", HEX32, "--biss2-id", HEX32}},
  {{"--biss2-esw", HEX32}},
  {{"--biss2-id", HEX32}},
  {{"--biss1-sw", "0123456789ab", "--biss2-sw", HEX32}},
  {{"-f", "mkv", "-o", "second.mkv"}},
  {{"--metrics-inspect-ts-pids", "0x100"}},
  {{"-i", "srt://@127.0.0.1:9000", "--srt-streamid-in", A256}},
  {{"-i", "srt://@127.0.0.1:9000", "--srt-packetfilter-in", A256 A256}},
  {{"-i", "srt://@127.0.0.1:9000", "--srt-passphrase-in", A256}},
  {{"-o", "srt://127.0.0.1:9001", "--srt-streamid", A256}},
  {{"-o", "srt://127.0.0.1:9001", "--srt-packetfilter", A256 A256}},
  {{"-o", "srt://127.0.0.1:9001", "--srt-passphrase", A256}},
  {{"--not-an-option"}},
  {{"stray-argument"}},
};

START_TEST(invalid_options_and_combinations_are_rejected) {
  const bad_opt_t *c = &bad_opts[_i];
  char *argv[32];
  int n = 0;
  size_t i;
  config_t cfg = {0};

  argv[n++] = "dipidescramble";
  argv[n++] = "-i";
  argv[n++] = "udp://@239.1.1.1:5000";
  argv[n++] = "-o";
  argv[n++] = "out.ts";
  for (i = 0; i < MAX_EXTRA && c->extra[i]; i++)
    argv[n++] = (char *)c->extra[i];
  argv[n] = NULL;
  ck_assert_int_eq(args_parse(n, argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(too_many_output_targets_are_rejected) {
  char *argv[32];
  int n = 0;
  int i;
  config_t cfg = {0};
  static char names[DIPIDESCRAMBLE_MAX_OUT + 1][16];

  argv[n++] = "dipidescramble";
  argv[n++] = "-i";
  argv[n++] = "udp://@239.1.1.1:5000";
  for (i = 0; i < DIPIDESCRAMBLE_MAX_OUT + 1; i++) {
    snprintf(names[i], sizeof names[i], "o%d.ts", i);
    argv[n++] = "-o";
    argv[n++] = names[i];
  }
  argv[n] = NULL;
  ck_assert_int_eq(args_parse(n, argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(help_option_returns_help_status) {
  char *argv[] = {"dipidescramble", "--help", NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(repeated_output_option_collects_every_target) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "a.ts", "-o", "srt://127.0.0.1:9001", "-o", "rtmp://h/app/key", "-o", "rtmps://h/app/key", NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.n_out, 4);
  ck_assert_int_eq(cfg.out[0].kind, OUT_FILE);
  ck_assert_int_eq(cfg.out[1].kind, OUT_SRT);
  ck_assert_int_eq(cfg.out[2].kind, OUT_RTMP);
  ck_assert_int_eq(cfg.out[3].kind, OUT_RTMPS);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(repeated_ca_key_option_collects_every_path) {
  char *argv[] = {"dipidescramble", "-i", "udp://@239.1.1.1:5000", "-o", "out.ts", "--biss2-ca-key", "/etc/a.pem", "--biss2-ca-key", "/etc/keys", NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.n_biss2_ca_key, 2);
  ck_assert_str_eq(cfg.biss2_ca_key[0], "/etc/a.pem");
  ck_assert_str_eq(cfg.biss2_ca_key[1], "/etc/keys");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(too_many_ca_keys_are_rejected) {
  char *argv[8 + 2 * DIPIDESCRAMBLE_MAX_CA_KEYS + 2];
  int n = 0;
  int i;
  config_t cfg = {0};

  argv[n++] = "dipidescramble";
  argv[n++] = "-i";
  argv[n++] = "udp://@239.1.1.1:5000";
  argv[n++] = "-o";
  argv[n++] = "out.ts";
  for (i = 0; i < DIPIDESCRAMBLE_MAX_CA_KEYS + 1; i++) {
    argv[n++] = "--biss2-ca-key";
    argv[n++] = "/etc/ca.pem";
  }
  argv[n] = NULL;
  ck_assert_int_eq(args_parse(n, argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(describe_helpers_render_inputs_and_outputs) {
  config_t cfg = {0};
  char buf[128];
  out_target_t o;
  char *argv[] = {"dipidescramble", "-i", "rtp://@239.1.1.1:5000", "-o", "-", NULL};

  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  input_describe(&cfg.input, buf, sizeof buf);
  ck_assert_ptr_nonnull(strstr(buf, "239.1.1.1"));
  out_describe(&cfg.out[0], buf, sizeof buf);
  ck_assert_str_eq(buf, "- (stdout)");
  ck_assert_int_eq(dscr_cfg_set_input(&cfg, "udp://@239.1.1.1:5000"), 0);
  input_describe(&cfg.input, buf, sizeof buf);
  ck_assert_ptr_nonnull(strstr(buf, "udp://"));
  ck_assert_int_eq(dscr_cfg_set_input(&cfg, "-"), 0);
  input_describe(&cfg.input, buf, sizeof buf);
  ck_assert_str_eq(buf, "-");
  ck_assert_int_eq(dscr_cfg_set_input(&cfg, "rist://@127.0.0.1:6000"), 0);
  input_describe(&cfg.input, buf, sizeof buf);
  ck_assert_ptr_nonnull(strstr(buf, "rist://"));
  ck_assert_int_eq(dscr_cfg_set_input(&cfg, "srt://127.0.0.1:9000"), 0);
  input_describe(&cfg.input, buf, sizeof buf);
  ck_assert_ptr_nonnull(strstr(buf, "9000"));
  ck_assert_int_eq(dscr_cfg_add_out(&cfg, "srt://127.0.0.1:9001"), 0);
  o = cfg.out[cfg.n_out - 1];
  out_describe(&o, buf, sizeof buf);
  ck_assert_ptr_nonnull(strstr(buf, "9001"));
  ck_assert_int_eq(dscr_cfg_add_out(&cfg, "rtmp://h/app/key"), 0);
  out_describe(&cfg.out[cfg.n_out - 1], buf, sizeof buf);
  ck_assert_ptr_nonnull(strstr(buf, "rtmp://"));
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

static Suite *args_suite(void) {
  Suite *s = suite_create("dipidescramble_args");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, missing_input_is_rejected);
  tcase_add_test(tc, strip_lcevc_flag_is_parsed);
  tcase_add_test(tc, strip_lcevc_defaults_off);
  tcase_add_test(tc, missing_output_is_rejected);
  tcase_add_test(tc, minimal_valid_args_ok);
  tcase_add_test(tc, metrics_options_require_metrics_id);
  tcase_add_test(tc, metrics_id_alone_is_accepted);
  tcase_add_test(tc, rist_input_with_at_is_accepted);
  tcase_add_test(tc, rist_input_without_at_is_rejected);
  tcase_add_test(tc, rist_profile_main_is_parsed);
  tcase_add_test(tc, invalid_rist_profile_is_rejected);
  tcase_add_test(tc, rist_encryption_type_with_profile_main_is_parsed);
  tcase_add_test(tc, rist_encryption_type_without_profile_main_is_rejected);
  tcase_add_test(tc, rist_encryption_type_invalid_is_rejected);
  tcase_add_test(tc, rist_encryption_type_without_rist_input_is_harmless);
  tcase_add_test(tc, srt_input_listen_is_accepted);
  tcase_add_test(tc, srt_input_caller_is_accepted);
  tcase_add_test(tc, srt_passphrase_in_length_is_validated);
  tcase_add_test(tc, srt_pbkeylen_in_requires_passphrase_in);
  tcase_add_test(tc, srt_pbkeylen_in_bad_value_is_rejected);
  tcase_add_test(tc, srt_output_caller_is_accepted);
  tcase_add_test(tc, srt_output_listen_is_rejected);
  tcase_add_test(tc, srt_output_repeatable_independent_targets);
  tcase_add_test(tc, srt_passphrase_length_is_validated);
  tcase_add_test(tc, srt_pbkeylen_requires_passphrase);
  tcase_add_test(tc, config_file_provides_settings);
  tcase_add_test(tc, config_file_provides_rist_settings);
  tcase_add_test(tc, cmdline_wins_over_config);
  tcase_add_test(tc, config_missing_file_is_error);
  tcase_add_test(tc, config_invalid_value_is_error);
  tcase_add_test(tc, config_biss_conflict_is_rejected);
  tcase_add_test(tc, configtest_reports_by_exit_status);
  tcase_add_test(tc, inspect_ts_level_is_recorded);
  tcase_add_test(tc, inspect_ts_defaults_to_off);
  tcase_add_test(tc, inspect_ts_requires_metrics_id);
  tcase_add_test(tc, inspect_ts_rejects_unknown_level);
  tcase_add_test(tc, inspect_ts_from_yaml);
  tcase_add_test(tc, inspect_ts_pids_parsed);
  tcase_add_test(tc, inspect_ts_pids_need_full_level);
  tcase_add_test(tc, inspect_ts_pids_reject_bad_list);
  tcase_add_loop_test(tc, options_set_their_config_fields, 0, sizeof good_opts / sizeof good_opts[0]);
  tcase_add_loop_test(tc, invalid_options_and_combinations_are_rejected, 0, sizeof bad_opts / sizeof bad_opts[0]);
  tcase_add_test(tc, too_many_output_targets_are_rejected);
  tcase_add_test(tc, help_option_returns_help_status);
  tcase_add_test(tc, repeated_output_option_collects_every_target);
  tcase_add_test(tc, repeated_ca_key_option_collects_every_path);
  tcase_add_test(tc, too_many_ca_keys_are_rejected);
  tcase_add_test(tc, describe_helpers_render_inputs_and_outputs);
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
