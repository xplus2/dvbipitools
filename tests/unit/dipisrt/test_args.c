/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../cfg_fixture.h"
#include "../log_capture.h"
#include "lib/config/yamlcfg.h"

#include "dipisrt/cli/args.h"
#include "dipisrt/cli/priv.h"
#include "lib/helper/log.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(sender_ok_with_nonsrt_in_and_srt_out) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(config_is_sender(&cfg), 1);
  ck_assert_int_eq(cfg.out.n_srt, 1);
  ck_assert_int_eq(cfg.out.listen, 0);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(receiver_ok_with_srt_in_and_nonsrt_out) {
  char *argv[] = {"dipisrt", "-i", "srt://@0.0.0.0:9000", "-o", "rtp://@239.1.1.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(config_is_sender(&cfg), 0);
  ck_assert_int_eq(cfg.in.n_srt, 1);
  ck_assert_int_eq(cfg.in.listen, 1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(both_srt_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "srt://@0.0.0.0:9000", "-o", "srt://1.2.3.4:9000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(neither_srt_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "udp://@239.1.1.2:5001", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(sink_srt_with_at_is_ok) {
  /* unlike RIST, caller/listener is independent of which side is -i/-o */
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://@1.2.3.4:9000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.out.listen, 1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(source_srt_without_at_is_ok) {
  char *argv[] = {"dipisrt", "-i", "srt://1.2.3.4:9000", "-o", "rtp://@239.1.1.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.in.listen, 0);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(repeated_srt_out_needs_group_mode) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "-o", "srt://5.6.7.8:9000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(repeated_srt_out_with_group_mode_bonds_peers) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "-o", "srt://5.6.7.8:9000", "--group-mode", "broadcast", NULL};
  config_t cfg = {0};
#ifdef DIPISRT_HAVE_BONDING
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.out.n_srt, 2);
  ck_assert_int_eq(cfg.group_mode, SRTGROUP_BROADCAST);
  ck_assert_str_eq(cfg.out.srt_host[0], "1.2.3.4");
  ck_assert_str_eq(cfg.out.srt_host[1], "5.6.7.8");
#else
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
#endif
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(group_mode_without_bonding_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "--group-mode", "backup", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(unknown_group_mode_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "-o", "srt://5.6.7.8:9000", "--group-mode", "loadbalance", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(mixed_listen_peers_on_same_flag_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "-o", "srt://@5.6.7.8:9000", "--group-mode", "broadcast", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(repeated_nonsrt_out_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "udp://@239.1.1.2:5001",
                  "-o", "udp://@239.1.1.3:5001", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(mixed_srt_and_nonsrt_on_same_flag_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "-o", "udp://@239.1.1.2:5001", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(missing_in_is_rejected) {
  char *argv[] = {"dipisrt", "-o", "srt://1.2.3.4:9000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(missing_out_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(http_source_ok) {
  char *argv[] = {"dipisrt", "-i", "http://10.0.0.1:4022/rtp/239.19.75.1:8700", "-o", "srt://1.2.3.4:9000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(http_sink_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "srt://@0.0.0.0:9000", "-o", "http://10.0.0.1:4022/rtp/239.19.75.1:8700", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(non_multicast_direct_address_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@10.0.0.1:5000", "-o", "srt://1.2.3.4:9000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(non_numeric_srt_host_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://example.com:9000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rendezvous_requires_local) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000", "--rendezvous", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rendezvous_with_local_is_accepted) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "--rendezvous", "--local", "0.0.0.0:9001", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rendezvous, 1);
  ck_assert_str_eq(cfg.local_host, "0.0.0.0");
  ck_assert_uint_eq(cfg.local_port, 9001u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rendezvous_with_listen_peer_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://@1.2.3.4:9000",
                  "--rendezvous", "--local", "0.0.0.0:9001", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rendezvous_with_group_mode_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "-o", "srt://5.6.7.8:9000", "--group-mode", "broadcast",
                  "--rendezvous", "--local", "0.0.0.0:9001", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(passphrase_too_short_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000", "--passphrase", "short", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(passphrase_valid_length_is_accepted) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "--passphrase", "correcthorsebattery", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.passphrase, "correcthorsebattery");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(pbkeylen_without_passphrase_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000", "--pbkeylen", "24", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(pbkeylen_invalid_value_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "--passphrase", "correcthorsebattery", "--pbkeylen", "20", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(pbkeylen_valid_value_is_accepted) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "--passphrase", "correcthorsebattery", "--pbkeylen", "32", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.pbkeylen, 32);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(streamid_and_packetfilter_and_latency_are_applied) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "--streamid", "chan1", "--packetfilter", "fec,cols:10,rows:5", "--latency", "250", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.streamid, "chan1");
  ck_assert_str_eq(cfg.packetfilter, "fec,cols:10,rows:5");
  ck_assert_uint_eq(cfg.latency_ms, 250u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(metrics_options_require_metrics_id) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "--metrics", "/tmp/x.sock", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(metrics_id_alone_is_accepted) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "--metrics-id", "inst1", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.metrics_id, "inst1");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(help_returns_help_status) {
  char *argv[] = {"dipisrt", "-h", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(unexpected_positional_argument_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000", "extra", NULL};
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

START_TEST(config_file_provides_settings) {
  char path[] = "/tmp/dipisrt_cfg_XXXXXX";
  char *argv[] = {"dipisrt", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "in:\n  - srt://@0.0.0.0:9000\nout: rtp://@239.1.1.1:5000\nlatency: 300\npassphrase: 0123456789ab\npbkeylen: 24\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.n_in, 1);
  ck_assert_int_eq(cfg.in.n_srt, 1);
  ck_assert_uint_eq(cfg.latency_ms, 300u);
  ck_assert_int_eq(cfg.pbkeylen, 24);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(cmdline_wins_over_config) {
  char path[] = "/tmp/dipisrt_cfg_XXXXXX";
  char *argv[] = {"dipisrt", "-c", path, "-i", "srt://@0.0.0.0:9100", NULL};
  config_t cfg = {0};
  write_cfg(path, "in: srt://@0.0.0.0:9000\nout: rtp://@239.1.1.1:5000\nlatency: 300\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_uint_eq(cfg.in.srt_port[0], 9100u);
  ck_assert_uint_eq(cfg.latency_ms, 300u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_missing_file_is_error) {
  char *argv[] = {"dipisrt", "-c", "/nonexistent/dipisrt.yaml", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_invalid_value_is_error) {
  char path[] = "/tmp/dipisrt_cfg_XXXXXX";
  char *argv[] = {"dipisrt", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "in: srt://@0.0.0.0:9000\nout: rtp://@239.1.1.1:5000\npbkeylen: 20\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(configtest_reports_by_exit_status) {
  char path[] = "/tmp/dipisrt_cfg_XXXXXX";
  char *argv[] = {"dipisrt", "--configtest", "-c", path, NULL};
  char *argv2[] = {"dipisrt", "--configtest", "-c", "/nonexistent/dipisrt.yaml", NULL};
  config_t cfg = {0};
  write_cfg(path, "bogus: 1\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
  ck_assert_int_eq(args_parse(ARGC(argv2), argv2, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_level_is_recorded) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000", "--metrics-id", "inst1", "--metrics-inspect-ts", "medium", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_MEDIUM);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_defaults_to_off) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000", "--metrics-id", "inst1", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_OFF);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_requires_metrics_id) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000", "--metrics-inspect-ts", "basic", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_rejects_unknown_level) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000", "--metrics-id", "inst1", "--metrics-inspect-ts", "bogus", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_from_yaml) {
  char path[] = "/tmp/dipisrt_inspect_XXXXXX";
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000", "-c", path, NULL};
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
#define A64 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define A128 A64 A64
#define A256 A128 A128

typedef struct {
  const char *argv[ARGV_MAX];
  int argc;
  cfg_kind_t kind;
  size_t off;
  double num;
  const char *str;
} option_case_t;

#define OPT(field, kind, num, str, ...) {{"dipisrt", __VA_ARGS__}, 1 + (int)(sizeof((const char *[]){__VA_ARGS__}) / sizeof(char *)), kind, offsetof(config_t, field), num, str}
#define SEND_BASE "-i", "udp://@239.1.2.3:5000", "-o", "srt://1.2.3.4:9000"
#define RECV_BASE "-i", "srt://@0.0.0.0:9000", "-o", "udp://239.1.2.3:5000"

static const option_case_t option_cases[] = {
  OPT(passphrase, CFG_CHARARR, 0, "0123456789ab", SEND_BASE, "--passphrase", "0123456789ab"),
  OPT(pbkeylen, CFG_INT, 24, NULL, SEND_BASE, "--passphrase", "0123456789ab", "--pbkeylen", "24"),
  OPT(pbkeylen, CFG_INT, 32, NULL, SEND_BASE, "--passphrase", "0123456789ab", "--pbkeylen", "32"),
  OPT(streamid, CFG_CHARARR, 0, "sid1", SEND_BASE, "--streamid", "sid1"),
  OPT(packetfilter, CFG_CHARARR, 0, "fec,cols:4", SEND_BASE, "--packetfilter", "fec,cols:4"),
  OPT(latency_ms, CFG_UINT, 123, NULL, SEND_BASE, "--latency", "123"),
  OPT(latency_ms, CFG_UINT, 60000, NULL, SEND_BASE, "--latency", "60000"),
  OPT(send_buffer_mult, CFG_UINT, 8, NULL, SEND_BASE, "--send-buffer-mult", "8"),
  OPT(rendezvous, CFG_INT, 1, NULL, SEND_BASE, "--rendezvous", "--local", "0.0.0.0:9100"),
  OPT(local_host, CFG_CHARARR, 0, "127.0.0.1", SEND_BASE, "--rendezvous", "--local", "127.0.0.1:9100"),
  OPT(local_port, CFG_UINT, 9100, NULL, SEND_BASE, "--rendezvous", "--local", "127.0.0.1:9100"),
  OPT(al_fec_l, CFG_UINT, 10, NULL, "-i", "rtp://@239.1.2.3:5000", "-o", "srt://1.2.3.4:9000", "--al-fec", "10:5", "--al-fec-port", "6004"),
  OPT(al_fec_d, CFG_UINT, 5, NULL, SEND_BASE, "--al-fec", "10:5", "--al-fec-port", "6004"),
  OPT(al_fec_port, CFG_UINT, 6004, NULL, SEND_BASE, "--al-fec", "10:5", "--al-fec-port", "6004"),
  OPT(iface, CFG_STRPTR, 0, "eth7", SEND_BASE, "-I", "eth7"),
  OPT(insecure_tls, CFG_INT, 1, NULL, "-i", "https://example.org/live.ts", "-o", "srt://1.2.3.4:9000", "-k"),
  OPT(color_mode, CFG_INT, LOG_COLOR_NEVER, NULL, SEND_BASE, "--color", "never"),
  OPT(metrics_sock, CFG_STRPTR, 0, "/tmp/m.sock", SEND_BASE, "--metrics", "/tmp/m.sock", "--metrics-id", "s1"),
  OPT(metrics_id, CFG_STRPTR, 0, "s1", SEND_BASE, "--metrics-id", "s1"),
  OPT(metrics_interval_s, CFG_UINT, 42, NULL, SEND_BASE, "--metrics-id", "s1", "--metrics-interval", "42"),
  OPT(metrics_inspect_ts, CFG_INT, METRICS_INSPECT_TS_FULL, NULL, SEND_BASE, "--metrics-id", "s1", "--metrics-inspect-ts", "full"),
  OPT(verbose, CFG_INT, 1, NULL, SEND_BASE, "-v"),
  OPT(daemonize, CFG_INT, 1, NULL, SEND_BASE, "-d"),
  OPT(out.n_srt, CFG_INT, 1, NULL, SEND_BASE),
  OPT(out.listen, CFG_INT, 0, NULL, SEND_BASE),
  OPT(in.listen, CFG_INT, 1, NULL, RECV_BASE),
  OPT(in.srt_port[0], CFG_UINT, 9000, NULL, RECV_BASE),
  OPT(in.nonsrt.kind, CFG_INT, PLAIN_EP_HTTP, NULL, "-i", "http://example.org/live.ts", "-o", "srt://1.2.3.4:9000"),
  OPT(in.nonsrt.kind, CFG_INT, PLAIN_EP_FILE, NULL, "-i", "-", "-o", "srt://1.2.3.4:9000"),
  OPT(out.nonsrt.kind, CFG_INT, PLAIN_EP_FILE, NULL, "-i", "srt://@0.0.0.0:9000", "-o", "out.ts"),
  OPT(out.nonsrt.kind, CFG_INT, PLAIN_EP_UDP, NULL, RECV_BASE),
  OPT(out.family[0], CFG_INT, AF_INET6, NULL, "-i", "udp://@239.1.2.3:5000", "-o", "srt://[2001:db8::1]:9000"),
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

#define MSG(status, message, ...) {{"dipisrt", __VA_ARGS__}, 1 + (int)(sizeof((const char *[]){__VA_ARGS__}) / sizeof(char *)), status, message}

static const message_case_t message_cases[] = {
  MSG(ARGS_ERR, "--passphrase too long", SEND_BASE, "--passphrase", A128),
  MSG(ARGS_ERR, "invalid --pbkeylen: 20 (16|24|32)", SEND_BASE, "--passphrase", "0123456789ab", "--pbkeylen", "20"),
  MSG(ARGS_ERR, "--streamid too long", SEND_BASE, "--streamid", A128),
  MSG(ARGS_ERR, "--packetfilter too long", SEND_BASE, "--packetfilter", A256),
  MSG(ARGS_ERR, "invalid --latency: 0 (1..60000 ms)", SEND_BASE, "--latency", "0"),
  MSG(ARGS_ERR, "invalid --send-buffer-mult: 33 (1..32)", SEND_BASE, "--send-buffer-mult", "33"),
  MSG(ARGS_ERR, "invalid --local: bogus", SEND_BASE, "--local", "bogus"),
  MSG(ARGS_ERR, "invalid --al-fec: 41:1", SEND_BASE, "--al-fec", "41:1"),
  MSG(ARGS_ERR, "invalid --al-fec-port: 0", SEND_BASE, "--al-fec-port", "0"),
  MSG(ARGS_ERR, "invalid --color: rainbow", SEND_BASE, "--color", "rainbow"),
  MSG(ARGS_ERR, "invalid --metrics-interval: 0", SEND_BASE, "--metrics-id", "m", "--metrics-interval", "0"),
  MSG(ARGS_ERR, "invalid --metrics-inspect-ts: loud", SEND_BASE, "--metrics-id", "m", "--metrics-inspect-ts", "loud"),
  MSG(ARGS_ERR, "invalid -i: srt://:9000", "-i", "srt://:9000", "-o", "udp://239.1.2.3:5000"),
  MSG(ARGS_ERR, "invalid -o: rtp://bogus:5000", "-i", "srt://@0.0.0.0:9000", "-o", "rtp://bogus:5000"),
  MSG(ARGS_ERR, "invalid -o: srt://5.6.7.8:9000", "-i", "udp://@239.1.2.3:5000", "-o", "out.ts", "-o", "srt://5.6.7.8:9000"),
  MSG(ARGS_ERR, "invalid -o: srt://@5.6.7.8:9000", "-i", "udp://@239.1.2.3:5000", "-o", "srt://1.2.3.4:9000", "-o", "srt://@5.6.7.8:9000"),
  MSG(ARGS_ERR, "invalid -o: srt://1.2.3.9:9000", "-i", "udp://@239.1.2.3:5000", "-o", "srt://1.2.3.1:9000", "-o", "srt://1.2.3.2:9000", "-o", "srt://1.2.3.3:9000", "-o", "srt://1.2.3.4:9000", "-o", "srt://1.2.3.5:9000", "-o", "srt://1.2.3.6:9000", "-o", "srt://1.2.3.7:9000", "-o", "srt://1.2.3.8:9000", "-o", "srt://1.2.3.9:9000"),
  MSG(ARGS_ERR, "invalid -o: http://example.org/x", "-i", "udp://@239.1.2.3:5000", "-o", "http://example.org/x"),
  MSG(ARGS_ERR, "missing -i input", "-o", "srt://1.2.3.4:9000"),
  MSG(ARGS_ERR, "missing -o output", "-i", "udp://@239.1.2.3:5000"),
  MSG(ARGS_ERR, "exactly one of -i/-o must be srt://", "-i", "udp://@239.1.2.3:5000", "-o", "out.ts"),
  MSG(ARGS_ERR, "--rendezvous is not combinable with srt://@ (listener)", RECV_BASE, "--rendezvous", "--local", "0.0.0.0:9100"),
  MSG(ARGS_ERR, "--rendezvous requires --local <host:port>", SEND_BASE, "--rendezvous"),
  MSG(ARGS_ERR, "--passphrase must be 10..79 characters", SEND_BASE, "--passphrase", "short"),
  MSG(ARGS_ERR, "--pbkeylen requires --passphrase", SEND_BASE, "--pbkeylen", "16"),
  MSG(ARGS_ERR, "--metrics/--metrics-interval require --metrics-id", SEND_BASE, "--metrics", "/tmp/m.sock"),
  MSG(ARGS_ERR, "--metrics-inspect-ts requires --metrics-id", SEND_BASE, "--metrics-inspect-ts", "basic"),
  MSG(ARGS_ERR, "--al-fec requires --al-fec-port", SEND_BASE, "--al-fec", "10:5"),
  MSG(ARGS_ERR, "unexpected argument: stray", SEND_BASE, "stray"),
  MSG(ARGS_ERR, "", SEND_BASE, "--no-such-option"),
  MSG(ARGS_OK, "--al-fec-port has no effect without --al-fec", SEND_BASE, "--al-fec-port", "6004"),
  MSG(ARGS_OK, "--al-fec has no effect, the non-srt:// side isn't rtp://", SEND_BASE, "--al-fec", "10:5", "--al-fec-port", "6004"),
  MSG(ARGS_OK, "--insecure needs -i https://", SEND_BASE, "-k"),
  MSG(ARGS_OK, "--send-buffer-mult needs -o srt://", RECV_BASE, "--send-buffer-mult", "4"),
#ifndef DIPISRT_HAVE_BONDING
  MSG(ARGS_ERR, "--group-mode needs a libsrt built with bonding support", SEND_BASE, "--group-mode", "backup"),
#else
  MSG(ARGS_ERR, "invalid --group-mode: sideways (broadcast|backup)", SEND_BASE, "-o", "srt://5.6.7.8:9000", "--group-mode", "sideways"),
  MSG(ARGS_ERR, "bonding several srt:// peers requires --group-mode", SEND_BASE, "-o", "srt://5.6.7.8:9000"),
  MSG(ARGS_ERR, "--group-mode has no effect with a single srt:// peer", SEND_BASE, "--group-mode", "backup"),
  MSG(ARGS_ERR, "--rendezvous is not combinable with --group-mode", SEND_BASE, "-o", "srt://5.6.7.8:9000", "--group-mode", "backup", "--rendezvous", "--local", "0.0.0.0:9100"),
#endif
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
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.2.3:5000", "-o", "srt://1.2.3.4:9000", "--al-fec", "10:5", "--al-fec-port", "6004", NULL};
  config_t cfg = {0};

  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.in.nonsrt.al_fec_l, 10u);
  ck_assert_uint_eq(cfg.in.nonsrt.al_fec_d, 5u);
  ck_assert_uint_eq(cfg.in.nonsrt.al_fec_port, 6004u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

typedef struct {
  const char *uri;
  int is_out;
  int ret;
} endpoint_case_t;

static const endpoint_case_t endpoint_cases[] = {
  {"srt://@0.0.0.0:9000", 0, 0},
  {"srt://1.2.3.4:9000", 1, 0},
  {"srt://[2001:db8::1]:9000", 1, 0},
  {"srt://1.2.3.4", 1, -1},
  {"srt://:9000", 1, -1},
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

  ck_assert_int_eq(srt_cfg_add_endpoint(&cfg, c->is_out, c->uri), c->ret);
  ck_assert_int_eq(c->is_out ? cfg.n_out : cfg.n_in, c->ret ? 0 : 1);
}
END_TEST

START_TEST(endpoint_limits_and_mixing) {
  char uri[64];
  config_t cfg = {0};

  for (int i = 0; i < SRTCOMMON_MAX_PEERS; i++) {
    snprintf(uri, sizeof uri, "srt://1.2.3.%d:9000", i + 1);
    ck_assert_int_eq(srt_cfg_add_endpoint(&cfg, 1, uri), 0);
  }
  ck_assert_int_eq(srt_cfg_add_endpoint(&cfg, 1, "srt://1.2.3.99:9000"), -1);
  ck_assert_int_eq(cfg.out.n_srt, SRTCOMMON_MAX_PEERS);
  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(srt_cfg_add_endpoint(&cfg, 1, "srt://1.2.3.4:9000"), 0);
  ck_assert_int_eq(srt_cfg_add_endpoint(&cfg, 1, "udp://239.1.2.3:5000"), -1);
  ck_assert_int_eq(srt_cfg_add_endpoint(&cfg, 1, "srt://@1.2.3.5:9000"), -1);
  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(srt_cfg_add_endpoint(&cfg, 1, "udp://239.1.2.3:5000"), 0);
  ck_assert_int_eq(srt_cfg_add_endpoint(&cfg, 1, "srt://1.2.3.4:9000"), -1);
}
END_TEST

typedef struct {
  const char *uris[3];
  int n;
  int is_out;
  const char *want;
} describe_case_t;

static const describe_case_t describe_cases[] = {
  {{"srt://1.2.3.4:9000"}, 1, 1, "srt://1.2.3.4:9000"},
  {{"srt://@0.0.0.0:9000"}, 1, 0, "srt://@0.0.0.0:9000"},
  {{"srt://1.2.3.4:9000", "srt://1.2.3.5:9000"}, 2, 1, "srt://1.2.3.4:9000 +1 more"},
  {{"srt://1.2.3.4:9000", "srt://1.2.3.5:9000", "srt://1.2.3.6:9000"}, 3, 1, "srt://1.2.3.4:9000 +2 more"},
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

  for (int i = 0; i < c->n; i++) ck_assert_int_eq(srt_cfg_add_endpoint(&cfg, c->is_out, c->uris[i]), 0);
  endpoint_describe(e, buf, sizeof buf);
  ck_assert_str_eq(buf, c->want);
}
END_TEST

static Suite *args_suite(void) {
  Suite *s = suite_create("dipisrt_args");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, sender_ok_with_nonsrt_in_and_srt_out);
  tcase_add_test(tc, receiver_ok_with_srt_in_and_nonsrt_out);
  tcase_add_test(tc, both_srt_is_rejected);
  tcase_add_test(tc, neither_srt_is_rejected);
  tcase_add_test(tc, sink_srt_with_at_is_ok);
  tcase_add_test(tc, source_srt_without_at_is_ok);
  tcase_add_test(tc, repeated_srt_out_needs_group_mode);
  tcase_add_test(tc, repeated_srt_out_with_group_mode_bonds_peers);
  tcase_add_test(tc, group_mode_without_bonding_is_rejected);
  tcase_add_test(tc, unknown_group_mode_is_rejected);
  tcase_add_test(tc, mixed_listen_peers_on_same_flag_is_rejected);
  tcase_add_test(tc, repeated_nonsrt_out_is_rejected);
  tcase_add_test(tc, mixed_srt_and_nonsrt_on_same_flag_is_rejected);
  tcase_add_test(tc, missing_in_is_rejected);
  tcase_add_test(tc, missing_out_is_rejected);
  tcase_add_test(tc, http_source_ok);
  tcase_add_test(tc, http_sink_is_rejected);
  tcase_add_test(tc, non_multicast_direct_address_is_rejected);
  tcase_add_test(tc, non_numeric_srt_host_is_rejected);
  tcase_add_test(tc, rendezvous_requires_local);
  tcase_add_test(tc, rendezvous_with_local_is_accepted);
  tcase_add_test(tc, rendezvous_with_listen_peer_is_rejected);
  tcase_add_test(tc, rendezvous_with_group_mode_is_rejected);
  tcase_add_test(tc, passphrase_too_short_is_rejected);
  tcase_add_test(tc, passphrase_valid_length_is_accepted);
  tcase_add_test(tc, pbkeylen_without_passphrase_is_rejected);
  tcase_add_test(tc, pbkeylen_invalid_value_is_rejected);
  tcase_add_test(tc, pbkeylen_valid_value_is_accepted);
  tcase_add_test(tc, streamid_and_packetfilter_and_latency_are_applied);
  tcase_add_test(tc, metrics_options_require_metrics_id);
  tcase_add_test(tc, metrics_id_alone_is_accepted);
  tcase_add_test(tc, help_returns_help_status);
  tcase_add_test(tc, unexpected_positional_argument_is_rejected);
  tcase_add_test(tc, config_file_provides_settings);
  tcase_add_test(tc, cmdline_wins_over_config);
  tcase_add_test(tc, config_missing_file_is_error);
  tcase_add_test(tc, config_invalid_value_is_error);
  tcase_add_test(tc, configtest_reports_by_exit_status);
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
