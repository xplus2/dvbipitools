/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../cfg_fixture.h"
#include "dipixy/config.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/log.h"

static const cfg_field_case_t field_cases[] = {
  CFG_FIELD(config_t, "iface: veth7\n", CFG_STRPTR, iface, 0, "veth7"),
  CFG_FIELD(config_t, "workers: 5\n", CFG_INT, workers_spec, 5, NULL),
  CFG_FIELD(config_t, "workers: -2\n", CFG_INT, workers_spec, -2, NULL),
  CFG_FIELD(config_t, "max-clients: 77\n", CFG_INT, max_clients, 77, NULL),
  CFG_FIELD(config_t, "max-channels: 66\n", CFG_INT, max_channels, 66, NULL),
  CFG_FIELD(config_t, "idle-timeout: 33\n", CFG_UINT, idle_timeout_s, 33, NULL),
  CFG_FIELD(config_t, "capture-ring-size: 8192\n", CFG_UINT, capture_ring_kib, 8192, NULL),
  CFG_FIELD(config_t, "ts:\n  startup-timeout: 7.5\n", CFG_DOUBLE, ts_startup_timeout_s, 7.5, NULL),
  CFG_FIELD(config_t, "join-all: true\n", CFG_INT, join_all, 1, NULL),
  CFG_FIELD(config_t, "insecure: yes\n", CFG_INT, insecure_tls, 1, NULL),
  CFG_FIELD(config_t, "sds:\n  timeout: 4.5\n", CFG_DOUBLE, sds_timeout_s, 4.5, NULL),
  CFG_FIELD(config_t, "sds:\n  refresh-interval: 20\n", CFG_DOUBLE, sds_refresh_interval_s, 20, NULL),
  CFG_FIELD(config_t, "segment-size: 4\n", CFG_DOUBLE, segment_size, 4, NULL),
  CFG_FIELD(config_t, "segment-count: 6\n", CFG_INT, segment_count, 6, NULL),
  CFG_FIELD(config_t, "hls:\n  part-size: 0.5\n", CFG_DOUBLE, hls_part_size, 0.5, NULL),
  CFG_FIELD(config_t, "hls:\n  seg-pool: 12\n", CFG_INT, hls_seg_pool, 12, NULL),
  CFG_FIELD(config_t, "dash:\n  part-size: 0.4\n", CFG_DOUBLE, dash_part_size, 0.4, NULL),
  CFG_FIELD(config_t, "dash:\n  utc-url: http://time.example/utc\n", CFG_STRPTR, dash_utc_url, 0, "http://time.example/utc"),
  CFG_FIELD(config_t, "metrics:\n  sock: /tmp/m.sock\n", CFG_STRPTR, metrics_sock, 0, "/tmp/m.sock"),
  CFG_FIELD(config_t, "metrics:\n  id: inst9\n", CFG_STRPTR, metrics_id, 0, "inst9"),
  CFG_FIELD(config_t, "metrics:\n  interval: 42\n", CFG_UINT, metrics_interval_s, 42, NULL),
  CFG_FIELD(config_t, "metrics:\n  inspect-ts: full\n", CFG_INT, metrics_inspect_ts, METRICS_INSPECT_TS_FULL, NULL),
  CFG_FIELD(config_t, "metrics:\n  http: on\n", CFG_INT, metrics_http, 1, NULL),
  CFG_FIELD(config_t, "metrics:\n  auth: 'user:pass'\n", CFG_CHARARR, http_metrics_auth, 0, "Basic dXNlcjpwYXNz"),
  CFG_FIELD(config_t, "no:\n  url-rtp: true\n", CFG_INT, no_url_rtp, 1, NULL),
  CFG_FIELD(config_t, "no:\n  url-udp: true\n", CFG_INT, no_url_udp, 1, NULL),
  CFG_FIELD(config_t, "no:\n  url-srt: true\n", CFG_INT, no_url_srt, 1, NULL),
  CFG_FIELD(config_t, "no:\n  pid-filters: true\n", CFG_INT, no_pid_filters, 1, NULL),
  CFG_FIELD(config_t, "no:\n  lcevc: true\n", CFG_INT, no_lcevc, 1, NULL),
  CFG_FIELD(config_t, "no:\n  http2: true\n", CFG_INT, no_http2, 1, NULL),
  CFG_FIELD(config_t, "no:\n  http3: true\n", CFG_INT, no_http3, 1, NULL),
  CFG_FIELD(config_t, "no:\n  fcc: true\n", CFG_INT, no_fcc, 1, NULL),
  CFG_FIELD(config_t, "no:\n  ret: true\n", CFG_INT, no_ret, 1, NULL),
  CFG_FIELD(config_t, "no:\n  al-fec: true\n", CFG_INT, no_al_fec, 1, NULL),
  CFG_FIELD(config_t, "no:\n  status: true\n", CFG_INT, no_status, 1, NULL),
  CFG_FIELD(config_t, "h3:\n  altsvc-port: 8443\n", CFG_UINT, h3_altsvc_port, 8443, NULL),
  CFG_FIELD(config_t, "h3:\n  max-streams: 120\n", CFG_UINT, h3_max_streams, 120, NULL),
  CFG_FIELD(config_t, "h3:\n  max-conns: 500\n", CFG_UINT, h3_max_conns, 500, NULL),
  CFG_FIELD(config_t, "h3:\n  idle-timeout: 44\n", CFG_UINT, h3_idle_s, 44, NULL),
  CFG_FIELD(config_t, "h3:\n  retry: always\n", CFG_INT, h3_retry, H3_RETRY_CFG_ALWAYS, NULL),
  CFG_FIELD(config_t, "h3:\n  max-udp-payload: 1400\n", CFG_UINT, h3_max_udp, 1400, NULL),
  CFG_FIELD(config_t, "h3:\n  window: 512\n", CFG_UINT, h3_window_kib, 512, NULL),
  CFG_FIELD(config_t, "h3:\n  cc: bbr\n", CFG_INT, h3_cc, H3_CC_CFG_BBR, NULL),
  CFG_FIELD(config_t, "al-fec: 5:4\n", CFG_UINT, al_fec_l, 5, NULL),
  CFG_FIELD(config_t, "al-fec: 5:4\n", CFG_UINT, al_fec_d, 4, NULL),
  CFG_FIELD(config_t, "auth: 'user:pass'\n", CFG_CHARARR, http_auth, 0, "Basic dXNlcjpwYXNz"),
  CFG_FIELD(config_t, "cors-origin: '*'\n", CFG_STRPTR, cors_origins, 0, "*"),
  CFG_FIELD(config_t, "ssdp:\n  ttl: 9\n", CFG_INT, ssdp_ttl, 9, NULL),
  CFG_FIELD(config_t, "ssdp:\n  iface: eth9\n", CFG_STRPTR, ssdp_iface, 0, "eth9"),
  CFG_FIELD(config_t, "ssdp:\n  interval: 30\n", CFG_DOUBLE, ssdp_interval_s, 30, NULL),
  CFG_FIELD(config_t, "ssdp:\n  max-age: 900\n", CFG_UINT, ssdp_max_age_s, 900, NULL),
  CFG_FIELD(config_t, "enable-dlna: true\n", CFG_INT, enable_dlna, 1, NULL),
  CFG_FIELD(config_t, "dlna:\n  host: dvb.example:9080\n", CFG_STRPTR, dlna_host_opt, 0, "dvb.example:9080"),
  CFG_FIELD(config_t, "dlna:\n  name: Living Room\n", CFG_STRPTR, dlna_name, 0, "Living Room"),
  CFG_FIELD(config_t, "dlna:\n  keep-multicast: true\n", CFG_INT, dlna_keep_multicast, 1, NULL),
  CFG_FIELD(config_t, "daemonize: true\n", CFG_INT, daemonize, 1, NULL),
  CFG_FIELD(config_t, "verbose: true\n", CFG_INT, verbose, 1, NULL),
  CFG_FIELD(config_t, "color: never\n", CFG_INT, color_mode, LOG_COLOR_NEVER, NULL),
};

