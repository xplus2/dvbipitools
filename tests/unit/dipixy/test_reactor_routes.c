/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "../run_helper.h"
#include "reactor_rig.h"

#define STATUS_AUTH "dXNlcjpwYXNz"

static const char *const NO_ARGS[] = {NULL};
static const char *const METRICS_ARGS[] = {"--metrics-http", NULL};
static const char *const METRICS_AUTH_ARGS[] = {"--metrics-http", "--metrics-auth", "user:pass", NULL};
static const char *const AUTH_ARGS[] = {"--auth", "user:pass", NULL};
static const char *const DLNA_ARGS[] = {"--enable-dlna", "--dlna-host", "127.0.0.1:9080", NULL};
static const char *const HLS_ONLY_ARGS[] = {"--format", "hls", NULL};
static const char *const NO_RTP_ARGS[] = {"--no-url-rtp", NULL};

static size_t request(const run_t *r, const char *method, const char *path, const char *headers, const char *body, char *reply, size_t cap) {
  char req[1024];

  snprintf(req, sizeof req, "%s %s HTTP/1.1\r\nHost: x\r\nConnection: close\r\n%sContent-Length: %zu\r\n\r\n%s", method, path, headers ? headers : "",
           body ? strlen(body) : 0u, body ? body : "");
  return raw_exchange(r->port, req, reply, cap);
}

static int has_status(const char *reply, const char *code) {
  return strncmp(reply, "HTTP/1.1 ", 9) == 0 && strncmp(reply + 9, code, 3) == 0;
}

START_TEST(unsupported_method_is_405) {
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, NO_ARGS);
  request(&r, "PUT", "/", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "405"));
  run_stop(&r);
}
END_TEST

START_TEST(malformed_request_is_400) {
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, NO_ARGS);
  raw_exchange(r.port, "\x01\x02 / HTTP/1.1\r\n\r\n", reply, sizeof reply);
  ck_assert(has_status(reply, "400"));
  run_stop(&r);
}
END_TEST

START_TEST(oversized_request_headers_are_431) {
  run_t r;
  char reply[REPLY_MAX];
  char req[9500];
  size_t n = (size_t)snprintf(req, sizeof req, "GET / HTTP/1.1\r\nX-Pad: ");

  run_start(&r, 0, NO_ARGS);
  memset(req + n, 'a', sizeof req - n - 1);
  req[sizeof req - 1] = '\0';
  raw_exchange(r.port, req, reply, sizeof reply);
  ck_assert(has_status(reply, "431"));
  run_stop(&r);
}
END_TEST

START_TEST(oversized_request_body_is_400) {
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, NO_ARGS);
  raw_exchange(r.port, "POST /dlna/cd_control HTTP/1.1\r\nHost: x\r\nContent-Length: 100000\r\n\r\n", reply, sizeof reply);
  ck_assert(has_status(reply, "400"));
  run_stop(&r);
}
END_TEST

static size_t count_responses(const char *reply) {
  size_t n = 0;
  for (const char *p = reply; (p = strstr(p, "HTTP/1.1 ")) != NULL; p += 9) n++;
  return n;
}

START_TEST(get_body_is_not_parsed_as_next_request) {
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, NO_ARGS);
  raw_exchange(r.port,
               "GET /index.html HTTP/1.1\r\nHost: x\r\nContent-Length: 40\r\n\r\nGET /index.html HTTP/1.1\r\nHost: x\r\n\r\n"
               "GET /index.html HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n",
               reply, sizeof reply);
  ck_assert_uint_eq(count_responses(reply), 2);
  run_stop(&r);
}
END_TEST

START_TEST(transfer_encoding_is_501) {
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, NO_ARGS);
  raw_exchange(r.port, "POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n", reply, sizeof reply);
  ck_assert(has_status(reply, "501"));
  run_stop(&r);
}
END_TEST

START_TEST(transfer_encoding_with_content_length_is_400) {
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, NO_ARGS);
  raw_exchange(r.port, "POST / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\nContent-Length: 5\r\n\r\n0\r\n\r\n", reply, sizeof reply);
  ck_assert(has_status(reply, "400"));
  run_stop(&r);
}
END_TEST

START_TEST(bad_content_length_is_400) {
  static const char *const reqs[] = {
      "GET / HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nContent-Length: 5\r\n\r\nabcde",
      "GET / HTTP/1.1\r\nHost: x\r\nContent-Length: +5\r\n\r\nabcde",
      "GET / HTTP/1.1\r\nHost: x\r\nContent-Length: -1\r\n\r\n",
      "GET / HTTP/1.1\r\nHost: x\r\nContent-Length: \r\n\r\n",
  };
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, NO_ARGS);
  for (size_t i = 0; i < sizeof reqs / sizeof reqs[0]; i++) {
    raw_exchange(r.port, reqs[i], reply, sizeof reply);
    ck_assert_msg(has_status(reply, "400"), "case %zu", i);
  }
  run_stop(&r);
}
END_TEST

