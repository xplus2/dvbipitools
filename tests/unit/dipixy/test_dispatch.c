/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipixy/altsvc.h"
#include "dipixy/cli/args.h"
#include "dipixy/dlna/dlna.h"
#include "dipixy/reactor/internal.h"
#include "dipixy/reactor/dispatch/priv.h"

static unsigned g_http_errors;
void dipixy_metrics_note_http_error(void) { g_http_errors++; }
const char *altsvc_h1_line(int is_tls) { (void)is_tls; return ""; }
int conn_queue(conn_t *c, const void *data, size_t len) { (void)c; (void)data; (void)len; return 0; }
void set_persistence(conn_t *c, int keep_alive) { (void)c; (void)keep_alive; }
const config_t *reactor_cfg(void) { return NULL; }
int dlna_device_desc_xml(const config_t *cfg, char **out, size_t *out_len) { (void)cfg; (void)out; (void)out_len; return -1; }
void dlna_cd_scpd_xml(const char **out, size_t *out_len) { *out = ""; *out_len = 0; }
void dlna_cm_scpd_xml(const char **out, size_t *out_len) { *out = ""; *out_len = 0; }

static struct phr_header make_header(const char *name, const char *value) {
  struct phr_header h;
  h.name = name;
  h.name_len = strlen(name);
  h.value = value;
  h.value_len = strlen(value);
  return h;
}

START_TEST(find_header_matches_case_insensitively) {
  struct phr_header hdrs[2];
  char out[64];
  hdrs[0] = make_header("Content-Type", "text/plain");
  hdrs[1] = make_header("X-Foo", "bar");

  ck_assert_int_eq(find_header(hdrs, 2, "content-type", out, sizeof out), 1);
  ck_assert_str_eq(out, "text/plain");
  ck_assert_int_eq(find_header(hdrs, 2, "X-FOO", out, sizeof out), 1);
  ck_assert_str_eq(out, "bar");
}
END_TEST

START_TEST(find_header_missing_returns_zero) {
  struct phr_header hdrs[1];
  char out[64];
  hdrs[0] = make_header("X-Foo", "bar");
  ck_assert_int_eq(find_header(hdrs, 1, "Authorization", out, sizeof out), 0);
}
END_TEST

START_TEST(find_header_truncates_to_buffer) {
  struct phr_header hdrs[1];
  char out[4];
  hdrs[0] = make_header("X-Foo", "abcdef");
  ck_assert_int_eq(find_header(hdrs, 1, "X-Foo", out, sizeof out), 1);
  ck_assert_str_eq(out, "abc"); /* outsz=4: 3 chars + NUL */
}
END_TEST

START_TEST(find_header_empty_list_returns_zero) {
  char out[8];
  ck_assert_int_eq(find_header(NULL, 0, "X-Foo", out, sizeof out), 0);
}
END_TEST

START_TEST(auth_disabled_when_cfg_empty) {
  config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(http_auth_ok(cfg.http_auth, NULL), 1);
  ck_assert_int_eq(http_auth_ok(cfg.http_auth, "Basic anything"), 1);
}
END_TEST

START_TEST(auth_requires_exact_match_when_enabled) {
  config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  strcpy(cfg.http_auth, "Basic dXNlcjpwYXNz");
  ck_assert_int_eq(http_auth_ok(cfg.http_auth, NULL), 0);
  ck_assert_int_eq(http_auth_ok(cfg.http_auth, "Basic wrong"), 0);
  ck_assert_int_eq(http_auth_ok(cfg.http_auth, "Basic dXNlcjpwYXNz"), 1);
  ck_assert_int_eq(http_auth_ok(cfg.http_auth, "basic dXNlcjpwYXNz"), 1);
  ck_assert_int_eq(http_auth_ok(cfg.http_auth, "BASIC  dXNlcjpwYXNz"), 1);
  ck_assert_int_eq(http_auth_ok(cfg.http_auth, "BasicdXNlcjpwYXNz"), 0);
  ck_assert_int_eq(http_auth_ok(cfg.http_auth, "Bearer dXNlcjpwYXNz"), 0);
}
END_TEST

