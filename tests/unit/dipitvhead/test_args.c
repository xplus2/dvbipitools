/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dipitvhead/args.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(single_input_matches_legacy_defaults) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_inputs, 1u);
  ck_assert_uint_eq(cfg.inputs[0].sid, 1u);
  ck_assert_int_eq(cfg.inputs[0].sdt_mode, TABLE_PASSTHROUGH);
  ck_assert_uint_eq(cfg.inputs[0].pmt_pid, 0u);
}
END_TEST

START_TEST(multi_input_sid_auto_assign_skips_explicit) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "--sid", "5",
                  "-i", "udp://@239.1.1.2:5000",
                  "-i", "udp://@239.1.1.3:5000", "--sid", "2",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_inputs, 3u);
  ck_assert_uint_eq(cfg.inputs[0].sid, 5u);
  ck_assert_uint_eq(cfg.inputs[1].sid, 1u); /* lowest free id, 5 and 2 are taken */
  ck_assert_uint_eq(cfg.inputs[2].sid, 2u);
}
END_TEST

START_TEST(paired_options_apply_to_preceding_input) {
  char *argv[] = {"dipitvhead",
                  "-i", "udp://@239.1.1.1:5000", "--sid", "10", "-s", "Channel A", "-p", "0x0100",
                  "-i", "udp://@239.1.1.2:5000", "--sid", "20", "-s", "Channel B", "--strip-eit",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.inputs[0].sid, 10u);
  ck_assert_int_eq(cfg.inputs[0].sdt_mode, TABLE_OVERRIDE);
  ck_assert_str_eq(cfg.inputs[0].sdt_text, "Channel A");
  ck_assert_uint_eq(cfg.inputs[0].pmt_pid, 0x0100u);
  ck_assert_int_eq(cfg.inputs[0].strip_eit, 0);
  ck_assert_uint_eq(cfg.inputs[1].sid, 20u);
  ck_assert_str_eq(cfg.inputs[1].sdt_text, "Channel B");
  ck_assert_int_eq(cfg.inputs[1].strip_eit, 1);
}
END_TEST

START_TEST(hbbtv_triplet_pairs_with_preceding_input) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000",
                  "--hbbtv", "http://example.invalid/app.html", "--hbbtv-org-id", "1", "--hbbtv-app-id", "2",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.inputs[0].hbbtv_url, "http://example.invalid/app.html");
  ck_assert_uint_eq(cfg.inputs[0].hbbtv_org_id, 1u);
  ck_assert_uint_eq(cfg.inputs[0].hbbtv_app_id, 2u);
}
END_TEST

