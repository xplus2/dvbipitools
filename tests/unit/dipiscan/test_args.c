/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "../cfg_fixture.h"
#include "../log_capture.h"
#include "dipiscan/cli/priv.h"
#include "lib/config/yamlcfg.h"
#include "lib/helper/log.h"

#define ARGV_MAX 8
#define MSG_MAX 1024
#define HOST_LEN 300

typedef struct {
  char *argv[ARGV_MAX];
  int argc;
  cfg_kind_t kind;
  size_t off;
  double num;
  const char *str;
} option_case_t;

#define OPT(field, kind, num, str, ...) {{"dipiscan", __VA_ARGS__}, 1 + (int)(sizeof((const char *[]){__VA_ARGS__}) / sizeof(char *)), kind, offsetof(config_t, field), num, str}

static const option_case_t option_cases[] = {
  OPT(total, CFG_UINT, 254, NULL, "-m", "239.1.2.3"),
  OPT(total, CFG_UINT, 254, NULL, "--mcast", "239.1.2.0/24"),
  OPT(total, CFG_UINT, 9, NULL, "-m", "239.1.2.1-239.1.2.9"),
  OPT(family, CFG_INT, AF_INET6, NULL, "-m", "ff0e::1"),
  OPT(port_lo, CFG_UINT, 8000, NULL, "-p", "8000-8002"),
  OPT(port_hi, CFG_UINT, 8002, NULL, "--port", "8000-8002"),
  OPT(port_lo, CFG_UINT, 9100, NULL, "-p", "9100"),
  OPT(format, CFG_INT, OUT_M3U, NULL, "-f", "m3u"),
  OPT(format, CFG_INT, OUT_CSV, NULL, "-f", "csv"),
  OPT(format, CFG_INT, OUT_XSPF, NULL, "--format", "xspf"),
  OPT(format, CFG_INT, OUT_NULL, NULL, "-f", "null"),
  OPT(format, CFG_INT, OUT_XML, NULL, "-f", "xml", "-P", "provider.example"),
  OPT(provider, CFG_STRPTR, 0, "provider.example", "-P", "provider.example"),
  OPT(provider, CFG_STRPTR, 0, "long.example", "--provider", "long.example"),
  OPT(out_path, CFG_STRPTR, 0, "out.m3u", "-o", "out.m3u"),
  OPT(out_path, CFG_STRPTR, 0, "-", "--out", "-"),
  OPT(timeout_ms, CFG_INT, 5000, NULL, "-t", "5"),
  OPT(timeout_ms, CFG_INT, 3600000, NULL, "--timeout", "3600"),
  OPT(jets, CFG_UINT, 8, NULL, "-j", "8"),
  OPT(jets, CFG_UINT, DIPISCAN_MAX_JETS, NULL, "--jets", "256"),
  OPT(mpts, CFG_INT, 1, NULL, "-M"),
  OPT(mpts, CFG_INT, 1, NULL, "--mpts"),
  OPT(http_proxy, CFG_INT, 1, NULL, "-u", "proxy.example:8080"),
  OPT(http_proxy_port, CFG_UINT, 8080, NULL, "--http-proxy", "proxy.example:8080"),
  OPT(http_proxy_host, CFG_CHARARR, 0, "proxy.example", "-u", "proxy.example:8080"),
  OPT(http_proxy_port, CFG_UINT, 80, NULL, "-u", "proxy.example"),
  OPT(http_path_tmpl, CFG_STRPTR, 0, "/stream/%g/%p/", "-u", "p:1", "-x", "/stream/%g/%p/"),
  OPT(http_path_tmpl, CFG_STRPTR, 0, "/a/%%/", "-u", "p:1", "--http-path", "/a/%%/"),
  OPT(iface, CFG_STRPTR, 0, "eth7", "-I", "eth7"),
  OPT(iface, CFG_STRPTR, 0, "eth8", "--iface", "eth8"),
  OPT(verbose, CFG_INT, 1, NULL, "-v"),
  OPT(verbose, CFG_INT, 1, NULL, "--verbose"),
  OPT(color_mode, CFG_INT, LOG_COLOR_NEVER, NULL, "--color", "never"),
  OPT(color_mode, CFG_INT, LOG_COLOR_ALWAYS, NULL, "--color", "always"),
};

