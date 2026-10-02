/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/config/yamlcfg.h"
#include "lib/helper/log.h"
#include "lib/net/netconnect.h"

#include "dipiradiohead/cli/args.h"
#include "dipiradiohead/config.h"
#include "dipiradiohead/config/priv.h"

#define ARGC(argv) (int)(sizeof(argv) / sizeof(argv[0]) - 1) /* -1: drop trailing NULL */

START_TEST(single_input_matches_legacy_defaults) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_inputs, 1u);
  ck_assert_str_eq(cfg.inputs[0].uri, "http://a");
  ck_assert_uint_eq(cfg.inputs[0].sid, 1u);
  ck_assert_str_eq(cfg.inputs[0].sdt_text, "dipiradiohead");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(multi_input_sid_auto_assign_skips_explicit) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "--sid", "5",
                  "-i", "http://b",
                  "-i", "http://c", "--sid", "2",
                  "-m", "239.1.1.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_inputs, 3u);
  ck_assert_uint_eq(cfg.inputs[0].sid, 5u);
  ck_assert_uint_eq(cfg.inputs[1].sid, 1u); /* lowest free id, 5 and 2 are taken */
  ck_assert_uint_eq(cfg.inputs[2].sid, 2u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(multi_input_sdt_auto_default_is_numbered) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-i", "http://b", "-i", "http://c", "-m", "239.1.1.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_str_eq(cfg.inputs[0].sdt_text, "dipiradiohead 1");
  ck_assert_str_eq(cfg.inputs[1].sdt_text, "dipiradiohead 2");
  ck_assert_str_eq(cfg.inputs[2].sdt_text, "dipiradiohead 3");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(paired_sid_and_sdt_apply_to_preceding_input) {
  char *argv[] = {"dipiradiohead",
                  "-i", "http://a", "--sid", "10", "--sdt", "Radio A",
                  "-i", "http://b", "--sid", "20", "--sdt", "Radio B",
                  "-m", "239.1.1.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.inputs[0].sid, 10u);
  ck_assert_str_eq(cfg.inputs[0].sdt_text, "Radio A");
  ck_assert_uint_eq(cfg.inputs[1].sid, 20u);
  ck_assert_str_eq(cfg.inputs[1].sdt_text, "Radio B");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(sid_before_any_input_is_rejected) {
  char *argv[] = {"dipiradiohead", "--sid", "5", "-i", "http://a", "-m", "239.1.1.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(jitter_ms_pairs_with_preceding_input) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "--jitter-ms", "1500", "-i", "http://b", "-m", "239.1.1.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.inputs[0].jitter_ms, 1500u);
  ck_assert_uint_eq(cfg.inputs[1].jitter_ms, 0u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(jitter_ms_out_of_range_or_unpaired_is_rejected) {
  char *zero[] = {"dipiradiohead", "-i", "http://a", "--jitter-ms", "0", "-m", "239.1.1.1:5000", NULL};
  char *big[] = {"dipiradiohead", "-i", "http://a", "--jitter-ms", "10001", "-m", "239.1.1.1:5000", NULL};
  char *early[] = {"dipiradiohead", "--jitter-ms", "100", "-i", "http://a", "-m", "239.1.1.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(zero), zero, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
  cfg = (config_t){0};
  ck_assert_int_eq(args_parse(ARGC(big), big, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
  cfg = (config_t){0};
  ck_assert_int_eq(args_parse(ARGC(early), early, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(sdt_before_any_input_is_rejected) {
  char *argv[] = {"dipiradiohead", "--sdt", "x", "-i", "http://a", "-m", "239.1.1.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(duplicate_explicit_sid_is_rejected) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "--sid", "10", "-i", "http://b", "--sid", "10", "-m", "239.1.1.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(missing_input_is_rejected) {
  char *argv[] = {"dipiradiohead", "-m", "239.1.1.1:5000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(missing_mcast_is_rejected) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(too_many_inputs_is_rejected) {
  char *argv[1 + (RADIOHEAD_MAX_INPUTS + 1) * 2 + 2 + 1];
  int n = 0;
  int i;
  config_t cfg = {0};
  argv[n++] = "dipiradiohead";
  for (i = 0; i < RADIOHEAD_MAX_INPUTS + 1; i++) {
    argv[n++] = "-i";
    argv[n++] = "http://x";
  }
  argv[n++] = "-m";
  argv[n++] = "239.1.1.1:5000";
  argv[n] = NULL;
  ck_assert_int_eq(args_parse(n, argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(biss2_ca_receivers_enables_and_sets_dir) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "--biss2-ca-receivers", "/etc/biss-ca/receivers", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.biss2_ca_enabled, 1);
  ck_assert_str_eq(cfg.biss2_ca_receivers_dir, "/etc/biss-ca/receivers");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(biss2_ca_session_id_parses_hex) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "--biss2-ca-receivers", "/etc/biss-ca/receivers", "--biss2-ca-session-id", "0x1234", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.biss2_ca_session_id_given, 1);
  ck_assert_uint_eq(cfg.biss2_ca_session_id, 0x1234u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(biss2_ca_receivers_mutually_exclusive_with_cas_algo) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "--cas-algo", "cissa", "--biss2-ca-receivers", "/etc/biss-ca/receivers", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(biss2_ca_receivers_mutually_exclusive_with_biss2_sw) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000","--biss2-sw", "00112233445566778899aabbccddeeff", "--biss2-ca-receivers", "/etc/biss-ca/receivers", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(biss2_ca_session_id_without_receivers_is_rejected) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "--biss2-ca-session-id", "1", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_peer_is_repeatable_and_bonded) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "rist://1.2.3.4:6000", "-R", "rist://5.6.7.8:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_rist, 2u);
  ck_assert_str_eq(cfg.rist_uri[0], "rist://1.2.3.4:6000");
  ck_assert_str_eq(cfg.rist_uri[1], "rist://5.6.7.8:6000");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_peer_without_rist_scheme_is_rejected) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "udp://1.2.3.4:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_secret_without_profile_main_is_rejected) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "rist://1.2.3.4:6000", "--rist-secret", "hunter2", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_encryption_type_without_profile_main_is_rejected) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "rist://1.2.3.4:6000", "--rist-encryption-type", "256", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_encryption_type_with_profile_main_is_accepted) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "rist://1.2.3.4:6000", "--rist-profile", "main", "--rist-encryption-type", "256", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rist_key_size, 256);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_encryption_type_invalid_is_rejected) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "rist://1.2.3.4:6000", "--rist-profile", "main", "--rist-encryption-type", "192", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_secret_with_profile_main_is_accepted) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "rist://1.2.3.4:6000", "--rist-profile", "main", "--rist-secret", "hunter2", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.rist_profile, RIST_PROF_MAIN);
  ck_assert_str_eq(cfg.rist_secret, "hunter2");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_peer_is_accepted) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "srt://1.2.3.4:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_srt, 1u);
  ck_assert_str_eq(cfg.srt_host[0], "1.2.3.4");
  ck_assert_uint_eq(cfg.srt_port[0], 6000u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_peer_with_at_is_rejected) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "srt://@1.2.3.4:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_peers_bonded_require_group_mode) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "srt://1.2.3.4:6000", "-R", "srt://5.6.7.8:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_peers_bonded_with_group_mode_is_accepted) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "srt://1.2.3.4:6000", "-R", "srt://5.6.7.8:6000", "--srt-group-mode", "broadcast", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.n_srt, 2u);
  ck_assert_int_eq(cfg.srt_group_mode, SRT_BOND_BROADCAST);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_group_mode_with_single_peer_is_rejected) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "srt://1.2.3.4:6000", "--srt-group-mode", "broadcast", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(rist_and_srt_peers_cannot_mix) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "rist://1.2.3.4:6000", "-R", "srt://5.6.7.8:6000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_passphrase_length_is_validated) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "srt://1.2.3.4:6000", "--srt-passphrase", "short", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(srt_pbkeylen_requires_passphrase) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-R", "srt://1.2.3.4:6000", "--srt-pbkeylen", "16", NULL};
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
  char path[] = "/tmp/dipiradiohead_cfg_XXXXXX";
  char *argv[] = {"dipiradiohead", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "input:\n  - http://a\n  - http://b\nmcast: 239.1.1.1:5000\nrtp: on\nttl: 4\nnit: Net\nmetrics:\n  id: rh\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_uint_eq(cfg.n_inputs, 2u);
  ck_assert_str_eq(cfg.inputs[0].uri, "http://a");
  ck_assert_uint_eq(cfg.inputs[0].sid, 1u);
  ck_assert_str_eq(cfg.inputs[1].uri, "http://b");
  ck_assert_uint_eq(cfg.inputs[1].sid, 2u);
  ck_assert_uint_eq(cfg.mcast_port, 5000u);
  ck_assert_int_eq(cfg.rtp, 1);
  ck_assert_uint_eq(cfg.ttl, 4u);
  ck_assert_str_eq(cfg.metrics_id, "rh");
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_inputs_and_vendors_take_items) {
  char path[] = "/tmp/dipiradiohead_cfg_XXXXXX";
  char *argv[] = {"dipiradiohead", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "input:\n  - http://a:\n      sid: 7\n      sdt: A\n      provider: P\nmcast: 239.1.1.1:5000\ncas:\n  algo: csa2\n  ecmg:\n    - tcp://h1:2222:\n        super-id: 0x1234\n        ecm-id: 5\n        ecmg-version: 3\n        required: on\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_str_eq(cfg.inputs[0].uri, "http://a");
  ck_assert_uint_eq(cfg.inputs[0].sid, 7u);
  ck_assert_str_eq(cfg.inputs[0].sdt_text, "A");
  ck_assert_str_eq(cfg.inputs[0].provider_text, "P");
  ck_assert_uint_eq(cfg.n_cas_vendors, 1u);
  ck_assert_str_eq(cfg.cas_vendors[0].ecmg_host, "h1");
  ck_assert_uint_eq(cfg.cas_vendors[0].ecmg_version, 3u);
  ck_assert_int_eq(cfg.cas_vendors[0].required, 1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_item_without_source_is_error) {
  char path[] = "/tmp/dipiradiohead_cfg_XXXXXX";
  char *argv[] = {"dipiradiohead", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "input:\n  - sid: 3\nmcast: 239.1.1.1:5000\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(cmdline_lists_replace_config_lists) {
  char path[] = "/tmp/dipiradiohead_cfg_XXXXXX";
  char *argv[] = {"dipiradiohead", "-c", path, "-i", "http://c", "--sid", "9", "-R", "srt://127.0.0.1:7000", NULL};
  config_t cfg = {0};
  write_cfg(path, "input:\n  - http://a\n  - http://b\nrist:\n  - rist://h:1\nmcast: 239.1.1.1:5000\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_uint_eq(cfg.n_inputs, 1u);
  ck_assert_str_eq(cfg.inputs[0].uri, "http://c");
  ck_assert_uint_eq(cfg.inputs[0].sid, 9u);
  ck_assert_uint_eq(cfg.n_rist, 0u);
  ck_assert_uint_eq(cfg.n_srt, 1u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(cmdline_scoped_option_needs_its_own_input) {
  char path[] = "/tmp/dipiradiohead_cfg_XXXXXX";
  char *argv[] = {"dipiradiohead", "-c", path, "--sid", "9", NULL};
  config_t cfg = {0};
  write_cfg(path, "input: http://a\nmcast: 239.1.1.1:5000\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_missing_file_is_error) {
  char *argv[] = {"dipiradiohead", "-c", "/nonexistent/dipiradiohead.yaml", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_invalid_value_is_error) {
  char path[] = "/tmp/dipiradiohead_cfg_XXXXXX";
  char *argv[] = {"dipiradiohead", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "input: http://a\nmcast: 239.1.1.1:5000\nttl: 300\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(config_conflict_is_rejected) {
  char path[] = "/tmp/dipiradiohead_cfg_XXXXXX";
  char *argv[] = {"dipiradiohead", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "input: http://a\nmcast: 239.1.1.1:5000\nal-fec: 5:5\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(configtest_reports_by_exit_status) {
  char path[] = "/tmp/dipiradiohead_cfg_XXXXXX";
  char *argv[] = {"dipiradiohead", "--configtest", "-c", path, NULL};
  char *argv2[] = {"dipiradiohead", "--configtest", "-c", "/nonexistent/dipiradiohead.yaml", NULL};
  char *argv3[] = {"dipiradiohead", "--configtest", NULL};
  config_t cfg = {0};
  write_cfg(path, "bogus: 1\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_HELP);
  ck_assert_int_eq(args_parse(ARGC(argv2), argv2, &cfg), ARGS_ERR);
  ck_assert_int_eq(args_parse(ARGC(argv3), argv3, &cfg), ARGS_ERR);
  unlink(path);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_level_is_recorded) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "--metrics-id", "inst1", "--metrics-inspect-ts", "medium", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_MEDIUM);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_defaults_to_off) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "--metrics-id", "inst1", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_OFF);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_requires_metrics_id) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "--metrics-inspect-ts", "basic", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_rejects_unknown_level) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "--metrics-id", "inst1", "--metrics-inspect-ts", "bogus", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_from_yaml) {
  char path[] = "/tmp/dipiradiohead_inspect_XXXXXX";
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "-c", path, NULL};
  config_t cfg = {0};
  write_cfg(path, "metrics:\n  id: a\n  inspect-ts: full\n");
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  unlink(path);
  ck_assert_int_eq(cfg.metrics_inspect_ts, METRICS_INSPECT_TS_FULL);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_pids_parsed) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "--metrics-id", "inst1", "--metrics-inspect-ts", "full", "--metrics-inspect-ts-pids", "0x100,300", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  ck_assert_uint_eq(cfg.metrics_n_known_pids, 2u);
  ck_assert_uint_eq(cfg.metrics_known_pids[0], 256u);
  ck_assert_uint_eq(cfg.metrics_known_pids[1], 300u);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_pids_need_full_level) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "--metrics-id", "inst1", "--metrics-inspect-ts", "basic", "--metrics-inspect-ts-pids", "0x100", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(inspect_ts_pids_reject_bad_list) {
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", "239.1.1.1:5000", "--metrics-id", "inst1", "--metrics-inspect-ts", "full", "--metrics-inspect-ts-pids", "0x100,9000", NULL};
  config_t cfg = {0};
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_ERR);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(mcast_describe_brackets_only_ipv6) {
  static const char *const mcast[] = {"239.1.1.1:5000", "[ff02::1234]:6000"};
  char *argv[] = {"dipiradiohead", "-i", "http://a", "-m", (char *)mcast[_i], NULL};
  config_t cfg = {0};
  char buf[64];
  ck_assert_int_eq(args_parse(ARGC(argv), argv, &cfg), ARGS_OK);
  mcast_describe(&cfg, buf, sizeof buf);
  ck_assert_str_eq(buf, mcast[_i]);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

typedef int (*setter_fn)(void *c, const char *v, char *e, size_t n);

typedef enum { SCOPE_NONE, SCOPE_VENDOR } scope_t;

typedef struct {
  const char *name;
  setter_fn fn;
  scope_t scope;
  const char *value;
  const char *err;
  int check;
  size_t off;
  long expect;
} setter_case_t;

#define CFG_OFF(f) offsetof(config_t, f)
#define VD_OFF(f) offsetof(cas_vendor_t, f)
#define BAD(n, f, sc, v, e) {n, f, sc, v, e, 0, 0, 0}
#define OK(n, f, sc, v) {n, f, sc, v, "", 0, 0, 0}
#define OKU(n, f, sc, v, off, x) {n, f, sc, v, "", 1, off, x}

static const setter_case_t setter_cases[] = {
    BAD("cas ecmg malformed endpoint", rdh_apply_cas_ecmg, SCOPE_NONE, "not an endpoint", "invalid 'not an endpoint' (tcp://host:port)"),
    OKU("cas ecmg adds a vendor", rdh_apply_cas_ecmg, SCOPE_NONE, "tcp://h1:2222", CFG_OFF(n_cas_vendors), 1),
    OKU("cas ecmg sets cas flag", rdh_apply_cas_ecmg, SCOPE_NONE, "tcp://h1:2222", CFG_OFF(any_cas_flag), 1),
    BAD("cas required outside vendor", rdh_apply_cas_required, SCOPE_NONE, "on", "only valid inside a cas.ecmg list"),
    BAD("cas required invalid", rdh_apply_cas_required, SCOPE_VENDOR, "perhaps", "invalid boolean 'perhaps'"),
    OKU("cas required on", rdh_apply_cas_required, SCOPE_VENDOR, "yes", VD_OFF(required), 1),
    OKU("cas required off", rdh_apply_cas_required, SCOPE_VENDOR, "off", VD_OFF(required), 0),
    BAD("color bogus", rdh_apply_color, SCOPE_NONE, "bogus", "invalid 'bogus' (auto|always|never)"),
    OKU("color always", rdh_apply_color, SCOPE_NONE, "always", CFG_OFF(color_mode), LOG_COLOR_ALWAYS),
    OKU("color never", rdh_apply_color, SCOPE_NONE, "never", CFG_OFF(color_mode), LOG_COLOR_NEVER),
    OKU("color auto", rdh_apply_color, SCOPE_NONE, "auto", CFG_OFF(color_mode), LOG_COLOR_AUTO),
    BAD("daemonize maybe", rdh_apply_daemonize, SCOPE_NONE, "maybe", "invalid boolean 'maybe'"),
    OKU("daemonize yes", rdh_apply_daemonize, SCOPE_NONE, "yes", CFG_OFF(daemonize), 1),
    OKU("daemonize off", rdh_apply_daemonize, SCOPE_NONE, "off", CFG_OFF(daemonize), 0),
    BAD("dscp unknown name", rdh_apply_dscp, SCOPE_NONE, "urgent", "invalid 'urgent' (video-high|video-low|voice|signalling|best-effort|0..63)"),
    BAD("dscp above 63", rdh_apply_dscp, SCOPE_NONE, "64", "invalid '64' (video-high|video-low|voice|signalling|best-effort|0..63)"),
    OKU("dscp video-high", rdh_apply_dscp, SCOPE_NONE, "video-high", CFG_OFF(dscp), NET_DSCP_VIDEO_HIGH),
    OKU("dscp voice", rdh_apply_dscp, SCOPE_NONE, "voice", CFG_OFF(dscp), NET_DSCP_VOICE_BEARER),
    OKU("dscp best-effort", rdh_apply_dscp, SCOPE_NONE, "best-effort", CFG_OFF(dscp), NET_DSCP_BEST_EFFORT),
    OKU("dscp numeric", rdh_apply_dscp, SCOPE_NONE, "46", CFG_OFF(dscp), 46 << 2),
    OKU("dscp zero", rdh_apply_dscp, SCOPE_NONE, "0", CFG_OFF(dscp), 0),
    BAD("al-fec not a pair", rdh_apply_al_fec, SCOPE_NONE, "5", "invalid '5' (want L:D, L*D<=400, L<=40)"),
    BAD("al-fec matrix too large", rdh_apply_al_fec, SCOPE_NONE, "40:11", "invalid '40:11' (want L:D, L*D<=400, L<=40)"),
    BAD("al-fec columns above 40", rdh_apply_al_fec, SCOPE_NONE, "41:1", "invalid '41:1' (want L:D, L*D<=400, L<=40)"),
    OKU("al-fec columns", rdh_apply_al_fec, SCOPE_NONE, "10:4", CFG_OFF(al_fec_l), 10),
    OKU("al-fec rows", rdh_apply_al_fec, SCOPE_NONE, "10:4", CFG_OFF(al_fec_d), 4),
    BAD("al-fec-port zero", rdh_apply_al_fec_port, SCOPE_NONE, "0", "invalid '0' (need 1..65535)"),
    BAD("al-fec-port too large", rdh_apply_al_fec_port, SCOPE_NONE, "65536", "invalid '65536' (need 1..65535)"),
    OKU("al-fec-port", rdh_apply_al_fec_port, SCOPE_NONE, "5004", CFG_OFF(al_fec_port), 5004),
};

static void setup_scope(config_t *cfg, scope_t scope) {
  char e[128];

  rdh_cfg_defaults(cfg);
  if (scope == SCOPE_VENDOR) {
    ck_assert_int_eq(rdh_item_hook(cfg, "cas.ecmg", 1, e, sizeof e), 0);
    ck_assert_int_eq(rdh_apply_cas_ecmg(cfg, "tcp://127.0.0.1:2222", e, sizeof e), 0);
  }
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
    if (c->check) {
      const char *base = c->scope == SCOPE_VENDOR ? (const char *)&cfg.cas_vendors[0] : (const char *)&cfg;
      unsigned got = *(const unsigned *)(base + c->off);

      ck_assert_msg((long)got == c->expect, "%s: stored %u, want %ld", c->name, got, c->expect);
    }
  }
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(default_provider_is_stored_and_bounded) {
  config_t cfg;
  char e[64];
  char big[sizeof cfg.default_provider_text + 1];

  rdh_cfg_defaults(&cfg);
  ck_assert_int_eq(rdh_apply_default_provider(&cfg, "Provider X", e, sizeof e), 0);
  ck_assert_str_eq(cfg.default_provider_text, "Provider X");
  memset(big, 'x', sizeof big - 1);
  big[sizeof big - 1] = '\0';
  ck_assert_int_eq(rdh_apply_default_provider(&cfg, big, e, sizeof e), -1);
  ck_assert_str_eq(e, "too long (max 255)");
  ck_assert_str_eq(cfg.default_provider_text, "Provider X");
}
END_TEST

typedef struct {
  const char *name;
  const char *yaml;
} warn_case_t;

static const warn_case_t warn_cases[] = {
    {"metrics sock without id", "input: http://a\nmcast: 239.1.1.1:5000\nmetrics:\n  sock: /run/x.sock\n"},
    {"metrics interval without id", "input: http://a\nmcast: 239.1.1.1:5000\nmetrics:\n  interval: 5\n"},
    {"inspect-ts without id", "input: http://a\nmcast: 239.1.1.1:5000\nmetrics:\n  inspect-ts: basic\n"},
    {"al-fec without port", "input: http://a\nmcast: 239.1.1.1:5000\nrtp: on\nal-fec: 5:5\n"},
    {"al-fec-port without al-fec", "input: http://a\nmcast: 239.1.1.1:5000\nal-fec-port: 5004\n"},
    {"al-fec without rtp", "input: http://a\nmcast: 239.1.1.1:5000\nal-fec: 5:5\nal-fec-port: 5004\n"},
    {"al-fec without mcast", "input: http://a\nremote: rist://h:1\nrtp: on\nal-fec: 5:5\nal-fec-port: 5004\n"},
    {"rist settings without peer", "input: http://a\nmcast: 239.1.1.1:5000\nrist:\n  cname: x\n"},
    {"rist secret without main profile", "input: http://a\nremote: rist://h:1\nrist:\n  secret: s3cretsecret\n"},
    {"rist encryption type without main profile", "input: http://a\nremote: rist://h:1\nrist:\n  encryption-type: 256\n"},
    {"several srt peers without group mode", "input: http://a\nremote:\n  - srt://1.2.3.4:6000\n  - srt://1.2.3.5:6000\n"},
    {"group mode with single srt peer", "input: http://a\nremote: srt://1.2.3.4:6000\nsrt:\n  group-mode: backup\n"},
    {"srt settings without peer", "input: http://a\nmcast: 239.1.1.1:5000\nsrt:\n  latency: 200\n"},
    {"srt passphrase too short", "input: http://a\nremote: srt://1.2.3.4:6000\nsrt:\n  passphrase: short\n"},
    {"srt pbkeylen without passphrase", "input: http://a\nremote: srt://1.2.3.4:6000\nsrt:\n  pbkeylen: 16\n"},
    {"cas options without algo", "input: http://a\nmcast: 239.1.1.1:5000\ncas:\n  cp-duration: 5000\n"},
};

START_TEST(configtest_flags_each_conflicting_setting) {
  const warn_case_t *c = &warn_cases[_i];
  char path[] = "/tmp/dipiradiohead_cfgtest_XXXXXX";
  int lenient;
  int strict;

  write_cfg(path, c->yaml);
  lenient = rdh_cfg_test(path, 0);
  strict = rdh_cfg_test(path, 1);
  unlink(path);
  ck_assert_msg(lenient == 0, "%s: lenient result %d", c->name, lenient);
  ck_assert_msg(strict == -1, "%s: strict result %d", c->name, strict);
}
END_TEST

START_TEST(configtest_accepts_clean_file) {
  char path[] = "/tmp/dipiradiohead_cfgtest_XXXXXX";

  write_cfg(path, "input: http://a\nmcast: 239.1.1.1:5000\n");
  ck_assert_int_eq(rdh_cfg_test(path, 1), 0);
  unlink(path);
}
END_TEST

static Suite *args_suite(void) {
  Suite *s = suite_create("args");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, single_input_matches_legacy_defaults);
  tcase_add_test(tc, multi_input_sid_auto_assign_skips_explicit);
  tcase_add_test(tc, multi_input_sdt_auto_default_is_numbered);
  tcase_add_test(tc, jitter_ms_pairs_with_preceding_input);
  tcase_add_test(tc, jitter_ms_out_of_range_or_unpaired_is_rejected);
  tcase_add_test(tc, paired_sid_and_sdt_apply_to_preceding_input);
  tcase_add_test(tc, sid_before_any_input_is_rejected);
  tcase_add_test(tc, sdt_before_any_input_is_rejected);
  tcase_add_test(tc, duplicate_explicit_sid_is_rejected);
  tcase_add_test(tc, missing_input_is_rejected);
  tcase_add_test(tc, missing_mcast_is_rejected);
  tcase_add_test(tc, too_many_inputs_is_rejected);
  tcase_add_loop_test(tc, mcast_describe_brackets_only_ipv6, 0, 2);
  tcase_add_loop_test(tc, setters_validate_values_and_report_specific_errors, 0, (int)(sizeof setter_cases / sizeof setter_cases[0]));
  tcase_add_test(tc, default_provider_is_stored_and_bounded);
  tcase_add_loop_test(tc, configtest_flags_each_conflicting_setting, 0, (int)(sizeof warn_cases / sizeof warn_cases[0]));
  tcase_add_test(tc, configtest_accepts_clean_file);
  tcase_add_test(tc, biss2_ca_receivers_enables_and_sets_dir);
  tcase_add_test(tc, biss2_ca_session_id_parses_hex);
  tcase_add_test(tc, biss2_ca_receivers_mutually_exclusive_with_cas_algo);
  tcase_add_test(tc, biss2_ca_receivers_mutually_exclusive_with_biss2_sw);
  tcase_add_test(tc, biss2_ca_session_id_without_receivers_is_rejected);
  tcase_add_test(tc, rist_peer_is_repeatable_and_bonded);
  tcase_add_test(tc, rist_peer_without_rist_scheme_is_rejected);
  tcase_add_test(tc, rist_secret_without_profile_main_is_rejected);
  tcase_add_test(tc, rist_secret_with_profile_main_is_accepted);
  tcase_add_test(tc, rist_encryption_type_without_profile_main_is_rejected);
  tcase_add_test(tc, rist_encryption_type_with_profile_main_is_accepted);
  tcase_add_test(tc, rist_encryption_type_invalid_is_rejected);
  tcase_add_test(tc, srt_peer_is_accepted);
  tcase_add_test(tc, srt_peer_with_at_is_rejected);
  tcase_add_test(tc, srt_peers_bonded_require_group_mode);
  tcase_add_test(tc, srt_peers_bonded_with_group_mode_is_accepted);
  tcase_add_test(tc, srt_group_mode_with_single_peer_is_rejected);
  tcase_add_test(tc, rist_and_srt_peers_cannot_mix);
  tcase_add_test(tc, srt_passphrase_length_is_validated);
  tcase_add_test(tc, srt_pbkeylen_requires_passphrase);
  tcase_add_test(tc, config_file_provides_settings);
  tcase_add_test(tc, config_inputs_and_vendors_take_items);
  tcase_add_test(tc, config_item_without_source_is_error);
  tcase_add_test(tc, cmdline_lists_replace_config_lists);
  tcase_add_test(tc, cmdline_scoped_option_needs_its_own_input);
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