START_TEST(hbbtv_url_without_org_or_app_id_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000",
                  "--hbbtv", "http://example.invalid/app.html",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(hbbtv_org_id_without_url_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000",
                  "--hbbtv-org-id", "1", "--hbbtv-app-id", "2",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(sid_before_any_input_is_rejected) {
  char *argv[] = {"dipitvhead", "--sid", "5", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(sdt_before_any_input_is_rejected) {
  char *argv[] = {"dipitvhead", "-s", "x", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(pmt_pid_before_any_input_is_rejected) {
  char *argv[] = {"dipitvhead", "-p", "0x0100", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(iface_before_any_input_is_rejected) {
  char *argv[] = {"dipitvhead", "-I", "eth0", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(duplicate_explicit_sid_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "--sid", "10",
                  "-i", "udp://@239.1.1.2:5000", "--sid", "10",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(missing_input_is_rejected) {
  char *argv[] = {"dipitvhead", "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(missing_mcast_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(too_many_inputs_is_rejected) {
  char *argv[1 + (ARGS_MAX_INPUTS + 1) * 2 + 2 + 1];
  int n = 0;
  config_t cfg;
  argv[n++] = "dipitvhead";
  for (int i = 0; i < ARGS_MAX_INPUTS + 1; i++) {
    argv[n++] = "-i";
    argv[n++] = "udp://@239.1.1.1:5000";
  }
  argv[n++] = "-m";
  argv[n++] = "239.1.2.1:5000";
  argv[n] = NULL;
  ck_assert_int_eq(args_parse(n, argv, &cfg), ARGS_ERR);
}
END_TEST

/* mux-wide flags stay global, not per-input: NIT, tsid/onid, bitrate pacing, CAS */
START_TEST(global_flags_are_not_per_input) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-i", "udp://@239.1.1.2:5000",
                  "-n", "My Network", "--tsid", "7", "--onid", "8", "-b", "5000", "-S",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.nit_mode, TABLE_OVERRIDE);
  ck_assert_str_eq(cfg.nit_text, "My Network");
  ck_assert_uint_eq(cfg.tsid, 7u);
  ck_assert_uint_eq(cfg.onid, 8u);
  ck_assert_uint_eq(cfg.bitrate_kbps, 5000u);
  ck_assert_int_eq(cfg.stuff, 1);
}
END_TEST

START_TEST(biss2_ca_receivers_enables_and_sets_dir) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "--biss2-ca-receivers", "/etc/biss-ca/receivers", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.biss2_ca_enabled, 1);
  ck_assert_str_eq(cfg.biss2_ca_receivers_dir, "/etc/biss-ca/receivers");
  ck_assert_int_eq(cfg.biss2_ca_session_id_given, 0);
}
END_TEST

START_TEST(biss2_ca_session_id_parses_hex_and_dec) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "--biss2-ca-receivers", "/etc/biss-ca/receivers",
                  "--biss2-ca-session-id", "0x1234", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.biss2_ca_session_id_given, 1);
  ck_assert_uint_eq(cfg.biss2_ca_session_id, 0x1234u);
}
END_TEST

START_TEST(biss2_ca_session_id_without_receivers_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "--biss2-ca-session-id", "1", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(biss2_ca_receivers_rejects_out_of_range_session_id) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "--biss2-ca-receivers", "/etc/biss-ca/receivers",
                  "--biss2-ca-session-id", "0x10000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(biss2_ca_receivers_mutually_exclusive_with_cas_algo) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "--cas-algo", "cissa",
                  "--biss2-ca-receivers", "/etc/biss-ca/receivers", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(biss2_ca_receivers_mutually_exclusive_with_biss2_sw) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "--biss2-sw", "00112233445566778899aabbccddeeff",
                  "--biss2-ca-receivers", "/etc/biss-ca/receivers", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(biss2_ca_receivers_defaults_cas_pids_to_video_audio) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "--biss2-ca-receivers", "/etc/biss-ca/receivers", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.cas_pids_video, 1);
  ck_assert_int_eq(cfg.cas_pids_audio, 1);
}
END_TEST

START_TEST(rist_peer_is_repeatable_and_bonded) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "-R", "rist://1.2.3.4:6000", "-R", "rist://5.6.7.8:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_rist, 2u);
  ck_assert_str_eq(cfg.rist_uri[0], "rist://1.2.3.4:6000");
  ck_assert_str_eq(cfg.rist_uri[1], "rist://5.6.7.8:6000");
}
END_TEST

START_TEST(rist_peer_without_rist_scheme_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "-R", "udp://1.2.3.4:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rist_and_mcast_output_coexist) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "-R", "rist://1.2.3.4:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.mcast_group, "239.1.2.1");
  ck_assert_uint_eq(cfg.n_rist, 1u);
}
END_TEST

START_TEST(rist_secret_without_profile_main_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "-R", "rist://1.2.3.4:6000", "--rist-secret", "hunter2", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rist_secret_with_profile_main_is_accepted) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "-R", "rist://1.2.3.4:6000", "--rist-profile", "main", "--rist-secret", "hunter2", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rist_profile, RIST_PROF_MAIN);
  ck_assert_str_eq(cfg.rist_secret, "hunter2");
}
END_TEST

START_TEST(rist_input_with_at_is_accepted) {
  char *argv[] = {"dipitvhead", "-i", "rist://@127.0.0.1:6000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.inputs[0].input.kind, SRC_RIST);
  ck_assert_str_eq(cfg.inputs[0].input.rist_uri, "rist://@127.0.0.1:6000");
}
END_TEST

START_TEST(rist_input_without_at_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "rist://127.0.0.1:6000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rist_profile_in_pairs_with_preceding_input) {
  char *argv[] = {"dipitvhead",
                  "-i", "udp://@239.1.1.1:5000",
                  "-i", "rist://@127.0.0.1:6000", "--rist-profile-in", "main",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.inputs[0].rist_profile_main, 0);
  ck_assert_int_eq(cfg.inputs[1].rist_profile_main, 1);
}
END_TEST

START_TEST(rist_profile_in_before_any_input_is_rejected) {
  char *argv[] = {"dipitvhead", "--rist-profile-in", "main", "-i", "rist://@127.0.0.1:6000",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(more_than_one_rist_input_is_rejected) {
  /* librist isn't safe with more than one rist_ctx per process */
  char *argv[] = {"dipitvhead",
                  "-i", "rist://@127.0.0.1:6000",
                  "-i", "rist://@127.0.0.1:6002",
                  "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rist_input_and_rist_output_together_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "rist://@127.0.0.1:6000", "-R", "rist://1.2.3.4:6000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(srt_input_listen_is_accepted) {
  char *argv[] = {"dipitvhead", "-i", "srt://@127.0.0.1:6000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.inputs[0].input.kind, SRC_SRT);
  ck_assert_int_eq(cfg.inputs[0].input.srt_listen, 1);
  ck_assert_str_eq(cfg.inputs[0].input.srt_host, "127.0.0.1");
  ck_assert_uint_eq(cfg.inputs[0].input.srt_port, 6000);
}
END_TEST

START_TEST(srt_input_caller_is_accepted) {
  char *argv[] = {"dipitvhead", "-i", "srt://127.0.0.1:6000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.inputs[0].input.srt_listen, 0);
}
END_TEST

START_TEST(srt_passphrase_in_pairs_with_preceding_input) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-i", "srt://@127.0.0.1:6000", "--srt-passphrase-in", "0123456789", "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.inputs[0].srt_passphrase_in, "");
  ck_assert_str_eq(cfg.inputs[1].srt_passphrase_in, "0123456789");
}
END_TEST

START_TEST(srt_passphrase_in_before_any_input_is_rejected) {
  char *argv[] = {"dipitvhead", "--srt-passphrase-in", "0123456789", "-i", "srt://@127.0.0.1:6000", "-m", "239.1.2.1:5000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(srt_output_caller_is_accepted) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-R", "srt://1.2.3.4:7000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_srt, 1u);
  ck_assert_str_eq(cfg.srt_host[0], "1.2.3.4");
  ck_assert_uint_eq(cfg.srt_port[0], 7000);
}
END_TEST

START_TEST(srt_output_listen_is_rejected) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-R", "srt://@1.2.3.4:7000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(srt_peers_bonded_require_group_mode) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-R", "srt://1.2.3.4:7000", "-R", "srt://5.6.7.8:7000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(srt_peers_bonded_with_group_mode_is_accepted) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-R", "srt://1.2.3.4:7000", "-R", "srt://5.6.7.8:7000", "--srt-group-mode", "backup", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_srt, 2u);
  ck_assert_int_eq(cfg.srt_group_mode, SRT_BOND_BACKUP);
}
END_TEST

START_TEST(rist_and_srt_output_peers_cannot_mix) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-R", "rist://1.2.3.4:7000", "-R", "srt://5.6.7.8:7000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(srt_input_and_srt_output_together_is_accepted) {
  /* unlike rist://, srt has no per-process context limit */
  char *argv[] = {"dipitvhead", "-i", "srt://@127.0.0.1:6000", "-R", "srt://1.2.3.4:7000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
}
END_TEST

START_TEST(srt_passphrase_length_is_validated) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-R", "srt://1.2.3.4:7000", "--srt-passphrase", "short", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(srt_pbkeylen_requires_passphrase) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-R", "srt://1.2.3.4:7000", "--srt-pbkeylen", "16", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(rist_options_without_any_rist_peer_are_rejected_by_profile_check_only) {
  /* --rist-secret without --rist-profile main still fails validation even with no -R;
     the no-op warning (logged, not fatal) doesn't change ARGS_OK/ARGS_ERR here */
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000",
                  "--rist-buffer", "500", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_rist, 0u);
  ck_assert_uint_eq(cfg.rist_buffer_ms, 500u);
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
  config_t cfg;
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
}
END_TEST

START_TEST(config_inputs_take_keyed_items) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "-c", path, NULL};
  config_t cfg;
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
}
END_TEST

START_TEST(config_input_srt_keys_pair_with_their_input) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "-c", path, NULL};
  config_t cfg;
  write_cfg(path, "input:\n  - srt://1.2.3.4:7000:\n      srt-passphrase-in: 0123456789ab\n      srt-latency-in: 200\nmcast: 239.1.3.1:5000\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_str_eq(cfg.inputs[0].srt_passphrase_in, "0123456789ab");
  ck_assert_uint_eq(cfg.inputs[0].srt_latency_in_ms, 200u);
}
END_TEST

START_TEST(config_cas_ecmg_takes_keyed_items) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "-c", path, NULL};
  config_t cfg;
  write_cfg(path, "input: udp://@239.1.1.1:5000\nmcast: 239.1.3.1:5000\ncas:\n  algo: csa2\n  ecmg:\n    - tcp://h1:2222:\n        super-id: 0x1234\n        ecm-id: 5\n        ecmg-version: 3\n        required: on\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_uint_eq(cfg.n_cas_vendors, 1u);
  ck_assert_str_eq(cfg.cas_vendors[0].ecmg_host, "h1");
  ck_assert_uint_eq(cfg.cas_vendors[0].ecmg_version, 3u);
  ck_assert_int_eq(cfg.cas_vendors[0].required, 1);
}
END_TEST

START_TEST(config_item_without_source_is_error) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "-c", path, NULL};
  config_t cfg;
  write_cfg(path, "input:\n  - sid: 3\nmcast: 239.1.3.1:5000\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
}
END_TEST