static const cfg_field_case_t file_cases[] = {
  CFG_FIELD(config_t, "tls:\n  cert: @FILE@\n", CFG_STRPTR, tls_cert, 0, NULL),
  CFG_FIELD(config_t, "tls:\n  key: @FILE@\n", CFG_STRPTR, tls_key, 0, NULL),
  CFG_FIELD(config_t, "status-tpl: @FILE@\n", CFG_STRPTR, status_template, 0, NULL),
};

typedef struct {
  const char *yaml;
} bad_case_t;

static const bad_case_t bad_cases[] = {
  {"listen: nonsense\n"},
  {"listen-tls: nonsense\n"},
  {"workers: 0\n"},
  {"cpu-affinity: 5-2\n"},
  {"max-clients: 0\n"},
  {"max-channels: 2000\n"},
  {"idle-timeout: 86401\n"},
  {"capture-ring-size: 0\n"},
  {"ts:\n  startup-timeout: -1\n"},
  {"join-all: maybe\n"},
  {"insecure: maybe\n"},
  {"sds:\n  timeout: -1\n"},
  {"sds:\n  refresh-interval: -1\n"},
  {"segment-size: 1\n"},
  {"segment-count: 2\n"},
  {"hls:\n  part-size: 0.01\n"},
  {"hls:\n  seg-pool: 0\n"},
  {"dash:\n  part-size: 9\n"},
  {"metrics:\n  interval: 0\n"},
  {"metrics:\n  inspect-ts: loud\n"},
  {"metrics:\n  http: maybe\n"},
  {"metrics:\n  auth: nocolon\n"},
  {"format: bogus\n"},
  {"no:\n  url-rtp: maybe\n"},
  {"h3:\n  altsvc-port: 0\n"},
  {"h3:\n  max-streams: 1\n"},
  {"h3:\n  max-conns: 0\n"},
  {"h3:\n  idle-timeout: 0\n"},
  {"h3:\n  retry: sometimes\n"},
  {"h3:\n  max-udp-payload: 100\n"},
  {"h3:\n  window: 1\n"},
  {"h3:\n  cc: fast\n"},
  {"al-fec: 0:0\n"},
  {"auth: nocolon\n"},
  {"ssdp:\n  ttl: 0\n"},
  {"ssdp:\n  interval: -1\n"},
  {"ssdp:\n  max-age: 0\n"},
  {"enable-dlna: maybe\n"},
  {"dlna:\n  keep-multicast: maybe\n"},
  {"daemonize: maybe\n"},
  {"verbose: maybe\n"},
  {"color: rainbow\n"},
  {"input:\n  - bogus.unknown\n"},
  {"input:\n  - rist://nohost\n"},
  {"input:\n  - sds://nocolon\n"},
  {"input:\n  - rtp://nocolon\n"},
  {"input:\n  - channels.m3u:\n      media-type: neither\n"},
  {"input:\n  - rist://@a:1\n  - rist://@b:2\n"},
  {"input:\n  - name: nameless\n"},
  {"input.name: orphan\n"},
  {"input.media-type: tv\n"},
  {"dash:\n  utc-url: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n"},
};