START_TEST(head_requests_get_headers_only) {
  run_t r;
  char reply[REPLY_MAX];
  const char *body;

  run_start(&r, 0, NO_ARGS);
  request(&r, "HEAD", "/", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "200"));
  body = strstr(reply, "\r\n\r\n");
  ck_assert_ptr_nonnull(body);
  ck_assert_int_eq(body[4], '\0');
  request(&r, "HEAD", "/index.html", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "200"));
  run_stop(&r);
}
END_TEST

START_TEST(status_script_is_json) {
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, NO_ARGS);
  request(&r, "GET", "/ui/status.js", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "200"));
  ck_assert_ptr_nonnull(strstr(reply, "Content-Type: application/json"));
  run_stop(&r);
}
END_TEST

START_TEST(metrics_are_off_by_default) {
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, NO_ARGS);
  request(&r, "GET", "/metrics", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "404"));
  run_stop(&r);
}
END_TEST

START_TEST(metrics_are_served_when_enabled) {
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, METRICS_ARGS);
  request(&r, "GET", "/metrics", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "200"));
  ck_assert_ptr_nonnull(strstr(reply, "text/plain; version=0.0.4"));
  run_stop(&r);
}
END_TEST

START_TEST(metrics_auth_guards_only_metrics) {
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, METRICS_AUTH_ARGS);
  request(&r, "GET", "/metrics", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "401"));
  ck_assert_ptr_nonnull(strstr(reply, "WWW-Authenticate: Basic"));
  request(&r, "GET", "/metrics", "Authorization: Basic " STATUS_AUTH "\r\n", NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "200"));
  request(&r, "GET", "/", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "200"));
  run_stop(&r);
}
END_TEST

START_TEST(status_auth_guards_the_status_page) {
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, AUTH_ARGS);
  request(&r, "GET", "/", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "401"));
  request(&r, "GET", "/ui/status.js", "Authorization: Basic wrong\r\n", NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "401"));
  request(&r, "GET", "/", "Authorization: Basic " STATUS_AUTH "\r\n", NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "200"));
  run_stop(&r);
}
END_TEST

START_TEST(playlist_export_renders_each_type) {
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, NO_ARGS);
  request(&r, "GET", "/export/ts/m3u", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "200"));
  ck_assert_ptr_nonnull(strstr(reply, "audio/x-mpegurl"));
  request(&r, "GET", "/export/ts/xspf", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "200"));
  ck_assert_ptr_nonnull(strstr(reply, "application/xspf+xml"));
  request(&r, "GET", "/export/ts/m3u?plain", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "200"));
  ck_assert_ptr_nonnull(strstr(reply, "text/plain; charset=utf-8"));
  request(&r, "HEAD", "/export/ts/m3u", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "200"));
  run_stop(&r);
}
END_TEST

START_TEST(playlist_export_rejects_unknown_and_disabled_formats) {
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, HLS_ONLY_ARGS);
  request(&r, "GET", "/export/bogus/m3u", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "404"));
  request(&r, "GET", "/export/ts/m3u", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "404"));
  request(&r, "GET", "/export/hls/m3u", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "200"));
  run_stop(&r);
}
END_TEST

START_TEST(disabled_url_kinds_are_404) {
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, NO_RTP_ARGS);
  request(&r, "GET", "/rtp/239.1.1.1:5000", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "404"));
  run_stop(&r);
}
END_TEST

START_TEST(unresolvable_sources_are_404_for_every_format) {
  static const char *const paths[] = {
    "/list/1/1", "/list/1/1/ts", "/list/1/1/spts", "/list/1/1/rawaudio", "/list/1/1/hls", "/list/1/1/hls-fmp4", "/list/1/1/llhls",
    "/list/1/1/dash", "/list/1/1/lldash", "/list/1/1/mp4", "/list/1/1/seg00001.ts", "/list/1/1/dseg1000.m4s", "/list/1/1/init.mp4",
    "/stdin", "/stdin/ts", "/rist", "/rist/hls", "/nosuchname", "/nosuchname/ts", "/list/nosuchlist/nosuchitem", "/list/1/nosuchitem/ts",
  };
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, NO_ARGS);
  for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
    request(&r, "GET", paths[i], NULL, NULL, reply, sizeof reply);
    ck_assert_msg(has_status(reply, "404"), "%s: %.40s", paths[i], reply);
    request(&r, "HEAD", paths[i], NULL, NULL, reply, sizeof reply);
    ck_assert_msg(has_status(reply, "404"), "HEAD %s: %.40s", paths[i], reply);
  }
  run_stop(&r);
}
END_TEST