START_TEST(cmdline_lists_replace_config_lists) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "-c", path, "-i", "udp://@239.9.9.9:5000", "--sid", "9", "-R", "srt://127.0.0.1:7000", NULL};
  config_t cfg;
  write_cfg(path, "input:\n  - udp://@239.1.1.1:5000\n  - udp://@239.1.1.2:5000\nrist:\n  - rist://h:1\nmcast: 239.1.3.1:5000\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_uint_eq(cfg.n_inputs, 1u);
  ck_assert_uint_eq(cfg.inputs[0].sid, 9u);
  ck_assert_uint_eq(cfg.n_rist, 0u);
  ck_assert_uint_eq(cfg.n_srt, 1u);
}
END_TEST

START_TEST(cmdline_scoped_option_needs_its_own_input) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "-c", path, "--sid", "9", NULL};
  config_t cfg;
  write_cfg(path, "input: udp://@239.1.1.1:5000\nmcast: 239.1.3.1:5000\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
}
END_TEST

START_TEST(cmdline_wins_over_config) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "-c", path, "-T", "9", NULL};
  config_t cfg;
  write_cfg(path, "input: udp://@239.1.1.1:5000\nmcast: 239.1.3.1:5000\nttl: 4\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_uint_eq(cfg.ttl, 9u);
}
END_TEST

