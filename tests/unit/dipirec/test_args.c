/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../cfg_fixture.h"
#include "../log_capture.h"
#include "lib/config/yamlcfg.h"

#include "dipirec/cli/args.h"
#include "dipirec/cli/priv.h"
#include "dipirec/filter/ts.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(rist_out_uri_is_parsed) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.out[0].kind, OUT_RIST);
  ck_assert_str_eq(cfg.out[0].rist_uri, "rist://1.2.3.4:6000");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_out_rejects_mkv_format) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "-f", "mkv", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_out_default_format_is_ts) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.format, FMT_TS);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(strip_lcevc_token_is_accepted) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "out.ts", "--strip", "LCEVC", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.strip_mask, STRIP_LCEVC);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(strip_lcevc_combines_with_other_tokens) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "out.ts", "--strip", "NUL,LCEVC", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.strip_mask, STRIP_NUL | STRIP_LCEVC);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(strip_unknown_token_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "out.ts", "--strip", "BOGUS", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(default_profile_is_simple) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rist_profile, RIST_PROF_SIMPLE);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(profile_main_is_accepted) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--rist-profile", "main", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rist_profile, RIST_PROF_MAIN);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(unknown_profile_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--rist-profile", "advanced", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(secret_without_profile_main_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--rist-secret", "hunter2", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(secret_with_profile_main_is_accepted) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--rist-profile", "main", "--rist-secret", "hunter2", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.rist_secret, "hunter2");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(encryption_type_with_profile_main_is_accepted) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--rist-profile", "main", "--rist-encryption-type", "256", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rist_key_size, 256);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(encryption_type_without_profile_main_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--rist-encryption-type", "128", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(encryption_type_invalid_is_rejected) {
  static char bad[][4] = {"0", "192", "abc", ""};
  for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
    char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--rist-profile", "main", "--rist-encryption-type", bad[i], NULL};
    config_t cfg = {0};
    ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
    yamlcfg_strpool_free(cfg.str_pool);
  }
}
END_TEST