START_TEST(dlna_requests_are_404_when_disabled) {
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, NO_ARGS);
  request(&r, "POST", "/dlna/cd_control", NULL, "<x/>", reply, sizeof reply);
  ck_assert(has_status(reply, "404"));
  request(&r, "GET", "/dlna/desc.xml", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "404"));
  run_stop(&r);
}
END_TEST

START_TEST(dlna_descriptions_are_served) {
  static const char *const paths[] = {"/dlna/desc.xml", "/dlna/cd_scpd.xml", "/dlna/cm_scpd.xml"};
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, DLNA_ARGS);
  for (size_t i = 0; i < sizeof paths / sizeof paths[0]; i++) {
    request(&r, "GET", paths[i], NULL, NULL, reply, sizeof reply);
    ck_assert_msg(has_status(reply, "200"), "%s: %.40s", paths[i], reply);
    ck_assert_ptr_nonnull(strstr(reply, "text/xml; charset=utf-8"));
  }
  run_stop(&r);
}
END_TEST

START_TEST(dlna_control_runs_soap_actions) {
  static const char browse[] =
    "<ObjectID>0</ObjectID><BrowseFlag>BrowseMetadata</BrowseFlag><StartingIndex>0</StartingIndex><RequestedCount>0</RequestedCount>";
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, DLNA_ARGS);
  request(&r, "POST", "/dlna/cd_control", "SOAPACTION: \"urn:schemas-upnp-org:service:ContentDirectory:1#Browse\"\r\n", browse, reply, sizeof reply);
  ck_assert(has_status(reply, "200"));
  ck_assert_ptr_nonnull(strstr(reply, "text/xml; charset=utf-8"));
  request(&r, "POST", "/dlna/cm_control", "SOAPACTION: \"urn:schemas-upnp-org:service:ConnectionManager:1#GetProtocolInfo\"\r\n", "", reply, sizeof reply);
  ck_assert(has_status(reply, "200"));
  request(&r, "POST", "/dlna/cd_control", "SOAPACTION: \"urn:schemas-upnp-org:service:ContentDirectory:1#NoSuchAction\"\r\n", "", reply, sizeof reply);
  ck_assert(has_status(reply, "500"));
  request(&r, "POST", "/dlna/cd_control", NULL, browse, reply, sizeof reply);
  ck_assert(has_status(reply, "400"));
  request(&r, "POST", "/dlna/other", NULL, "<x/>", reply, sizeof reply);
  ck_assert(has_status(reply, "405"));
  run_stop(&r);
}
END_TEST

START_TEST(dlna_events_subscribe_renew_and_unsubscribe) {
  run_t r;
  char reply[REPLY_MAX];
  char sid[96];
  char hdr[160];
  const char *at;
  size_t n = 0;

  run_start(&r, 0, DLNA_ARGS);
  request(&r, "SUBSCRIBE", "/dlna/cd_event", "CALLBACK: <http://127.0.0.1:1/>\r\nNT: upnp:event\r\n", NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "200"));
  at = strstr(reply, "SID: ");
  ck_assert_ptr_nonnull(at);
  at += 5;
  while (at[n] && at[n] != '\r' && n < sizeof sid - 1) {
    sid[n] = at[n];
    n++;
  }
  sid[n] = '\0';
  snprintf(hdr, sizeof hdr, "SID: %s\r\n", sid);
  request(&r, "SUBSCRIBE", "/dlna/cd_event", hdr, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "200"));
  request(&r, "UNSUBSCRIBE", "/dlna/cd_event", hdr, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "200"));
  request(&r, "SUBSCRIBE", "/dlna/other", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "405"));
  request(&r, "UNSUBSCRIBE", "/dlna/other", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "405"));
  run_stop(&r);
}
END_TEST

START_TEST(pipelined_requests_are_answered_in_order) {
  static const char reqs[] =
    "GET /no/such/route HTTP/1.1\r\nHost: x\r\n\r\n"
    "GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
  run_t r;
  char reply[REPLY_MAX];
  const char *second;

  run_start(&r, 0, NO_ARGS);
  raw_exchange(r.port, reqs, reply, sizeof reply);
  ck_assert(has_status(reply, "404"));
  ck_assert_ptr_nonnull(strstr(reply, "Connection: keep-alive"));
  second = strstr(reply + 9, "HTTP/1.1 200");
  ck_assert_ptr_nonnull(second);
  run_stop(&r);
}
END_TEST