START_TEST(every_option_sets_its_field) {
  const option_case_t *c = &option_cases[_i];
  cfg_field_case_t fc = {NULL, c->kind, c->off, c->num, c->str};
  config_t cfg;
  char *av[ARGV_MAX];

  memcpy(av, c->argv, sizeof av);
  ck_assert_int_eq(args_parse(c->argc, av, &cfg), ARGS_OK);
  cfg_field_check(&cfg, &fc);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

typedef struct {
  char *argv[ARGV_MAX];
  int argc;
  args_status_t status;
  const char *message;
} error_case_t;

#define ERR(status, message, ...) {{"dipiscan", __VA_ARGS__}, 1 + (int)(sizeof((const char *[]){__VA_ARGS__}) / sizeof(char *)), status, message}

static const error_case_t error_cases[] = {
  ERR(ARGS_ERR, "invalid -m address: bogus", "-m", "bogus"),
  ERR(ARGS_ERR, "invalid -m address: 239.1.2.3/40", "-m", "239.1.2.3/40"),
  ERR(ARGS_ERR, "invalid -p port range: 0", "-p", "0"),
  ERR(ARGS_ERR, "invalid -p port range: 9-3", "-p", "9-3"),
  ERR(ARGS_ERR, "invalid -f format: pdf", "-f", "pdf"),
  ERR(ARGS_ERR, "invalid -t timeout: 0", "-t", "0"),
  ERR(ARGS_ERR, "invalid -t timeout: 3601", "-t", "3601"),
  ERR(ARGS_ERR, "invalid -t timeout: abc", "-t", "abc"),
  ERR(ARGS_ERR, "invalid -j jets: 0", "-j", "0"),
  ERR(ARGS_ERR, "invalid -j jets: 257", "-j", "257"),
  ERR(ARGS_ERR, "invalid -u http-proxy address: [::1", "-u", "[::1"),
  ERR(ARGS_ERR, "invalid -u http-proxy address: host:0", "-u", "host:0"),
  ERR(ARGS_ERR, "invalid -x path template: /a/%z", "-u", "p:1", "-x", "/a/%z"),
  ERR(ARGS_ERR, "invalid --color: rainbow", "--color", "rainbow"),
  ERR(ARGS_ERR, "unexpected argument: stray", "-v", "stray"),
  ERR(ARGS_ERR, "missing -P provider (required for -f xml)", "-f", "xml"),
  ERR(ARGS_ERR, "", "--no-such-option"),
  ERR(ARGS_OK, "--http-path needs -u/--http-proxy", "-x", "/a/%g/"),
};

START_TEST(error_messages_and_status) {
  const error_case_t *c = &error_cases[_i];
  char msg[MSG_MAX];
  config_t cfg;
  char *av[ARGV_MAX];
  args_status_t st;

  memcpy(av, c->argv, sizeof av);
  log_capture_begin();
  st = args_parse(c->argc, av, &cfg);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(st, c->status);
  ck_assert_msg(strstr(msg, c->message) != NULL, "want '%s' in '%s'", c->message, msg);
  yamlcfg_strpool_free(cfg.str_pool);
}
END_TEST

START_TEST(help_option_returns_help_status) {
  char *argv[] = {"dipiscan", "-v", "--help"};
  char msg[MSG_MAX];
  config_t cfg;
  args_status_t st;

  log_capture_begin();
  st = args_parse(3, argv, &cfg);
  log_capture_end(msg, sizeof msg);
  ck_assert_int_eq(st, ARGS_HELP);
}
END_TEST

typedef struct {
  const char *s;
  int ret;
  const char *host;
  unsigned port;
} proxy_case_t;

static const proxy_case_t proxy_cases[] = {
  {"host", 0, "host", 80},
  {"host:8080", 0, "host", 8080},
  {"10.0.0.1:65535", 0, "10.0.0.1", 65535},
  {"[::1]", 0, "::1", 80},
  {"[fe80::1]:9000", 0, "fe80::1", 9000},
  {"[::1", -1, NULL, 0},
  {"[]", -1, NULL, 0},
  {"[]:80", -1, NULL, 0},
  {"[::1]x", -1, NULL, 0},
  {":80", -1, NULL, 0},
  {"host:", -1, NULL, 0},
  {"host:0", -1, NULL, 0},
  {"host:65536", -1, NULL, 0},
  {"host:abc", -1, NULL, 0},
};

START_TEST(http_proxy_parse_cases) {
  const proxy_case_t *c = &proxy_cases[_i];
  config_t cfg;

  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(scan_http_proxy_parse(c->s, &cfg), c->ret);
  if (c->ret) return;
  ck_assert_str_eq(cfg.http_proxy_host, c->host);
  ck_assert_uint_eq(cfg.http_proxy_port, c->port);
}
END_TEST

START_TEST(http_proxy_parse_rejects_overlong_hosts) {
  char host[HOST_LEN + 8];
  char bracketed[HOST_LEN + 16];
  config_t cfg;

  memset(host, 'h', HOST_LEN);
  host[HOST_LEN] = '\0';
  snprintf(bracketed, sizeof bracketed, "[%s]", host);
  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(scan_http_proxy_parse(host, &cfg), -1);
  ck_assert_int_eq(scan_http_proxy_parse(bracketed, &cfg), -1);
}
END_TEST

START_TEST(cfg_http_proxy_marks_proxy_mode) {
  config_t cfg;

  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(scan_cfg_http_proxy(&cfg, "bad:0"), -1);
  ck_assert_int_eq(cfg.http_proxy, 0);
  ck_assert_int_eq(scan_cfg_http_proxy(&cfg, "good:81"), 0);
  ck_assert_int_eq(cfg.http_proxy, 1);
}
END_TEST

typedef struct {
  const char *tmpl;
  int ret;
} tmpl_case_t;

static const tmpl_case_t tmpl_cases[] = {
  {"", 0},
  {"/plain/", 0},
  {"%g", 0},
  {"%p", 0},
  {"%%", 0},
  {"/udp/%g:%p/", 0},
  {"100%%/%g", 0},
  {"%x", -1},
  {"%", -1},
  {"abc%", -1},
  {"/udp/%g:%q/", -1},
  {"%G", -1},
};

START_TEST(http_path_template_validation) {
  const tmpl_case_t *c = &tmpl_cases[_i];

  ck_assert_int_eq(scan_http_path_tmpl_valid(c->tmpl), c->ret);
  ck_assert_int_eq(scan_cfg_http_path(c->tmpl), c->ret);
}
END_TEST

typedef struct {
  const char *mcast;
  const char *want;
} range_case_t;

static const range_case_t range_cases[] = {
  {"239.1.2.3", "239.1.2.1-239.1.2.254"},
  {"239.1.2.0/30", "239.1.2.1-239.1.2.2"},
  {"239.1.2.5-239.1.2.9", "239.1.2.5-239.1.2.9"},
  {"ff0e::5", "ff0e::1-ff0e::fe"},
  {"ff0e::1-ff0e::9", "ff0e::1-ff0e::9"},
};

START_TEST(range_describe_formats_start_and_end) {
  const range_case_t *c = &range_cases[_i];
  config_t cfg;
  char buf[128];

  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(scan_cfg_mcast(&cfg, c->mcast), 0);
  args_range_describe(&cfg, buf, sizeof buf);
  ck_assert_str_eq(buf, c->want);
}
END_TEST

START_TEST(range_describe_truncates_to_buffer) {
  config_t cfg;
  char buf[8];

  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(scan_cfg_mcast(&cfg, "239.1.2.3"), 0);
  args_range_describe(&cfg, buf, sizeof buf);
  ck_assert_uint_eq(strlen(buf), sizeof buf - 1);
  ck_assert_int_eq(strncmp(buf, "239.1.2.1-239", sizeof buf - 1), 0);
}
END_TEST

static Suite *args_suite(void) {
  Suite *s = suite_create("dipiscan_args");
  TCase *tc = tcase_create("core");

  tcase_add_loop_test(tc, every_option_sets_its_field, 0, (int)(sizeof option_cases / sizeof option_cases[0]));
  tcase_add_loop_test(tc, error_messages_and_status, 0, (int)(sizeof error_cases / sizeof error_cases[0]));
  tcase_add_test(tc, help_option_returns_help_status);
  tcase_add_loop_test(tc, http_proxy_parse_cases, 0, (int)(sizeof proxy_cases / sizeof proxy_cases[0]));
  tcase_add_test(tc, http_proxy_parse_rejects_overlong_hosts);
  tcase_add_test(tc, cfg_http_proxy_marks_proxy_mode);
  tcase_add_loop_test(tc, http_path_template_validation, 0, (int)(sizeof tmpl_cases / sizeof tmpl_cases[0]));
  tcase_add_loop_test(tc, range_describe_formats_start_and_end, 0, (int)(sizeof range_cases / sizeof range_cases[0]));
  tcase_add_test(tc, range_describe_truncates_to_buffer);
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