START_TEST(config_missing_file_is_error) {
  char *argv[] = {"dipitvhead", "-c", "/nonexistent/dipitvhead.yaml", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(config_invalid_value_is_error) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "-c", path, NULL};
  config_t cfg;
  write_cfg(path, "input: udp://@239.1.1.1:5000\nmcast: 239.1.3.1:5000\nttl: 300\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
}
END_TEST

START_TEST(config_conflict_is_rejected) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "-c", path, NULL};
  config_t cfg;
  write_cfg(path, "input: udp://@239.1.1.1:5000\nmcast: 239.1.3.1:5000\nal-fec: 5:5\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
}
END_TEST

START_TEST(configtest_reports_by_exit_status) {
  char path[] = "/tmp/dipitvhead_cfg_XXXXXX";
  char *argv[] = {"dipitvhead", "--configtest", "-c", path, NULL};
  char *argv2[] = {"dipitvhead", "--configtest", "-c", "/nonexistent/dipitvhead.yaml", NULL};
  config_t cfg;
  write_cfg(path, "bogus: 1\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
  ck_assert_int_eq(args_parse(ARGC(argv2), argv2, &cfg), ARGS_ERR);
  unlink(path);
}
END_TEST

START_TEST(inspect_ts_level_is_recorded) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--metrics-id", "inst1", "--metrics-inspect-ts", "medium", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_MEDIUM);
}
END_TEST

START_TEST(inspect_ts_defaults_to_off) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--metrics-id", "inst1", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_OFF);
}
END_TEST

START_TEST(inspect_ts_requires_metrics_id) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--metrics-inspect-ts", "basic", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(inspect_ts_rejects_unknown_level) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--metrics-id", "inst1", "--metrics-inspect-ts", "bogus", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(inspect_ts_from_yaml) {
  char path[] = "/tmp/dipitvhead_inspect_XXXXXX";
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "-c", path, NULL};
  config_t cfg;
  write_cfg(path, "metrics:\n  id: a\n  inspect-ts: full\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_FULL);
}
END_TEST

START_TEST(inspect_ts_pids_parsed) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--metrics-id", "inst1", "--metrics-inspect-ts", "full", "--metrics-inspect-ts-pids", "0x100,300", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.metrics_n_known_pids, 2u);
  ck_assert_uint_eq(cfg.metrics_known_pids[0], 256u);
  ck_assert_uint_eq(cfg.metrics_known_pids[1], 300u);
}
END_TEST

START_TEST(inspect_ts_pids_need_full_level) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--metrics-id", "inst1", "--metrics-inspect-ts", "basic", "--metrics-inspect-ts-pids", "0x100", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
}
END_TEST

START_TEST(inspect_ts_pids_reject_bad_list) {
  char *argv[] = {"dipitvhead", "-i", "udp://@239.1.1.1:5000", "-m", "239.1.2.1:5000", "--metrics-id", "inst1", "--metrics-inspect-ts", "full", "--metrics-inspect-ts-pids", "0x100,9000", NULL};
  config_t cfg;
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
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