START_TEST(requests_split_across_packets_are_reassembled) {
  static const char head[] = "GET /no/such/route HTTP/1.1\r\nHost: x\r\n";
  static const char tail[] = "Connection: close\r\n\r\n";
  run_t r;
  char reply[REPLY_MAX];
  struct timespec ts = {0, 50000000};
  int fd;

  run_start(&r, 0, NO_ARGS);
  fd = connect_to(r.port);
  ck_assert_int_eq((int)write(fd, head, sizeof head - 1), (int)sizeof head - 1);
  nanosleep(&ts, NULL);
  ck_assert_int_eq((int)write(fd, tail, sizeof tail - 1), (int)sizeof tail - 1);
  read_reply(fd, NULL, reply, sizeof reply);
  close(fd);
  ck_assert(has_status(reply, "404"));
  run_stop(&r);
}
END_TEST

START_TEST(request_bodies_split_across_packets_are_reassembled) {
  static const char head[] = "POST /dlna/other HTTP/1.1\r\nHost: x\r\nConnection: close\r\nContent-Length: 4\r\n\r\n<x";
  static const char tail[] = "/>";
  run_t r;
  char reply[REPLY_MAX];
  struct timespec ts = {0, 50000000};
  int fd;

  run_start(&r, 0, DLNA_ARGS);
  fd = connect_to(r.port);
  ck_assert_int_eq((int)write(fd, head, sizeof head - 1), (int)sizeof head - 1);
  nanosleep(&ts, NULL);
  ck_assert_int_eq((int)write(fd, tail, sizeof tail - 1), (int)sizeof tail - 1);
  read_reply(fd, NULL, reply, sizeof reply);
  close(fd);
  ck_assert(has_status(reply, "405"));
  run_stop(&r);
}
END_TEST

START_TEST(http_1_0_requests_close_by_default) {
  run_t r;
  char reply[REPLY_MAX];

  run_start(&r, 0, NO_ARGS);
  raw_exchange(r.port, "GET /no/such/route HTTP/1.0\r\n\r\n", reply, sizeof reply);
  ck_assert(has_status(reply, "404"));
  ck_assert_ptr_nonnull(strstr(reply, "Connection: close"));
  run_stop(&r);
}
END_TEST

static void write_m3u(char *path, size_t cap, const char *content) {
  int fd;
  FILE *f;

  snprintf(path, cap, "/tmp/dvbipitools_test_routes_XXXXXX.m3u");
  fd = mkstemps(path, 4);
  ck_assert_int_ge(fd, 0);
  f = fdopen(fd, "w");
  ck_assert_ptr_nonnull(f);
  fputs(content, f);
  fclose(f);
}

static route_t make_route(route_kind_t kind) {
  route_t rt;

  memset(&rt, 0, sizeof rt);
  rt.kind = kind;
  rt.fmt = ROUTE_FMT_TS;
  rt.family = AF_INET;
  strcpy(rt.addr, "239.1.1.1");
  rt.port = 5000;
  return rt;
}

START_TEST(client_info_describes_direct_network_routes) {
  config_t cfg;
  route_item_bufs_t bufs;
  client_info_t info;
  route_t rt;

  memset(&cfg, 0, sizeof cfg);
  reactor_set_context(&cfg, NULL, NULL);

  rt = make_route(ROUTE_RTP);
  strcpy(rt.src, "10.0.0.1");
  route_client_info(&rt, 0, NULL, 0x100, "192.0.2.7", 2, &bufs, &info);
  ck_assert_str_eq(info.src_proto, "rtp");
  ck_assert_str_eq(info.src_addr, "10.0.0.1@239.1.1.1:5000");
  ck_assert_str_eq(info.ip, "192.0.2.7");
  ck_assert_int_eq(info.http_ver, 2);
  ck_assert_uint_eq(info.pmt_pid, 0x100u);

  rt = make_route(ROUTE_UDP);
  route_client_info(&rt, 0, NULL, 0, "192.0.2.7", 1, &bufs, &info);
  ck_assert_str_eq(info.src_proto, "udp");
  ck_assert_str_eq(info.src_addr, "239.1.1.1:5000");

  rt = make_route(ROUTE_SRT);
  route_client_info(&rt, 0, NULL, 0, "192.0.2.7", 1, &bufs, &info);
  ck_assert_str_eq(info.src_proto, "srt");
}
END_TEST

