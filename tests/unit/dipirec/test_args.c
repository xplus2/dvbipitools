/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dipirec/args.h"
#include "dipirec/filter/ts.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(rist_out_uri_is_parsed) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.out[0].kind, OUT_RIST);
  ck_assert_str_eq(cfg.out[0].rist_uri, "rist://1.2.3.4:6000");
}
END_TEST

START_TEST(rist_out_rejects_mkv_format) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "-f", "mkv", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rist_out_default_format_is_ts) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.format, FMT_TS);
}
END_TEST

START_TEST(strip_lcevc_token_is_accepted) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "out.ts", "--strip", "LCEVC", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.strip_mask, STRIP_LCEVC);
}
END_TEST

START_TEST(strip_lcevc_combines_with_other_tokens) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "out.ts", "--strip", "NUL,LCEVC", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.strip_mask, STRIP_NUL | STRIP_LCEVC);
}
END_TEST

START_TEST(strip_unknown_token_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "out.ts", "--strip", "BOGUS", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(default_profile_is_simple) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rist_profile, RIST_PROF_SIMPLE);
}
END_TEST

START_TEST(profile_main_is_accepted) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--profile", "main", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rist_profile, RIST_PROF_MAIN);
}
END_TEST

START_TEST(unknown_profile_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--profile", "advanced", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(secret_without_profile_main_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--secret", "hunter2", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(secret_with_profile_main_is_accepted) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000",
                  "--profile", "main", "--secret", "hunter2", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.rist_secret, "hunter2");
}
END_TEST

START_TEST(cname_and_buffer_are_applied) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000",
                  "--cname", "encoder1", "--buffer", "1000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.rist_cname, "encoder1");
  ck_assert_uint_eq(cfg.rist_buffer_ms, 1000u);
}
END_TEST

START_TEST(buffer_zero_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--buffer", "0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rist_options_without_rist_out_are_harmless) {
  /* --profile/--secret/--cname/--buffer with a non-rist -o: no-op, just a logged warning */
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "show.ts", "--cname", "x", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.out[0].kind, OUT_FILE);
}
END_TEST

START_TEST(out_iface_has_no_effect_on_rist_but_is_not_an_error) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "-O", "eth0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
}
END_TEST

START_TEST(metrics_options_require_metrics_id) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "show.ts", "--metrics", "/tmp/x.sock", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(metrics_id_alone_is_accepted) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "show.ts",
                  "--metrics-id", "inst1", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.metrics_id, "inst1");
}
END_TEST

START_TEST(inspect_ts_level_is_recorded) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "show.ts", "--metrics-id", "inst1", "--metrics-inspect-ts", "medium", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_MEDIUM);
}
END_TEST

START_TEST(inspect_ts_defaults_to_off) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "show.ts", "--metrics-id", "inst1", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_OFF);
}
END_TEST

START_TEST(inspect_ts_requires_metrics_id) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "show.ts", "--metrics-inspect-ts", "basic", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(inspect_ts_rejects_unknown_level) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "show.ts", "--metrics-id", "inst1", "--metrics-inspect-ts", "bogus", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rist_in_uri_with_at_is_accepted) {
  char *argv[] = {"dipirec", "-i", "rist://@127.0.0.1:6000", "-o", "-", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.source.kind, URI_RIST);
  ck_assert_str_eq(cfg.source.rist_uri, "rist://@127.0.0.1:6000");
}
END_TEST

START_TEST(rist_in_uri_without_at_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rist://127.0.0.1:6000", "-o", "-", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(profile_in_main_is_accepted) {
  char *argv[] = {"dipirec", "-i", "rist://@127.0.0.1:6000", "-o", "-", "--profile-in", "main", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rist_profile_in, RIST_PROF_MAIN);
}
END_TEST

START_TEST(unknown_profile_in_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rist://@127.0.0.1:6000", "-o", "-", "--profile-in", "advanced", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(profile_in_without_rist_in_is_harmless) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "-", "--profile-in", "main", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
}
END_TEST

