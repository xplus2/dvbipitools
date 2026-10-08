/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

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

static Suite *routes_suite(void) {
  Suite *s = suite_create("dipixy_reactor_routes");
  TCase *tc = tcase_create("core");

  tcase_set_timeout(tc, 60);
  tcase_add_test(tc, unsupported_method_is_405);
  tcase_add_test(tc, malformed_request_is_400);
  tcase_add_test(tc, oversized_request_headers_are_431);
  tcase_add_test(tc, oversized_request_body_is_400);
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