START_TEST(client_info_describes_rist_stdin_and_named_routes) {
  config_t cfg;
  route_item_bufs_t bufs;
  client_info_t info;
  route_t rt;

  memset(&cfg, 0, sizeof cfg);
  cfg.rist_ordinal = 2;
  cfg.rist_name = "feed";
  cfg.stdin_ordinal = 3;
  cfg.stdin_name = "pipe";
  reactor_set_context(&cfg, NULL, NULL);

  rt = make_route(ROUTE_RIST);
  route_client_info(&rt, 0, NULL, 0, "192.0.2.7", 1, &bufs, &info);
  ck_assert_str_eq(info.src_proto, "rist");
  ck_assert_int_eq(info.src_ordinal, 2);
  ck_assert_str_eq(info.src_name, "feed");

  rt = make_route(ROUTE_STDIN);
  route_client_info(&rt, 0, NULL, 0, "192.0.2.7", 1, &bufs, &info);
  ck_assert_str_eq(info.src_proto, "stdin");
  ck_assert_int_eq(info.src_ordinal, 3);
  ck_assert_str_eq(info.src_name, "pipe");

  rt = make_route(ROUTE_NAMED_BARE);
  strcpy(rt.src_name, "feed");
  route_client_info(&rt, 0, NULL, 0, "192.0.2.7", 1, &bufs, &info);
  ck_assert_str_eq(info.src_proto, "rist");
  ck_assert_int_eq(info.src_ordinal, 2);

  strcpy(rt.src_name, "pipe");
  route_client_info(&rt, 0, NULL, 0, "192.0.2.7", 1, &bufs, &info);
  ck_assert_str_eq(info.src_proto, "stdin");
  ck_assert_int_eq(info.src_ordinal, 3);

  strcpy(rt.src_name, "nobody");
  route_client_info(&rt, 0, NULL, 0, "192.0.2.7", 1, &bufs, &info);
  ck_assert_ptr_null(info.src_proto);
}
END_TEST

START_TEST(list_routes_resolve_by_ordinal_item_and_name) {
  config_t cfg;
  source_def_t src;
  channels_t *ch;
  route_item_bufs_t bufs;
  client_info_t info;
  unsigned list_num = 0;
  char path[96];
  route_t rt;

  write_m3u(path, sizeof path, "#EXTINF:-1,First\nrtp://@239.1.1.1:5000\n#EXTINF:-1,Second\nudp://@239.1.1.2:6000\n");
  memset(&cfg, 0, sizeof cfg);
  memset(&src, 0, sizeof src);
  src.kind = SRC_M3U;
  src.value = path;
  src.ordinal = 1;
  src.name = "pl";
  cfg.sources = &src;
  cfg.n_sources = 1;
  ch = channels_build(&cfg);
  unlink(path);
  ck_assert_ptr_nonnull(ch);
  reactor_set_context(&cfg, ch, NULL);

  rt = make_route(ROUTE_LIST_ITEM);
  rt.list_num = 1;
  rt.item_num = 2;
  route_client_info(&rt, 1, NULL, 0, "192.0.2.7", 1, &bufs, &info);
  ck_assert_int_eq(info.src_ordinal, 1);
  ck_assert_str_eq(info.src_name, "pl");
  ck_assert_uint_eq(info.item_num, 2u);
  ck_assert_str_eq(info.item_name, "Second");

  rt = make_route(ROUTE_LIST_NAME);
  strcpy(rt.item_name, "First");
  route_client_info(&rt, 1, NULL, 0, "192.0.2.7", 1, &bufs, &info);
  ck_assert_uint_eq(info.item_num, 1u);

  rt = make_route(ROUTE_LIST_ITEM);
  rt.item_num = 9;
  route_client_info(&rt, 7, NULL, 0, "192.0.2.7", 1, &bufs, &info);
  ck_assert_ptr_null(info.item_name);
  ck_assert_int_eq(info.src_ordinal, 0);

  rt = make_route(ROUTE_LIST_ITEM);
  strcpy(rt.src_name, "pl");
  rt.item_num = 9;
  ck_assert_ptr_null(open_source(&rt, &list_num));
  ck_assert_uint_eq(list_num, 1u);

  strcpy(rt.src_name, "missing");
  list_num = 0;
  ck_assert_ptr_null(open_source(&rt, &list_num));
  ck_assert_uint_eq(list_num, 0u);

  channels_free(ch);
}
END_TEST

