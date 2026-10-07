/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "../cfg_fixture.h"
#include "dipirec/config.h"
#include "dipirec/filter/ts.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/log.h"

#define A64 "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#define A128 A64 A64
#define A256 A128 A128

static const cfg_field_case_t field_cases[] = {
  CFG_FIELD(config_t, "in: udp://@239.1.2.3:5000\n", CFG_UINT, source.port, 5000, NULL),
  CFG_FIELD(config_t, "in: rtp://@239.1.2.3:5000\n", CFG_INT, source.rtp_wrapped, 1, NULL),
  CFG_FIELD(config_t, "in: udp://@239.1.2.3:5000\n", CFG_INT, fl.have_in, 1, NULL),
  CFG_FIELD(config_t, "out: out.ts\n", CFG_INT, n_out, 1, NULL),
  CFG_FIELD(config_t, "out: out.ts\n", CFG_INT, out[0].kind, OUT_FILE, NULL),
  CFG_FIELD(config_t, "out:\n  - a.ts\n  - rtp://239.1.2.3:5000\n", CFG_INT, n_out, 2, NULL),
  CFG_FIELD(config_t, "out:\n  - a.ts\n  - rtp://239.1.2.3:5000\n", CFG_INT, out[1].kind, OUT_RTP, NULL),
  CFG_FIELD(config_t, "audio: 2\n", CFG_UINT, audio_track, 2, NULL),
  CFG_FIELD(config_t, "audio: 2\n", CFG_INT, audio_all, 0, NULL),
  CFG_FIELD(config_t, "audio: all\n", CFG_INT, audio_all, 1, NULL),
  CFG_FIELD(config_t, "format: mkv\n", CFG_INT, format, FMT_MKV, NULL),
  CFG_FIELD(config_t, "format: m4a\n", CFG_INT, format, FMT_M4A, NULL),
  CFG_FIELD(config_t, "pmt-pid: 0x100\n", CFG_UINT, pmt_pid, 256, NULL),
  CFG_FIELD(config_t, "pmt-pid: 0x100\n", CFG_INT, pmt_sel, PMT_SEL_PID, NULL),
  CFG_FIELD(config_t, "pmt-pid: all\n", CFG_INT, pmt_sel, PMT_SEL_ALL, NULL),
  CFG_FIELD(config_t, "subtitles: srt\n", CFG_INT, subs, SUB_SRT, NULL),
  CFG_FIELD(config_t, "subtitles: strip\n", CFG_INT, subs, SUB_STRIP, NULL),
  CFG_FIELD(config_t, "time: 90\n", CFG_LONG, duration_s, 90, NULL),
  CFG_FIELD(config_t, "time: 5m30s\n", CFG_LONG, duration_s, 330, NULL),
  CFG_FIELD(config_t, "iface: eth7\n", CFG_STRPTR, iface_in, 0, "eth7"),
  CFG_FIELD(config_t, "out-iface: eth8\n", CFG_STRPTR, iface_out, 0, "eth8"),
  CFG_FIELD(config_t, "ttl: 12\n", CFG_INT, out_ttl, 12, NULL),
  CFG_FIELD(config_t, "al-fec: 10:5\n", CFG_UINT, al_fec_l, 10, NULL),
  CFG_FIELD(config_t, "al-fec: 10:5\n", CFG_UINT, al_fec_d, 5, NULL),
  CFG_FIELD(config_t, "al-fec-port: 6004\n", CFG_UINT, al_fec_port, 6004, NULL),
  CFG_FIELD(config_t, "rist:\n  profile: main\n", CFG_INT, rist_profile, RIST_PROF_MAIN, NULL),
  CFG_FIELD(config_t, "rist:\n  secret: s3cret\n", CFG_CHARARR, rist_secret, 0, "s3cret"),
  CFG_FIELD(config_t, "rist:\n  secret: s3cret\n", CFG_INT, fl.have_secret, 1, NULL),
  CFG_FIELD(config_t, "rist:\n  encryption-type: 256\n", CFG_INT, rist_key_size, 256, NULL),
  CFG_FIELD(config_t, "rist:\n  cname: cam1\n", CFG_CHARARR, rist_cname, 0, "cam1"),
  CFG_FIELD(config_t, "rist:\n  buffer: 750\n", CFG_UINT, rist_buffer_ms, 750, NULL),
  CFG_FIELD(config_t, "rist:\n  profile-in: main\n", CFG_INT, rist_profile_in, RIST_PROF_MAIN, NULL),
  CFG_FIELD(config_t, "rist:\n  encryption-type-in: 128\n", CFG_INT, rist_key_size_in, 128, NULL),
  CFG_FIELD(config_t, "insecure: true\n", CFG_INT, insecure_tls, 1, NULL),
  CFG_FIELD(config_t, "verbose: on\n", CFG_INT, verbose, 1, NULL),
  CFG_FIELD(config_t, "sub-lead: 2500\n", CFG_LONG, sub_lead_ms, 2500, NULL),
  CFG_FIELD(config_t, "color: never\n", CFG_INT, color_mode, LOG_COLOR_NEVER, NULL),
  CFG_FIELD(config_t, "pace: yes\n", CFG_INT, pace, 1, NULL),
  CFG_FIELD(config_t, "strip: LCEVC\n", CFG_UINT, strip_mask, STRIP_LCEVC, NULL),
  CFG_FIELD(config_t, "strip: none\n", CFG_UINT, strip_mask, 0, NULL),
  CFG_FIELD(config_t, "metrics:\n  sock: /tmp/m.sock\n", CFG_STRPTR, metrics_sock, 0, "/tmp/m.sock"),
  CFG_FIELD(config_t, "metrics:\n  id: rec1\n", CFG_STRPTR, metrics_id, 0, "rec1"),
  CFG_FIELD(config_t, "metrics:\n  interval: 42\n", CFG_UINT, metrics_interval_s, 42, NULL),
  CFG_FIELD(config_t, "metrics:\n  inspect-ts: full\n", CFG_INT, metrics_inspect_ts, METRICS_INSPECT_TS_FULL, NULL),
  CFG_FIELD(config_t, "metrics:\n  inspect-ts-pids: 256,512\n", CFG_UINT, metrics_n_known_pids, 2, NULL),
  CFG_FIELD(config_t, "ret:\n  addr: 10.0.0.1:6000\n", CFG_INT, ret.enabled, 1, NULL),
  CFG_FIELD(config_t, "ret:\n  addr: 10.0.0.1:6000\n", CFG_CHARARR, ret.addr, 0, "10.0.0.1"),
  CFG_FIELD(config_t, "ret:\n  addr: 10.0.0.1:6000\n", CFG_UINT, ret.port, 6000, NULL),
  CFG_FIELD(config_t, "ret:\n  no-mc: true\n", CFG_INT, ret.mc_enabled, 0, NULL),
  CFG_FIELD(config_t, "ret:\n  mc-port: 6002\n", CFG_UINT, ret.mc_port, 6002, NULL),
  CFG_FIELD(config_t, "ret:\n  pt: 100\n", CFG_UCHAR, ret.rtx_pt, 100, NULL),
  CFG_FIELD(config_t, "ret:\n  wait: 350\n", CFG_UINT, ret.wait_ms, 350, NULL),
  CFG_FIELD(config_t, "srt:\n  passphrase-in: 0123456789ab\n", CFG_CHARARR, srt_passphrase_in, 0, "0123456789ab"),
  CFG_FIELD(config_t, "srt:\n  pbkeylen-in: 24\n", CFG_INT, srt_pbkeylen_in, 24, NULL),
  CFG_FIELD(config_t, "srt:\n  streamid-in: sid1\n", CFG_CHARARR, srt_streamid_in, 0, "sid1"),
  CFG_FIELD(config_t, "srt:\n  packetfilter-in: fec,cols:4\n", CFG_CHARARR, srt_packetfilter_in, 0, "fec,cols:4"),
  CFG_FIELD(config_t, "srt:\n  latency-in: 123\n", CFG_UINT, srt_latency_in_ms, 123, NULL),
  CFG_FIELD(config_t, "srt:\n  passphrase: 0123456789cd\n", CFG_CHARARR, srt_passphrase, 0, "0123456789cd"),
  CFG_FIELD(config_t, "srt:\n  pbkeylen: 32\n", CFG_INT, srt_pbkeylen, 32, NULL),
  CFG_FIELD(config_t, "srt:\n  streamid: sid2\n", CFG_CHARARR, srt_streamid, 0, "sid2"),
  CFG_FIELD(config_t, "srt:\n  packetfilter: fec,rows:3\n", CFG_CHARARR, srt_packetfilter, 0, "fec,rows:3"),
  CFG_FIELD(config_t, "srt:\n  latency: 321\n", CFG_UINT, srt_latency_ms, 321, NULL),
};