static cfg_fixture_t g_fx;

static int load_from(const char *path, config_t *cfg) {
  dixy_cfg_defaults(cfg);
  return dixy_cfg_load(cfg, path, 1);
}

static int load(const char *text, config_t *cfg) {
  int rc;

  cfg_fixture_write(&g_fx, text);
  rc = load_from(g_fx.path, cfg);
  cfg_fixture_remove(&g_fx);
  return rc;
}

START_TEST(each_key_sets_its_config_field) {
  const cfg_field_case_t *c = &field_cases[_i];
  config_t cfg;

  ck_assert_int_eq(load(c->yaml, &cfg), 0);
  cfg_field_check(&cfg, c);
  args_free(&cfg);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(path_keys_take_an_existing_file) {
  const cfg_field_case_t *c = &file_cases[_i];
  config_t cfg;
  const char *got;

  cfg_fixture_write_with_file(&g_fx, c->yaml, "@FILE@", "item.pem");
  ck_assert_int_eq(load_from(g_fx.path, &cfg), 0);
  got = *(const char *const *)((const unsigned char *)&cfg + c->off);
  ck_assert_str_eq(got, g_fx.extra);
  cfg_fixture_remove(&g_fx);
  args_free(&cfg);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(path_keys_reject_a_missing_file) {
  const cfg_field_case_t *c = &file_cases[_i];
  char text[128];
  config_t cfg;
  const char *at = strstr(c->yaml, "@FILE@");

  snprintf(text, sizeof text, "%.*s/nonexistent/item.pem\n", (int)(at - c->yaml), c->yaml);
  ck_assert_int_eq(load(text, &cfg), -1);
  args_free(&cfg);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(listen_addresses_are_parsed) {
  config_t cfg;

  ck_assert_int_eq(load("listen: 10.1.2.3:8080\nlisten-tls: 10.1.2.3:8443\n", &cfg), 0);
  ck_assert_int_eq(cfg.listen.scope, LISTEN_V4);
  ck_assert_str_eq(cfg.listen.addr, "10.1.2.3");
  ck_assert_uint_eq(cfg.listen.port, 8080u);
  ck_assert_uint_eq(cfg.listen_tls.port, 8443u);
  args_free(&cfg);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(cpu_affinity_values_are_parsed) {
  config_t cfg;

  ck_assert_int_eq(load("cpu-affinity: 2-3,6\n", &cfg), 0);
  ck_assert_int_eq(cfg.cpu_affinity.mode, CPUAFF_LIST);
  ck_assert_uint_eq(cfg.cpu_affinity.n, 3u);
  ck_assert_uint_eq(cfg.cpu_affinity.cpus[2], 6u);
  args_free(&cfg);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(format_list_enables_only_named_outputs) {
  config_t cfg;

  ck_assert_int_eq(load("format: ts,hls\n", &cfg), 0);
  ck_assert_int_eq(cfg.no_ts, 0);
  ck_assert_int_eq(cfg.no_hls, 0);
  ck_assert_int_eq(cfg.no_spts, 1);
  ck_assert_int_eq(cfg.no_dash, 1);
  ck_assert_int_eq(cfg.no_mp4, 1);
  args_free(&cfg);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(input_list_items_take_name_and_media_type) {
  config_t cfg;
  const char *text =
    "input:\n"
    "  - sds://239.19.75.1:3937\n"
    "  - channels.m3u:\n"
    "      name: MyChannels\n"
    "      media-type: radio\n"
    "  - http://list.example/channels.xml\n";

  ck_assert_int_eq(load(text, &cfg), 0);
  ck_assert_int_eq(cfg.n_sources, 3);
  ck_assert_int_eq(cfg.sources[0].kind, SRC_SDS);
  ck_assert_int_eq(cfg.sources[1].kind, SRC_M3U);
  ck_assert_str_eq(cfg.sources[1].name, "MyChannels");
  ck_assert_int_eq(cfg.sources[1].media_type, MEDIA_RADIO);
  ck_assert_int_eq(cfg.sources[2].kind, SRC_HTTP);
  args_free(&cfg);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(invalid_values_are_rejected) {
  config_t cfg;

  ck_assert_int_eq(load(bad_cases[_i].yaml, &cfg), -1);
  args_free(&cfg);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(unknown_key_is_rejected_in_strict_mode_only) {
  config_t cfg;

  cfg_fixture_write(&g_fx, "bogus-key: 1\n");
  dixy_cfg_defaults(&cfg);
  ck_assert_int_eq(dixy_cfg_load(&cfg, g_fx.path, 1), -1);
  dixy_cfg_defaults(&cfg);
  ck_assert_int_eq(dixy_cfg_load(&cfg, g_fx.path, 0), 0);
  cfg_fixture_remove(&g_fx);
  args_free(&cfg);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(missing_explicit_file_fails) {
  config_t cfg;

  dixy_cfg_defaults(&cfg);
  ck_assert_int_eq(dixy_cfg_load(&cfg, "/nonexistent/dipixy.yaml", 0), -1);
}
END_TEST

START_TEST(defaults_are_the_documented_values) {
  config_t cfg;

  dixy_cfg_defaults(&cfg);
  ck_assert_int_eq(cfg.max_clients, 256);
  ck_assert_int_eq(cfg.max_channels, 32);
  ck_assert_uint_eq(cfg.capture_ring_kib, 4096u);
  ck_assert_int_eq(cfg.segment_count, 4);
  ck_assert_int_eq(cfg.ssdp_ttl, 3);
  ck_assert_uint_eq(cfg.ssdp_max_age_s, 1800u);
  ck_assert_str_eq(cfg.dash_utc_url, "http://time.akamai.com/?iso&ms");
}
END_TEST

START_TEST(config_test_reports_warnings_and_fails_only_in_strict_mode) {
  static const char *const warning_cases[] = {
    "tls:\n  cert: @FILE@\n",
    "tls:\n  key: @FILE@\n",
    "segment-size: 2\nhls:\n  part-size: 3\n",
    "segment-size: 2\ndash:\n  part-size: 3\n",
    "ssdp:\n  interval: 60\n  max-age: 100\n",
    "metrics:\n  sock: /tmp/x.sock\n",
    "metrics:\n  inspect-ts: basic\n",
    "enable-dlna: true\nformat: ts,rawaudio\ndlna:\n  host: h:1\n",
    "enable-dlna: true\nformat: ts,spts\ndlna:\n  host: h:1\n",
    "enable-dlna: true\n",
  };

  for (size_t i = 0; i < sizeof warning_cases / sizeof warning_cases[0]; i++) {
    cfg_fixture_write_with_file(&g_fx, warning_cases[i], "@FILE@", "item.pem");
    ck_assert_int_eq(dixy_cfg_test(g_fx.path, 0), 0);
    ck_assert_int_eq(dixy_cfg_test(g_fx.path, 1), -1);
    cfg_fixture_remove(&g_fx);
  }

  cfg_fixture_write(&g_fx, "listen: 10.0.0.1:9080\nmetrics:\n  id: m1\n");
  ck_assert_int_eq(dixy_cfg_test(g_fx.path, 1), 0);
  cfg_fixture_remove(&g_fx);

  cfg_fixture_write(&g_fx, "max-clients: 0\n");
  ck_assert_int_eq(dixy_cfg_test(g_fx.path, 1), -1);
  cfg_fixture_remove(&g_fx);

  ck_assert_int_eq(dixy_cfg_test("/nonexistent/dipixy.yaml", 0), -1);
}
END_TEST

static Suite *config_suite(void) {
  Suite *s = suite_create("dipixy_config");
  TCase *tc = tcase_create("core");

  tcase_add_loop_test(tc, each_key_sets_its_config_field, 0, sizeof field_cases / sizeof field_cases[0]);
  tcase_add_loop_test(tc, path_keys_take_an_existing_file, 0, sizeof file_cases / sizeof file_cases[0]);
  tcase_add_loop_test(tc, path_keys_reject_a_missing_file, 0, sizeof file_cases / sizeof file_cases[0]);
  tcase_add_test(tc, listen_addresses_are_parsed);
  tcase_add_test(tc, cpu_affinity_values_are_parsed);
  tcase_add_test(tc, format_list_enables_only_named_outputs);
  tcase_add_test(tc, input_list_items_take_name_and_media_type);
  tcase_add_loop_test(tc, invalid_values_are_rejected, 0, sizeof bad_cases / sizeof bad_cases[0]);
  tcase_add_test(tc, unknown_key_is_rejected_in_strict_mode_only);
  tcase_add_test(tc, missing_explicit_file_fails);
  tcase_add_test(tc, defaults_are_the_documented_values);
  tcase_add_test(tc, config_test_reports_warnings_and_fails_only_in_strict_mode);
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
