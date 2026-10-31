/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dipixy/altsvc.h"
#include "dipixy/cli/args.h"
#include "dipixy/dlna/dlna.h"
#include "dipixy/reactor/internal.h"
#include "dipixy/reactor/dispatch/priv.h"
#include "dipixy/ws/ws_clients.h"

static unsigned g_http_errors;
void dipixy_metrics_note_http_error(void) { g_http_errors++; }
const char *altsvc_h1_line(int is_tls) { (void)is_tls; return ""; }
int conn_queue(conn_t *c, const void *data, size_t len) { (void)c; (void)data; (void)len; return 0; }
void set_persistence(conn_t *c, int keep_alive) { (void)c; (void)keep_alive; }
static const config_t *g_cfg;
const config_t *reactor_cfg(void) { return g_cfg; }
const channels_t *reactor_channels(void) { return NULL; }
static int g_lookup_rc;
int channels_item_lookup(const channels_t *ch, unsigned list_num, unsigned item_num, const char *item_name, unsigned *out_item_num, char *out_name, size_t out_namesz, char *out_proto, size_t out_protosz, char *out_addr, size_t out_addrsz) {
  (void)ch;
  (void)list_num;
  (void)item_name;
  (void)out_namesz;
  (void)out_protosz;
  (void)out_addrsz;
  if (g_lookup_rc) return -1;
  *out_item_num = item_num ? item_num : 7;
  strcpy(out_name, item_name ? "named" : "");
  strcpy(out_proto, "rtp");
  strcpy(out_addr, "239.1.1.1:5000");
  return 0;
}
int playlist_fmt_disabled(const config_t *cfg, route_fmt_t fmt) {
  switch (fmt) {
    case ROUTE_FMT_TS: return cfg->no_ts;
    case ROUTE_FMT_SPTS: return cfg->no_spts;
    case ROUTE_FMT_RAWAUDIO: return cfg->no_rawaudio;
    case ROUTE_FMT_HLS:
    case ROUTE_FMT_HLS_FMP4: return cfg->no_hls;
    case ROUTE_FMT_LLHLS:
    case ROUTE_FMT_LLHLS_FMP4: return cfg->no_llhls;
    case ROUTE_FMT_DASH: return cfg->no_dash;
    case ROUTE_FMT_LLDASH: return cfg->no_lldash;
    case ROUTE_FMT_MP4: return cfg->no_mp4;
  }
  return 1;
}
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

