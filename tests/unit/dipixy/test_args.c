/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipixy/args.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(no_args_applies_all_defaults) {
  char *argv[] = {"dipixy", "-I", "eth0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.listen.scope, LISTEN_ANY);
  ck_assert_uint_eq(cfg.listen.port, 9080u);
  ck_assert_int_eq(cfg.listen_tls.scope, LISTEN_ANY);
  ck_assert_uint_eq(cfg.listen_tls.port, 9443u);
  ck_assert_int_eq(cfg.workers_spec, -1);
  ck_assert_int_eq(cfg.max_clients, 256);
  ck_assert_int_eq(cfg.n_sources, 0);
  ck_assert_str_eq(cfg.iface, "eth0");
  ck_assert_double_eq_tol(cfg.segment_size, 3.0, 1e-9);
  ck_assert_int_eq(cfg.segment_count, 4);
  ck_assert_double_eq_tol(cfg.hls_part_size, 0.35, 1e-9);
  args_free(&cfg);
}
END_TEST

START_TEST(segment_size_is_overridable) {
  char *argv[] = {"dipixy", "--segment-size", "6", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_double_eq_tol(cfg.segment_size, 6.0, 1e-9);
  args_free(&cfg);
}
END_TEST

START_TEST(segment_size_rejects_below_minimum) {
  char *argv[] = {"dipixy", "--segment-size", "1", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(segment_count_is_overridable) {
  char *argv[] = {"dipixy", "--segment-count", "6", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.segment_count, 6);
  args_free(&cfg);
}
END_TEST

START_TEST(segment_count_rejects_below_minimum) {
  char *argv[] = {"dipixy", "--segment-count", "2", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(hls_part_size_is_overridable) {
  char *argv[] = {"dipixy", "--hls-part-size", "0.5", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_double_eq_tol(cfg.hls_part_size, 0.5, 1e-9);
  args_free(&cfg);
}
END_TEST

START_TEST(hls_part_size_rejects_out_of_range) {
  char *argv[] = {"dipixy", "--hls-part-size", "0.01", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(hls_part_size_rejects_not_smaller_than_segment_size) {
  char *argv[] = {"dipixy", "--segment-size", "2", "--hls-part-size", "2", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(dash_part_size_defaults_to_333ms) {
  char *argv[] = {"dipixy", "-I", "eth0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_double_eq_tol(cfg.dash_part_size, 0.333, 1e-9);
  args_free(&cfg);
}
END_TEST

START_TEST(dash_part_size_is_overridable) {
  char *argv[] = {"dipixy", "--dash-part-size", "0.5", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_double_eq_tol(cfg.dash_part_size, 0.5, 1e-9);
  args_free(&cfg);
}
END_TEST

START_TEST(dash_part_size_rejects_out_of_range) {
  char *argv[] = {"dipixy", "--dash-part-size", "0.01", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(dash_part_size_rejects_zero) {
  char *argv[] = {"dipixy", "--dash-part-size", "0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(dash_part_size_rejects_not_smaller_than_segment_size) {
  char *argv[] = {"dipixy", "--segment-size", "2", "--dash-part-size", "2", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(listen_v4_is_overridable) {
  char *argv[] = {"dipixy", "-l", "0.0.0.0:8080", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.listen.scope, LISTEN_V4);
  ck_assert_str_eq(cfg.listen.addr, "0.0.0.0");
  ck_assert_uint_eq(cfg.listen.port, 8080u);
  args_free(&cfg);
}
END_TEST

START_TEST(listen_v6_bracket_form_parses) {
  char *argv[] = {"dipixy", "--listen", "[::1]:8080", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.listen.scope, LISTEN_V6);
  ck_assert_str_eq(cfg.listen.addr, "::1");
  ck_assert_uint_eq(cfg.listen.port, 8080u);
  args_free(&cfg);
}
END_TEST

START_TEST(listen_tls_is_overridable) {
  char *argv[] = {"dipixy", "-L", "0.0.0.0:9444", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.listen_tls.scope, LISTEN_V4);
  ck_assert_uint_eq(cfg.listen_tls.port, 9444u);
  args_free(&cfg);
}
END_TEST

START_TEST(listen_rejects_malformed_addr) {
  char *argv[] = {"dipixy", "-l", "not-an-addr", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(tls_cert_without_key_rejected) {
  char *argv[] = {"dipixy", "--tls-cert", "server.crt", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(tls_key_without_cert_rejected) {
  char *argv[] = {"dipixy", "--tls-key", "server.key", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(tls_cert_and_key_together_accepted) {
  char *argv[] = {"dipixy", "--tls-cert", "server.crt", "--tls-key", "server.key", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.tls_cert, "server.crt");
  ck_assert_str_eq(cfg.tls_key, "server.key");
  args_free(&cfg);
}
END_TEST

START_TEST(workers_relative_values_accepted) {
  char *argv[] = {"dipixy", "-j", "-2", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.workers_spec, -2);
  args_free(&cfg);
}
END_TEST

START_TEST(workers_absolute_value_accepted) {
  char *argv[] = {"dipixy", "--workers", "8", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.workers_spec, 8);
  args_free(&cfg);
}
END_TEST

START_TEST(workers_rejects_zero_and_out_of_range) {
  char *argv1[] = {"dipixy", "-j", "0", NULL};
  char *argv2[] = {"dipixy", "-j", "-4", NULL};
  char *argv3[] = {"dipixy", "-j", "abc", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv1), argv1, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(argv2), argv2, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(argv3), argv3, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(max_clients_is_overridable) {
  char *argv[] = {"dipixy", "--max-clients", "64", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.max_clients, 64);
  args_free(&cfg);
}
END_TEST

START_TEST(max_clients_short_flag_accepted) {
  char *argv[] = {"dipixy", "-c", "10", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.max_clients, 10);
  args_free(&cfg);
}
END_TEST

START_TEST(max_clients_rejects_zero_and_out_of_range) {
  char *argv1[] = {"dipixy", "-c", "0", NULL};
  char *argv2[] = {"dipixy", "-c", "65537", NULL};
  char *argv3[] = {"dipixy", "-c", "abc", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv1), argv1, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(argv2), argv2, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(argv3), argv3, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(sources_recorded_in_definition_order) {
  char *argv[] = {"dipixy", "-i", "a.m3u", "-i", "sds://239.1.1.1:3937", "-i", "b.csv", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.n_sources, 3);
  ck_assert_int_eq(cfg.sources[0].kind, SRC_M3U);
  ck_assert_str_eq(cfg.sources[0].value, "a.m3u");
  ck_assert_int_eq(cfg.sources[1].kind, SRC_SDS);
  ck_assert_str_eq(cfg.sources[1].value, "239.1.1.1:3937");
  ck_assert_int_eq(cfg.sources[2].kind, SRC_CSV);
  ck_assert_str_eq(cfg.sources[2].value, "b.csv");
  args_free(&cfg);
}
END_TEST

START_TEST(sds_rejects_malformed_addr) {
  char *argv[] = {"dipixy", "-i", "sds://not-an-addr", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(xspf_source_recorded) {
  char *argv[] = {"dipixy", "-i", "ch.xspf", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.n_sources, 1);
  ck_assert_int_eq(cfg.sources[0].kind, SRC_XSPF);
  args_free(&cfg);
}
END_TEST

START_TEST(xml_source_recorded) {
  char *argv[] = {"dipixy", "-i", "scan.xml", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.n_sources, 1);
  ck_assert_int_eq(cfg.sources[0].kind, SRC_XML);
  ck_assert_str_eq(cfg.sources[0].value, "scan.xml");
  args_free(&cfg);
}
END_TEST

START_TEST(m3u8_extension_recorded_as_m3u) {
  char *argv[] = {"dipixy", "-i", "chan.m3u8", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.n_sources, 1);
  ck_assert_int_eq(cfg.sources[0].kind, SRC_M3U);
  args_free(&cfg);
}
END_TEST

START_TEST(extension_match_is_case_insensitive) {
  char *argv[] = {"dipixy", "-i", "chan.XSPF", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.n_sources, 1);
  ck_assert_int_eq(cfg.sources[0].kind, SRC_XSPF);
  args_free(&cfg);
}
END_TEST

START_TEST(https_source_recorded_as_http) {
  char *argv[] = {"dipixy", "-i", "https://example.invalid/stream.ts", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.n_sources, 1);
  ck_assert_int_eq(cfg.sources[0].kind, SRC_HTTP);
  args_free(&cfg);
}
END_TEST

START_TEST(unrecognized_source_form_is_rejected) {
  char *argv[] = {"dipixy", "-i", "not-a-recognizable-source", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(metrics_disabled_by_default) {
  char *argv[] = {"dipixy", "-I", "eth0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_ptr_eq(cfg.metrics_id, NULL);
  ck_assert_int_eq(cfg.metrics_http, 0);
  args_free(&cfg);
}
END_TEST

START_TEST(metrics_id_enables_metrics) {
  char *argv[] = {"dipixy", "--metrics-id", "xy1", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.metrics_id, "xy1");
  args_free(&cfg);
}
END_TEST

START_TEST(metrics_sock_without_id_rejected) {
  char *argv[] = {"dipixy", "--metrics", "/tmp/x.sock", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(metrics_interval_rejects_out_of_range) {
  char *argv[] = {"dipixy", "--metrics-id", "xy1", "--metrics-interval", "0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(metrics_http_flag_recorded) {
  char *argv[] = {"dipixy", "--metrics-http", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.metrics_http, 1);
  args_free(&cfg);
}
END_TEST

START_TEST(daemonize_flag_recorded) {
  char *argv[] = {"dipixy", "-d", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.daemonize, 1);
  args_free(&cfg);
}
END_TEST

START_TEST(feature_toggle_flags_default_off) {
  char *argv[] = {"dipixy", "-I", "eth0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.no_hls, 0);
  ck_assert_int_eq(cfg.no_llhls, 0);
  ck_assert_int_eq(cfg.no_dash, 0);
  ck_assert_int_eq(cfg.no_lldash, 0);
  ck_assert_int_eq(cfg.no_ts, 0);
  ck_assert_int_eq(cfg.no_spts, 0);
  ck_assert_int_eq(cfg.no_rawaudio, 0);
  ck_assert_int_eq(cfg.no_url_rtp, 0);
  ck_assert_int_eq(cfg.no_url_udp, 0);
  ck_assert_int_eq(cfg.no_url_srt, 0);
  ck_assert_int_eq(cfg.no_pid_filters, 0);
  ck_assert_int_eq(cfg.no_status, 0);
  args_free(&cfg);
}
END_TEST

START_TEST(feature_toggle_flags_recorded) {
  char *argv[] = {"dipixy", "--no-url-rtp", "--no-url-udp",
                   "--no-url-srt", "--no-pid-filters", "--no-status", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.no_url_rtp, 1);
  ck_assert_int_eq(cfg.no_url_udp, 1);
  ck_assert_int_eq(cfg.no_url_srt, 1);
  ck_assert_int_eq(cfg.no_pid_filters, 1);
  ck_assert_int_eq(cfg.no_status, 1);
  args_free(&cfg);
}
END_TEST

START_TEST(format_whitelist_enables_only_listed) {
  char *argv[] = {"dipixy", "-f", "spts,dash", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.no_ts, 1);
  ck_assert_int_eq(cfg.no_spts, 0);
  ck_assert_int_eq(cfg.no_rawaudio, 1);
  ck_assert_int_eq(cfg.no_hls, 1);
  ck_assert_int_eq(cfg.no_llhls, 1);
  ck_assert_int_eq(cfg.no_dash, 0);
  ck_assert_int_eq(cfg.no_lldash, 1);
  args_free(&cfg);
}
END_TEST

START_TEST(format_whitelist_lldash_enables_it) {
  char *argv[] = {"dipixy", "-f", "lldash", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.no_lldash, 0);
  ck_assert_int_eq(cfg.no_dash, 1);
  args_free(&cfg);
}
END_TEST

START_TEST(format_whitelist_unknown_token_rejected) {
  char *argv[] = {"dipixy", "-f", "bogus", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(format_whitelist_empty_rejected) {
  char *argv[] = {"dipixy", "-f", "", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(stdin_flag_accepts_dash) {
  char *argv[] = {"dipixy", "-i", "-", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.stdin_path, "-");
  args_free(&cfg);
}
END_TEST

START_TEST(stdin_flag_rejects_non_dash) {
  char *argv[] = {"dipixy", "-i", "somefile", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rist_flag_records_uri) {
  char *argv[] = {"dipixy", "-i", "rist://@239.0.0.1:9000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.rist_uri, "rist://@239.0.0.1:9000");
  args_free(&cfg);
}
END_TEST

START_TEST(rist_flag_given_twice_rejected) {
  char *argv[] = {"dipixy", "-i", "rist://@a:1", "-i", "rist://@b:2", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rist_flag_without_listen_marker_rejected) {
  char *argv[] = {"dipixy", "-i", "rist://239.0.0.1:9000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(http_in_flag_appends_source) {
  char *argv[] = {"dipixy", "-i", "http://example.invalid/stream.ts", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.n_sources, 1);
  ck_assert_int_eq(cfg.sources[0].kind, SRC_HTTP);
  ck_assert_str_eq(cfg.sources[0].value, "http://example.invalid/stream.ts");
  args_free(&cfg);
}
END_TEST

START_TEST(name_attaches_to_preceding_source) {
  char *argv[] = {"dipixy", "-i", "a.m3u", "-n", "mylist", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.n_sources, 1);
  ck_assert_str_eq(cfg.sources[0].name, "mylist");
  args_free(&cfg);
}
END_TEST

START_TEST(name_attaches_to_preceding_stdin) {
  char *argv[] = {"dipixy", "-i", "-", "-n", "mystdin", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.stdin_name, "mystdin");
  args_free(&cfg);
}
END_TEST

START_TEST(name_attaches_to_preceding_rist) {
  char *argv[] = {"dipixy", "-i", "rist://@239.0.0.1:9000", "-n", "myrist", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.rist_name, "myrist");
  args_free(&cfg);
}
END_TEST

START_TEST(name_without_preceding_input_rejected) {
  char *argv[] = {"dipixy", "-n", "orphan", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(name_given_twice_for_same_input_rejected) {
  char *argv[] = {"dipixy", "-i", "a.m3u", "-n", "one", "-n", "two", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(duplicate_name_across_inputs_rejected) {
  char *argv[] = {"dipixy", "-i", "a.m3u", "-n", "same", "-i", "b.csv", "-n", "same", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(name_containing_slash_rejected) {
  char *argv[] = {"dipixy", "-i", "a.m3u", "-n", "foo/bar", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(name_starting_with_dot_rejected) {
  char *argv[] = {"dipixy", "-i", "a.m3u", "-n", ".hidden", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(name_matching_reserved_word_rejected) {
  const char *reserved[] = {"udp", "rtp", "srt", "rist", "stdin", "list", "metrics", "ui", "api"};
  size_t i;
  for (i = 0; i < sizeof reserved / sizeof reserved[0]; i++) {
    char *argv[] = {"dipixy", "-i", "a.m3u", "-n", (char *)reserved[i], NULL};
    config_t cfg;
    ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  }
}
END_TEST

START_TEST(name_empty_rejected) {
  char *argv[] = {"dipixy", "-i", "a.m3u", "-n", "", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(media_type_defaults_to_tv) {
  char *argv[] = {"dipixy", "-i", "a.m3u", "-i", "-", "-i", "rist://@239.0.0.1:9000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.sources[0].media_type, MEDIA_TV);
  ck_assert_int_eq(cfg.stdin_media_type, MEDIA_TV);
  ck_assert_int_eq(cfg.rist_media_type, MEDIA_TV);
  args_free(&cfg);
}
END_TEST

START_TEST(media_type_attaches_to_preceding_source) {
  char *argv[] = {"dipixy", "-i", "a.m3u", "--media-type", "radio", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.sources[0].media_type, MEDIA_RADIO);
  args_free(&cfg);
}
END_TEST

START_TEST(media_type_attaches_to_preceding_stdin) {
  char *argv[] = {"dipixy", "-i", "-", "--media-type", "radio", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.stdin_media_type, MEDIA_RADIO);
  args_free(&cfg);
}
END_TEST

START_TEST(media_type_attaches_to_preceding_rist) {
  char *argv[] = {"dipixy", "-i", "rist://@239.0.0.1:9000", "--media-type", "radio", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rist_media_type, MEDIA_RADIO);
  args_free(&cfg);
}
END_TEST

START_TEST(media_type_explicit_tv_accepted) {
  char *argv[] = {"dipixy", "-i", "a.m3u", "--media-type", "tv", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.sources[0].media_type, MEDIA_TV);
  args_free(&cfg);
}
END_TEST

START_TEST(media_type_without_preceding_input_rejected) {
  char *argv[] = {"dipixy", "--media-type", "radio", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(media_type_given_twice_for_same_input_rejected) {
  char *argv[] = {"dipixy", "-i", "a.m3u", "--media-type", "radio", "--media-type", "tv", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(media_type_rejects_unknown_value) {
  char *argv[] = {"dipixy", "-i", "a.m3u", "--media-type", "music", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(media_type_independent_per_source) {
  char *argv[] = {"dipixy", "-i", "a.m3u", "--media-type", "radio", "-i", "b.csv", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.sources[0].media_type, MEDIA_RADIO);
  ck_assert_int_eq(cfg.sources[1].media_type, MEDIA_TV);
  args_free(&cfg);
}
END_TEST

START_TEST(dlna_disabled_by_default) {
  char *argv[] = {"dipixy", "-I", "eth0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.enable_dlna, 0);
  ck_assert_int_eq(cfg.ssdp_ttl, 3);
  ck_assert_ptr_eq(cfg.ssdp_iface, NULL);
  args_free(&cfg);
}
END_TEST

START_TEST(ssdp_ttl_is_overridable) {
  char *argv[] = {"dipixy", "--ssdp-ttl", "8", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.ssdp_ttl, 8);
  args_free(&cfg);
}
END_TEST

START_TEST(ssdp_ttl_rejects_out_of_range) {
  char *argv[] = {"dipixy", "--ssdp-ttl", "0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(ssdp_iface_is_recorded) {
  char *argv[] = {"dipixy", "--ssdp-iface", "eth1", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.ssdp_iface, "eth1");
  args_free(&cfg);
}
END_TEST

START_TEST(cors_origin_defaults_to_null) {
  char *argv[] = {"dipixy", "-I", "eth0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_ptr_eq(cfg.cors_origins, NULL);
  args_free(&cfg);
}
END_TEST

START_TEST(cors_origin_is_recorded) {
  char *argv[] = {"dipixy", "--cors-origin", "https://a.example,https://b.example", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.cors_origins, "https://a.example,https://b.example");
  args_free(&cfg);
}
END_TEST

START_TEST(enable_dlna_falls_back_to_concrete_listen) {
  char *argv[] = {"dipixy", "-l", "192.0.2.1:9080", "--enable-dlna", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.enable_dlna, 1);
  ck_assert_str_eq(cfg.dlna_host, "192.0.2.1:9080");
  args_free(&cfg);
}
END_TEST

START_TEST(enable_dlna_without_host_or_concrete_listen_rejected) {
  char *argv[] = {"dipixy", "--enable-dlna", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(enable_dlna_records_explicit_host) {
  char *argv[] = {"dipixy", "--enable-dlna", "--dlna-host", "dvb.example:9080", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.dlna_host, "dvb.example:9080");
  args_free(&cfg);
}
END_TEST

START_TEST(enable_dlna_with_no_spts_rejected) {
  char *argv[] = {"dipixy", "--enable-dlna", "--dlna-host", "dvb.example:9080", "-f", "rawaudio", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(enable_dlna_with_no_rawaudio_rejected) {
  char *argv[] = {"dipixy", "--enable-dlna", "--dlna-host", "dvb.example:9080", "-f", "spts", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(enable_dlna_records_explicit_name) {
  char *argv[] = {"dipixy", "--enable-dlna", "--dlna-host", "dvb.example:9080", "--dlna-name", "Living Room", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.dlna_name, "Living Room");
  args_free(&cfg);
}
END_TEST

START_TEST(dlna_name_defaults_to_null) {
  char *argv[] = {"dipixy", "--enable-dlna", "--dlna-host", "dvb.example:9080", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_ptr_null(cfg.dlna_name);
  args_free(&cfg);
}
END_TEST

START_TEST(dlna_keep_multicast_defaults_to_off) {
  char *argv[] = {"dipixy", "--enable-dlna", "--dlna-host", "dvb.example:9080", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.dlna_keep_multicast, 0);
  args_free(&cfg);
}
END_TEST

START_TEST(dlna_keep_multicast_is_recorded) {
  char *argv[] = {"dipixy", "--enable-dlna", "--dlna-host", "dvb.example:9080", "--dlna-keep-multicast", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.dlna_keep_multicast, 1);
  args_free(&cfg);
}
END_TEST

START_TEST(invalid_color_mode_is_rejected) {
  char *argv[] = {"dipixy", "--color", "sometimes", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(unexpected_positional_argument_is_rejected) {
  char *argv[] = {"dipixy", "extra", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(help_returns_help_status) {
  char *argv[] = {"dipixy", "-h", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
}
END_TEST

START_TEST(no_argv_at_all_returns_noargs) {
  char *argv[] = {"dipixy", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_NOARGS);
}
END_TEST

static Suite *args_suite(void) {
  Suite *s = suite_create("dipixy_args");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, no_args_applies_all_defaults);
  tcase_add_test(tc, segment_size_is_overridable);
  tcase_add_test(tc, segment_size_rejects_below_minimum);
  tcase_add_test(tc, segment_count_is_overridable);
  tcase_add_test(tc, segment_count_rejects_below_minimum);
  tcase_add_test(tc, hls_part_size_is_overridable);
  tcase_add_test(tc, hls_part_size_rejects_out_of_range);
  tcase_add_test(tc, hls_part_size_rejects_not_smaller_than_segment_size);
  tcase_add_test(tc, dash_part_size_defaults_to_333ms);
  tcase_add_test(tc, dash_part_size_is_overridable);
  tcase_add_test(tc, dash_part_size_rejects_out_of_range);
  tcase_add_test(tc, dash_part_size_rejects_zero);
  tcase_add_test(tc, dash_part_size_rejects_not_smaller_than_segment_size);
  tcase_add_test(tc, listen_v4_is_overridable);
  tcase_add_test(tc, listen_v6_bracket_form_parses);
  tcase_add_test(tc, listen_tls_is_overridable);
  tcase_add_test(tc, listen_rejects_malformed_addr);
  tcase_add_test(tc, tls_cert_without_key_rejected);
  tcase_add_test(tc, tls_key_without_cert_rejected);
  tcase_add_test(tc, tls_cert_and_key_together_accepted);
  tcase_add_test(tc, workers_relative_values_accepted);
  tcase_add_test(tc, workers_absolute_value_accepted);
  tcase_add_test(tc, workers_rejects_zero_and_out_of_range);
  tcase_add_test(tc, max_clients_is_overridable);
  tcase_add_test(tc, max_clients_short_flag_accepted);
  tcase_add_test(tc, max_clients_rejects_zero_and_out_of_range);
  tcase_add_test(tc, sources_recorded_in_definition_order);
  tcase_add_test(tc, sds_rejects_malformed_addr);
  tcase_add_test(tc, xspf_source_recorded);
  tcase_add_test(tc, xml_source_recorded);
  tcase_add_test(tc, m3u8_extension_recorded_as_m3u);
  tcase_add_test(tc, extension_match_is_case_insensitive);
  tcase_add_test(tc, https_source_recorded_as_http);
  tcase_add_test(tc, unrecognized_source_form_is_rejected);
  tcase_add_test(tc, metrics_disabled_by_default);
  tcase_add_test(tc, metrics_id_enables_metrics);
  tcase_add_test(tc, metrics_sock_without_id_rejected);
  tcase_add_test(tc, metrics_interval_rejects_out_of_range);
  tcase_add_test(tc, metrics_http_flag_recorded);
  tcase_add_test(tc, daemonize_flag_recorded);
  tcase_add_test(tc, feature_toggle_flags_default_off);
  tcase_add_test(tc, feature_toggle_flags_recorded);
  tcase_add_test(tc, format_whitelist_enables_only_listed);
  tcase_add_test(tc, format_whitelist_lldash_enables_it);
  tcase_add_test(tc, format_whitelist_unknown_token_rejected);
  tcase_add_test(tc, format_whitelist_empty_rejected);
  tcase_add_test(tc, stdin_flag_accepts_dash);
  tcase_add_test(tc, stdin_flag_rejects_non_dash);
  tcase_add_test(tc, rist_flag_records_uri);
  tcase_add_test(tc, rist_flag_given_twice_rejected);
  tcase_add_test(tc, rist_flag_without_listen_marker_rejected);
  tcase_add_test(tc, http_in_flag_appends_source);
  tcase_add_test(tc, name_attaches_to_preceding_source);
  tcase_add_test(tc, name_attaches_to_preceding_stdin);
  tcase_add_test(tc, name_attaches_to_preceding_rist);
  tcase_add_test(tc, name_without_preceding_input_rejected);
  tcase_add_test(tc, name_given_twice_for_same_input_rejected);
  tcase_add_test(tc, duplicate_name_across_inputs_rejected);
  tcase_add_test(tc, name_containing_slash_rejected);
  tcase_add_test(tc, name_starting_with_dot_rejected);
  tcase_add_test(tc, name_matching_reserved_word_rejected);
  tcase_add_test(tc, name_empty_rejected);
  tcase_add_test(tc, media_type_defaults_to_tv);
  tcase_add_test(tc, media_type_attaches_to_preceding_source);
  tcase_add_test(tc, media_type_attaches_to_preceding_stdin);
  tcase_add_test(tc, media_type_attaches_to_preceding_rist);
  tcase_add_test(tc, media_type_explicit_tv_accepted);
  tcase_add_test(tc, media_type_without_preceding_input_rejected);
  tcase_add_test(tc, media_type_given_twice_for_same_input_rejected);
  tcase_add_test(tc, media_type_rejects_unknown_value);
  tcase_add_test(tc, media_type_independent_per_source);
  tcase_add_test(tc, dlna_disabled_by_default);
  tcase_add_test(tc, ssdp_ttl_is_overridable);
  tcase_add_test(tc, ssdp_ttl_rejects_out_of_range);
  tcase_add_test(tc, ssdp_iface_is_recorded);
  tcase_add_test(tc, cors_origin_defaults_to_null);
  tcase_add_test(tc, cors_origin_is_recorded);
  tcase_add_test(tc, enable_dlna_falls_back_to_concrete_listen);
  tcase_add_test(tc, enable_dlna_without_host_or_concrete_listen_rejected);
  tcase_add_test(tc, enable_dlna_records_explicit_host);
  tcase_add_test(tc, enable_dlna_with_no_spts_rejected);
  tcase_add_test(tc, enable_dlna_with_no_rawaudio_rejected);
  tcase_add_test(tc, enable_dlna_records_explicit_name);
  tcase_add_test(tc, dlna_name_defaults_to_null);
  tcase_add_test(tc, dlna_keep_multicast_defaults_to_off);
  tcase_add_test(tc, dlna_keep_multicast_is_recorded);
  tcase_add_test(tc, invalid_color_mode_is_rejected);
  tcase_add_test(tc, unexpected_positional_argument_is_rejected);
  tcase_add_test(tc, help_returns_help_status);
  tcase_add_test(tc, no_argv_at_all_returns_noargs);
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