START_TEST(encryption_type_in_with_profile_in_main_is_accepted) {
  char *argv[] = {"dipirec", "-i", "rist://@127.0.0.1:6000", "-o", "-", "--rist-profile-in", "main", "--rist-encryption-type-in", "256", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rist_key_size_in, 256);
  ck_assert_int_eq(cfg.rist_key_size, 0);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(encryption_type_in_without_profile_in_main_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rist://@127.0.0.1:6000", "-o", "-", "--rist-encryption-type-in", "128", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(encryption_type_in_invalid_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rist://@127.0.0.1:6000", "-o", "-", "--rist-profile-in", "main", "--rist-encryption-type-in", "512", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(encryption_type_in_without_rist_in_is_harmless) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "-", "--rist-encryption-type-in", "128", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(cname_and_buffer_are_applied) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000",
                  "--rist-cname", "encoder1", "--rist-buffer", "1000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.rist_cname, "encoder1");
  ck_assert_uint_eq(cfg.rist_buffer_ms, 1000u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(buffer_zero_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--rist-buffer", "0", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_options_without_rist_out_are_harmless) {
  /* --rist-profile/--rist-secret/--rist-cname/--rist-buffer with a non-rist -o: no-op, just a logged warning */
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "show.ts", "--rist-cname", "x", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.out[0].kind, OUT_FILE);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(out_iface_has_no_effect_on_rist_but_is_not_an_error) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "-O", "eth0", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(metrics_options_require_metrics_id) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "show.ts", "--metrics", "/tmp/x.sock", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(metrics_id_alone_is_accepted) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "show.ts",
                  "--metrics-id", "inst1", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.metrics_id, "inst1");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_level_is_recorded) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "show.ts", "--metrics-id", "inst1", "--metrics-inspect-ts", "medium", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_MEDIUM);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_defaults_to_off) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "show.ts", "--metrics-id", "inst1", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_OFF);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_requires_metrics_id) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "show.ts", "--metrics-inspect-ts", "basic", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_rejects_unknown_level) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "show.ts", "--metrics-id", "inst1", "--metrics-inspect-ts", "bogus", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_in_uri_with_at_is_accepted) {
  char *argv[] = {"dipirec", "-i", "rist://@127.0.0.1:6000", "-o", "-", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.source.kind, URI_RIST);
  ck_assert_str_eq(cfg.source.rist_uri, "rist://@127.0.0.1:6000");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_in_uri_without_at_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rist://127.0.0.1:6000", "-o", "-", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(profile_in_main_is_accepted) {
  char *argv[] = {"dipirec", "-i", "rist://@127.0.0.1:6000", "-o", "-", "--rist-profile-in", "main", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rist_profile_in, RIST_PROF_MAIN);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(unknown_profile_in_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rist://@127.0.0.1:6000", "-o", "-", "--rist-profile-in", "advanced", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(profile_in_without_rist_in_is_harmless) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "-", "--rist-profile-in", "main", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_input_and_rist_output_together_is_rejected) {
  /* librist isn't safe with more than one rist_ctx per process */
  char *argv[] = {"dipirec", "-i", "rist://@127.0.0.1:6000", "-o", "rist://1.2.3.4:6002", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(more_than_one_rist_output_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000",
                  "-o", "rist://1.2.3.4:6000", "-o", "rist://5.6.7.8:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(ret_is_rejected_with_rist_input) {
  /* --ret needs RTP sequence numbers; a RIST source has its own ARQ instead */
  char *argv[] = {"dipirec", "-i", "rist://@127.0.0.1:6000", "-o", "-", "--ret", "1.2.3.4:6001", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_input_listen_is_accepted) {
  char *argv[] = {"dipirec", "-i", "srt://@127.0.0.1:6000", "-o", "-", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.source.kind, URI_SRT);
  ck_assert_int_eq(cfg.source.srt_listen, 1);
  ck_assert_str_eq(cfg.source.srt_host, "127.0.0.1");
  ck_assert_uint_eq(cfg.source.srt_port, 6000);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_input_caller_is_accepted) {
  char *argv[] = {"dipirec", "-i", "srt://127.0.0.1:6000", "-o", "-", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.source.kind, URI_SRT);
  ck_assert_int_eq(cfg.source.srt_listen, 0);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_passphrase_in_length_is_validated) {
  char *argv[] = {"dipirec", "-i", "srt://@127.0.0.1:6000", "-o", "-", "--srt-passphrase-in", "short", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_pbkeylen_in_requires_passphrase_in) {
  char *argv[] = {"dipirec", "-i", "srt://@127.0.0.1:6000", "-o", "-", "--srt-pbkeylen-in", "16", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_output_caller_is_accepted) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:7000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.out[0].kind, OUT_SRT);
  ck_assert_str_eq(cfg.out[0].srt_host, "1.2.3.4");
  ck_assert_uint_eq(cfg.out[0].srt_port, 7000);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_output_listen_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://@1.2.3.4:7000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(more_than_one_srt_output_is_accepted) {
  /* unlike rist://, srtout has no per-process context limit: independent targets are fine */
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:7000", "-o", "srt://5.6.7.8:7000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.n_out, 2);
  ck_assert_int_eq(cfg.out[0].kind, OUT_SRT);
  ck_assert_int_eq(cfg.out[1].kind, OUT_SRT);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_input_and_srt_output_together_is_accepted) {
  /* unlike rist://, srt has no per-process context limit */
  char *argv[] = {"dipirec", "-i", "srt://@127.0.0.1:6000", "-o", "srt://1.2.3.4:7000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_passphrase_length_is_validated) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:7000", "--srt-passphrase", "short", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_pbkeylen_requires_passphrase) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:7000", "--srt-pbkeylen", "16", NULL};
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

START_TEST(inspect_ts_pids_parsed) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "show.ts", "--metrics-id", "inst1", "--metrics-inspect-ts", "full", "--metrics-inspect-ts-pids", "0x100,300", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.metrics_n_known_pids, 2u);
  ck_assert_uint_eq(cfg.metrics_known_pids[0], 256u);
  ck_assert_uint_eq(cfg.metrics_known_pids[1], 300u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_pids_require_full) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "show.ts", "--metrics-id", "inst1", "--metrics-inspect-ts", "basic", "--metrics-inspect-ts-pids", "256", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(encryption_types_from_yaml) {
  char path[] = "/tmp/dipirec_enc_XXXXXX";
  char *argv[] = {"dipirec", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "in: rist://@127.0.0.1:6000\nout: a.ts\nrist:\n  profile-in: main\n  encryption-type-in: 128\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.rist_key_size_in, 128);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_from_yaml) {
  char path[] = "/tmp/dipirec_inspect_XXXXXX";
  char *argv[] = {"dipirec", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "in: udp://@239.1.1.1:5000\nout: a.ts\nmetrics:\n  id: a\n  inspect-ts: full\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_FULL);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_file_provides_settings) {
  char path[] = "/tmp/dipirec_cfg_XXXXXX";
  char *argv[] = {"dipirec", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "in: udp://@239.1.1.1:5000\nout:\n  - a.ts\n  - rtp://@239.2.2.2:6000\nformat: ts\nsubtitles: strip\ntime: 5m\nret:\n  wait: 300\nsrt:\n  latency: 100\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.n_out, 2);
  ck_assert_int_eq(cfg.source.kind, URI_UDP);
  ck_assert_int_eq(cfg.format, FMT_TS);
  ck_assert_int_eq(cfg.subs, SUB_STRIP);
  ck_assert_int_eq(cfg.duration_s, 300);
  ck_assert_uint_eq(cfg.ret.wait_ms, 300u);
  ck_assert_uint_eq(cfg.srt_latency_ms, 100u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(cmdline_wins_over_config) {
  char path[] = "/tmp/dipirec_cfg_XXXXXX";
  char *argv[] = {"dipirec", "-c", path, "-o", "b.ts", "-f", "raw", NULL};
  config_t cfg = {0};
  write_cfg(path, "in: udp://@239.1.1.1:5000\nout:\n  - a.ts\n  - c.ts\nformat: mkv\nsubtitles: keep\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.n_out, 1);
  ck_assert_str_eq(cfg.out[0].file_path, "b.ts");
  ck_assert_int_eq(cfg.format, FMT_RAW);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_missing_file_is_error) {
  char *argv[] = {"dipirec", "-c", "/nonexistent/dipirec.yaml", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_invalid_value_is_error) {
  char path[] = "/tmp/dipirec_cfg_XXXXXX";
  char *argv[] = {"dipirec", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "in: udp://@239.1.1.1:5000\nout: a.ts\nttl: 300\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(configtest_reports_by_exit_status) {
  char path[] = "/tmp/dipirec_cfg_XXXXXX";
  char *argv[] = {"dipirec", "--configtest", "-c", path, NULL};
  char *argv2[] = {"dipirec", "--configtest", "-c", "/nonexistent/dipirec.yaml", NULL};
  config_t cfg = {0};
  write_cfg(path, "bogus: 1\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
  ck_assert_int_eq(args_parse(ARGC(argv2), argv2, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

#define ARGV_MAX 24
#define MSG_MAX 2048
#define A128 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define A256 A128 A128

typedef struct {
  const char *argv[ARGV_MAX];
  int argc;
  cfg_kind_t kind;
  size_t off;
  double num;
  const char *str;
} option_case_t;

#define OPT(field, kind, num, str, ...) {{"dipirec", __VA_ARGS__}, 1 + (int)(sizeof((const char *[]){__VA_ARGS__}) / sizeof(char *)), kind, offsetof(config_t, field), num, str}
#define RTP_IN "-i", "rtp://@239.1.1.1:5000"
#define FILE_OUT "-o", "out.ts"
#define SRT_IN "-i", "srt://@127.0.0.1:9000"
#define SRT_OUT "-o", "srt://127.0.0.1:9001"

static const option_case_t stream_option_cases[] = {
  OPT(ret.enabled, CFG_INT, 1, NULL, RTP_IN, FILE_OUT, "--ret", "10.0.0.1:6000"),
  OPT(ret.addr, CFG_CHARARR, 0, "10.0.0.1", RTP_IN, FILE_OUT, "--ret", "10.0.0.1:6000"),
  OPT(ret.port, CFG_UINT, 6000, NULL, RTP_IN, FILE_OUT, "--ret", "10.0.0.1:6000"),
  OPT(ret.mc_enabled, CFG_INT, 0, NULL, RTP_IN, FILE_OUT, "--ret", "10.0.0.1:6000", "--no-ret-mc"),
  OPT(ret.mc_port, CFG_UINT, 6002, NULL, RTP_IN, FILE_OUT, "--ret-mc-port", "6002"),
  OPT(ret.rtx_pt, CFG_UCHAR, 100, NULL, RTP_IN, FILE_OUT, "--ret-pt", "100"),
  OPT(ret.wait_ms, CFG_UINT, 350, NULL, RTP_IN, FILE_OUT, "--ret-wait", "350"),
  OPT(strip_mask, CFG_UINT, STRIP_NUL | STRIP_NIT, NULL, RTP_IN, FILE_OUT, "--strip", "NUL,NIT"),
  OPT(strip_mask, CFG_UINT, 0, NULL, RTP_IN, FILE_OUT, "--strip", "none"),
  OPT(pace, CFG_INT, 1, NULL, "-i", "in.ts", FILE_OUT, "--pace"),
  OPT(iface_out, CFG_STRPTR, 0, "eth8", RTP_IN, "-o", "rtp://239.1.1.2:5000", "-O", "eth8"),
  OPT(iface_out, CFG_STRPTR, 0, "eth9", RTP_IN, "-o", "udp://239.1.1.2:5000", "--out-iface", "eth9"),
  OPT(out_ttl, CFG_INT, 12, NULL, RTP_IN, "-o", "rtp://239.1.1.2:5000", "--ttl", "12"),
  OPT(out_ttl, CFG_INT, 255, NULL, RTP_IN, "-o", "rtp://239.1.1.2:5000", "--ttl", "255"),
  OPT(al_fec_l, CFG_UINT, 10, NULL, RTP_IN, FILE_OUT, "--al-fec", "10:5", "--al-fec-port", "6004"),
  OPT(al_fec_d, CFG_UINT, 5, NULL, RTP_IN, FILE_OUT, "--al-fec", "10:5", "--al-fec-port", "6004"),
  OPT(al_fec_port, CFG_UINT, 6004, NULL, RTP_IN, FILE_OUT, "--al-fec", "10:5", "--al-fec-port", "6004"),
};

static const option_case_t srt_option_cases[] = {
  OPT(srt_passphrase_in, CFG_CHARARR, 0, "0123456789ab", SRT_IN, FILE_OUT, "--srt-passphrase-in", "0123456789ab"),
  OPT(srt_pbkeylen_in, CFG_INT, 24, NULL, SRT_IN, FILE_OUT, "--srt-passphrase-in", "0123456789ab", "--srt-pbkeylen-in", "24"),
  OPT(srt_streamid_in, CFG_CHARARR, 0, "sid-in", SRT_IN, FILE_OUT, "--srt-streamid-in", "sid-in"),
  OPT(srt_packetfilter_in, CFG_CHARARR, 0, "fec,cols:4", SRT_IN, FILE_OUT, "--srt-packetfilter-in", "fec,cols:4"),
  OPT(srt_latency_in_ms, CFG_UINT, 123, NULL, SRT_IN, FILE_OUT, "--srt-latency-in", "123"),
  OPT(srt_latency_in_ms, CFG_UINT, 60000, NULL, SRT_IN, FILE_OUT, "--srt-latency-in", "60000"),
  OPT(srt_passphrase, CFG_CHARARR, 0, "0123456789cd", RTP_IN, SRT_OUT, "--srt-passphrase", "0123456789cd"),
  OPT(srt_pbkeylen, CFG_INT, 32, NULL, RTP_IN, SRT_OUT, "--srt-passphrase", "0123456789cd", "--srt-pbkeylen", "32"),
  OPT(srt_streamid, CFG_CHARARR, 0, "sid-out", RTP_IN, SRT_OUT, "--srt-streamid", "sid-out"),
  OPT(srt_packetfilter, CFG_CHARARR, 0, "fec,rows:3", RTP_IN, SRT_OUT, "--srt-packetfilter", "fec,rows:3"),
  OPT(srt_latency_ms, CFG_UINT, 321, NULL, RTP_IN, SRT_OUT, "--srt-latency", "321"),
  OPT(srt_latency_ms, CFG_UINT, 1, NULL, RTP_IN, SRT_OUT, "--srt-latency", "1"),
};

static void run_option_case(const option_case_t *c) {
  cfg_field_case_t fc = {NULL, c->kind, c->off, c->num, c->str};
  config_t cfg = {0};

  ck_assert_int_eq(args_parse(c->argc, (char **)c->argv, &cfg), ARGS_OK);
  cfg_field_check(&cfg, &fc);
  yamlcfg_strpool_free(cfg.str_pool);
}

START_TEST(stream_options_set_their_fields) {
  run_option_case(&stream_option_cases[_i]);
}
END_TEST

START_TEST(srt_options_set_their_fields) {
  run_option_case(&srt_option_cases[_i]);
}
END_TEST

typedef struct {
  const char *argv[ARGV_MAX];
  int argc;
  args_status_t status;
  const char *message;
} message_case_t;

#define MSG(status, message, ...) {{"dipirec", __VA_ARGS__}, 1 + (int)(sizeof((const char *[]){__VA_ARGS__}) / sizeof(char *)), status, message}
#define BASE RTP_IN, FILE_OUT

static const message_case_t error_cases[] = {
  MSG(ARGS_ERR, "invalid --ret addr:port: bogus", BASE, "--ret", "bogus"),
  MSG(ARGS_ERR, "invalid --ret-mc-port: 0", BASE, "--ret-mc-port", "0"),
  MSG(ARGS_ERR, "invalid --ret-pt: 128 (0..127)", BASE, "--ret-pt", "128"),
  MSG(ARGS_ERR, "invalid --ret-wait: 0 (ms)", BASE, "--ret-wait", "0"),
  MSG(ARGS_ERR, "invalid --strip: BOGUS", BASE, "--strip", "BOGUS"),
  MSG(ARGS_ERR, "invalid --ttl: 256 (0..255)", BASE, "--ttl", "256"),
  MSG(ARGS_ERR, "invalid --al-fec: 41:1", BASE, "--al-fec", "41:1"),
  MSG(ARGS_ERR, "invalid --al-fec-port: 0", BASE, "--al-fec-port", "0"),
  MSG(ARGS_ERR, "invalid --srt-pbkeylen-in: 20 (16|24|32)", SRT_IN, FILE_OUT, "--srt-pbkeylen-in", "20"),
  MSG(ARGS_ERR, "invalid --srt-pbkeylen: 20 (16|24|32)", BASE, "--srt-pbkeylen", "20"),
  MSG(ARGS_ERR, "invalid --srt-latency-in: 0 (1..60000 ms)", SRT_IN, FILE_OUT, "--srt-latency-in", "0"),
  MSG(ARGS_ERR, "invalid --srt-latency: 60001 (1..60000 ms)", BASE, "--srt-latency", "60001"),
  MSG(ARGS_ERR, "--srt-passphrase-in too long", SRT_IN, FILE_OUT, "--srt-passphrase-in", A128),
  MSG(ARGS_ERR, "--srt-streamid-in too long", SRT_IN, FILE_OUT, "--srt-streamid-in", A128),
  MSG(ARGS_ERR, "--srt-packetfilter-in too long", SRT_IN, FILE_OUT, "--srt-packetfilter-in", A256),
  MSG(ARGS_ERR, "--srt-passphrase too long", BASE, "--srt-passphrase", A128),
  MSG(ARGS_ERR, "--srt-streamid too long", BASE, "--srt-streamid", A128),
  MSG(ARGS_ERR, "--srt-packetfilter too long", BASE, "--srt-packetfilter", A256),
  MSG(ARGS_ERR, "invalid --rist-profile: ultra", BASE, "--rist-profile", "ultra"),
  MSG(ARGS_ERR, "invalid --rist-encryption-type: 192", BASE, "--rist-encryption-type", "192"),
  MSG(ARGS_ERR, "invalid --rist-encryption-type-in: 100", BASE, "--rist-encryption-type-in", "100"),
  MSG(ARGS_ERR, "invalid --rist-buffer: 0", BASE, "--rist-buffer", "0"),
  MSG(ARGS_ERR, "invalid --rist-profile-in: ultra", BASE, "--rist-profile-in", "ultra"),
  MSG(ARGS_ERR, "--rist-secret too long", BASE, "--rist-secret", A128),
  MSG(ARGS_ERR, "--rist-cname too long", BASE, "--rist-cname", A128),
  MSG(ARGS_ERR, "too many -o targets (max 8)", RTP_IN, "-o", "1.ts", "-o", "2.ts", "-o", "3.ts", "-o", "4.ts", "-o", "5.ts", "-o", "6.ts", "-o", "7.ts", "-o", "8.ts", "-o", "9.ts"),
  MSG(ARGS_ERR, "invalid -o target: rtp://", RTP_IN, "-o", "rtp://"),
  MSG(ARGS_ERR, "invalid -i uri: rtp://bogus:5000", "-i", "rtp://bogus:5000", FILE_OUT),
  MSG(ARGS_ERR, "invalid -a track: 0", BASE, "-a", "0"),
  MSG(ARGS_ERR, "invalid -f format: avi", BASE, "-f", "avi"),
  MSG(ARGS_ERR, "invalid -p pmt-pid: 5", BASE, "-p", "5"),
  MSG(ARGS_ERR, "invalid -s: burn", BASE, "-s", "burn"),
  MSG(ARGS_ERR, "invalid -t duration: 5x", BASE, "-t", "5x"),
  MSG(ARGS_ERR, "invalid --color: rainbow", BASE, "--color", "rainbow"),
  MSG(ARGS_ERR, "invalid --sub-lead: 10001", BASE, "--sub-lead", "10001"),
  MSG(ARGS_ERR, "invalid --metrics-interval: 0", BASE, "--metrics-id", "m1", "--metrics-interval", "0"),
  MSG(ARGS_ERR, "invalid --metrics-inspect-ts: loud", BASE, "--metrics-id", "m1", "--metrics-inspect-ts", "loud"),
  MSG(ARGS_ERR, "invalid --metrics-inspect-ts-pids: 9000", BASE, "--metrics-id", "m1", "--metrics-inspect-ts", "full", "--metrics-inspect-ts-pids", "9000"),
  MSG(ARGS_ERR, "unexpected argument: stray", BASE, "stray"),
  MSG(ARGS_ERR, "", "-i", "rtp://@239.1.1.1:5000", "--no-such-option"),
};

static const message_case_t check_cases[] = {
  MSG(ARGS_ERR, "missing -o output", RTP_IN),
  MSG(ARGS_ERR, "missing -i input", FILE_OUT),
  MSG(ARGS_ERR, "at most one -o rist:// target", RTP_IN, "-o", "rist://1.2.3.4:6000", "-o", "rist://1.2.3.5:6000"),
  MSG(ARGS_ERR, "-i rist:// and -o rist:// cannot combine", "-i", "rist://@0.0.0.0:6000", "-o", "rist://1.2.3.4:6000"),
  MSG(ARGS_ERR, "--ret requires -i rtp://", "-i", "udp://@239.1.1.1:5000", FILE_OUT, "--ret", "10.0.0.1:6000"),
  MSG(ARGS_ERR, "--ret family must match", RTP_IN, FILE_OUT, "--ret", "[2001:db8::1]:6000"),
  MSG(ARGS_ERR, "--pace requires -i - or -i <path>", BASE, "--pace"),
  MSG(ARGS_ERR, "-f mkv/mka/mp4/m4a requires exactly one -o file target", RTP_IN, "-o", "a.mkv", "-o", "b.mkv", "-f", "mkv"),
  MSG(ARGS_ERR, "-f mkv/mka/mp4/m4a requires exactly one -o file target", RTP_IN, "-o", "rtp://239.1.1.2:5000", "-f", "mp4"),
  MSG(ARGS_ERR, "-f raw is incompatible with an -o rtmp://rtmps:// target", RTP_IN, "-o", "rtmp://127.0.0.1/live/key", "-f", "raw"),
  MSG(ARGS_ERR, "--al-fec requires --al-fec-port", BASE, "--al-fec", "10:5"),
  MSG(ARGS_ERR, "-s srt requires -f mkv, mka, mp4 or m4a", BASE, "-f", "ts", "-s", "srt"),
  MSG(ARGS_ERR, "--rist-secret requires --rist-profile main", RTP_IN, "-o", "rist://1.2.3.4:6000", "--rist-secret", "s3cret"),
  MSG(ARGS_ERR, "--rist-encryption-type requires --rist-profile main", RTP_IN, "-o", "rist://1.2.3.4:6000", "--rist-encryption-type", "128"),
  MSG(ARGS_ERR, "--rist-encryption-type-in requires --rist-profile-in main", "-i", "rist://@0.0.0.0:6000", FILE_OUT, "--rist-encryption-type-in", "128"),
  MSG(ARGS_ERR, "--metrics/--metrics-interval require --metrics-id", BASE, "--metrics", "/tmp/m.sock"),
  MSG(ARGS_ERR, "--metrics-inspect-ts requires --metrics-id", BASE, "--metrics-inspect-ts", "basic"),
  MSG(ARGS_ERR, "--metrics-inspect-ts-pids requires --metrics-inspect-ts full", BASE, "--metrics-id", "m1", "--metrics-inspect-ts", "basic", "--metrics-inspect-ts-pids", "256"),
  MSG(ARGS_ERR, "--srt-passphrase-in must be 10..79 characters", SRT_IN, FILE_OUT, "--srt-passphrase-in", "short"),
  MSG(ARGS_ERR, "--srt-pbkeylen-in requires --srt-passphrase-in", SRT_IN, FILE_OUT, "--srt-pbkeylen-in", "16"),
  MSG(ARGS_ERR, "--srt-passphrase must be 10..79 characters", RTP_IN, SRT_OUT, "--srt-passphrase", "short"),
  MSG(ARGS_ERR, "--srt-pbkeylen requires --srt-passphrase", RTP_IN, SRT_OUT, "--srt-pbkeylen", "16"),
  MSG(ARGS_OK, "--out-iface needs -o rtp:// or udp:// target", BASE, "--out-iface", "eth8"),
  MSG(ARGS_OK, "--ttl needs -o rtp:// or udp:// target", BASE, "--ttl", "5"),
  MSG(ARGS_OK, "--al-fec-port needs --al-fec", BASE, "--al-fec-port", "6004"),
  MSG(ARGS_OK, "--al-fec needs -i rtp:// or -o rtp:// target", "-i", "udp://@239.1.1.1:5000", "-o", "udp://239.1.1.2:5000", "--al-fec", "10:5", "--al-fec-port", "6004"),
  MSG(ARGS_OK, "--insecure needs -o rtmps:// target or -i https:// source", BASE, "--insecure"),
  MSG(ARGS_OK, "--strip has no effect outside -f ts", RTP_IN, "-o", "out.mkv", "--strip", "NUL"),
  MSG(ARGS_OK, "need -o rist:// target", BASE, "--rist-buffer", "500"),
  MSG(ARGS_OK, "need -i rist:// source", BASE, "--rist-profile-in", "main"),
  MSG(ARGS_OK, "--srt-*-in needs -i srt:// source", BASE, "--srt-latency-in", "100"),
  MSG(ARGS_OK, "--srt-* needs -o srt:// target", BASE, "--srt-latency", "100"),
};

static void run_message_case(const message_case_t *c) {
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

START_TEST(argument_errors_report_their_message) {
  run_message_case(&error_cases[_i]);
}
END_TEST

START_TEST(consistency_checks_report_their_message) {
  run_message_case(&check_cases[_i]);
}
END_TEST

typedef struct {
  const char *s;
  long want;
} duration_case_t;

static const duration_case_t duration_cases[] = {
  {"90", 90},
  {"1", 1},
  {"5m30s", 330},
  {"2h", 7200},
  {"1h2m3s", 3723},
  {"01:20:03", 4803},
  {"20:03", 1203},
  {"0:30", 30},
  {"", -1},
  {"0", -1},
  {"-5", -1},
  {"5x", -1},
  {"1m1h", -1},
  {"1m1m", -1},
  {"0s", -1},
  {"h", -1},
  {"1:60", -1},
  {"1:60:00", -1},
  {"1:2:3:4", -1},
  {"1::2", -1},
  {"1:a", -1},
  {"00:00", -1},
};

START_TEST(duration_parse_cases) {
  ck_assert_int_eq((int)duration_parse(duration_cases[_i].s), (int)duration_cases[_i].want);
}
END_TEST

typedef struct {
  const char *s;
  int ret;
  pmt_sel_t sel;
  unsigned pid;
} pmt_case_t;

static const pmt_case_t pmt_cases[] = {
  {"all", 0, PMT_SEL_ALL, 0},
  {"256", 0, PMT_SEL_PID, 256},
  {"0x100", 0, PMT_SEL_PID, 256},
  {"0x10", 0, PMT_SEL_PID, 16},
  {"0x1FFE", 0, PMT_SEL_PID, 0x1FFE},
  {"0xF", -1, PMT_SEL_AUTO, 0},
  {"0x1FFF", -1, PMT_SEL_AUTO, 0},
  {"12abc", -1, PMT_SEL_AUTO, 0},
  {"", -1, PMT_SEL_AUTO, 0},
  {"ALL", -1, PMT_SEL_AUTO, 0},
};

START_TEST(pmt_selection_cases) {
  const pmt_case_t *c = &pmt_cases[_i];
  config_t cfg = {0};

  ck_assert_int_eq(rec_cfg_pmt(&cfg, c->s), c->ret);
  ck_assert_int_eq(cfg.pmt_sel, c->sel);
  ck_assert_uint_eq(cfg.pmt_pid, c->pid);
}
END_TEST

typedef struct {
  const char *s;
  int ret;
  int all;
  unsigned track;
} audio_case_t;

static const audio_case_t audio_cases[] = {
  {"all", 0, 1, 0},
  {"1", 0, 0, 1},
  {"65535", 0, 0, 65535},
  {"0", -1, 1, 0},
  {"65536", -1, 1, 0},
  {"two", -1, 1, 0},
  {"", -1, 1, 0},
};

START_TEST(audio_selection_cases) {
  const audio_case_t *c = &audio_cases[_i];
  config_t cfg = {0};

  cfg.audio_all = 1;
  ck_assert_int_eq(rec_cfg_audio(&cfg, c->s), c->ret);
  ck_assert_int_eq(cfg.audio_all, c->all);
  ck_assert_uint_eq(cfg.audio_track, c->track);
}
END_TEST

typedef struct {
  const char *path;
  int ret;
  out_fmt_t fmt;
} suffix_case_t;

static const suffix_case_t suffix_cases[] = {
  {"a.ts", 1, FMT_TS},
  {"a.MKV", 1, FMT_MKV},
  {"dir.d/a.mka", 1, FMT_MKA},
  {"a.mp4", 1, FMT_MP4},
  {"a.M4A", 1, FMT_M4A},
  {"a.raw", 0, FMT_RAW},
  {"a.mkvx", 0, FMT_RAW},
  {"noext", 0, FMT_RAW},
  {"a.", 0, FMT_RAW},
};

START_TEST(format_from_suffix_cases) {
  const suffix_case_t *c = &suffix_cases[_i];
  out_fmt_t f = FMT_RAW;

  ck_assert_int_eq(rec_fmt_from_suffix(c->path, &f), c->ret);
  ck_assert_int_eq(f, c->fmt);
}
END_TEST

START_TEST(subtitle_and_profile_values) {
  config_t cfg = {0};

  ck_assert_int_eq(rec_cfg_subs(&cfg, "keep"), 0);
  ck_assert_int_eq(cfg.subs, SUB_KEEP);
  ck_assert_int_eq(rec_cfg_subs(&cfg, "strip"), 0);
  ck_assert_int_eq(cfg.subs, SUB_STRIP);
  ck_assert_int_eq(rec_cfg_subs(&cfg, "SRT"), -1);
  ck_assert_int_eq(rec_cfg_profile(&cfg, "main", 0), 0);
  ck_assert_int_eq(cfg.rist_profile, RIST_PROF_MAIN);
  ck_assert_int_eq(cfg.fl.have_profile, 1);
  ck_assert_int_eq(rec_cfg_profile(&cfg, "simple", 1), 0);
  ck_assert_int_eq(cfg.rist_profile_in, RIST_PROF_SIMPLE);
  ck_assert_int_eq(cfg.fl.have_profile_in, 1);
  ck_assert_int_eq(rec_cfg_profile(&cfg, "bogus", 1), -1);
}
END_TEST

START_TEST(strip_token_parsing) {
  config_t cfg = {0};

  ck_assert_int_eq(rec_cfg_strip(&cfg, "NUL,NIT,AIT,EIT,CAT,ECM,EMM,RST,TDT,TOT,INT,LCEVC"), 0);
  ck_assert_uint_eq(cfg.strip_mask, 0xFFFu);
  ck_assert_int_eq(rec_cfg_strip(&cfg, "NUL,"), 0);
  ck_assert_uint_eq(cfg.strip_mask, STRIP_NUL);
  ck_assert_int_eq(rec_cfg_strip(&cfg, ",NUL"), -1);
  ck_assert_int_eq(rec_cfg_strip(&cfg, "NUL,,NIT"), -1);
  ck_assert_int_eq(rec_cfg_strip(&cfg, "TOOLONGTOKEN"), -1);
  ck_assert_int_eq(rec_cfg_strip(&cfg, "nul"), -1);
}
END_TEST

typedef struct {
  const char *uri;
  int ret;
  uri_kind_t kind;
  const char *described;
} in_uri_case_t;

static const in_uri_case_t in_uri_cases[] = {
  {"-", 0, URI_FILE, "- (stdin)"},
  {"capture.ts", 0, URI_FILE, "capture.ts"},
  {"rtp://@239.1.1.1:5000", 0, URI_RTP, "rtp://@239.1.1.1:5000"},
  {"udp://239.1.1.1:5000", 0, URI_UDP, "udp://@239.1.1.1:5000"},
  {"rtp://@[ff15::1]:5000", 0, URI_RTP, "rtp://@[ff15::1]:5000"},
  {"http://example.org:8080/live.ts", 0, URI_HTTP, "http://example.org:8080/live.ts"},
  {"https://example.org/live.ts", 0, URI_HTTP, "https://example.org:443/live.ts"},
  {"rist://@0.0.0.0:6000", 0, URI_RIST, "rist://@0.0.0.0:6000"},
  {"srt://@127.0.0.1:9000", 0, URI_SRT, "srt://@127.0.0.1:9000"},
  {"srt://127.0.0.1:9000", 0, URI_SRT, "srt://127.0.0.1:9000"},
  {"rtp://bogus:5000", -1, URI_FILE, NULL},
  {"rtp://@127.0.0.1:5000", -1, URI_FILE, NULL},
  {"rist://1.2.3.4:6000", -1, URI_FILE, NULL},
  {"srt://@:9000", -1, URI_FILE, NULL},
  {"srt://@127.0.0.1", -1, URI_FILE, NULL},
};

START_TEST(input_uri_cases) {
  const in_uri_case_t *c = &in_uri_cases[_i];
  config_t cfg = {0};
  char buf[256];

  ck_assert_int_eq(rec_cfg_set_in(&cfg, c->uri), c->ret);
  if (c->ret) return;
  ck_assert_int_eq(cfg.source.kind, c->kind);
  source_describe(&cfg.source, buf, sizeof buf);
  ck_assert_str_eq(buf, c->described);
}
END_TEST

typedef struct {
  const char *uri;
  int ret;
  out_kind_t kind;
  const char *described;
} out_uri_case_t;

static const out_uri_case_t out_uri_cases[] = {
  {"out.ts", 0, OUT_FILE, "out.ts"},
  {"-", 0, OUT_FILE, "- (stdout)"},
  {"rtp://239.1.1.2:5000", 0, OUT_RTP, "rtp://@239.1.1.2:5000"},
  {"udp://239.1.1.2:5000", 0, OUT_UDP, "udp://@239.1.1.2:5000"},
  {"rist://1.2.3.4:6000", 0, OUT_RIST, "rist://1.2.3.4:6000"},
  {"srt://127.0.0.1:9001", 0, OUT_SRT, "srt://127.0.0.1:9001"},
  {"rtmp://127.0.0.1/live/key", 0, OUT_RTMP, "rtmp://127.0.0.1/live/key"},
  {"rtmps://127.0.0.1/live/key", 0, OUT_RTMPS, "rtmps://127.0.0.1/live/key"},
  {"srt://@127.0.0.1:9001", -1, OUT_FILE, NULL},
  {"rtp://", -1, OUT_FILE, NULL},
  {"rtp://127.0.0.1:5000", -1, OUT_FILE, NULL},
  {"srt://127.0.0.1", -1, OUT_FILE, NULL},
};

START_TEST(output_uri_cases) {
  const out_uri_case_t *c = &out_uri_cases[_i];
  config_t cfg = {0};
  char buf[256];

  ck_assert_int_eq(rec_cfg_add_out(&cfg, c->uri), c->ret);
  if (c->ret) {
    ck_assert_int_eq(cfg.n_out, 0);
    return;
  }
  ck_assert_int_eq(cfg.n_out, 1);
  ck_assert_int_eq(cfg.out[0].kind, c->kind);
  out_describe(&cfg.out[0], buf, sizeof buf);
  ck_assert_str_eq(buf, c->described);
}
END_TEST

START_TEST(overlong_uris_are_rejected) {
  char long_uri[1100];
  config_t cfg = {0};

  memset(long_uri, 'a', sizeof long_uri - 1);
  long_uri[sizeof long_uri - 1] = '\0';
  ck_assert_int_eq(rec_cfg_set_in(&cfg, long_uri), -1);
  ck_assert_int_eq(rec_cfg_add_out(&cfg, long_uri), -1);
  memcpy(long_uri, "rist://@", 8);
  ck_assert_int_eq(rec_cfg_set_in(&cfg, long_uri), -1);
  memcpy(long_uri, "rist://", 7);
  ck_assert_int_eq(rec_cfg_add_out(&cfg, long_uri), -1);
}
END_TEST

static Suite *args_suite(void) {
  Suite *s = suite_create("dipirec_args");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, rist_out_uri_is_parsed);
  tcase_add_test(tc, rist_out_rejects_mkv_format);
  tcase_add_test(tc, rist_out_default_format_is_ts);
  tcase_add_test(tc, strip_lcevc_token_is_accepted);
  tcase_add_test(tc, strip_lcevc_combines_with_other_tokens);
  tcase_add_test(tc, strip_unknown_token_is_rejected);
  tcase_add_test(tc, default_profile_is_simple);
  tcase_add_test(tc, profile_main_is_accepted);
  tcase_add_test(tc, unknown_profile_is_rejected);
  tcase_add_test(tc, secret_without_profile_main_is_rejected);
  tcase_add_test(tc, secret_with_profile_main_is_accepted);
  tcase_add_test(tc, encryption_type_with_profile_main_is_accepted);
  tcase_add_test(tc, encryption_type_without_profile_main_is_rejected);
  tcase_add_test(tc, encryption_type_invalid_is_rejected);
  tcase_add_test(tc, encryption_type_in_with_profile_in_main_is_accepted);
  tcase_add_test(tc, encryption_type_in_without_profile_in_main_is_rejected);
  tcase_add_test(tc, encryption_type_in_invalid_is_rejected);
  tcase_add_test(tc, encryption_type_in_without_rist_in_is_harmless);
  tcase_add_test(tc, encryption_types_from_yaml);
  tcase_add_test(tc, cname_and_buffer_are_applied);
  tcase_add_test(tc, buffer_zero_is_rejected);
  tcase_add_test(tc, rist_options_without_rist_out_are_harmless);
  tcase_add_test(tc, out_iface_has_no_effect_on_rist_but_is_not_an_error);
  tcase_add_test(tc, metrics_options_require_metrics_id);
  tcase_add_test(tc, metrics_id_alone_is_accepted);
  tcase_add_test(tc, inspect_ts_level_is_recorded);
  tcase_add_test(tc, inspect_ts_defaults_to_off);
  tcase_add_test(tc, inspect_ts_requires_metrics_id);
  tcase_add_test(tc, inspect_ts_rejects_unknown_level);
  tcase_add_test(tc, rist_in_uri_with_at_is_accepted);
  tcase_add_test(tc, rist_in_uri_without_at_is_rejected);
  tcase_add_test(tc, profile_in_main_is_accepted);
  tcase_add_test(tc, unknown_profile_in_is_rejected);
  tcase_add_test(tc, profile_in_without_rist_in_is_harmless);
  tcase_add_test(tc, ret_is_rejected_with_rist_input);
  tcase_add_test(tc, rist_input_and_rist_output_together_is_rejected);
  tcase_add_test(tc, more_than_one_rist_output_is_rejected);
  tcase_add_test(tc, srt_input_listen_is_accepted);
  tcase_add_test(tc, srt_input_caller_is_accepted);
  tcase_add_test(tc, srt_passphrase_in_length_is_validated);
  tcase_add_test(tc, srt_pbkeylen_in_requires_passphrase_in);
  tcase_add_test(tc, srt_output_caller_is_accepted);
  tcase_add_test(tc, srt_output_listen_is_rejected);
  tcase_add_test(tc, more_than_one_srt_output_is_accepted);
  tcase_add_test(tc, srt_input_and_srt_output_together_is_accepted);
  tcase_add_test(tc, srt_passphrase_length_is_validated);
  tcase_add_test(tc, srt_pbkeylen_requires_passphrase);
  tcase_add_test(tc, inspect_ts_pids_parsed);
  tcase_add_test(tc, inspect_ts_pids_require_full);
  tcase_add_test(tc, inspect_ts_from_yaml);
  tcase_add_test(tc, config_file_provides_settings);
  tcase_add_test(tc, cmdline_wins_over_config);
  tcase_add_test(tc, config_missing_file_is_error);
  tcase_add_test(tc, config_invalid_value_is_error);
  tcase_add_test(tc, configtest_reports_by_exit_status);
  tcase_add_loop_test(tc, stream_options_set_their_fields, 0, (int)(sizeof stream_option_cases / sizeof stream_option_cases[0]));
  tcase_add_loop_test(tc, srt_options_set_their_fields, 0, (int)(sizeof srt_option_cases / sizeof srt_option_cases[0]));
  tcase_add_loop_test(tc, argument_errors_report_their_message, 0, (int)(sizeof error_cases / sizeof error_cases[0]));
  tcase_add_loop_test(tc, consistency_checks_report_their_message, 0, (int)(sizeof check_cases / sizeof check_cases[0]));
  tcase_add_loop_test(tc, duration_parse_cases, 0, (int)(sizeof duration_cases / sizeof duration_cases[0]));
  tcase_add_loop_test(tc, pmt_selection_cases, 0, (int)(sizeof pmt_cases / sizeof pmt_cases[0]));
  tcase_add_loop_test(tc, audio_selection_cases, 0, (int)(sizeof audio_cases / sizeof audio_cases[0]));
  tcase_add_loop_test(tc, format_from_suffix_cases, 0, (int)(sizeof suffix_cases / sizeof suffix_cases[0]));
  tcase_add_test(tc, subtitle_and_profile_values);
  tcase_add_test(tc, strip_token_parsing);
  tcase_add_loop_test(tc, input_uri_cases, 0, (int)(sizeof in_uri_cases / sizeof in_uri_cases[0]));
  tcase_add_loop_test(tc, output_uri_cases, 0, (int)(sizeof out_uri_cases / sizeof out_uri_cases[0]));
  tcase_add_test(tc, overlong_uris_are_rejected);
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