START_TEST(ws_origin_rejects_malformed_hosts_and_origins) {
  static const struct {
    const char *origin;
    const char *host;
  } t[] = {
      {"http://a.example", ""},
      {"http://a.example", "a/b"},
      {"http://a.example", "a?b"},
      {"http://a.example", "a#b"},
      {"http://a.example", "u@a.example"},
      {"http://a.example", "a b"},
      {"http://[::1", "[::1"},
      {"http://[::1]x", "[::1]x"},
      {"http://[::1]:abc", "[::1]:80"},
      {"http://a:b:c", "a:b:c"},
      {"http://a_b!c", "a_b!c"},
      {"http://:80", ":80"},
      {"http://a.example:", "a.example:"},
      {"http://a.example:0", "a.example:0"},
      {"http://a.example:65536", "a.example:65536"},
      {"http://a.example:80x", "a.example:80x"},
      {"http://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "x"},
      {"http://a.example", "b.example"},
  };
  config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.ws_allowed_hosts = "*";
  ck_assert_int_eq(ws_origin_ok(&cfg, t[_i].origin, t[_i].host), 0);
}
END_TEST

START_TEST(ws_origin_host_list_token_forms) {
  static const struct {
    const char *list;
    const char *name;
    int ok;
  } t[] = {
      {"", "h.lan", 0},
      {",,  ,", "h.lan", 0},
      {"h.lan", "h.lan", 1},
      {"  h.lan  ", "h.lan", 1},
      {"x.lan,h.lan", "h.lan", 1},
      {"http://h.lan", "h.lan", 1},
      {"https://h.lan:8443", "h.lan", 1},
      {"http://[::1]:80", "h.lan", 0},
      {"bad host", "h.lan", 0},
      {"x.lan", "h.lan", 0},
      {"*", "h.lan", 0},
      {"x.lan, *", "h.lan", 0},
  };
  config_t cfg;
  char origin[64];
  char host[64];
  memset(&cfg, 0, sizeof cfg);
  cfg.ws_allowed_hosts = t[_i].list;
  snprintf(origin, sizeof origin, "http://%s", t[_i].name);
  snprintf(host, sizeof host, "%s", t[_i].name);
  if (!strcmp(t[_i].list, "*") || strstr(t[_i].list, ", *")) {
    ck_assert_int_eq(ws_origin_ok(&cfg, origin, host), 1);
    return;
  }
  ck_assert_int_eq(ws_origin_ok(&cfg, origin, host), t[_i].ok);
}
END_TEST

START_TEST(ws_origin_oversized_list_token_is_skipped) {
  config_t cfg;
  char list[300];
  memset(&cfg, 0, sizeof cfg);
  memset(list, 'a', 200);
  strcpy(list + 200, ",h.lan");
  cfg.ws_allowed_hosts = list;
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://h.lan", "h.lan"), 1);
  cfg.ws_allowed_hosts = "h.lan";
  ck_assert_int_eq(ws_origin_ok(&cfg, "HTTPS://H.LAN:443", "h.lan:443"), 1);
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://[fe80::1]:8080", "[FE80::1]:8080"), 1);
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://[bad]:80", "[bad]:80"), 0);
  ck_assert_int_eq(ws_origin_ok(&cfg, "http://[::1]:80", "[::1]:80"), 1);
}
END_TEST

START_TEST(cors_list_skips_blank_tokens) {
  config_t cfg;
  int vary;
  memset(&cfg, 0, sizeof cfg);
  cfg.cors_origins = "https://a.com, ,,https://b.com";
  ck_assert_ptr_null(cors_match(&cfg, NULL, &vary));
  ck_assert_str_eq(cors_match(&cfg, "https://b.com", &vary), "https://b.com");
  ck_assert_int_eq(vary, 1);
  ck_assert_ptr_null(cors_match(&cfg, "https://a.co", &vary));
  ck_assert_ptr_null(cors_match(&cfg, "https://a.comx", &vary));
}
END_TEST

START_TEST(special_path_auth_selects_credentials) {
  config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  g_cfg = &cfg;
  ck_assert_ptr_null(special_path_auth("/"));
  ck_assert_ptr_null(special_path_auth("/metrics"));
  ck_assert_ptr_null(special_path_auth("/other"));
  strcpy(cfg.http_auth, "Basic aaa");
  strcpy(cfg.http_metrics_auth, "Basic bbb");
  ck_assert_str_eq(special_path_auth("/"), "Basic aaa");
  ck_assert_str_eq(special_path_auth("/index.html"), "Basic aaa");
  ck_assert_str_eq(special_path_auth("/ui/status.js"), "Basic aaa");
  ck_assert_str_eq(special_path_auth("/ui/ws"), "Basic aaa");
  ck_assert_str_eq(special_path_auth("/ui/ws/"), "Basic aaa");
  ck_assert_str_eq(special_path_auth("/metrics"), "Basic bbb");
  ck_assert_ptr_null(special_path_auth("/other"));
  ck_assert_int_eq(path_is_ui_ws("/ui/ws"), 1);
  ck_assert_int_eq(path_is_ui_ws("/ui/wsx"), 0);
  g_cfg = NULL;
}
END_TEST

START_TEST(route_disabled_follows_kind_and_format_flags) {
  config_t cfg;
  route_t rt;
  memset(&cfg, 0, sizeof cfg);
  memset(&rt, 0, sizeof rt);
  g_cfg = &cfg;
  rt.kind = ROUTE_RTP;
  rt.fmt = ROUTE_FMT_TS;
  ck_assert_int_eq(route_disabled(&rt), 0);
  cfg.no_url_rtp = 1;
  ck_assert_int_eq(route_disabled(&rt), 1);
  cfg.no_url_rtp = 0;
  rt.kind = ROUTE_UDP;
  cfg.no_url_udp = 1;
  ck_assert_int_eq(route_disabled(&rt), 1);
  cfg.no_url_udp = 0;
  rt.kind = ROUTE_SRT;
  cfg.no_url_srt = 1;
  ck_assert_int_eq(route_disabled(&rt), 1);
  cfg.no_url_srt = 0;
  rt.kind = ROUTE_STDIN;
  cfg.no_ts = 1;
  ck_assert_int_eq(route_disabled(&rt), 1);
  cfg.no_ts = 0;
  rt.fmt = ROUTE_FMT_HLS;
  cfg.no_hls = 1;
  ck_assert_int_eq(route_disabled(&rt), 1);
  cfg.no_hls = 0;
  rt.fmt = ROUTE_FMT_MP4;
  ck_assert_int_eq(route_disabled(&rt), 0);
  cfg.no_mp4 = 1;
  ck_assert_int_eq(route_disabled(&rt), 1);
  g_cfg = NULL;
}
END_TEST

START_TEST(route_client_info_per_route_kind) {
  config_t cfg;
  route_t rt;
  route_item_bufs_t bufs;
  client_info_t ci;
  source_def_t srcs[2];
  memset(&cfg, 0, sizeof cfg);
  memset(&rt, 0, sizeof rt);
  memset(srcs, 0, sizeof srcs);
  srcs[0].ordinal = 1;
  srcs[1].ordinal = 2;
  srcs[1].name = "second";
  cfg.sources = srcs;
  cfg.n_sources = 2;
  cfg.rist_name = "ristname";
  cfg.rist_ordinal = 3;
  cfg.stdin_name = "stdinname";
  cfg.stdin_ordinal = 4;
  g_cfg = &cfg;
  g_lookup_rc = 0;

  rt.kind = ROUTE_RTP;
  rt.fmt = ROUTE_FMT_HLS;
  strcpy(rt.addr, "239.1.1.1");
  rt.port = 5000;
  route_client_info(&rt, 0, NULL, 0x100, "10.0.0.9", 2, &bufs, &ci);
  ck_assert_str_eq(ci.src_proto, "rtp");
  ck_assert_str_eq(ci.src_addr, "239.1.1.1:5000");
  ck_assert_uint_eq(ci.pmt_pid, 0x100u);
  rt.kind = ROUTE_UDP;
  strcpy(rt.src, "10.1.1.1");
  route_client_info(&rt, 0, NULL, 0, "10.0.0.9", 1, &bufs, &ci);
  ck_assert_str_eq(ci.src_proto, "udp");
  ck_assert_str_eq(ci.src_addr, "10.1.1.1@239.1.1.1:5000");
  rt.kind = ROUTE_SRT;
  route_client_info(&rt, 0, NULL, 0, "10.0.0.9", 1, &bufs, &ci);
  ck_assert_str_eq(ci.src_proto, "srt");

  rt.kind = ROUTE_RIST;
  route_client_info(&rt, 0, NULL, 0, "x", 1, &bufs, &ci);
  ck_assert_str_eq(ci.src_proto, "rist");
  ck_assert_int_eq(ci.src_ordinal, 3);
  rt.kind = ROUTE_STDIN;
  route_client_info(&rt, 0, NULL, 0, "x", 1, &bufs, &ci);
  ck_assert_str_eq(ci.src_proto, "stdin");
  ck_assert_int_eq(ci.src_ordinal, 4);

  rt.kind = ROUTE_NAMED_BARE;
  strcpy(rt.src_name, "ristname");
  route_client_info(&rt, 0, NULL, 0, "x", 1, &bufs, &ci);
  ck_assert_str_eq(ci.src_proto, "rist");
  strcpy(rt.src_name, "stdinname");
  route_client_info(&rt, 0, NULL, 0, "x", 1, &bufs, &ci);
  ck_assert_str_eq(ci.src_proto, "stdin");
  strcpy(rt.src_name, "nope");
  route_client_info(&rt, 0, NULL, 0, "x", 1, &bufs, &ci);
  ck_assert_ptr_null(ci.src_proto);

  rt.kind = ROUTE_LIST_ITEM;
  rt.item_num = 2;
  route_client_info(&rt, 2, NULL, 0, "x", 1, &bufs, &ci);
  ck_assert_int_eq(ci.src_ordinal, 2);
  ck_assert_str_eq(ci.src_name, "second");
  ck_assert_uint_eq(ci.item_num, 2u);
  ck_assert_str_eq(ci.src_proto, "rtp");
  ck_assert_ptr_null(ci.item_name);
  rt.kind = ROUTE_LIST_NAME;
  route_client_info(&rt, 9, NULL, 0, "x", 1, &bufs, &ci);
  ck_assert_int_eq(ci.src_ordinal, 0);
  ck_assert_str_eq(ci.item_name, "named");
  g_lookup_rc = 1;
  route_client_info(&rt, 1, NULL, 0, "x", 1, &bufs, &ci);
  ck_assert_ptr_null(ci.src_proto);
  g_cfg = NULL;
}
END_TEST

START_TEST(find_header_name_length_mismatch_and_tiny_buffer) {
  struct phr_header hdrs[2];
  char out[1];
  hdrs[0] = make_header("X-Foo-Long", "v");
  hdrs[1] = make_header("X-Foo", "value");
  ck_assert_int_eq(find_header(hdrs, 2, "X-Foo", out, sizeof out), 1);
  ck_assert_str_eq(out, "");
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
  tcase_add_loop_test(tc, ws_origin_rejects_malformed_hosts_and_origins, 0, 18);
  tcase_add_loop_test(tc, ws_origin_host_list_token_forms, 0, 12);
  tcase_add_test(tc, ws_origin_oversized_list_token_is_skipped);
  tcase_add_test(tc, cors_list_skips_blank_tokens);
  tcase_add_test(tc, special_path_auth_selects_credentials);
  tcase_add_test(tc, route_disabled_follows_kind_and_format_flags);
  tcase_add_test(tc, route_client_info_per_route_kind);
  tcase_add_test(tc, find_header_name_length_mismatch_and_tiny_buffer);
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