START_TEST(open_source_without_matching_input_is_null) {
  config_t cfg;
  route_t rt;

  memset(&cfg, 0, sizeof cfg);
  cfg.rist_name = "feed";
  cfg.stdin_name = "pipe";
  reactor_set_context(&cfg, NULL, NULL);

  rt = make_route(ROUTE_RIST);
  ck_assert_ptr_null(open_source(&rt, NULL));
  rt = make_route(ROUTE_STDIN);
  ck_assert_ptr_null(open_source(&rt, NULL));
  rt = make_route(ROUTE_NAMED_BARE);
  strcpy(rt.src_name, "feed");
  ck_assert_ptr_null(open_source(&rt, NULL));
  strcpy(rt.src_name, "pipe");
  ck_assert_ptr_null(open_source(&rt, NULL));
  strcpy(rt.src_name, "nobody");
  ck_assert_ptr_null(open_source(&rt, NULL));
}
END_TEST

typedef struct {
  route_kind_t kind;
  route_fmt_t fmt;
  size_t flag_off;
} disabled_case_t;

static const disabled_case_t disabled_cases[] = {
    {ROUTE_RTP, ROUTE_FMT_HLS, offsetof(config_t, no_url_rtp)},
    {ROUTE_UDP, ROUTE_FMT_HLS, offsetof(config_t, no_url_udp)},
    {ROUTE_SRT, ROUTE_FMT_HLS, offsetof(config_t, no_url_srt)},
    {ROUTE_RIST, ROUTE_FMT_TS, offsetof(config_t, no_ts)},
    {ROUTE_RIST, ROUTE_FMT_SPTS, offsetof(config_t, no_spts)},
    {ROUTE_RIST, ROUTE_FMT_HLS, offsetof(config_t, no_hls)},
    {ROUTE_RIST, ROUTE_FMT_HLS_FMP4, offsetof(config_t, no_hls)},
    {ROUTE_RIST, ROUTE_FMT_LLHLS, offsetof(config_t, no_llhls)},
    {ROUTE_RIST, ROUTE_FMT_DASH, offsetof(config_t, no_dash)},
    {ROUTE_RIST, ROUTE_FMT_LLDASH, offsetof(config_t, no_lldash)},
    {ROUTE_RIST, ROUTE_FMT_RAWAUDIO, offsetof(config_t, no_rawaudio)},
    {ROUTE_RIST, ROUTE_FMT_MP4, offsetof(config_t, no_mp4)},
};

START_TEST(route_disabled_follows_each_config_flag) {
  const disabled_case_t *dc = &disabled_cases[_i];
  config_t cfg;
  route_t rt = make_route(dc->kind);

  memset(&cfg, 0, sizeof cfg);
  reactor_set_context(&cfg, NULL, NULL);
  rt.fmt = dc->fmt;
  ck_assert_int_eq(route_disabled(&rt), 0);
  *(int *)((char *)&cfg + dc->flag_off) = 1;
  ck_assert_int_eq(route_disabled(&rt), 1);
}
END_TEST

#define LIVE_SLOT 41

static void live_path(char *out, size_t cap, const char *suffix) {
  snprintf(out, cap, "/udp/%.31s:%u%s", run_helper_group_n(LIVE_SLOT), run_helper_port(LIVE_SLOT), suffix);
}

START_TEST(head_on_a_live_source_answers_headers_for_stream_formats) {
  static const char *const suffixes[] = {"/ts", "/spts", "/rawaudio", "/mp4"};
  char path[128];
  char reply[REPLY_MAX];
  run_t r;

  run_start(&r, 0, NO_ARGS);
  for (size_t i = 0; i < sizeof suffixes / sizeof suffixes[0]; i++) {
    live_path(path, sizeof path, suffixes[i]);
    request(&r, "HEAD", path, NULL, NULL, reply, sizeof reply);
    ck_assert_msg(has_status(reply, "200"), "HEAD %s: %.40s", path, reply);
    ck_assert_msg(strstr(reply, "Content-Type: video/") || strstr(reply, "Content-Type: audio/"), "%s: %s", path, reply);
  }
  run_stop(&r);
}
END_TEST

START_TEST(unknown_segments_of_a_live_source_are_404_without_waiting) {
  static const char *const suffixes[] = {
      "/seg00007.ts", "/seg00007.m4s", "/init.mp4", "/seg00007.00.ts", "/dseg500.m4s",
  };
  char path[128];
  char reply[REPLY_MAX];
  run_t r;

  run_start(&r, 0, NO_ARGS);
  for (size_t i = 0; i < sizeof suffixes / sizeof suffixes[0]; i++) {
    live_path(path, sizeof path, suffixes[i]);
    request(&r, "GET", path, NULL, NULL, reply, sizeof reply);
    ck_assert_msg(has_status(reply, "404"), "GET %s: %.40s", path, reply);
    request(&r, "HEAD", path, NULL, NULL, reply, sizeof reply);
    ck_assert_msg(has_status(reply, "404"), "HEAD %s: %.40s", path, reply);
  }
  run_stop(&r);
}
END_TEST