START_TEST(metrics_auth_is_independent_of_auth) {
  config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  strcpy(cfg.http_metrics_auth, "Basic dXNlcjpwYXNz");
  ck_assert_int_eq(http_auth_ok(cfg.http_auth, NULL), 1);
  ck_assert_int_eq(http_auth_ok(cfg.http_metrics_auth, NULL), 0);
  ck_assert_int_eq(http_auth_ok(cfg.http_metrics_auth, "Basic dXNlcjpwYXNz"), 1);
}
END_TEST

START_TEST(cors_defaults_to_star_when_unset) {
  config_t cfg;
  int vary;
  memset(&cfg, 0, sizeof cfg);
  cfg.cors_origins = NULL;
  ck_assert_str_eq(cors_match(&cfg, "https://example.com", &vary), "*");
  ck_assert_int_eq(vary, 0);
}
END_TEST

START_TEST(cors_star_entry_in_list_matches_anything) {
  config_t cfg;
  int vary;
  memset(&cfg, 0, sizeof cfg);
  cfg.cors_origins = "https://a.com, *, https://b.com";
  ck_assert_str_eq(cors_match(&cfg, "https://whatever.example", &vary), "*");
  ck_assert_int_eq(vary, 0);
}
END_TEST

START_TEST(cors_allowlist_matches_and_sets_vary) {
  config_t cfg;
  int vary;
  memset(&cfg, 0, sizeof cfg);
  cfg.cors_origins = "https://a.com,https://b.com";
  ck_assert_str_eq(cors_match(&cfg, "https://b.com", &vary), "https://b.com");
  ck_assert_int_eq(vary, 1);
}
END_TEST

START_TEST(cors_allowlist_trims_surrounding_spaces) {
  config_t cfg;
  int vary;
  memset(&cfg, 0, sizeof cfg);
  cfg.cors_origins = "https://a.com , https://b.com";
  ck_assert_str_eq(cors_match(&cfg, "https://b.com", &vary), "https://b.com");
  ck_assert_int_eq(vary, 1);
}
END_TEST

START_TEST(cors_allowlist_rejects_unlisted_origin) {
  config_t cfg;
  int vary;
  memset(&cfg, 0, sizeof cfg);
  cfg.cors_origins = "https://a.com,https://b.com";
  ck_assert_ptr_null(cors_match(&cfg, "https://evil.example", &vary));
  ck_assert_int_eq(vary, 0);
}
END_TEST

START_TEST(ws_origin_absent_is_ok) {
  config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(ws_origin_ok(&cfg, NULL, "evil.tld:9080"), 1);
  ck_assert_int_eq(ws_origin_ok(&cfg, "", "evil.tld:9080"), 1);
}
END_TEST

START_TEST(ws_origin_ip_and_localhost_hosts_pass) {
  config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://192.168.1.5:9080", "192.168.1.5:9080"), 1);
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://[::1]:9080", "[::1]:9080"), 1);
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://LocalHost:9080", "localhost:9080"), 1);
}
END_TEST

START_TEST(ws_origin_rebound_name_is_rejected) {
  config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://evil.tld:9080", "evil.tld:9080"), 0);
}
END_TEST

START_TEST(ws_origin_must_match_host) {
  config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://10.0.0.1:9080", "10.0.0.2:9080"), 0);
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://10.0.0.1:9080", "10.0.0.1:9081"), 0);
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://10.0.0.1:9080", NULL), 0);
  ck_assert_int_eq(ws_origin_ok(&cfg, "null", "10.0.0.1:9080"), 0);
  ck_assert_int_eq(ws_origin_ok(&cfg, "ftp://10.0.0.1", "10.0.0.1"), 0);
}
END_TEST

START_TEST(ws_origin_default_ports_are_normalized) {
  config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://10.0.0.1", "10.0.0.1:80"), 1);
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://10.0.0.1:80", "10.0.0.1"), 1);
  ck_assert_int_eq(ws_origin_ok(&cfg, "https://10.0.0.1", "10.0.0.1:443"), 1);
  ck_assert_int_eq(ws_origin_ok(&cfg, "https://10.0.0.1", "10.0.0.1:80"), 0);
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://[::1]", "[::1]:80"), 1);
}
END_TEST