static const char *const bad_cases[] = {
  "in: rtp://not-an-address:5000\n",
  "in: rist://1.2.3.4:6000\n",
  "in: srt://:9000\n",
  "out: rtp://\n",
  "out:\n  - a.ts\n  - b.ts\n  - c.ts\n  - d.ts\n  - e.ts\n  - f.ts\n  - g.ts\n  - h.ts\n  - i.ts\n",
  "audio: 0\n",
  "audio: loud\n",
  "format: avi\n",
  "pmt-pid: 0x5\n",
  "pmt-pid: somewhere\n",
  "subtitles: burn\n",
  "time: 5x\n",
  "ttl: 256\n",
  "al-fec: 41:1\n",
  "al-fec: 0:4\n",
  "al-fec-port: 0\n",
  "rist:\n  profile: ultra\n",
  "rist:\n  secret: " A128 "\n",
  "rist:\n  encryption-type: 192\n",
  "rist:\n  cname: " A128 "\n",
  "rist:\n  buffer: 0\n",
  "rist:\n  profile-in: ultra\n",
  "rist:\n  encryption-type-in: 100\n",
  "insecure: maybe\n",
  "verbose: perhaps\n",
  "sub-lead: 10001\n",
  "color: rainbow\n",
  "pace: maybe\n",
  "strip: BOGUS\n",
  "metrics:\n  interval: 0\n",
  "metrics:\n  interval: 86401\n",
  "metrics:\n  inspect-ts: loud\n",
  "metrics:\n  inspect-ts-pids: 9000\n",
  "ret:\n  addr: not-an-address\n",
  "ret:\n  no-mc: maybe\n",
  "ret:\n  mc-port: 0\n",
  "ret:\n  pt: 128\n",
  "ret:\n  wait: 0\n",
  "srt:\n  passphrase-in: " A128 "\n",
  "srt:\n  pbkeylen-in: 20\n",
  "srt:\n  streamid-in: " A128 "\n",
  "srt:\n  packetfilter-in: " A256 "\n",
  "srt:\n  latency-in: 0\n",
  "srt:\n  latency-in: 60001\n",
  "srt:\n  passphrase: " A128 "\n",
  "srt:\n  pbkeylen: 20\n",
  "srt:\n  streamid: " A128 "\n",
  "srt:\n  packetfilter: " A256 "\n",
  "srt:\n  latency: 0\n",
};

