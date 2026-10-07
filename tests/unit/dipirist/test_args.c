/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../cfg_fixture.h"
#include "../log_capture.h"
#include "lib/config/yamlcfg.h"

#include "dipirist/cli/args.h"
#include "dipirist/cli/priv.h"
#include "lib/helper/log.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(sender_ok_with_nonrist_in_and_rist_out) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(config_is_sender(&cfg), 1);
  ck_assert_int_eq(cfg.out.n_rist, 1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(receiver_ok_with_rist_in_and_nonrist_out) {
  char *argv[] = {"dipirist", "-i", "rist://@0.0.0.0:6000", "-o", "rtp://@239.1.1.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(config_is_sender(&cfg), 0);
  ck_assert_int_eq(cfg.in.n_rist, 1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(both_rist_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rist://@0.0.0.0:6000", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(neither_rist_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "udp://@239.1.1.2:5001", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(sink_rist_with_at_is_rejected) {
  /* -o rist:// calls out: an '@' (listen) address makes no sense as a call target */
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://@1.2.3.4:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(source_rist_without_at_is_rejected) {
  /* -i rist:// listens: without '@' librist treats it as a caller, never binds */
  char *argv[] = {"dipirist", "-i", "rist://0.0.0.0:6000", "-o", "rtp://@239.1.1.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(repeated_rist_out_bonds_peers) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "-o", "rist://5.6.7.8:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.out.n_rist, 2);
  ck_assert_str_eq(cfg.out.rist_uri[0], "rist://1.2.3.4:6000");
  ck_assert_str_eq(cfg.out.rist_uri[1], "rist://5.6.7.8:6000");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(repeated_nonrist_out_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "udp://@239.1.1.2:5001", "-o", "udp://@239.1.1.3:5001", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(mixed_rist_and_nonrist_on_same_flag_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "-o", "udp://@239.1.1.2:5001", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(missing_in_is_rejected) {
  char *argv[] = {"dipirist", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(missing_out_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(http_source_ok) {
  char *argv[] = {"dipirist", "-i", "http://10.0.0.1:4022/rtp/239.19.75.1:8700", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(http_source_accepts_arbitrary_path) {
  char *argv[] = {"dipirist", "-i", "https://10.0.0.1:8443/any/path/at/all", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.in.nonrist.kind, PLAIN_EP_HTTP);
  ck_assert_int_eq(cfg.in.nonrist.http.tls, 1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(http_sink_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rist://@0.0.0.0:6000", "-o", "http://10.0.0.1:4022/rtp/239.19.75.1:8700", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(non_multicast_direct_address_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rtp://@10.0.0.1:5000", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(default_profile_is_simple) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.profile, RIST_PROF_SIMPLE);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(profile_main_is_accepted) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--profile", "main", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.profile, RIST_PROF_MAIN);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(unknown_profile_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--profile", "advanced", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(secret_without_profile_main_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--secret", "hunter2", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(secret_with_profile_main_is_accepted) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--profile", "main", "--secret", "hunter2", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.secret, "hunter2");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(encryption_type_with_profile_main_is_accepted) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--profile", "main", "--encryption-type", "256", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.key_size, 256);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(encryption_type_without_profile_main_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--encryption-type", "128", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(encryption_type_invalid_is_rejected) {
  static char bad[][4] = {"0", "192", "abc", ""};
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--profile", "main", "--encryption-type", bad[i], NULL};
    config_t cfg = {0};
    ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
    yamlcfg_strpool_free(cfg.str_pool);
  }
}
END_TEST

START_TEST(buffer_out_of_range_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--buffer", "0", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(buffer_value_is_applied) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--buffer", "1000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.buffer_ms, 1000u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(metrics_options_require_metrics_id) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--metrics", "/tmp/x.sock", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(metrics_id_alone_is_accepted) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--metrics-id", "inst1", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.metrics_id, "inst1");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(help_returns_help_status) {
  char *argv[] = {"dipirist", "-h", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(unexpected_positional_argument_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "extra", NULL};
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

START_TEST(config_file_provides_encryption_type) {
  char path[] = "/tmp/dipirist_cfg_XXXXXX";
  char *argv[] = {"dipirist", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "in: rtp://@239.1.1.1:5000\nout: rist://1.2.3.4:6000\nprofile: main\nencryption-type: 128\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.key_size, 128);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_file_provides_settings) {
  char path[] = "/tmp/dipirist_cfg_XXXXXX";
  char *argv[] = {"dipirist", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "in: rtp://@239.1.1.1:5000\nout:\n  - rist://1.2.3.4:6000\n  - rist://5.6.7.8:6000\nbuffer: 500\nprofile: main\nsecret: pw\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.n_in, 1);
  ck_assert_int_eq(cfg.n_out, 2);
  ck_assert_int_eq(cfg.out.n_rist, 2);
  ck_assert_uint_eq(cfg.buffer_ms, 500u);
  ck_assert_int_eq(cfg.profile, RIST_PROF_MAIN);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(cmdline_wins_over_config) {
  char path[] = "/tmp/dipirist_cfg_XXXXXX";
  char *argv[] = {"dipirist", "-c", path, "-o", "rist://9.9.9.9:7000", NULL};
  config_t cfg = {0};
  write_cfg(path, "in: rtp://@239.1.1.1:5000\nout:\n  - rist://1.2.3.4:6000\n  - rist://5.6.7.8:6000\nbuffer: 500\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.out.n_rist, 1);
  ck_assert_str_eq(cfg.out.rist_uri[0], "rist://9.9.9.9:7000");
  ck_assert_uint_eq(cfg.buffer_ms, 500u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_missing_file_is_error) {
  char *argv[] = {"dipirist", "-c", "/nonexistent/dipirist.yaml", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_invalid_value_is_error) {
  char path[] = "/tmp/dipirist_cfg_XXXXXX";
  char *argv[] = {"dipirist", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "in: rtp://@239.1.1.1:5000\nout: rist://1.2.3.4:6000\nbuffer: 0\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(configtest_reports_by_exit_status) {
  char path[] = "/tmp/dipirist_cfg_XXXXXX";
  char *argv[] = {"dipirist", "--configtest", "-c", path, NULL};
  char *argv2[] = {"dipirist", "--configtest", "-c", "/nonexistent/dipirist.yaml", NULL};
  config_t cfg = {0};
  write_cfg(path, "bogus: 1\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
  ck_assert_int_eq(args_parse(ARGC(argv2), argv2, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST
START_TEST(config_scalar_key_rejects_list) {
  char path[] = "/tmp/dipirist_cfg_XXXXXX";
  char *argv[] = {"dipirist", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "in: rtp://@239.1.1.1:5000\nout: rist://1.2.3.4:6000\nbuffer: [1, 2]\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_level_is_recorded) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--metrics-id", "inst1", "--metrics-inspect-ts", "medium", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_MEDIUM);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_defaults_to_off) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--metrics-id", "inst1", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_OFF);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_requires_metrics_id) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--metrics-inspect-ts", "basic", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_rejects_unknown_level) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--metrics-id", "inst1", "--metrics-inspect-ts", "bogus", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_from_yaml) {
  char path[] = "/tmp/dipirist_inspect_XXXXXX";
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "metrics:\n  id: a\n  inspect-ts: full\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_FULL);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

#define ARGV_MAX 24
#define MSG_MAX 2048
#define A128 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"

typedef struct {
  const char *argv[ARGV_MAX];
  int argc;
  cfg_kind_t kind;
  size_t off;
  double num;
  const char *str;
} option_case_t;

#define OPT(field, kind, num, str, ...) {{"dipirist", __VA_ARGS__}, 1 + (int)(sizeof((const char *[]){__VA_ARGS__}) / sizeof(char *)), kind, offsetof(config_t, field), num, str}
#define SEND_BASE "-i", "udp://@239.1.2.3:5000", "-o", "rist://1.2.3.4:6000"
#define RECV_BASE "-i", "rist://@0.0.0.0:6000", "-o", "udp://239.1.2.3:5000"

static const option_case_t option_cases[] = {
  OPT(profile, CFG_INT, RIST_PROF_MAIN, NULL, SEND_BASE, "--profile", "main"),
  OPT(profile, CFG_INT, RIST_PROF_SIMPLE, NULL, SEND_BASE, "--profile", "simple"),
  OPT(secret, CFG_CHARARR, 0, "s3cret", SEND_BASE, "--profile", "main", "--secret", "s3cret"),
  OPT(key_size, CFG_INT, 256, NULL, SEND_BASE, "--profile", "main", "--encryption-type", "256"),
  OPT(key_size, CFG_INT, 128, NULL, SEND_BASE, "--profile", "main", "--encryption-type", "128"),
  OPT(cname, CFG_CHARARR, 0, "cam1", SEND_BASE, "--cname", "cam1"),
  OPT(buffer_ms, CFG_UINT, 750, NULL, SEND_BASE, "--buffer", "750"),
  OPT(buffer_ms, CFG_UINT, 60000, NULL, SEND_BASE, "--buffer", "60000"),
  OPT(al_fec_l, CFG_UINT, 10, NULL, SEND_BASE, "--al-fec", "10:5", "--al-fec-port", "6004"),
  OPT(al_fec_d, CFG_UINT, 5, NULL, SEND_BASE, "--al-fec", "10:5", "--al-fec-port", "6004"),
  OPT(al_fec_port, CFG_UINT, 6004, NULL, SEND_BASE, "--al-fec", "10:5", "--al-fec-port", "6004"),
  OPT(iface, CFG_STRPTR, 0, "eth7", SEND_BASE, "-I", "eth7"),
  OPT(insecure_tls, CFG_INT, 1, NULL, "-i", "https://example.org/live.ts", "-o", "rist://1.2.3.4:6000", "-k"),
  OPT(color_mode, CFG_INT, LOG_COLOR_NEVER, NULL, SEND_BASE, "--color", "never"),
  OPT(metrics_sock, CFG_STRPTR, 0, "/tmp/m.sock", SEND_BASE, "--metrics", "/tmp/m.sock", "--metrics-id", "r1"),
  OPT(metrics_id, CFG_STRPTR, 0, "r1", SEND_BASE, "--metrics-id", "r1"),
  OPT(metrics_interval_s, CFG_UINT, 42, NULL, SEND_BASE, "--metrics-id", "r1", "--metrics-interval", "42"),
  OPT(metrics_inspect_ts, CFG_INT, METRICS_INSPECT_TS_FULL, NULL, SEND_BASE, "--metrics-id", "r1", "--metrics-inspect-ts", "full"),
  OPT(verbose, CFG_INT, 1, NULL, SEND_BASE, "-v"),
  OPT(daemonize, CFG_INT, 1, NULL, SEND_BASE, "-d"),
  OPT(out.n_rist, CFG_INT, 2, NULL, "-i", "udp://@239.1.2.3:5000", "-o", "rist://1.2.3.4:6000", "-o", "rist://1.2.3.5:6000"),
  OPT(in.n_rist, CFG_INT, 2, NULL, "-i", "rist://@0.0.0.0:6000", "-i", "rist://@0.0.0.0:6002", "-o", "udp://239.1.2.3:5000"),
  OPT(in.nonrist.kind, CFG_INT, PLAIN_EP_HTTP, NULL, "-i", "http://example.org/live.ts", "-o", "rist://1.2.3.4:6000"),
  OPT(in.nonrist.kind, CFG_INT, PLAIN_EP_FILE, NULL, "-i", "-", "-o", "rist://1.2.3.4:6000"),
  OPT(out.nonrist.kind, CFG_INT, PLAIN_EP_FILE, NULL, "-i", "rist://@0.0.0.0:6000", "-o", "out.ts"),
  OPT(out.nonrist.kind, CFG_INT, PLAIN_EP_UDP, NULL, RECV_BASE),
};

START_TEST(options_set_their_fields) {
  const option_case_t *c = &option_cases[_i];
  cfg_field_case_t fc = {NULL, c->kind, c->off, c->num, c->str};
  config_t cfg = {0};

  ck_assert_int_eq(args_parse(c->argc, (char **)c->argv, &cfg), ARGS_OK);
  cfg_field_check(&cfg, &fc);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

typedef struct {
  const char *argv[ARGV_MAX];
  int argc;
  args_status_t status;
  const char *message;
} message_case_t;

#define MSG(status, message, ...) {{"dipirist", __VA_ARGS__}, 1 + (int)(sizeof((const char *[]){__VA_ARGS__}) / sizeof(char *)), status, message}

static const message_case_t message_cases[] = {
  MSG(ARGS_ERR, "invalid --profile: ultra (simple|main)", SEND_BASE, "--profile", "ultra"),
  MSG(ARGS_ERR, "--secret too long", SEND_BASE, "--secret", A128),
  MSG(ARGS_ERR, "invalid --encryption-type: 192 (128|256)", SEND_BASE, "--encryption-type", "192"),
  MSG(ARGS_ERR, "--cname too long", SEND_BASE, "--cname", A128),
  MSG(ARGS_ERR, "invalid --buffer: 0 (1..60000 ms)", SEND_BASE, "--buffer", "0"),
  MSG(ARGS_ERR, "invalid --buffer: 60001 (1..60000 ms)", SEND_BASE, "--buffer", "60001"),
  MSG(ARGS_ERR, "invalid --al-fec: 41:1", SEND_BASE, "--al-fec", "41:1"),
  MSG(ARGS_ERR, "invalid --al-fec-port: 0", SEND_BASE, "--al-fec-port", "0"),
  MSG(ARGS_ERR, "invalid --color: rainbow", SEND_BASE, "--color", "rainbow"),
  MSG(ARGS_ERR, "invalid --metrics-interval: 0", SEND_BASE, "--metrics-id", "m", "--metrics-interval", "0"),
  MSG(ARGS_ERR, "invalid --metrics-inspect-ts: loud", SEND_BASE, "--metrics-id", "m", "--metrics-inspect-ts", "loud"),
  MSG(ARGS_ERR, "invalid -i: rist://1.2.3.4:6000", "-i", "rist://1.2.3.4:6000", "-o", "udp://239.1.2.3:5000"),
  MSG(ARGS_ERR, "invalid -o: rist://@1.2.3.4:6000", "-i", "udp://@239.1.2.3:5000", "-o", "rist://@1.2.3.4:6000"),
  MSG(ARGS_ERR, "invalid -o: http://example.org/x", "-i", "udp://@239.1.2.3:5000", "-o", "http://example.org/x"),
  MSG(ARGS_ERR, "invalid -o: out.ts", "-i", "udp://@239.1.2.3:5000", "-o", "rist://1.2.3.4:6000", "-o", "out.ts"),
  MSG(ARGS_ERR, "invalid -i: udp://@239.1.2.4:5000", "-i", "udp://@239.1.2.3:5000", "-i", "udp://@239.1.2.4:5000", "-o", "rist://1.2.3.4:6000"),
  MSG(ARGS_ERR, "invalid -o: rtp://bogus:5000", "-i", "udp://@239.1.2.3:5000", "-o", "rtp://bogus:5000"),
  MSG(ARGS_ERR, "missing -i input", "-o", "rist://1.2.3.4:6000"),
  MSG(ARGS_ERR, "missing -o output", "-i", "udp://@239.1.2.3:5000"),
  MSG(ARGS_ERR, "exactly one of -i/-o must be rist://", "-i", "udp://@239.1.2.3:5000", "-o", "out.ts"),
  MSG(ARGS_ERR, "--secret requires --profile main", SEND_BASE, "--secret", "s3cret"),
  MSG(ARGS_ERR, "--encryption-type requires --profile main", SEND_BASE, "--encryption-type", "128"),
  MSG(ARGS_ERR, "--metrics/--metrics-interval require --metrics-id", SEND_BASE, "--metrics", "/tmp/m.sock"),
  MSG(ARGS_ERR, "--metrics-inspect-ts requires --metrics-id", SEND_BASE, "--metrics-inspect-ts", "basic"),
  MSG(ARGS_ERR, "--al-fec requires --al-fec-port", SEND_BASE, "--al-fec", "10:5"),
  MSG(ARGS_ERR, "unexpected argument: stray", SEND_BASE, "stray"),
  MSG(ARGS_ERR, "", SEND_BASE, "--no-such-option"),
  MSG(ARGS_OK, "--al-fec-port has no effect without --al-fec", SEND_BASE, "--al-fec-port", "6004"),
  MSG(ARGS_OK, "--al-fec has no effect, non-rist:// side isn't rtp://", SEND_BASE, "--al-fec", "10:5", "--al-fec-port", "6004"),
  MSG(ARGS_OK, "--insecure needs -i https://", SEND_BASE, "-k"),
};

START_TEST(argument_messages_and_status) {
  const message_case_t *c = &message_cases[_i];
  char msg[MSG_MAX];
  config_t cfg = {0};
  args_status_t st;

  log_capture_begin();
  st = args_parse(c->argc, (char **)c->argv, &cfg);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(st, c->status);
  ck_assert_msg(strstr(msg, c->message) != NULL, "want '%s' in '%s'", c->message, msg);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(sender_with_rtp_in_gets_al_fec_settings) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.2.3:5000", "-o", "rist://1.2.3.4:6000", "--al-fec", "10:5", "--al-fec-port", "6004", NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.in.nonrist.al_fec_l, 10u);
  ck_assert_uint_eq(cfg.in.nonrist.al_fec_d, 5u);
  ck_assert_uint_eq(cfg.in.nonrist.al_fec_port, 6004u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

typedef struct {
  const char *uri;
  int is_out;
  int ret;
} endpoint_case_t;

static const endpoint_case_t endpoint_cases[] = {
  {"rist://@0.0.0.0:6000", 0, 0},
  {"rist://1.2.3.4:6000", 1, 0},
  {"rist://1.2.3.4:6000", 0, -1},
  {"rist://@0.0.0.0:6000", 1, -1},
  {"udp://@239.1.2.3:5000", 0, 0},
  {"rtp://239.1.2.3:5000", 1, 0},
  {"http://example.org/live.ts", 0, 0},
  {"http://example.org/live.ts", 1, -1},
  {"-", 0, 0},
  {"out.ts", 1, 0},
  {"rtp://not-an-address:5000", 0, -1},
};

START_TEST(single_endpoint_parsing) {
  const endpoint_case_t *c = &endpoint_cases[_i];
  config_t cfg = {0};

  ck_assert_int_eq(rist_cfg_add_endpoint(&cfg, c->is_out, c->uri), c->ret);
  ck_assert_int_eq(c->is_out ? cfg.n_out : cfg.n_in, c->ret ? 0 : 1);
}
END_TEST

START_TEST(endpoint_limits_and_mixing) {
  char uri[300];
  config_t cfg = {0};

  for (int i = 0; i < DIPIRIST_MAX_PEERS; i++) {
    snprintf(uri, sizeof uri, "rist://1.2.3.%d:6000", i + 1);
    ck_assert_int_eq(rist_cfg_add_endpoint(&cfg, 1, uri), 0);
  }
  ck_assert_int_eq(rist_cfg_add_endpoint(&cfg, 1, "rist://1.2.3.99:6000"), -1);
  ck_assert_int_eq(cfg.out.n_rist, DIPIRIST_MAX_PEERS);
  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(rist_cfg_add_endpoint(&cfg, 1, "rist://1.2.3.4:6000"), 0);
  ck_assert_int_eq(rist_cfg_add_endpoint(&cfg, 1, "udp://239.1.2.3:5000"), -1);
  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(rist_cfg_add_endpoint(&cfg, 1, "udp://239.1.2.3:5000"), 0);
  ck_assert_int_eq(rist_cfg_add_endpoint(&cfg, 1, "udp://239.1.2.4:5000"), -1);
  memset(&cfg, 0, sizeof cfg);
  memset(uri, 'a', sizeof uri - 1);
  memcpy(uri, "rist://1.2.3.4:", 15);
  uri[sizeof uri - 1] = '\0';
  ck_assert_int_eq(rist_cfg_add_endpoint(&cfg, 1, uri), -1);
}
END_TEST

typedef struct {
  const char *uris[3];
  int n;
  int is_out;
  const char *want;
} describe_case_t;

static const describe_case_t describe_cases[] = {
  {{"rist://1.2.3.4:6000"}, 1, 1, "rist://1.2.3.4:6000"},
  {{"rist://1.2.3.4:6000", "rist://1.2.3.5:6000"}, 2, 1, "rist://1.2.3.4:6000 +1 more"},
  {{"rist://1.2.3.4:6000", "rist://1.2.3.5:6000", "rist://1.2.3.6:6000"}, 3, 1, "rist://1.2.3.4:6000 +2 more"},
  {{"udp://239.1.2.3:5000"}, 1, 0, "udp://@239.1.2.3:5000"},
  {{"rtp://239.1.2.3:5000"}, 1, 0, "rtp://@239.1.2.3:5000"},
  {{"http://example.org:8080/live.ts"}, 1, 0, "http://example.org:8080/live.ts"},
  {{"capture.ts"}, 1, 0, "capture.ts"},
  {{"-"}, 1, 0, "- (stdin/stdout)"},
};

START_TEST(endpoint_describe_formats) {
  const describe_case_t *c = &describe_cases[_i];
  config_t cfg = {0};
  char buf[256];
  const endpoint_t *e = c->is_out ? &cfg.out : &cfg.in;

  for (int i = 0; i < c->n; i++) ck_assert_int_eq(rist_cfg_add_endpoint(&cfg, c->is_out, c->uris[i]), 0);
  endpoint_describe(e, buf, sizeof buf);
  ck_assert_str_eq(buf, c->want);
}
END_TEST

static Suite *args_suite(void) {
  Suite *s = suite_create("dipirist_args");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, sender_ok_with_nonrist_in_and_rist_out);
  tcase_add_test(tc, receiver_ok_with_rist_in_and_nonrist_out);
  tcase_add_test(tc, both_rist_is_rejected);
  tcase_add_test(tc, neither_rist_is_rejected);
  tcase_add_test(tc, sink_rist_with_at_is_rejected);
  tcase_add_test(tc, source_rist_without_at_is_rejected);
  tcase_add_test(tc, repeated_rist_out_bonds_peers);
  tcase_add_test(tc, repeated_nonrist_out_is_rejected);
  tcase_add_test(tc, mixed_rist_and_nonrist_on_same_flag_is_rejected);
  tcase_add_test(tc, missing_in_is_rejected);
  tcase_add_test(tc, missing_out_is_rejected);
  tcase_add_test(tc, http_source_ok);
  tcase_add_test(tc, http_source_accepts_arbitrary_path);
  tcase_add_test(tc, http_sink_is_rejected);
  tcase_add_test(tc, non_multicast_direct_address_is_rejected);
  tcase_add_test(tc, default_profile_is_simple);
  tcase_add_test(tc, profile_main_is_accepted);
  tcase_add_test(tc, unknown_profile_is_rejected);
  tcase_add_test(tc, secret_without_profile_main_is_rejected);
  tcase_add_test(tc, secret_with_profile_main_is_accepted);
  tcase_add_test(tc, encryption_type_with_profile_main_is_accepted);
  tcase_add_test(tc, encryption_type_without_profile_main_is_rejected);
  tcase_add_test(tc, encryption_type_invalid_is_rejected);
  tcase_add_test(tc, config_file_provides_encryption_type);
  tcase_add_test(tc, buffer_out_of_range_is_rejected);
  tcase_add_test(tc, buffer_value_is_applied);
  tcase_add_test(tc, metrics_options_require_metrics_id);
  tcase_add_test(tc, metrics_id_alone_is_accepted);
  tcase_add_test(tc, help_returns_help_status);
  tcase_add_test(tc, unexpected_positional_argument_is_rejected);
  tcase_add_test(tc, config_file_provides_settings);
  tcase_add_test(tc, cmdline_wins_over_config);
  tcase_add_test(tc, config_missing_file_is_error);
  tcase_add_test(tc, config_invalid_value_is_error);
  tcase_add_test(tc, configtest_reports_by_exit_status);
  tcase_add_test(tc, config_scalar_key_rejects_list);
  tcase_add_test(tc, inspect_ts_level_is_recorded);
  tcase_add_test(tc, inspect_ts_defaults_to_off);
  tcase_add_test(tc, inspect_ts_requires_metrics_id);
  tcase_add_test(tc, inspect_ts_rejects_unknown_level);
  tcase_add_test(tc, inspect_ts_from_yaml);
  tcase_add_loop_test(tc, options_set_their_fields, 0, (int)(sizeof option_cases / sizeof option_cases[0]));
  tcase_add_loop_test(tc, argument_messages_and_status, 0, (int)(sizeof message_cases / sizeof message_cases[0]));
  tcase_add_test(tc, sender_with_rtp_in_gets_al_fec_settings);
  tcase_add_loop_test(tc, single_endpoint_parsing, 0, (int)(sizeof endpoint_cases / sizeof endpoint_cases[0]));
  tcase_add_test(tc, endpoint_limits_and_mixing);
  tcase_add_loop_test(tc, endpoint_describe_formats, 0, (int)(sizeof describe_cases / sizeof describe_cases[0]));
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
