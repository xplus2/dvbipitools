/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipirist/args.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(sender_ok_with_nonrist_in_and_rist_out) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(config_is_sender(&cfg), 1);
  ck_assert_int_eq(cfg.out.n_rist, 1);
}
END_TEST

START_TEST(receiver_ok_with_rist_in_and_nonrist_out) {
  char *argv[] = {"dipirist", "-i", "rist://@0.0.0.0:6000", "-o", "rtp://@239.1.1.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(config_is_sender(&cfg), 0);
  ck_assert_int_eq(cfg.in.n_rist, 1);
}
END_TEST

START_TEST(both_rist_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rist://@0.0.0.0:6000", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(neither_rist_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "udp://@239.1.1.2:5001", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(sink_rist_with_at_is_rejected) {
  /* -o rist:// calls out: an '@' (listen) address makes no sense as a call target */
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://@1.2.3.4:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(source_rist_without_at_is_rejected) {
  /* -i rist:// listens: without '@' librist treats it as a caller, never binds */
  char *argv[] = {"dipirist", "-i", "rist://0.0.0.0:6000", "-o", "rtp://@239.1.1.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(repeated_rist_out_bonds_peers) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000",
                  "-o", "rist://5.6.7.8:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.out.n_rist, 2);
  ck_assert_str_eq(cfg.out.rist_uri[0], "rist://1.2.3.4:6000");
  ck_assert_str_eq(cfg.out.rist_uri[1], "rist://5.6.7.8:6000");
}
END_TEST

START_TEST(repeated_nonrist_out_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "udp://@239.1.1.2:5001",
                  "-o", "udp://@239.1.1.3:5001", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(mixed_rist_and_nonrist_on_same_flag_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000",
                  "-o", "udp://@239.1.1.2:5001", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(missing_in_is_rejected) {
  char *argv[] = {"dipirist", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(missing_out_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(http_source_ok) {
  char *argv[] = {"dipirist", "-i", "http://10.0.0.1:4022/rtp/239.19.75.1:8700", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
}
END_TEST

START_TEST(http_source_accepts_arbitrary_path) {
  char *argv[] = {"dipirist", "-i", "https://10.0.0.1:8443/any/path/at/all", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.in.nonrist.kind, NONRIST_HTTP);
  ck_assert_int_eq(cfg.in.nonrist.http.tls, 1);
}
END_TEST

START_TEST(http_sink_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rist://@0.0.0.0:6000", "-o", "http://10.0.0.1:4022/rtp/239.19.75.1:8700", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(non_multicast_direct_address_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rtp://@10.0.0.1:5000", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(default_profile_is_simple) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.profile, RIST_PROF_SIMPLE);
}
END_TEST

START_TEST(profile_main_is_accepted) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--profile", "main", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.profile, RIST_PROF_MAIN);
}
END_TEST

START_TEST(unknown_profile_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--profile", "advanced", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(secret_without_profile_main_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--secret", "hunter2", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(secret_with_profile_main_is_accepted) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000",
                  "--profile", "main", "--secret", "hunter2", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.secret, "hunter2");
}
END_TEST

START_TEST(buffer_out_of_range_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--buffer", "0", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(buffer_value_is_applied) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "--buffer", "1000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.buffer_ms, 1000u);
}
END_TEST

START_TEST(metrics_options_require_metrics_id) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000",
                  "--metrics", "/tmp/x.sock", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(metrics_id_alone_is_accepted) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000",
                  "--metrics-id", "inst1", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.metrics_id, "inst1");
}
END_TEST

START_TEST(help_returns_help_status) {
  char *argv[] = {"dipirist", "-h", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
}
END_TEST

START_TEST(unexpected_positional_argument_is_rejected) {
  char *argv[] = {"dipirist", "-i", "rtp://@239.1.1.1:5000", "-o", "rist://1.2.3.4:6000", "extra", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
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
  tcase_add_test(tc, buffer_out_of_range_is_rejected);
  tcase_add_test(tc, buffer_value_is_applied);
  tcase_add_test(tc, metrics_options_require_metrics_id);
  tcase_add_test(tc, metrics_id_alone_is_accepted);
  tcase_add_test(tc, help_returns_help_status);
  tcase_add_test(tc, unexpected_positional_argument_is_rejected);
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