START_TEST(ws_origin_listed_names_pass) {
  config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.cors_origins = "https://player.example, http://tv.lan:8080";
  cfg.ws_allowed_hosts = "box.lan, other.lan";
  strcpy(cfg.dlna_host, "media.lan:9080");
  ck_assert_int_eq(ws_origin_ok(&cfg, "https://player.example", "player.example"), 1);
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://tv.lan:9080", "tv.lan:9080"), 1);
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://BOX.lan:9080", "box.LAN:9080"), 1);
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://other.lan", "other.lan"), 1);
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://media.lan:9080", "media.lan:9080"), 1);
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://evil.tld:9080", "evil.tld:9080"), 0);
}
END_TEST

START_TEST(ws_origin_cors_star_does_not_allow_names) {
  config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.cors_origins = "*";
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://evil.tld:9080", "evil.tld:9080"), 0);
}
END_TEST

START_TEST(ws_origin_allowed_hosts_star_allows_any) {
  config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.ws_allowed_hosts = "*";
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://anything.tld:9080", "anything.tld:9080"), 1);
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://a.tld:9080", "b.tld:9080"), 0);
}
END_TEST

START_TEST(keepalive_connection_token_list) {
  struct phr_header h;
  h = make_header("Connection", "Upgrade, close");
  ck_assert_int_eq(wants_keepalive(1, &h, 1), 0);
  h = make_header("Connection", "close, Upgrade");
  ck_assert_int_eq(wants_keepalive(1, &h, 1), 0);
  h = make_header("Connection", "Upgrade,CLOSE ");
  ck_assert_int_eq(wants_keepalive(1, &h, 1), 0);
  h = make_header("Connection", "Upgrade, keep-alive");
  ck_assert_int_eq(wants_keepalive(0, &h, 1), 1);
  h = make_header("Connection", "Upgrade");
  ck_assert_int_eq(wants_keepalive(1, &h, 1), 1);
  ck_assert_int_eq(wants_keepalive(0, &h, 1), 0);
  h = make_header("Connection", "closed");
  ck_assert_int_eq(wants_keepalive(1, &h, 1), 1);
  ck_assert_int_eq(wants_keepalive(1, NULL, 0), 1);
}
END_TEST

static Suite *dispatch_suite(void) {
  Suite *s = suite_create("dipixy_dispatch");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, find_header_matches_case_insensitively);
  tcase_add_test(tc, find_header_missing_returns_zero);
  tcase_add_test(tc, find_header_truncates_to_buffer);
  tcase_add_test(tc, find_header_empty_list_returns_zero);
  tcase_add_test(tc, keepalive_connection_token_list);
  tcase_add_test(tc, auth_disabled_when_cfg_empty);
  tcase_add_test(tc, auth_requires_exact_match_when_enabled);
  tcase_add_test(tc, metrics_auth_is_independent_of_auth);
  tcase_add_test(tc, cors_defaults_to_star_when_unset);
  tcase_add_test(tc, cors_star_entry_in_list_matches_anything);
  tcase_add_test(tc, cors_allowlist_matches_and_sets_vary);
  tcase_add_test(tc, cors_allowlist_trims_surrounding_spaces);
  tcase_add_test(tc, cors_allowlist_rejects_unlisted_origin);
  tcase_add_test(tc, ws_origin_absent_is_ok);
  tcase_add_test(tc, ws_origin_ip_and_localhost_hosts_pass);
  tcase_add_test(tc, ws_origin_rebound_name_is_rejected);
  tcase_add_test(tc, ws_origin_must_match_host);
  tcase_add_test(tc, ws_origin_default_ports_are_normalized);
  tcase_add_test(tc, ws_origin_listed_names_pass);
  tcase_add_test(tc, ws_origin_cors_star_does_not_allow_names);
  tcase_add_test(tc, ws_origin_allowed_hosts_star_allows_any);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(dispatch_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
