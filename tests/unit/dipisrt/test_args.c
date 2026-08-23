/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipisrt/args.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(sender_ok_with_nonsrt_in_and_srt_out) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(config_is_sender(&cfg), 1);
  ck_assert_int_eq(cfg.out.n_srt, 1);
  ck_assert_int_eq(cfg.out.listen, 0);
}
END_TEST

START_TEST(receiver_ok_with_srt_in_and_nonsrt_out) {
  char *argv[] = {"dipisrt", "-i", "srt://@0.0.0.0:9000", "-o", "rtp://@239.1.1.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(config_is_sender(&cfg), 0);
  ck_assert_int_eq(cfg.in.n_srt, 1);
  ck_assert_int_eq(cfg.in.listen, 1);
}
END_TEST

START_TEST(both_srt_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "srt://@0.0.0.0:9000", "-o", "srt://1.2.3.4:9000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(neither_srt_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "udp://@239.1.1.2:5001", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(sink_srt_with_at_is_ok) {
  /* unlike RIST, caller/listener is independent of which side is -i/-o */
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://@1.2.3.4:9000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.out.listen, 1);
}
END_TEST

START_TEST(source_srt_without_at_is_ok) {
  char *argv[] = {"dipisrt", "-i", "srt://1.2.3.4:9000", "-o", "rtp://@239.1.1.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.in.listen, 0);
}
END_TEST

START_TEST(repeated_srt_out_needs_group_mode) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "-o", "srt://5.6.7.8:9000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(repeated_srt_out_with_group_mode_bonds_peers) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "-o", "srt://5.6.7.8:9000", "--group-mode", "broadcast", NULL};
  config_t cfg;
#ifdef DIPISRT_HAVE_BONDING
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.out.n_srt, 2);
  ck_assert_int_eq(cfg.group_mode, SRT_GROUP_BROADCAST);
  ck_assert_str_eq(cfg.out.srt_host[0], "1.2.3.4");
  ck_assert_str_eq(cfg.out.srt_host[1], "5.6.7.8");
#else
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
#endif
}
END_TEST

START_TEST(group_mode_without_bonding_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "--group-mode", "backup", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(unknown_group_mode_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "-o", "srt://5.6.7.8:9000", "--group-mode", "loadbalance", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(mixed_listen_peers_on_same_flag_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "-o", "srt://@5.6.7.8:9000", "--group-mode", "broadcast", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(repeated_nonsrt_out_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "udp://@239.1.1.2:5001",
                  "-o", "udp://@239.1.1.3:5001", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(mixed_srt_and_nonsrt_on_same_flag_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "-o", "udp://@239.1.1.2:5001", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(missing_in_is_rejected) {
  char *argv[] = {"dipisrt", "-o", "srt://1.2.3.4:9000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(missing_out_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(http_source_ok) {
  char *argv[] = {"dipisrt", "-i", "http://10.0.0.1:4022/rtp/239.19.75.1:8700", "-o", "srt://1.2.3.4:9000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
}
END_TEST

START_TEST(http_sink_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "srt://@0.0.0.0:9000", "-o", "http://10.0.0.1:4022/rtp/239.19.75.1:8700", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(non_multicast_direct_address_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@10.0.0.1:5000", "-o", "srt://1.2.3.4:9000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(non_numeric_srt_host_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://example.com:9000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rendezvous_requires_local) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000", "--rendezvous", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rendezvous_with_local_is_accepted) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "--rendezvous", "--local", "0.0.0.0:9001", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rendezvous, 1);
  ck_assert_str_eq(cfg.local_host, "0.0.0.0");
  ck_assert_uint_eq(cfg.local_port, 9001u);
}
END_TEST

START_TEST(rendezvous_with_listen_peer_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://@1.2.3.4:9000",
                  "--rendezvous", "--local", "0.0.0.0:9001", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rendezvous_with_group_mode_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "-o", "srt://5.6.7.8:9000", "--group-mode", "broadcast",
                  "--rendezvous", "--local", "0.0.0.0:9001", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(passphrase_too_short_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000", "--passphrase", "short", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(passphrase_valid_length_is_accepted) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "--passphrase", "correcthorsebattery", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.passphrase, "correcthorsebattery");
}
END_TEST

START_TEST(pbkeylen_without_passphrase_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000", "--pbkeylen", "24", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(pbkeylen_invalid_value_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "--passphrase", "correcthorsebattery", "--pbkeylen", "20", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(pbkeylen_valid_value_is_accepted) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "--passphrase", "correcthorsebattery", "--pbkeylen", "32", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.pbkeylen, 32);
}
END_TEST

START_TEST(streamid_and_packetfilter_and_latency_are_applied) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "--streamid", "chan1", "--packetfilter", "fec,cols:10,rows:5", "--latency", "250", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.streamid, "chan1");
  ck_assert_str_eq(cfg.packetfilter, "fec,cols:10,rows:5");
  ck_assert_uint_eq(cfg.latency_ms, 250u);
}
END_TEST

START_TEST(metrics_options_require_metrics_id) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "--metrics", "/tmp/x.sock", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(metrics_id_alone_is_accepted) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000",
                  "--metrics-id", "inst1", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.metrics_id, "inst1");
}
END_TEST

START_TEST(help_returns_help_status) {
  char *argv[] = {"dipisrt", "-h", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
}
END_TEST

START_TEST(unexpected_positional_argument_is_rejected) {
  char *argv[] = {"dipisrt", "-i", "rtp://@239.1.1.1:5000", "-o", "srt://1.2.3.4:9000", "extra", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
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