START_TEST(dlna_subprotocol_rejects_wrong_method_and_path_pairs) {
  static const struct {
    const char *method;
    const char *path;
  } cases[] = {
      {"GET", "/dlna/cd_control"},
      {"HEAD", "/dlna/cd_control"},
      {"POST", "/dlna/unknown"},
      {"SUBSCRIBE", "/dlna/unknown"},
      {"UNSUBSCRIBE", "/dlna/unknown"},
  };
  char reply[REPLY_MAX];
  run_t r;

  run_start(&r, 0, DLNA_ARGS);
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
    request(&r, cases[i].method, cases[i].path, cases[i].method[0] == 'S' ? "NT: upnp:event\r\nCALLBACK: <http://127.0.0.1:1/>\r\n" : NULL, NULL, reply, sizeof reply);
    ck_assert_msg(has_status(reply, "405") || has_status(reply, "404"), "%s %s: %.40s", cases[i].method, cases[i].path, reply);
  }
  run_stop(&r);
}
END_TEST

static const char *const EXPORT_FMTS[] = {"ts", "spts", "rawaudio", "hls", "hls-fmp4", "llhls", "dash", "lldash", "mp4"};

START_TEST(playlist_export_names_every_format_and_honours_host_and_multicast_options) {
  char path[96];
  char reply[8192];
  char url[96];
  const char *args[] = {"-i", path, "-n", "My List", NULL};
  run_t r;

  write_m3u(path, sizeof path, "#EXTINF:-1,SSM Channel\nrtp://10.9.9.9@239.1.1.1:5000\n");
  run_start(&r, 0, args);
  unlink(path);

  for (size_t i = 0; i < sizeof EXPORT_FMTS / sizeof EXPORT_FMTS[0]; i++) {
    char want[48];

    snprintf(url, sizeof url, "/export/%s/m3u?plain&host=h.example:1234", EXPORT_FMTS[i]);
    request(&r, "GET", url, NULL, NULL, reply, sizeof reply);
    ck_assert_msg(has_status(reply, "200"), "%s: %.40s", url, reply);
    snprintf(want, sizeof want, "http://h.example:1234/My%%20List/item/1/%s", EXPORT_FMTS[i]);
    ck_assert_msg(strstr(reply, want) != NULL, "%s: missing %s", url, want);
  }

  request(&r, "GET", "/export/ts/m3u?plain&host=h.example", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "200"));
  ck_assert_ptr_nonnull(strstr(reply, "http://h.example:"));

  request(&r, "GET", "/export/ts/m3u?plain&keep_multicast", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "200"));
  ck_assert_ptr_nonnull(strstr(reply, "rtp://10.9.9.9@239.1.1.1:5000"));
  run_stop(&r);
}
END_TEST

START_TEST(playlist_export_refuses_every_disabled_format) {
  static const char *const args[] = {"--format", "ts", NULL};
  char url[64];
  char reply[REPLY_MAX];
  run_t r;

  run_start(&r, 0, args);
  for (size_t i = 1; i < sizeof EXPORT_FMTS / sizeof EXPORT_FMTS[0]; i++) {
    snprintf(url, sizeof url, "/export/%s/m3u", EXPORT_FMTS[i]);
    request(&r, "GET", url, NULL, NULL, reply, sizeof reply);
    ck_assert_msg(has_status(reply, "404"), "%s: %.40s", url, reply);
  }
  request(&r, "GET", "/export/ts/m3u", NULL, NULL, reply, sizeof reply);
  ck_assert(has_status(reply, "200"));
  run_stop(&r);
}
END_TEST

static void send_get(int fd, const char *path) {
  char req[256];
  int n = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n", path);

  ck_assert_int_eq((int)write(fd, req, (size_t)n), n);
}

static size_t read_reply_within(int fd, int wait_ms, char *buf, size_t cap) {
  size_t total = 0;
  struct pollfd pfd = {.fd = fd, .events = POLLIN};

  while (total < cap - 1 && poll(&pfd, 1, wait_ms) > 0) {
    ssize_t n = read(fd, buf + total, cap - 1 - total);

    if (n <= 0) break;
    total += (size_t)n;
  }
  buf[total] = '\0';
  return total;
}

