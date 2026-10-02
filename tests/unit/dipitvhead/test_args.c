/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/config/yamlcfg.h"
#include "lib/helper/log.h"

#include "dipitvhead/cli/args.h"
#include "dipitvhead/config.h"
#include "dipitvhead/config/priv.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(single_input_matches_legacy_defaults) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_inputs, 1u);
  ck_assert_uint_eq(cfg.inputs[0].sid, 1u);
  ck_assert_int_eq(cfg.inputs[0].sdt_mode, TABLE_PASSTHROUGH);
  ck_assert_uint_eq(cfg.inputs[0].pmt_pid, 0u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(multi_input_sid_auto_assign_skips_explicit) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "--sid", "5",
                  "-i", "udp://@239.1.1.2:5000",
                  "-i", "udp://@239.1.1.3:5000", "--sid", "2",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_inputs, 3u);
  ck_assert_uint_eq(cfg.inputs[0].sid, 5u);
  ck_assert_uint_eq(cfg.inputs[1].sid, 1u); /* lowest free id, 5 and 2 are taken */
  ck_assert_uint_eq(cfg.inputs[2].sid, 2u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(paired_options_apply_to_preceding_input) {
  char *argv[] = {"dipitvhead",
                  "-i", "udp://@239.1.1.1:5000", "--sid", "10", "-s", "Channel A", "-p", "0x0100",
                  "-i", "udp://@239.1.1.2:5000", "--sid", "20", "-s", "Channel B", "--strip-eit",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.inputs[0].sid, 10u);
  ck_assert_int_eq(cfg.inputs[0].sdt_mode, TABLE_OVERRIDE);
  ck_assert_str_eq(cfg.inputs[0].sdt_text, "Channel A");
  ck_assert_uint_eq(cfg.inputs[0].pmt_pid, 0x0100u);
  ck_assert_int_eq(cfg.inputs[0].strip_eit, 0);
  ck_assert_uint_eq(cfg.inputs[1].sid, 20u);
  ck_assert_str_eq(cfg.inputs[1].sdt_text, "Channel B");
  ck_assert_int_eq(cfg.inputs[1].strip_eit, 1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(hbbtv_triplet_pairs_with_preceding_input) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000",
                  "--hbbtv", "http://example.invalid/app.html", "--hbbtv-org-id", "1", "--hbbtv-app-id", "2",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.inputs[0].hbbtv_url, "http://example.invalid/app.html");
  ck_assert_uint_eq(cfg.inputs[0].hbbtv_org_id, 1u);
  ck_assert_uint_eq(cfg.inputs[0].hbbtv_app_id, 2u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(hbbtv_url_without_org_or_app_id_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "--hbbtv", "http://example.invalid/app.html", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(hbbtv_org_id_without_url_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "--hbbtv-org-id", "1", "--hbbtv-app-id", "2", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(sid_before_any_input_is_rejected) {
  char *argv[] = {"dipitvhead", "--sid", "5", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(sdt_before_any_input_is_rejected) {
  char *argv[] = {"dipitvhead", "-s", "x", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(pmt_pid_before_any_input_is_rejected) {
  char *argv[] = {"dipitvhead", "-p", "0x0100", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(iface_before_any_input_is_rejected) {
  char *argv[] = {"dipitvhead", "-I", "eth0", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(duplicate_explicit_sid_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "--sid", "10", "-i", "udp://@239.1.1.2:5000", "--sid", "10", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(missing_input_is_rejected) {
  char *argv[] = {"dipitvhead", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(missing_mcast_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(too_many_inputs_is_rejected) {
  char *argv[1 + (ARGS_MAX_INPUTS + 1) * 2 + 2 + 1];
  int n = 0;
  config_t cfg = {0};
  argv[n++] = "dipitvhead";
  for (int i = 0; i < ARGS_MAX_INPUTS + 1; i++) {
    argv[n++] = "-i";
    argv[n++] = "udp://@239.1.1.1:5000";
  }
  argv[n++] = "-m";
  argv[n++] = "239.1.2.1:5000";
  argv[n] = NULL;
  ck_assert_int_eq(args_parse(n, argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

/* mux-wide flags stay global, not per-input: NIT, tsid/onid, bitrate pacing, CAS */
START_TEST(global_flags_are_not_per_input) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-i", "udp://@239.1.1.2:5000", "-n", "My Network", "--tsid", "7", "--onid", "8", "-b", "5000", "-S", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.nit_mode, TABLE_OVERRIDE);
  ck_assert_str_eq(cfg.nit_text, "My Network");
  ck_assert_uint_eq(cfg.tsid, 7u);
  ck_assert_uint_eq(cfg.onid, 8u);
  ck_assert_uint_eq(cfg.bitrate_kbps, 5000u);
  ck_assert_int_eq(cfg.stuff, 1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(biss2_ca_receivers_enables_and_sets_dir) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--biss2-ca-receivers", "/etc/biss-ca/receivers", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.biss2_ca_enabled, 1);
  ck_assert_str_eq(cfg.biss2_ca_receivers_dir, "/etc/biss-ca/receivers");
  ck_assert_int_eq(cfg.biss2_ca_session_id_given, 0);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(biss2_ca_session_id_parses_hex_and_dec) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--biss2-ca-receivers", "/etc/biss-ca/receivers", "--biss2-ca-session-id", "0x1234", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.biss2_ca_session_id_given, 1);
  ck_assert_uint_eq(cfg.biss2_ca_session_id, 0x1234u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(biss2_ca_session_id_without_receivers_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--biss2-ca-session-id", "1", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(biss2_ca_receivers_rejects_out_of_range_session_id) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "--biss2-ca-receivers", "/etc/biss-ca/receivers", "--biss2-ca-session-id", "0x10000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(biss2_ca_receivers_mutually_exclusive_with_cas_algo) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--cas-algo", "cissa", "--biss2-ca-receivers", "/etc/biss-ca/receivers", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(biss2_ca_receivers_mutually_exclusive_with_biss2_sw) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--biss2-sw", "00112233445566778899aabbccddeeff", "--biss2-ca-receivers", "/etc/biss-ca/receivers", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(biss2_ca_receivers_defaults_cas_pids_to_video_audio) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--biss2-ca-receivers", "/etc/biss-ca/receivers", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.cas_pids_video, 1);
  ck_assert_int_eq(cfg.cas_pids_audio, 1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_peer_is_repeatable_and_bonded) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "-R", "rist://1.2.3.4:6000", "-R", "rist://5.6.7.8:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_rist, 2u);
  ck_assert_str_eq(cfg.rist_uri[0], "rist://1.2.3.4:6000");
  ck_assert_str_eq(cfg.rist_uri[1], "rist://5.6.7.8:6000");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_peer_without_rist_scheme_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "-R", "udp://1.2.3.4:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_and_mcast_output_coexist) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "-R", "rist://1.2.3.4:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.mcast_group, "239.1.2.1");
  ck_assert_uint_eq(cfg.n_rist, 1u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_secret_without_profile_main_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "-R", "rist://1.2.3.4:6000", "--rist-secret", "hunter2", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_secret_with_profile_main_is_accepted) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "-R", "rist://1.2.3.4:6000", "--rist-profile", "main", "--rist-secret", "hunter2", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rist_profile, RIST_PROF_MAIN);
  ck_assert_str_eq(cfg.rist_secret, "hunter2");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_encryption_type_without_profile_main_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "-R", "rist://1.2.3.4:6000", "--rist-encryption-type", "256", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_encryption_type_with_profile_main_is_accepted) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "-R", "rist://1.2.3.4:6000", "--rist-profile", "main", "--rist-encryption-type", "256", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rist_key_size, 256);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_encryption_type_invalid_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "-R", "rist://1.2.3.4:6000", "--rist-profile", "main", "--rist-encryption-type", "192", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_encryption_type_in_pairs_with_preceding_input) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-i", "rist://@127.0.0.1:6000", "--rist-profile-in", "main", "--rist-encryption-type-in", "256", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.inputs[0].rist_key_size_in, 0);
  ck_assert_int_eq(cfg.inputs[1].rist_key_size_in, 256);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_encryption_type_in_without_profile_in_main_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "rist://@127.0.0.1:6000", "--rist-encryption-type-in", "128", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_encryption_type_in_before_any_input_is_rejected) {
  char *argv[] = {"dipitvhead", "--rist-encryption-type-in", "128", "-i", "rist://@127.0.0.1:6000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_encryption_type_in_on_non_rist_input_is_harmless) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "--rist-encryption-type-in", "128", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_input_with_at_is_accepted) {
  char *argv[] = {"dipitvhead", "-i", "rist://@127.0.0.1:6000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.inputs[0].input.kind, SRC_RIST);
  ck_assert_str_eq(cfg.inputs[0].input.rist_uri, "rist://@127.0.0.1:6000");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_input_without_at_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "rist://127.0.0.1:6000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_profile_in_pairs_with_preceding_input) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-i", "rist://@127.0.0.1:6000", "--rist-profile-in", "main", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.inputs[0].rist_profile_main, 0);
  ck_assert_int_eq(cfg.inputs[1].rist_profile_main, 1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_profile_in_before_any_input_is_rejected) {
  char *argv[] = {"dipitvhead", "--rist-profile-in", "main", "-i", "rist://@127.0.0.1:6000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(more_than_one_rist_input_is_rejected) {
  /* librist isn't safe with more than one rist_ctx per process */
  char *argv[] = {"dipitvhead", "-i", "rist://@127.0.0.1:6000", "-i", "rist://@127.0.0.1:6002", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_input_and_rist_output_together_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "rist://@127.0.0.1:6000", "-R", "rist://1.2.3.4:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_input_listen_is_accepted) {
  char *argv[] = {"dipitvhead", "-i", "srt://@127.0.0.1:6000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.inputs[0].input.kind, SRC_SRT);
  ck_assert_int_eq(cfg.inputs[0].input.srt_listen, 1);
  ck_assert_str_eq(cfg.inputs[0].input.srt_host, "127.0.0.1");
  ck_assert_uint_eq(cfg.inputs[0].input.srt_port, 6000);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_input_caller_is_accepted) {
  char *argv[] = {"dipitvhead", "-i", "srt://127.0.0.1:6000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.inputs[0].input.srt_listen, 0);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_passphrase_in_pairs_with_preceding_input) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-i", "srt://@127.0.0.1:6000", "--srt-passphrase-in", "0123456789", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.inputs[0].srt_passphrase_in, "");
  ck_assert_str_eq(cfg.inputs[1].srt_passphrase_in, "0123456789");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_passphrase_in_before_any_input_is_rejected) {
  char *argv[] = {"dipitvhead", "--srt-passphrase-in", "0123456789", "-i", "srt://@127.0.0.1:6000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_output_caller_is_accepted) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-R", "srt://1.2.3.4:7000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_srt, 1u);
  ck_assert_str_eq(cfg.srt_host[0], "1.2.3.4");
  ck_assert_uint_eq(cfg.srt_port[0], 7000);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_output_listen_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-R", "srt://@1.2.3.4:7000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_peers_bonded_require_group_mode) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-R", "srt://1.2.3.4:7000", "-R", "srt://5.6.7.8:7000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_peers_bonded_with_group_mode_is_accepted) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-R", "srt://1.2.3.4:7000", "-R", "srt://5.6.7.8:7000", "--srt-group-mode", "backup", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_srt, 2u);
  ck_assert_int_eq(cfg.srt_group_mode, SRT_BOND_BACKUP);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_and_srt_output_peers_cannot_mix) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-R", "rist://1.2.3.4:7000", "-R", "srt://5.6.7.8:7000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_input_and_srt_output_together_is_accepted) {
  /* unlike rist://, srt has no per-process context limit */
  char *argv[] = {"dipitvhead", "-i", "srt://@127.0.0.1:6000", "-R", "srt://1.2.3.4:7000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_passphrase_length_is_validated) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-R", "srt://1.2.3.4:7000", "--srt-passphrase", "short", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_pbkeylen_requires_passphrase) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-R", "srt://1.2.3.4:7000", "--srt-pbkeylen", "16", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_options_without_any_rist_peer_are_rejected_by_profile_check_only) {
  /* --rist-secret without --rist-profile main still fails validation even with no -R;
     the no-op warning (logged, not fatal) doesn't change ARGS_OK/ARGS_ERR here */
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "--rist-buffer", "500", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_rist, 0u);
  ck_assert_uint_eq(cfg.rist_buffer_ms, 500u);
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
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "input:\n  - udp://@239.1.1.1:5000\n  - udp://@239.1.1.2:5000\nmcast: 239.1.2.1:5000\nttl: 4\nudp: on\nnit: Net\nbitrate: 8000\nstuff: on\nmetrics:\n  id: tvh\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_uint_eq(cfg.n_inputs, 2u);
  ck_assert_uint_eq(cfg.inputs[0].sid, 1u);
  ck_assert_uint_eq(cfg.inputs[1].sid, 2u);
  ck_assert_uint_eq(cfg.mcast_port, 5000u);
  ck_assert_uint_eq(cfg.ttl, 4u);
  ck_assert_int_eq(cfg.rtp, 0);
  ck_assert_int_eq(cfg.nit_mode, TABLE_OVERRIDE);
  ck_assert_str_eq(cfg.nit_text, "Net");
  ck_assert_uint_eq(cfg.bitrate_kbps, 8000u);
  ck_assert_int_eq(cfg.stuff, 1);
  ck_assert_str_eq(cfg.metrics_id, "tvh");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_inputs_take_keyed_items) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "input:\n  - udp://@239.1.1.1:5000\n  - udp://@239.1.1.2:5000:\n      sid: 7\n      sdt: A\n      provider: P\n      pmt-pid: 0x0100\n      strip-eit: on\n      hbbtv: http://h/app\n      hbbtv-org-id: 5\n      hbbtv-app-id: 6\nmcast: 239.1.3.1:5000\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_uint_eq(cfg.n_inputs, 2u);
  ck_assert_uint_eq(cfg.inputs[0].sid, 1u);
  ck_assert_uint_eq(cfg.inputs[1].sid, 7u);
  ck_assert_int_eq(cfg.inputs[1].sdt_mode, TABLE_OVERRIDE);
  ck_assert_str_eq(cfg.inputs[1].sdt_text, "A");
  ck_assert_str_eq(cfg.inputs[1].provider_text, "P");
  ck_assert_uint_eq(cfg.inputs[1].pmt_pid, 0x0100u);
  ck_assert_int_eq(cfg.inputs[1].strip_eit, 1);
  ck_assert_str_eq(cfg.inputs[1].hbbtv_url, "http://h/app");
  ck_assert_uint_eq(cfg.inputs[1].hbbtv_org_id, 5u);
  ck_assert_uint_eq(cfg.inputs[1].hbbtv_app_id, 6u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_input_srt_keys_pair_with_their_input) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "input:\n  - srt://1.2.3.4:7000:\n      srt-passphrase-in: 0123456789ab\n      srt-latency-in: 200\nmcast: 239.1.3.1:5000\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_str_eq(cfg.inputs[0].srt_passphrase_in, "0123456789ab");
  ck_assert_uint_eq(cfg.inputs[0].srt_latency_in_ms, 200u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_cas_ecmg_takes_keyed_items) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "input: udp://@239.1.1.1:5000\nmcast: 239.1.3.1:5000\ncas:\n  algo: csa2\n  ecmg:\n    - tcp://h1:2222:\n        super-id: 0x1234\n        ecm-id: 5\n        ecmg-version: 3\n        required: on\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_uint_eq(cfg.n_cas_vendors, 1u);
  ck_assert_str_eq(cfg.cas_vendors[0].ecmg_host, "h1");
  ck_assert_uint_eq(cfg.cas_vendors[0].ecmg_version, 3u);
  ck_assert_int_eq(cfg.cas_vendors[0].required, 1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_item_without_source_is_error) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "input:\n  - sid: 3\nmcast: 239.1.3.1:5000\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(cmdline_lists_replace_config_lists) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "-c", path, "-i", "udp://@239.9.9.9:5000", "--sid", "9", "-R", "srt://127.0.0.1:7000", NULL};
  config_t cfg = {0};
  write_cfg(path, "input:\n  - udp://@239.1.1.1:5000\n  - udp://@239.1.1.2:5000\nrist:\n  - rist://h:1\nmcast: 239.1.3.1:5000\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_uint_eq(cfg.n_inputs, 1u);
  ck_assert_uint_eq(cfg.inputs[0].sid, 9u);
  ck_assert_uint_eq(cfg.n_rist, 0u);
  ck_assert_uint_eq(cfg.n_srt, 1u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(cmdline_scoped_option_needs_its_own_input) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "-c", path, "--sid", "9", NULL};
  config_t cfg = {0};
  write_cfg(path, "input: udp://@239.1.1.1:5000\nmcast: 239.1.3.1:5000\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(cmdline_wins_over_config) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "-c", path, "-T", "9", NULL};
  config_t cfg = {0};
  write_cfg(path, "input: udp://@239.1.1.1:5000\nmcast: 239.1.3.1:5000\nttl: 4\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_uint_eq(cfg.ttl, 9u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(help_lists_pcr_options) {
  char path[] = "/tmp/dipitvhead_help_XXXXXX";
  char *argv[] = {"dipitvhead", "-h", NULL};
  char text[16384];
  config_t cfg = {0};
  int saved;
  int fd = mkstemp(path);
  FILE *f;
  size_t n;
  ck_assert_int_ge(fd, 0);
  fflush(stdout);
  saved = dup(STDOUT_FILENO);
  ck_assert_int_ge(saved, 0);
  ck_assert_int_ge(dup2(fd, STDOUT_FILENO), 0);
  close(fd);
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
  fflush(stdout);
  ck_assert_int_ge(dup2(saved, STDOUT_FILENO), 0);
  close(saved);
  f = fopen(path, "rb");
  ck_assert_ptr_nonnull(f);
  n = fread(text, 1, sizeof text - 1, f);
  fclose(f);
  unlink(path);
  text[n] = '\0';
  ck_assert_ptr_nonnull(strstr(text, "--pcr-mode <m>"));
  ck_assert_ptr_nonnull(strstr(text, "preserve|rebase|regenerate"));
  ck_assert_ptr_nonnull(strstr(text, "--pcr-lead-ms <ms>"));
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_pcr_mode_from_yaml) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "input: udp://@239.1.1.1:5000\nmcast: 239.1.2.1:5000\nbitrate: 8000\nstuff: on\npcr-mode: regenerate\npcr-lead-ms: 400\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.pcr_mode, PCR_MODE_REGENERATE);
  ck_assert_uint_eq(cfg.pcr_lead_ms, 400u);
  ck_assert_int_eq(cfg.pcr_lead_ms_given, 1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(cmdline_pcr_mode_wins_over_config) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "-c", path, "--pcr-mode", "rebase", NULL};
  config_t cfg = {0};
  write_cfg(path, "input: udp://@239.1.1.1:5000\nmcast: 239.1.2.1:5000\nbitrate: 8000\nstuff: on\npcr-mode: regenerate\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.pcr_mode, PCR_MODE_REBASE);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_missing_file_is_error) {
  char *argv[] = {"dipitvhead", "-c", "/nonexistent/dipitvhead.yaml", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_invalid_value_is_error) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "input: udp://@239.1.1.1:5000\nmcast: 239.1.3.1:5000\nttl: 300\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_conflict_is_rejected) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "input: udp://@239.1.1.1:5000\nmcast: 239.1.3.1:5000\nal-fec: 5:5\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(configtest_reports_by_exit_status) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "--configtest", "-c", path, NULL};
  char *argv2[] = {"dipitvhead", "--configtest", "-c", "/nonexistent/dipitvhead.yaml", NULL};
  config_t cfg = {0};
  write_cfg(path, "bogus: 1\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
  ck_assert_int_eq(args_parse(ARGC(argv2), argv2, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_level_is_recorded) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--metrics-id", "inst1", "--metrics-inspect-ts", "medium", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_MEDIUM);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(pcr_mode_defaults_to_preserve) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.pcr_mode, PCR_MODE_PRESERVE);
  ck_assert_uint_eq(cfg.pcr_lead_ms, (unsigned)PCR_LEAD_MS_DEFAULT);
  ck_assert_int_eq(cfg.pcr_lead_ms_given, 0);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(pcr_mode_preserve_and_rebase_are_recorded) {
  char *argv_p[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--pcr-mode", "preserve", NULL};
  char *argv_r[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--pcr-mode", "rebase", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv_p), argv_p, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.pcr_mode, PCR_MODE_PRESERVE);
  yamlcfg_strpool_free(cfg.str_pool);
  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(args_parse(ARGC(argv_r), argv_r, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.pcr_mode, PCR_MODE_REBASE);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(pcr_mode_regenerate_with_cbr_flags_is_recorded) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--pcr-mode", "regenerate", "-b", "8000", "-S", "-B", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.pcr_mode, PCR_MODE_REGENERATE);
  ck_assert_uint_eq(cfg.pcr_lead_ms, (unsigned)PCR_LEAD_MS_DEFAULT);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(pcr_mode_rejects_unknown_value) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--pcr-mode", "bogus", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(pcr_mode_regenerate_requires_bitrate_and_stuffing) {
  char *argv_none[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--pcr-mode", "regenerate", NULL};
  char *argv_b[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--pcr-mode", "regenerate", "-b", "8000", NULL};
  char *argv_bb[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--pcr-mode", "regenerate", "-b", "8000", "-B", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv_none), argv_none, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(args_parse(ARGC(argv_b), argv_b, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(args_parse(ARGC(argv_bb), argv_bb, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(pcr_mode_regenerate_without_burst_limit_is_accepted) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--pcr-mode", "regenerate", "-b", "8000", "-S", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.pcr_mode, PCR_MODE_REGENERATE);
  ck_assert_int_eq(cfg.burst_limit, 0);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(pcr_lead_ms_is_recorded_with_regenerate) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--pcr-mode", "regenerate", "-b", "8000", "-S", "--pcr-lead-ms", "500", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.pcr_lead_ms, 500u);
  ck_assert_int_eq(cfg.pcr_lead_ms_given, 1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(pcr_lead_ms_range_is_enforced) {
  static const struct { const char *val; args_status_t want; } cases[] = {
    {"0", ARGS_ERR}, {"1", ARGS_OK}, {"1000", ARGS_OK}, {"1001", ARGS_ERR}, {"x", ARGS_ERR},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--pcr-mode", "regenerate", "-b", "8000", "-S",
                    "--pcr-lead-ms", (char *)cases[i].val, NULL};
    config_t cfg = {0};
    ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), cases[i].want);
    yamlcfg_strpool_free(cfg.str_pool);
  }
}
END_TEST

START_TEST(pcr_lead_ms_requires_regenerate) {
  char *argv_alone[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--pcr-lead-ms", "500", NULL};
  char *argv_rebase[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--pcr-mode", "rebase", "--pcr-lead-ms", "500", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv_alone), argv_alone, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(args_parse(ARGC(argv_rebase), argv_rebase, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_defaults_to_off) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--metrics-id", "inst1", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_OFF);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_requires_metrics_id) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--metrics-inspect-ts", "basic", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_rejects_unknown_level) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--metrics-id", "inst1", "--metrics-inspect-ts", "bogus", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_from_yaml) {
  char path[] = "/tmp/dipitvhead_inspect_XXXXXX";
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "metrics:\n  id: a\n  inspect-ts: full\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_FULL);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_pids_parsed) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--metrics-id", "inst1", "--metrics-inspect-ts", "full", "--metrics-inspect-ts-pids", "0x100,300", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.metrics_n_known_pids, 2u);
  ck_assert_uint_eq(cfg.metrics_known_pids[0], 256u);
  ck_assert_uint_eq(cfg.metrics_known_pids[1], 300u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_pids_need_full_level) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--metrics-id", "inst1", "--metrics-inspect-ts", "basic", "--metrics-inspect-ts-pids", "0x100", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_pids_reject_bad_list) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--metrics-id", "inst1", "--metrics-inspect-ts", "full", "--metrics-inspect-ts-pids", "0x100,9000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

typedef int (*setter_fn)(void *c, const char *v, char *e, size_t n);

typedef enum { SCOPE_NONE, SCOPE_INPUT, SCOPE_VENDOR } scope_t;
typedef enum { CHECK_NONE, CHECK_UINT, CHECK_LONG } check_t;

typedef struct {
  const char *name;
  setter_fn fn;
  scope_t scope;
  const char *value;
  const char *err;
  check_t check;
  size_t off;
  long expect;
} setter_case_t;

#define CFG_OFF(f) offsetof(config_t, f)
#define IN_OFF(f) offsetof(dipitvhead_input_t, f)
#define VD_OFF(f) offsetof(cas_vendor_t, f)
#define BAD(n, f, sc, v, e) {n, f, sc, v, e, CHECK_NONE, 0, 0}
#define OK(n, f, sc, v) {n, f, sc, v, "", CHECK_NONE, 0, 0}
#define OKU(n, f, sc, v, off, x) {n, f, sc, v, "", CHECK_UINT, off, x}
#define OKL(n, f, sc, v, off, x) {n, f, sc, v, "", CHECK_LONG, off, x}

static const setter_case_t setter_cases[] = {
    BAD("color bogus", tvh_apply_color, SCOPE_NONE, "bogus", "invalid 'bogus' (auto|always|never)"),
    OKU("color always", tvh_apply_color, SCOPE_NONE, "always", CFG_OFF(color_mode), LOG_COLOR_ALWAYS),
    OKU("color never", tvh_apply_color, SCOPE_NONE, "never", CFG_OFF(color_mode), LOG_COLOR_NEVER),
    OKU("color auto", tvh_apply_color, SCOPE_NONE, "auto", CFG_OFF(color_mode), LOG_COLOR_AUTO),
    BAD("daemonize maybe", tvh_apply_daemonize, SCOPE_NONE, "maybe", "invalid boolean 'maybe'"),
    OKU("daemonize yes", tvh_apply_daemonize, SCOPE_NONE, "yes", CFG_OFF(daemonize), 1),
    OKU("daemonize off", tvh_apply_daemonize, SCOPE_NONE, "off", CFG_OFF(daemonize), 0),
    BAD("known pids out of range", tvh_apply_metrics_known_pids, SCOPE_NONE, "0x100,9000", "must be comma separated pids, 0..8191"),
    BAD("known pids not a number", tvh_apply_metrics_known_pids, SCOPE_NONE, "abc", "must be comma separated pids, 0..8191"),
    OKU("known pids list", tvh_apply_metrics_known_pids, SCOPE_NONE, "0x100,0x200", CFG_OFF(metrics_n_known_pids), 2),
    BAD("inspect-ts unknown level", tvh_apply_metrics_inspect_ts, SCOPE_NONE, "loud", "must be off|basic|medium|full"),
    OK("inspect-ts full", tvh_apply_metrics_inspect_ts, SCOPE_NONE, "full"),
    BAD("metrics interval zero", tvh_apply_metrics_interval, SCOPE_NONE, "0", "invalid '0' (need 1..86400)"),
    BAD("metrics interval too large", tvh_apply_metrics_interval, SCOPE_NONE, "86401", "invalid '86401' (need 1..86400)"),
    OKU("metrics interval", tvh_apply_metrics_interval, SCOPE_NONE, "30", CFG_OFF(metrics_interval_s), 30),
    OKU("nit drop", tvh_apply_nit, SCOPE_NONE, "-", CFG_OFF(nit_mode), TABLE_DROP),
    OKU("nit override", tvh_apply_nit, SCOPE_NONE, "Net", CFG_OFF(nit_mode), TABLE_OVERRIDE),
    BAD("bitrate zero", tvh_apply_bitrate, SCOPE_NONE, "0", "invalid '0' (need 1..1000000)"),
    BAD("bitrate too large", tvh_apply_bitrate, SCOPE_NONE, "1000001", "invalid '1000001' (need 1..1000000)"),
    OKU("bitrate", tvh_apply_bitrate, SCOPE_NONE, "8000", CFG_OFF(bitrate_kbps), 8000),
    BAD("stuff invalid", tvh_apply_stuff, SCOPE_NONE, "2", "invalid boolean '2'"),
    OKU("stuff on", tvh_apply_stuff, SCOPE_NONE, "on", CFG_OFF(stuff), 1),
    BAD("pcr-mode invalid", tvh_apply_pcr_mode, SCOPE_NONE, "bogus", "invalid 'bogus' (preserve|rebase|regenerate)"),
    OKU("pcr-mode rebase", tvh_apply_pcr_mode, SCOPE_NONE, "rebase", CFG_OFF(pcr_mode), PCR_MODE_REBASE),
    OKU("pcr-mode regenerate", tvh_apply_pcr_mode, SCOPE_NONE, "regenerate", CFG_OFF(pcr_mode), PCR_MODE_REGENERATE),
    BAD("pcr-lead-ms zero", tvh_apply_pcr_lead_ms, SCOPE_NONE, "0", "invalid '0' (1..1000 ms)"),
    BAD("pcr-lead-ms too large", tvh_apply_pcr_lead_ms, SCOPE_NONE, "1001", "invalid '1001' (1..1000 ms)"),
    OKU("pcr-lead-ms valid", tvh_apply_pcr_lead_ms, SCOPE_NONE, "250", CFG_OFF(pcr_lead_ms), 250),
    BAD("burst-limit invalid", tvh_apply_burst_limit, SCOPE_NONE, "x", "invalid boolean 'x'"),
    OKU("burst-limit true", tvh_apply_burst_limit, SCOPE_NONE, "true", CFG_OFF(burst_limit), 1),
    BAD("error negative", tvh_apply_error, SCOPE_NONE, "-1", "invalid '-1' (need 0..4294967295)"),
    BAD("error not a number", tvh_apply_error, SCOPE_NONE, "x", "invalid 'x' (need 0..4294967295)"),
    OKL("error retry", tvh_apply_error, SCOPE_NONE, "5", CFG_OFF(error_retry_s), 5),
    BAD("insecure invalid", tvh_apply_insecure, SCOPE_NONE, "x", "invalid boolean 'x'"),
    OKU("insecure on", tvh_apply_insecure, SCOPE_NONE, "on", CFG_OFF(insecure_tls), 1),
    BAD("tsid zero", tvh_apply_tsid, SCOPE_NONE, "0", "invalid '0' (need 1..65535)"),
    BAD("tsid too large", tvh_apply_tsid, SCOPE_NONE, "65536", "invalid '65536' (need 1..65535)"),
    OKU("tsid", tvh_apply_tsid, SCOPE_NONE, "7", CFG_OFF(tsid), 7),
    BAD("onid too large", tvh_apply_onid, SCOPE_NONE, "65536", "invalid '65536' (need 1..65535)"),
    OKU("onid", tvh_apply_onid, SCOPE_NONE, "65535", CFG_OFF(onid), 65535),
    BAD("verbose invalid", tvh_apply_verbose, SCOPE_NONE, "x", "invalid boolean 'x'"),
    OKU("verbose on", tvh_apply_verbose, SCOPE_NONE, "on", CFG_OFF(verbose), 1),
    BAD("multicast not multicast", tvh_apply_mcast, SCOPE_NONE, "10.0.0.1:5000", "invalid '10.0.0.1:5000' (multicast addr:port)"),
    BAD("ttl zero", tvh_apply_ttl, SCOPE_NONE, "0", "invalid '0' (need 1..255)"),
    BAD("ttl too large", tvh_apply_ttl, SCOPE_NONE, "256", "invalid '256' (need 1..255)"),
    OKU("ttl", tvh_apply_ttl, SCOPE_NONE, "255", CFG_OFF(ttl), 255),
    BAD("udp invalid", tvh_apply_udp, SCOPE_NONE, "zz", "invalid boolean 'zz'"),
    OKU("udp on clears rtp", tvh_apply_udp, SCOPE_NONE, "on", CFG_OFF(rtp), 0),
    BAD("dscp above range", tvh_apply_dscp, SCOPE_NONE, "64", "invalid '64' (video-high|video-low|voice|signalling|best-effort|0..63)"),
    BAD("dscp unknown name", tvh_apply_dscp, SCOPE_NONE, "loud", "invalid 'loud' (video-high|video-low|voice|signalling|best-effort|0..63)"),
    BAD("dscp negative", tvh_apply_dscp, SCOPE_NONE, "-1", "invalid '-1' (video-high|video-low|voice|signalling|best-effort|0..63)"),
    OKU("dscp numeric is stored as the tos byte", tvh_apply_dscp, SCOPE_NONE, "46", CFG_OFF(dscp), 46 << 2),
    OK("dscp name", tvh_apply_dscp, SCOPE_NONE, "voice"),
    BAD("al-fec zero rows", tvh_apply_al_fec, SCOPE_NONE, "0:5", "invalid '0:5' (want L:D, L*D<=400, L<=40)"),
    BAD("al-fec too many columns", tvh_apply_al_fec, SCOPE_NONE, "41:1", "invalid '41:1' (want L:D, L*D<=400, L<=40)"),
    BAD("al-fec matrix too large", tvh_apply_al_fec, SCOPE_NONE, "20:21", "invalid '20:21' (want L:D, L*D<=400, L<=40)"),
    BAD("al-fec not a pair", tvh_apply_al_fec, SCOPE_NONE, "x", "invalid 'x' (want L:D, L*D<=400, L<=40)"),
    OKU("al-fec columns", tvh_apply_al_fec, SCOPE_NONE, "10:10", CFG_OFF(al_fec_l), 10),
    OKU("al-fec rows", tvh_apply_al_fec, SCOPE_NONE, "10:12", CFG_OFF(al_fec_d), 12),
    BAD("al-fec port zero", tvh_apply_al_fec_port, SCOPE_NONE, "0", "invalid '0' (need 1..65535)"),
    BAD("al-fec port too large", tvh_apply_al_fec_port, SCOPE_NONE, "65536", "invalid '65536' (need 1..65535)"),
    OKU("al-fec port", tvh_apply_al_fec_port, SCOPE_NONE, "4000", CFG_OFF(al_fec_port), 4000),
    BAD("rist buffer zero", tvh_apply_buffer, SCOPE_NONE, "0", "invalid '0' (need 1..4294967295)"),
    OKU("rist buffer", tvh_apply_buffer, SCOPE_NONE, "500", CFG_OFF(rist_buffer_ms), 500),
    BAD("rist profile unknown", tvh_apply_profile, SCOPE_NONE, "x", "invalid 'x' (simple|main)"),
    BAD("rist encryption type unsupported", tvh_apply_encryption_type, SCOPE_NONE, "192", "invalid '192' (128|256)"),
    BAD("rist encryption type not a number", tvh_apply_encryption_type, SCOPE_NONE, "abc", "invalid 'abc' (128|256)"),
    OKU("rist encryption type 128", tvh_apply_encryption_type, SCOPE_NONE, "128", CFG_OFF(rist_key_size), 128),
    OKU("rist encryption type 256", tvh_apply_encryption_type, SCOPE_NONE, "256", CFG_OFF(rist_key_size), 256),
    BAD("srt group mode unknown", tvh_apply_srt_group_mode, SCOPE_NONE, "xx", "invalid 'xx' (broadcast|backup)"),
    OK("srt group mode broadcast", tvh_apply_srt_group_mode, SCOPE_NONE, "broadcast"),
    OK("srt group mode backup", tvh_apply_srt_group_mode, SCOPE_NONE, "backup"),
    BAD("srt pbkeylen unsupported", tvh_apply_srt_pbkeylen, SCOPE_NONE, "20", "invalid '20' (16|24|32)"),
    OKU("srt pbkeylen", tvh_apply_srt_pbkeylen, SCOPE_NONE, "32", CFG_OFF(srt_pbkeylen), 32),
    BAD("srt latency zero", tvh_apply_srt_latency, SCOPE_NONE, "0", "invalid '0' (need 1..60000)"),
    BAD("srt latency too large", tvh_apply_srt_latency, SCOPE_NONE, "60001", "invalid '60001' (need 1..60000)"),
    OKU("srt latency", tvh_apply_srt_latency, SCOPE_NONE, "60000", CFG_OFF(srt_latency_ms), 60000),
    BAD("input sid zero", tvh_apply_input_sid, SCOPE_INPUT, "0", "invalid '0' (need 1..65535)"),
    OKU("input sid", tvh_apply_input_sid, SCOPE_INPUT, "65535", IN_OFF(sid), 65535),
    BAD("input pmt pid zero", tvh_apply_input_pmt_pid, SCOPE_INPUT, "0", "invalid '0' (0x0010..0x1FFE)"),
    BAD("input pmt pid too large", tvh_apply_input_pmt_pid, SCOPE_INPUT, "0x2000", "invalid '0x2000' (0x0010..0x1FFE)"),
    BAD("input pmt pid not a number", tvh_apply_input_pmt_pid, SCOPE_INPUT, "zz", "invalid 'zz' (0x0010..0x1FFE)"),
    OKU("input pmt pid", tvh_apply_input_pmt_pid, SCOPE_INPUT, "0x0100", IN_OFF(pmt_pid), 0x100),
    BAD("input strip unknown", tvh_apply_input_strip, SCOPE_INPUT, "foo", "invalid 'foo' (comma list of DATA,ECM, or none)"),
    OK("input strip list", tvh_apply_input_strip, SCOPE_INPUT, "DATA,ECM"),
    BAD("input strip-eit invalid", tvh_apply_input_strip_eit, SCOPE_INPUT, "x", "invalid boolean 'x'"),
    OKU("input strip-eit", tvh_apply_input_strip_eit, SCOPE_INPUT, "on", IN_OFF(strip_eit), 1),
    BAD("hbbtv org id zero", tvh_apply_input_hbbtv_org_id, SCOPE_INPUT, "0", "invalid '0' (need 1..4294967295)"),
    BAD("hbbtv org id not a number", tvh_apply_input_hbbtv_org_id, SCOPE_INPUT, "x", "invalid 'x' (need 1..4294967295)"),
    BAD("hbbtv org id above 32 bits", tvh_apply_input_hbbtv_org_id, SCOPE_INPUT, "4294967296", "invalid '4294967296' (need 1..4294967295)"),
    OKL("hbbtv org id maximum", tvh_apply_input_hbbtv_org_id, SCOPE_INPUT, "4294967295", IN_OFF(hbbtv_org_id), 4294967295L),
    BAD("hbbtv app id zero", tvh_apply_input_hbbtv_app_id, SCOPE_INPUT, "0", "invalid '0' (need 1..65535)"),
    BAD("hbbtv app id too large", tvh_apply_input_hbbtv_app_id, SCOPE_INPUT, "65536", "invalid '65536' (need 1..65535)"),
    OKU("hbbtv app id maximum", tvh_apply_input_hbbtv_app_id, SCOPE_INPUT, "65535", IN_OFF(hbbtv_app_id), 65535),
    BAD("input rist profile unknown", tvh_apply_input_rist_profile, SCOPE_INPUT, "x", "invalid 'x' (simple|main)"),
    OKU("input rist profile main", tvh_apply_input_rist_profile, SCOPE_INPUT, "main", IN_OFF(rist_profile_main), 1),
    BAD("input rist encryption type unsupported", tvh_apply_input_rist_encryption_type, SCOPE_INPUT, "512", "invalid '512' (128|256)"),
    OKU("input rist encryption type 256", tvh_apply_input_rist_encryption_type, SCOPE_INPUT, "256", IN_OFF(rist_key_size_in), 256),
    BAD("input srt pbkeylen below range", tvh_apply_input_srt_pbkeylen, SCOPE_INPUT, "15", "invalid '15' (16|24|32)"),
    BAD("input srt pbkeylen between sizes", tvh_apply_input_srt_pbkeylen, SCOPE_INPUT, "20", "invalid '20' (16|24|32)"),
    BAD("input srt pbkeylen above range", tvh_apply_input_srt_pbkeylen, SCOPE_INPUT, "33", "invalid '33' (16|24|32)"),
    BAD("input srt pbkeylen not a number", tvh_apply_input_srt_pbkeylen, SCOPE_INPUT, "abc", "invalid 'abc' (16|24|32)"),
    OKU("input srt pbkeylen 16", tvh_apply_input_srt_pbkeylen, SCOPE_INPUT, "16", IN_OFF(srt_pbkeylen_in), 16),
    OKU("input srt pbkeylen 24", tvh_apply_input_srt_pbkeylen, SCOPE_INPUT, "24", IN_OFF(srt_pbkeylen_in), 24),
    OKU("input srt pbkeylen 32", tvh_apply_input_srt_pbkeylen, SCOPE_INPUT, "32", IN_OFF(srt_pbkeylen_in), 32),
    BAD("input srt latency zero", tvh_apply_input_srt_latency, SCOPE_INPUT, "0", "invalid '0' (need 1..60000)"),
    BAD("input srt latency too large", tvh_apply_input_srt_latency, SCOPE_INPUT, "60001", "invalid '60001' (need 1..60000)"),
    OKU("input srt latency", tvh_apply_input_srt_latency, SCOPE_INPUT, "60000", IN_OFF(srt_latency_in_ms), 60000),
    BAD("input jitter zero", tvh_apply_input_jitter_ms, SCOPE_INPUT, "0", "invalid '0' (need 1..2000)"),
    BAD("input jitter too large", tvh_apply_input_jitter_ms, SCOPE_INPUT, "2001", "invalid '2001' (need 1..2000)"),
    OKU("input jitter", tvh_apply_input_jitter_ms, SCOPE_INPUT, "2000", IN_OFF(jitter_ms), 2000),
    BAD("cas algo unknown", tvh_apply_cas_algo, SCOPE_NONE, "rot13", "invalid 'rot13' (cissa|csa2|csa1)"),
    OKU("cas algo csa1", tvh_apply_cas_algo, SCOPE_NONE, "csa1", CFG_OFF(cas_algo), CAS_ALGO_CSA1),
    BAD("cas pids unknown token", tvh_apply_cas_pids, SCOPE_NONE, "bogus", "invalid 'bogus' (pids and/or video|audio|lcevc)"),
    OK("cas pids tokens", tvh_apply_cas_pids, SCOPE_NONE, "video,audio"),
    BAD("cas cp duration not a number", tvh_apply_cas_cp_duration, SCOPE_NONE, "x", "invalid 'x' (ms, 1..86400000)"),
    OKU("cas cp duration", tvh_apply_cas_cp_duration, SCOPE_NONE, "5000", CFG_OFF(cas_cp_duration_ms), 5000),
    BAD("cas fallback-clear invalid", tvh_apply_cas_fallback_clear, SCOPE_NONE, "perhaps", "invalid boolean 'perhaps'"),
    OKU("cas fallback-clear", tvh_apply_cas_fallback_clear, SCOPE_NONE, "on", CFG_OFF(cas_fallback_clear), 1),
    BAD("cas ecmg malformed endpoint", tvh_apply_cas_ecmg, SCOPE_NONE, "not an endpoint", "invalid 'not an endpoint' (tcp://host:port)"),
    BAD("cas ecmg version unsupported", tvh_apply_cas_ecmg_version, SCOPE_VENDOR, "4", "invalid '4' (2|3)"),
    BAD("cas ecmg version not a number", tvh_apply_cas_ecmg_version, SCOPE_VENDOR, "x", "invalid 'x' (2|3)"),
    OKU("cas ecmg version", tvh_apply_cas_ecmg_version, SCOPE_VENDOR, "3", VD_OFF(ecmg_version), 3),
    BAD("cas super id not a number", tvh_apply_cas_super_id, SCOPE_VENDOR, "zz", "invalid 'zz' (32 bit, dec or 0x-hex)"),
    BAD("cas super id above 32 bits", tvh_apply_cas_super_id, SCOPE_VENDOR, "0x1FFFFFFFF", "invalid '0x1FFFFFFFF' (32 bit, dec or 0x-hex)"),
    OKL("cas super id", tvh_apply_cas_super_id, SCOPE_VENDOR, "0x4A750001", VD_OFF(super_cas_id), 0x4A750001L),
    BAD("cas ecm id zero", tvh_apply_cas_ecm_id, SCOPE_VENDOR, "0", "invalid '0' (1..65535)"),
    BAD("cas ecm id too large", tvh_apply_cas_ecm_id, SCOPE_VENDOR, "65536", "invalid '65536' (1..65535)"),
    OKU("cas ecm id", tvh_apply_cas_ecm_id, SCOPE_VENDOR, "65535", VD_OFF(ecm_id), 65535),
    BAD("cas ecm pid too large", tvh_apply_cas_ecm_pid, SCOPE_VENDOR, "0x2000", "invalid '0x2000' (0x0001..0x1FFE)"),
    BAD("cas ecm pid not a number", tvh_apply_cas_ecm_pid, SCOPE_VENDOR, "x", "invalid 'x' (0x0001..0x1FFE)"),
    OKU("cas ecm pid", tvh_apply_cas_ecm_pid, SCOPE_VENDOR, "0x1FFE", VD_OFF(ecm_pid), 0x1FFE),
    BAD("cas emmg port zero", tvh_apply_cas_emmg_port, SCOPE_VENDOR, "0", "invalid '0' (port)"),
    BAD("cas emmg port too large", tvh_apply_cas_emmg_port, SCOPE_VENDOR, "65536", "invalid '65536' (port)"),
    OKU("cas emmg port", tvh_apply_cas_emmg_port, SCOPE_VENDOR, "8002", VD_OFF(emmg_port), 8002),
    BAD("cas emmg max conns zero", tvh_apply_cas_emmg_max_conns, SCOPE_VENDOR, "0", "invalid '0' (1..64)"),
    BAD("cas emmg max conns too large", tvh_apply_cas_emmg_max_conns, SCOPE_VENDOR, "65", "invalid '65' (1..64)"),
    OKU("cas emmg max conns", tvh_apply_cas_emmg_max_conns, SCOPE_VENDOR, "64", VD_OFF(emmg_max_conns), 64),
    BAD("cas emmg version unsupported", tvh_apply_cas_emmg_version, SCOPE_VENDOR, "1", "invalid '1' (2|3)"),
    OKU("cas emmg version", tvh_apply_cas_emmg_version, SCOPE_VENDOR, "2", VD_OFF(emmg_version), 2),
    BAD("cas emmg reverse malformed", tvh_apply_cas_emmg_reverse, SCOPE_VENDOR, "nohost", "invalid 'nohost' (tcp://host:port)"),
    BAD("cas emm pid too large", tvh_apply_cas_emm_pid, SCOPE_VENDOR, "0x2000", "invalid '0x2000' (0x0001..0x1FFE)"),
    OKU("cas emm pid", tvh_apply_cas_emm_pid, SCOPE_VENDOR, "0x0021", VD_OFF(emm_pid), 0x21),
    BAD("cas resilience unknown", tvh_apply_cas_resilience, SCOPE_VENDOR, "shrug", "invalid 'shrug' (frozen|cycling|silent)"),
    OKU("cas resilience cycling", tvh_apply_cas_resilience, SCOPE_VENDOR, "cycling", VD_OFF(resilience), CAS_OUTAGE_CYCLING),
    BAD("cas cwenc algo unknown", tvh_apply_cas_cwenc_algo, SCOPE_VENDOR, "rot13", "invalid 'rot13' (des56|aes128|aes256)"),
    OK("cas cwenc algo aes128", tvh_apply_cas_cwenc_algo, SCOPE_VENDOR, "aes128"),
    BAD("cas cwenc aes mode unknown", tvh_apply_cas_cwenc_aes_mode, SCOPE_VENDOR, "xyz", "invalid 'xyz' (stream|ecb)"),
    OK("cas cwenc aes mode ecb", tvh_apply_cas_cwenc_aes_mode, SCOPE_VENDOR, "ecb"),
    BAD("cas required invalid", tvh_apply_cas_required, SCOPE_VENDOR, "perhaps", "invalid boolean 'perhaps'"),
    OKU("cas required", tvh_apply_cas_required, SCOPE_VENDOR, "yes", VD_OFF(required), 1),
    BAD("biss1 key too short", tvh_apply_biss1_sw, SCOPE_NONE, "short", "invalid 'short' (12 hex chars)"),
    BAD("biss2 key too short", tvh_apply_biss2_sw, SCOPE_NONE, "short", "invalid 'short' (32 hex chars)"),
    BAD("biss2 esw id not hex", tvh_apply_biss2_emit_esw, SCOPE_NONE, "zz", "invalid 'zz' (32 hex chars)"),
    BAD("biss2 session id not hex", tvh_apply_biss2_ca_session_id, SCOPE_NONE, "zz", "invalid 'zz' (16 bit, dec or 0x-hex)"),
};

static void setup_scope(config_t *cfg, scope_t scope) {
  char e[128];

  tvh_cfg_defaults(cfg);
  if (scope == SCOPE_INPUT) {
    ck_assert_int_eq(tvh_item_hook(cfg, "input", 1, e, sizeof e), 0);
    ck_assert_int_eq(tvh_apply_input(cfg, "udp://@239.1.1.1:5000", e, sizeof e), 0);
  } else if (scope == SCOPE_VENDOR) {
    ck_assert_int_eq(tvh_item_hook(cfg, "cas.ecmg", 1, e, sizeof e), 0);
    ck_assert_int_eq(tvh_apply_cas_ecmg(cfg, "127.0.0.1:2222", e, sizeof e), 0);
  }
}

static const void *scope_base(const config_t *cfg, scope_t scope) {
  if (scope == SCOPE_INPUT) return &cfg->inputs[0];
  if (scope == SCOPE_VENDOR) return &cfg->cas_vendors[0];
  return cfg;
}

START_TEST(setters_validate_values_and_report_specific_errors) {
  const setter_case_t *c = &setter_cases[_i];
  config_t cfg;
  char e[256];
  int rc;

  setup_scope(&cfg, c->scope);
  e[0] = '\0';
  rc = c->fn(&cfg, c->value, e, sizeof e);
  if (c->err[0]) {
    ck_assert_msg(rc == -1, "%s: accepted '%s'", c->name, c->value);
    ck_assert_msg(strcmp(e, c->err) == 0, "%s: error '%s', want '%s'", c->name, e, c->err);
  } else {
    ck_assert_msg(rc == 0, "%s: rejected '%s': %s", c->name, c->value, e);
    if (c->check == CHECK_UINT) {
      unsigned got = *(const unsigned *)((const char *)scope_base(&cfg, c->scope) + c->off);

      ck_assert_msg((long)got == c->expect, "%s: stored %u, want %ld", c->name, got, c->expect);
    } else if (c->check == CHECK_LONG) {
      long got = *(const long *)((const char *)scope_base(&cfg, c->scope) + c->off);

      ck_assert_msg(got == c->expect, "%s: stored %ld, want %ld", c->name, got, c->expect);
    }
  }
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

typedef struct {
  const char *name;
  setter_fn fn;
  scope_t scope;
  size_t size;
} buffer_case_t;

#define MEMBER_SIZE(type, f) sizeof(((type *)0)->f)

static const buffer_case_t buffer_cases[] = {
    {"nit text", tvh_apply_nit, SCOPE_NONE, MEMBER_SIZE(config_t, nit_text)},
    {"default provider", tvh_apply_default_provider, SCOPE_NONE, MEMBER_SIZE(config_t, default_provider_text)},
    {"rist secret", tvh_apply_secret, SCOPE_NONE, MEMBER_SIZE(config_t, rist_secret)},
    {"rist cname", tvh_apply_cname, SCOPE_NONE, MEMBER_SIZE(config_t, rist_cname)},
    {"srt passphrase", tvh_apply_srt_passphrase, SCOPE_NONE, MEMBER_SIZE(config_t, srt_passphrase)},
    {"srt streamid", tvh_apply_srt_streamid, SCOPE_NONE, MEMBER_SIZE(config_t, srt_streamid)},
    {"srt packetfilter", tvh_apply_srt_packetfilter, SCOPE_NONE, MEMBER_SIZE(config_t, srt_packetfilter)},
    {"input sdt text", tvh_apply_input_sdt, SCOPE_INPUT, MEMBER_SIZE(dipitvhead_input_t, sdt_text)},
    {"input provider", tvh_apply_input_provider, SCOPE_INPUT, MEMBER_SIZE(dipitvhead_input_t, provider_text)},
    {"input srt passphrase", tvh_apply_input_srt_passphrase, SCOPE_INPUT, MEMBER_SIZE(dipitvhead_input_t, srt_passphrase_in)},
    {"input srt streamid", tvh_apply_input_srt_streamid, SCOPE_INPUT, MEMBER_SIZE(dipitvhead_input_t, srt_streamid_in)},
    {"input srt packetfilter", tvh_apply_input_srt_packetfilter, SCOPE_INPUT, MEMBER_SIZE(dipitvhead_input_t, srt_packetfilter_in)},
    {"cas cwenc fixed key", tvh_apply_cas_cwenc_fixed_key, SCOPE_VENDOR, MEMBER_SIZE(cas_vendor_t, cwenc_fixed_key_hex)},
    {"cas cwenc key list a", tvh_apply_cas_cwenc_key_list_a, SCOPE_VENDOR, MEMBER_SIZE(cas_vendor_t, cwenc_key_list_a_path)},
    {"cas cwenc key list b", tvh_apply_cas_cwenc_key_list_b, SCOPE_VENDOR, MEMBER_SIZE(cas_vendor_t, cwenc_key_list_b_path)},
};

START_TEST(text_setters_enforce_their_buffer_size) {
  const buffer_case_t *c = &buffer_cases[_i];
  config_t cfg;
  char *value = malloc(c->size + 1);
  char e[128];
  char want[64];

  ck_assert_ptr_nonnull(value);
  setup_scope(&cfg, c->scope);
  memset(value, 'a', c->size);
  value[c->size] = '\0';
  e[0] = '\0';
  ck_assert_msg(c->fn(&cfg, value, e, sizeof e) == -1, "%s: accepted a value of %zu characters", c->name, c->size);
  snprintf(want, sizeof want, "too long (max %zu)", c->size - 1);
  ck_assert_msg(strcmp(e, want) == 0, "%s: error '%s', want '%s'", c->name, e, want);
  value[c->size - 1] = '\0';
  ck_assert_msg(c->fn(&cfg, value, e, sizeof e) == 0, "%s: rejected a value of %zu characters", c->name, c->size - 1);
  free(value);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(current_input_and_vendor_are_null_outside_their_lists) {
  config_t cfg;
  char e[128];

  tvh_cfg_defaults(&cfg);
  e[0] = '\0';
  ck_assert_ptr_null(tvh_cur_input(&cfg, e, sizeof e));
  ck_assert_str_eq(e, "only valid inside an input list");
  e[0] = '\0';
  ck_assert_ptr_null(tvh_cur_vendor(&cfg, e, sizeof e));
  ck_assert_str_eq(e, "only valid inside a cas.ecmg list");
}
END_TEST

START_TEST(current_input_and_vendor_point_at_the_last_added_item) {
  config_t cfg;
  char e[128];

  tvh_cfg_defaults(&cfg);
  ck_assert_int_eq(tvh_item_hook(&cfg, "input", 1, e, sizeof e), 0);
  ck_assert_int_eq(tvh_apply_input(&cfg, "udp://@239.1.1.1:5000", e, sizeof e), 0);
  ck_assert_int_eq(tvh_apply_input(&cfg, "udp://@239.1.1.2:5000", e, sizeof e), 0);
  ck_assert_ptr_eq(tvh_cur_input(&cfg, e, sizeof e), &cfg.inputs[1]);
  ck_assert_int_eq(tvh_item_hook(&cfg, "cas.ecmg", 1, e, sizeof e), 0);
  ck_assert_int_eq(tvh_apply_cas_ecmg(&cfg, "127.0.0.1:2222", e, sizeof e), 0);
  ck_assert_ptr_eq(tvh_cur_vendor(&cfg, e, sizeof e), &cfg.cas_vendors[0]);
}
END_TEST

START_TEST(scoped_options_before_their_item_is_added_are_rejected) {
  config_t cfg;
  char e[128];

  tvh_cfg_defaults(&cfg);
  ck_assert_int_eq(tvh_item_hook(&cfg, "input", 1, e, sizeof e), 0);
  e[0] = '\0';
  ck_assert_int_eq(tvh_apply_input_sid(&cfg, "5", e, sizeof e), -1);
  ck_assert_str_ne(e, "");
  ck_assert_int_eq(tvh_item_hook(&cfg, "cas.ecmg", 1, e, sizeof e), 0);
  e[0] = '\0';
  ck_assert_int_eq(tvh_apply_cas_ecm_id(&cfg, "5", e, sizeof e), -1);
  ck_assert_str_ne(e, "");
}
END_TEST

START_TEST(vendor_options_outside_a_vendor_list_are_rejected) {
  config_t cfg;
  char e[128];

  tvh_cfg_defaults(&cfg);
  e[0] = '\0';
  ck_assert_int_eq(tvh_apply_cas_required(&cfg, "yes", e, sizeof e), -1);
  ck_assert_str_eq(e, "only valid inside a cas.ecmg list");
  e[0] = '\0';
  ck_assert_int_eq(tvh_apply_cas_ecm_pid(&cfg, "0x20", e, sizeof e), -1);
  ck_assert_str_eq(e, "only valid inside a cas.ecmg list");
}
END_TEST

typedef struct {
  const char *name;
  const char *yaml;
  int rc_lenient;
  int rc_strict;
} cfgtest_case_t;

static const cfgtest_case_t cfgtest_cases[] = {
    {"clean file", "input: udp://@239.1.1.1:5000\nmcast: 239.1.3.1:5000\n", 0, 0},
    {"unknown key is a warning", "input: udp://@239.1.1.1:5000\nmcast: 239.1.3.1:5000\nbogus: 1\n", 0, -1},
    {"missing input and output is a warning", "ttl: 4\n", 0, -1},
    {"conflicting options are a warning", "input: udp://@239.1.1.1:5000\nmcast: 239.1.3.1:5000\nal-fec: 5:5\n", 0, -1},
    {"invalid value is a warning too", "input: udp://@239.1.1.1:5000\nmcast: 239.1.3.1:5000\nttl: 300\n", 0, -1},
};

START_TEST(configtest_separates_warnings_from_errors) {
  const cfgtest_case_t *c = &cfgtest_cases[_i];
  char path[] = "/tmp/dipitvhead_cfgtest_XXXXXX";
  int lenient;
  int strict;

  write_cfg(path, c->yaml);
  lenient = tvh_cfg_test(path, 0);
  strict = tvh_cfg_test(path, 1);
  unlink(path);
  ck_assert_msg(lenient == c->rc_lenient, "%s: lenient result %d", c->name, lenient);
  ck_assert_msg(strict == c->rc_strict, "%s: strict result %d", c->name, strict);
}
END_TEST

START_TEST(configtest_reports_unreadable_file_as_error) {
  ck_assert_int_eq(tvh_cfg_test("/nonexistent/dipitvhead.yaml", 0), -1);
  ck_assert_int_eq(tvh_cfg_test("/nonexistent/dipitvhead.yaml", 1), -1);
}
END_TEST

static Suite *args_suite(void) {
  Suite *s = suite_create("args");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, single_input_matches_legacy_defaults);
  tcase_add_test(tc, multi_input_sid_auto_assign_skips_explicit);
  tcase_add_test(tc, paired_options_apply_to_preceding_input);
  tcase_add_test(tc, hbbtv_triplet_pairs_with_preceding_input);
  tcase_add_test(tc, hbbtv_url_without_org_or_app_id_is_rejected);
  tcase_add_test(tc, hbbtv_org_id_without_url_is_rejected);
  tcase_add_test(tc, sid_before_any_input_is_rejected);
  tcase_add_test(tc, sdt_before_any_input_is_rejected);
  tcase_add_test(tc, pmt_pid_before_any_input_is_rejected);
  tcase_add_test(tc, iface_before_any_input_is_rejected);
  tcase_add_test(tc, duplicate_explicit_sid_is_rejected);
  tcase_add_test(tc, missing_input_is_rejected);
  tcase_add_test(tc, missing_mcast_is_rejected);
  tcase_add_test(tc, too_many_inputs_is_rejected);
  tcase_add_test(tc, global_flags_are_not_per_input);
  tcase_add_test(tc, biss2_ca_receivers_enables_and_sets_dir);
  tcase_add_test(tc, biss2_ca_session_id_parses_hex_and_dec);
  tcase_add_test(tc, biss2_ca_session_id_without_receivers_is_rejected);
  tcase_add_test(tc, biss2_ca_receivers_rejects_out_of_range_session_id);
  tcase_add_test(tc, biss2_ca_receivers_mutually_exclusive_with_cas_algo);
  tcase_add_test(tc, biss2_ca_receivers_mutually_exclusive_with_biss2_sw);
  tcase_add_test(tc, biss2_ca_receivers_defaults_cas_pids_to_video_audio);
  tcase_add_test(tc, rist_peer_is_repeatable_and_bonded);
  tcase_add_test(tc, rist_peer_without_rist_scheme_is_rejected);
  tcase_add_test(tc, rist_and_mcast_output_coexist);
  tcase_add_test(tc, rist_secret_without_profile_main_is_rejected);
  tcase_add_test(tc, rist_secret_with_profile_main_is_accepted);
  tcase_add_test(tc, rist_encryption_type_without_profile_main_is_rejected);
  tcase_add_test(tc, rist_encryption_type_with_profile_main_is_accepted);
  tcase_add_test(tc, rist_encryption_type_invalid_is_rejected);
  tcase_add_test(tc, rist_encryption_type_in_pairs_with_preceding_input);
  tcase_add_test(tc, rist_encryption_type_in_without_profile_in_main_is_rejected);
  tcase_add_test(tc, rist_encryption_type_in_before_any_input_is_rejected);
  tcase_add_test(tc, rist_encryption_type_in_on_non_rist_input_is_harmless);
  tcase_add_test(tc, rist_options_without_any_rist_peer_are_rejected_by_profile_check_only);
  tcase_add_test(tc, rist_input_with_at_is_accepted);
  tcase_add_test(tc, rist_input_without_at_is_rejected);
  tcase_add_test(tc, rist_profile_in_pairs_with_preceding_input);
  tcase_add_test(tc, rist_profile_in_before_any_input_is_rejected);
  tcase_add_test(tc, more_than_one_rist_input_is_rejected);
  tcase_add_test(tc, rist_input_and_rist_output_together_is_rejected);
  tcase_add_test(tc, srt_input_listen_is_accepted);
  tcase_add_test(tc, srt_input_caller_is_accepted);
  tcase_add_test(tc, srt_passphrase_in_pairs_with_preceding_input);
  tcase_add_test(tc, srt_passphrase_in_before_any_input_is_rejected);
  tcase_add_test(tc, srt_output_caller_is_accepted);
  tcase_add_test(tc, srt_output_listen_is_rejected);
  tcase_add_test(tc, srt_peers_bonded_require_group_mode);
  tcase_add_test(tc, srt_peers_bonded_with_group_mode_is_accepted);
  tcase_add_test(tc, rist_and_srt_output_peers_cannot_mix);
  tcase_add_test(tc, srt_input_and_srt_output_together_is_accepted);
  tcase_add_test(tc, srt_passphrase_length_is_validated);
  tcase_add_test(tc, srt_pbkeylen_requires_passphrase);
  tcase_add_test(tc, config_file_provides_settings);
  tcase_add_test(tc, config_inputs_take_keyed_items);
  tcase_add_test(tc, config_input_srt_keys_pair_with_their_input);
  tcase_add_test(tc, config_cas_ecmg_takes_keyed_items);
  tcase_add_test(tc, config_item_without_source_is_error);
  tcase_add_test(tc, cmdline_lists_replace_config_lists);
  tcase_add_test(tc, cmdline_scoped_option_needs_its_own_input);
  tcase_add_test(tc, cmdline_wins_over_config);
  tcase_add_test(tc, help_lists_pcr_options);
  tcase_add_test(tc, config_pcr_mode_from_yaml);
  tcase_add_test(tc, cmdline_pcr_mode_wins_over_config);
  tcase_add_test(tc, config_missing_file_is_error);
  tcase_add_test(tc, config_invalid_value_is_error);
  tcase_add_test(tc, config_conflict_is_rejected);
  tcase_add_test(tc, configtest_reports_by_exit_status);
  tcase_add_test(tc, inspect_ts_level_is_recorded);
  tcase_add_test(tc, inspect_ts_defaults_to_off);
  tcase_add_test(tc, inspect_ts_requires_metrics_id);
  tcase_add_test(tc, inspect_ts_rejects_unknown_level);
  tcase_add_test(tc, inspect_ts_from_yaml);
  tcase_add_test(tc, inspect_ts_pids_parsed);
  tcase_add_test(tc, inspect_ts_pids_need_full_level);
  tcase_add_test(tc, inspect_ts_pids_reject_bad_list);
  tcase_add_test(tc, pcr_mode_defaults_to_preserve);
  tcase_add_test(tc, pcr_mode_preserve_and_rebase_are_recorded);
  tcase_add_test(tc, pcr_mode_regenerate_with_cbr_flags_is_recorded);
  tcase_add_test(tc, pcr_mode_rejects_unknown_value);
  tcase_add_test(tc, pcr_mode_regenerate_requires_bitrate_and_stuffing);
  tcase_add_test(tc, pcr_mode_regenerate_without_burst_limit_is_accepted);
  tcase_add_test(tc, pcr_lead_ms_is_recorded_with_regenerate);
  tcase_add_test(tc, pcr_lead_ms_range_is_enforced);
  tcase_add_test(tc, pcr_lead_ms_requires_regenerate);
  tcase_add_loop_test(tc, setters_validate_values_and_report_specific_errors, 0, (int)(sizeof setter_cases / sizeof setter_cases[0]));
  tcase_add_loop_test(tc, text_setters_enforce_their_buffer_size, 0, (int)(sizeof buffer_cases / sizeof buffer_cases[0]));
  tcase_add_test(tc, current_input_and_vendor_are_null_outside_their_lists);
  tcase_add_test(tc, current_input_and_vendor_point_at_the_last_added_item);
  tcase_add_test(tc, scoped_options_before_their_item_is_added_are_rejected);
  tcase_add_test(tc, vendor_options_outside_a_vendor_list_are_rejected);
  tcase_add_loop_test(tc, configtest_separates_warnings_from_errors, 0, (int)(sizeof cfgtest_cases / sizeof cfgtest_cases[0]));
  tcase_add_test(tc, configtest_reports_unreadable_file_as_error);
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