static cfg_fixture_t g_fx;

static int load(const char *text, config_t *cfg) {
  int rc;

  cfg_fixture_write(&g_fx, text);
  rec_cfg_defaults(cfg);
  rc = rec_cfg_load(cfg, g_fx.path, 1);
  cfg_fixture_remove(&g_fx);
  return rc;
}

START_TEST(each_key_sets_its_config_field) {
  const cfg_field_case_t *c = &field_cases[_i];
  config_t cfg;

  ck_assert_int_eq(load(c->yaml, &cfg), 0);
  cfg_field_check(&cfg, c);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(invalid_values_are_rejected) {
  config_t cfg;

  ck_assert_int_eq(load(bad_cases[_i], &cfg), -1);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(defaults_are_the_documented_values) {
  config_t cfg;

  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(cfg.audio_all, 1);
  ck_assert_int_eq(cfg.subs, SUB_KEEP);
  ck_assert_int_eq((int)cfg.sub_lead_ms, 1000);
  ck_assert_int_eq(cfg.ret.mc_enabled, 1);
  ck_assert_uint_eq(cfg.ret.rtx_pt, 99u);
  ck_assert_uint_eq(cfg.ret.wait_ms, 200u);
  ck_assert_uint_eq(cfg.strip_mask, STRIP_DEFAULT);
}
END_TEST

START_TEST(unknown_key_is_rejected_in_strict_mode_only) {
  config_t cfg;

  cfg_fixture_write(&g_fx, "bogus-key: 1\n");
  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_load(&cfg, g_fx.path, 1), -1);
  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_load(&cfg, g_fx.path, 0), 0);
  cfg_fixture_remove(&g_fx);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(missing_explicit_file_fails) {
  config_t cfg;

  rec_cfg_defaults(&cfg);
  ck_assert_int_eq(rec_cfg_load(&cfg, "/nonexistent/dipirec.yaml", 0), -1);
}
END_TEST

typedef struct {
  const char *yaml;
  int strict_result;
} cfgtest_case_t;

static const cfgtest_case_t cfgtest_cases[] = {
  {"in: rtp://@239.1.2.3:5000\nout: a.ts\n", 0},
  {"out: a.ts\n", -1},
  {"in: rtp://@239.1.2.3:5000\n", -1},
  {"in: rtp://@239.1.2.3:5000\nout:\n  - rist://1.2.3.4:6000\n  - rist://1.2.3.5:6000\n", -1},
  {"in: rist://@0.0.0.0:6000\nout: rist://1.2.3.4:6000\n", -1},
  {"in: udp://@239.1.2.3:5000\nout: a.ts\nret:\n  addr: 10.0.0.1:6000\n", -1},
  {"in: rtp://@239.1.2.3:5000\nout: a.ts\nret:\n  addr: \"[2001:db8::1]:6000\"\n", -1},
  {"in: rtp://@239.1.2.3:5000\nout: a.ts\npace: true\n", -1},
  {"in: rtp://@239.1.2.3:5000\nout:\n  - a.mkv\n  - b.mkv\nformat: mkv\n", -1},
  {"in: rtp://@239.1.2.3:5000\nout: rtmp://127.0.0.1/live/key\nformat: raw\n", -1},
  {"in: rtp://@239.1.2.3:5000\nout: a.ts\nformat: ts\nsubtitles: srt\n", -1},
  {"in: rtp://@239.1.2.3:5000\nout: rist://1.2.3.4:6000\nrist:\n  secret: s3cret\n", -1},
  {"in: rtp://@239.1.2.3:5000\nout: rist://1.2.3.4:6000\nrist:\n  encryption-type: 128\n", -1},
  {"in: rist://@0.0.0.0:6000\nout: a.ts\nrist:\n  encryption-type-in: 128\n", -1},
  {"in: rtp://@239.1.2.3:5000\nout: a.ts\nal-fec: 10:5\n", -1},
  {"in: rtp://@239.1.2.3:5000\nout: a.ts\nmetrics:\n  sock: /tmp/x.sock\n", -1},
  {"in: rtp://@239.1.2.3:5000\nout: a.ts\nmetrics:\n  inspect-ts: basic\n", -1},
  {"in: rtp://@239.1.2.3:5000\nout: a.ts\nsrt:\n  passphrase-in: short\n", -1},
  {"in: rtp://@239.1.2.3:5000\nout: a.ts\nsrt:\n  pbkeylen-in: 16\n", -1},
  {"in: rtp://@239.1.2.3:5000\nout: srt://127.0.0.1:9000\nsrt:\n  passphrase: short\n", -1},
  {"in: rtp://@239.1.2.3:5000\nout: srt://127.0.0.1:9000\nsrt:\n  pbkeylen: 16\n", -1},
  {"in: rtp://@239.1.2.3:5000\nout: a.ts\nsrt:\n  latency: 100\n", -1},
};

START_TEST(config_test_reports_warnings_and_fails_only_in_strict_mode) {
  cfg_fixture_write(&g_fx, cfgtest_cases[_i].yaml);
  ck_assert_int_eq(rec_cfg_test(g_fx.path, 0), 0);
  ck_assert_int_eq(rec_cfg_test(g_fx.path, 1), cfgtest_cases[_i].strict_result);
  cfg_fixture_remove(&g_fx);
}
END_TEST

START_TEST(config_test_missing_file_fails) {
  ck_assert_int_eq(rec_cfg_test("/nonexistent/dipirec.yaml", 0), -1);
}
END_TEST

static Suite *config_suite(void) {
  Suite *s = suite_create("dipirec_config");
  TCase *tc = tcase_create("core");

  tcase_add_loop_test(tc, each_key_sets_its_config_field, 0, (int)(sizeof field_cases / sizeof field_cases[0]));
  tcase_add_loop_test(tc, invalid_values_are_rejected, 0, (int)(sizeof bad_cases / sizeof bad_cases[0]));
  tcase_add_test(tc, defaults_are_the_documented_values);
  tcase_add_test(tc, unknown_key_is_rejected_in_strict_mode_only);
  tcase_add_test(tc, missing_explicit_file_fails);
  tcase_add_loop_test(tc, config_test_reports_warnings_and_fails_only_in_strict_mode, 0, (int)(sizeof cfgtest_cases / sizeof cfgtest_cases[0]));
  tcase_add_test(tc, config_test_missing_file_fails);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(config_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