START_TEST(cold_manifest_requests_expire_unserved_and_abandoned_ones_are_dropped) {
  static const char *const suffixes[] = {"/hls", "/llhls", "/dash"};
  static const char *const args[] = {"--segment-size", "2", NULL};
  char path[128];
  char reply[REPLY_MAX];
  int fds[3];
  int gone;
  run_t r;

  run_start(&r, 0, args);
  for (size_t i = 0; i < 3; i++) {
    live_path(path, sizeof path, suffixes[i]);
    fds[i] = connect_to(r.port);
    send_get(fds[i], path);
  }
  live_path(path, sizeof path, "/hls");
  gone = connect_to(r.port);
  send_get(gone, path);
  usleep(200000);
  close(gone);

  for (size_t i = 0; i < 3; i++) {
    read_reply_within(fds[i], 20000, reply, sizeof reply);
    ck_assert_msg(has_status(reply, "404") || has_status(reply, "503"), "%s: %.40s", suffixes[i], reply);
    close(fds[i]);
  }
  run_stop(&r);
}
END_TEST

START_TEST(cold_ts_requests_time_out_as_504_and_abandoned_ones_are_dropped) {
  static const char *const args[] = {"--ts-startup-timeout", "0.4", NULL};
  char path[128];
  char reply[REPLY_MAX];
  int fd;
  int gone;
  run_t r;

  run_start(&r, 0, args);
  live_path(path, sizeof path, "/ts");
  fd = connect_to(r.port);
  send_get(fd, path);
  gone = connect_to(r.port);
  send_get(gone, path);
  usleep(100000);
  close(gone);

  read_reply_within(fd, 5000, reply, sizeof reply);
  ck_assert_msg(has_status(reply, "504"), "%.40s", reply);
  close(fd);
  run_stop(&r);
}
END_TEST

static Suite *routes_suite(void) {
  Suite *s = suite_create("dipixy_reactor_routes");
  TCase *tc = tcase_create("core");

  tcase_set_timeout(tc, 60);
  tcase_add_test(tc, unsupported_method_is_405);
  tcase_add_test(tc, malformed_request_is_400);
  tcase_add_test(tc, oversized_request_headers_are_431);
  tcase_add_test(tc, oversized_request_body_is_400);
  tcase_add_test(tc, get_body_is_not_parsed_as_next_request);
  tcase_add_test(tc, transfer_encoding_is_501);
  tcase_add_test(tc, transfer_encoding_with_content_length_is_400);
  tcase_add_test(tc, bad_content_length_is_400);
  tcase_add_test(tc, head_requests_get_headers_only);
  tcase_add_test(tc, status_script_is_json);
  tcase_add_test(tc, metrics_are_off_by_default);
  tcase_add_test(tc, metrics_are_served_when_enabled);
  tcase_add_test(tc, metrics_auth_guards_only_metrics);
  tcase_add_test(tc, status_auth_guards_the_status_page);
  tcase_add_test(tc, playlist_export_renders_each_type);
  tcase_add_test(tc, playlist_export_rejects_unknown_and_disabled_formats);
  tcase_add_test(tc, disabled_url_kinds_are_404);
  tcase_add_test(tc, unresolvable_sources_are_404_for_every_format);
  tcase_add_test(tc, dlna_requests_are_404_when_disabled);
  tcase_add_test(tc, dlna_descriptions_are_served);
  tcase_add_test(tc, dlna_control_runs_soap_actions);
  tcase_add_test(tc, dlna_events_subscribe_renew_and_unsubscribe);
  tcase_add_test(tc, pipelined_requests_are_answered_in_order);
  tcase_add_test(tc, requests_split_across_packets_are_reassembled);
  tcase_add_test(tc, request_bodies_split_across_packets_are_reassembled);
  tcase_add_test(tc, http_1_0_requests_close_by_default);
  tcase_add_test(tc, head_on_a_live_source_answers_headers_for_stream_formats);
  tcase_add_test(tc, unknown_segments_of_a_live_source_are_404_without_waiting);
  tcase_add_test(tc, dlna_subprotocol_rejects_wrong_method_and_path_pairs);
  tcase_add_test(tc, playlist_export_names_every_format_and_honours_host_and_multicast_options);
  tcase_add_test(tc, playlist_export_refuses_every_disabled_format);
  tcase_add_test(tc, cold_manifest_requests_expire_unserved_and_abandoned_ones_are_dropped);
  tcase_add_test(tc, cold_ts_requests_time_out_as_504_and_abandoned_ones_are_dropped);
  tcase_add_test(tc, client_info_describes_direct_network_routes);
  tcase_add_test(tc, client_info_describes_rist_stdin_and_named_routes);
  tcase_add_test(tc, list_routes_resolve_by_ordinal_item_and_name);
  tcase_add_test(tc, open_source_without_matching_input_is_null);
  tcase_add_loop_test(tc, route_disabled_follows_each_config_flag, 0, (int)(sizeof disabled_cases / sizeof disabled_cases[0]));
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(routes_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