START_TEST(rist_input_and_rist_output_together_is_rejected) {
  /* librist isn't safe with more than one rist_ctx per process */
  char *argv[] = {"dipirec", "-i", "rist://@127.0.0.1:6000", "-o", "rist://1.2.3.4:6002", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(more_than_one_rist_output_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000",
                  "-o", "rist://1.2.3.4:6000", "-o", "rist://5.6.7.8:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(ret_is_rejected_with_rist_input) {
  /* --ret needs RTP sequence numbers; a RIST source has its own ARQ instead */
  char *argv[] = {"dipirec", "-i", "rist://@127.0.0.1:6000", "-o", "-", "--ret", "1.2.3.4:6001", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(srt_input_listen_is_accepted) {
  char *argv[] = {"dipirec", "-i", "srt://@127.0.0.1:6000", "-o", "-", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.source.kind, URI_SRT);
  ck_assert_int_eq(cfg.source.srt_listen, 1);
  ck_assert_str_eq(cfg.source.srt_host, "127.0.0.1");
  ck_assert_uint_eq(cfg.source.srt_port, 6000);
}
END_TEST

START_TEST(srt_input_caller_is_accepted) {
  char *argv[] = {"dipirec", "-i", "srt://127.0.0.1:6000", "-o", "-", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.source.kind, URI_SRT);
  ck_assert_int_eq(cfg.source.srt_listen, 0);
}
END_TEST

START_TEST(srt_passphrase_in_length_is_validated) {
  char *argv[] = {"dipirec", "-i", "srt://@127.0.0.1:6000", "-o", "-", "--srt-passphrase-in", "short", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(srt_pbkeylen_in_requires_passphrase_in) {
  char *argv[] = {"dipirec", "-i", "srt://@127.0.0.1:6000", "-o", "-", "--srt-pbkeylen-in", "16", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(srt_output_caller_is_accepted) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:7000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.out[0].kind, OUT_SRT);
  ck_assert_str_eq(cfg.out[0].srt_host, "1.2.3.4");
  ck_assert_uint_eq(cfg.out[0].srt_port, 7000);
}
END_TEST

START_TEST(srt_output_listen_is_rejected) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://@1.2.3.4:7000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(more_than_one_srt_output_is_accepted) {
  /* unlike rist://, srtout has no per-process context limit: independent targets are fine */
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:7000", "-o", "srt://5.6.7.8:7000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.n_out, 2);
  ck_assert_int_eq(cfg.out[0].kind, OUT_SRT);
  ck_assert_int_eq(cfg.out[1].kind, OUT_SRT);
}
END_TEST

START_TEST(srt_input_and_srt_output_together_is_accepted) {
  /* unlike rist://, srt has no per-process context limit */
  char *argv[] = {"dipirec", "-i", "srt://@127.0.0.1:6000", "-o", "srt://1.2.3.4:7000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
}
END_TEST

START_TEST(srt_passphrase_length_is_validated) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:7000", "--srt-passphrase", "short", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(srt_pbkeylen_requires_passphrase) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:7000", "--srt-pbkeylen", "16", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
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
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.metrics_n_known_pids, 2u);
  ck_assert_uint_eq(cfg.metrics_known_pids[0], 256u);
  ck_assert_uint_eq(cfg.metrics_known_pids[1], 300u);
}
END_TEST

START_TEST(inspect_ts_pids_require_full) {
  char *argv[] = {"dipirec", "-i", "rtp://@239.1.1.1:5000", "-o", "show.ts", "--metrics-id", "inst1", "--metrics-inspect-ts", "basic", "--metrics-inspect-ts-pids", "256", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(inspect_ts_from_yaml) {
  char path[] = "/tmp/dipirec_inspect_XXXXXX";
  char *argv[] = {"dipirec", "-c", path, NULL};
  config_t cfg;
  write_cfg(path, "in: udp://@239.1.1.1:5000\nout: a.ts\nmetrics:\n  id: a\n  inspect-ts: full\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_FULL);
}
END_TEST

START_TEST(config_file_provides_settings) {
  char path[] = "/tmp/dipirec_cfg_XXXXXX";
  char *argv[] = {"dipirec", "-c", path, NULL};
  config_t cfg;
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
}
END_TEST

START_TEST(cmdline_wins_over_config) {
  char path[] = "/tmp/dipirec_cfg_XXXXXX";
  char *argv[] = {"dipirec", "-c", path, "-o", "b.ts", "-f", "raw", NULL};
  config_t cfg;
  write_cfg(path, "in: udp://@239.1.1.1:5000\nout:\n  - a.ts\n  - c.ts\nformat: mkv\nsubtitles: keep\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.n_out, 1);
  ck_assert_str_eq(cfg.out[0].file_path, "b.ts");
  ck_assert_int_eq(cfg.format, FMT_RAW);
}
END_TEST

START_TEST(config_missing_file_is_error) {
  char *argv[] = {"dipirec", "-c", "/nonexistent/dipirec.yaml", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(config_invalid_value_is_error) {
  char path[] = "/tmp/dipirec_cfg_XXXXXX";
  char *argv[] = {"dipirec", "-c", path, NULL};
  config_t cfg;
  write_cfg(path, "in: udp://@239.1.1.1:5000\nout: a.ts\nttl: 300\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
}
END_TEST

START_TEST(configtest_reports_by_exit_status) {
  char path[] = "/tmp/dipirec_cfg_XXXXXX";
  char *argv[] = {"dipirec", "--configtest", "-c", path, NULL};
  char *argv2[] = {"dipirec", "--configtest", "-c", "/nonexistent/dipirec.yaml", NULL};
  config_t cfg;
  write_cfg(path, "bogus: 1\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
  ck_assert_int_eq(args_parse(ARGC(argv2), argv2, &cfg), ARGS_ERR);
  unlink(path);
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
