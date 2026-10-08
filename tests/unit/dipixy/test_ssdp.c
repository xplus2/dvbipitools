/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "dipixy/cli/args.h"
#include "dipixy/dlna/ssdp.h"

static const char HOST[] = "127.0.0.1:9080";

typedef struct {
  int server;
  int client;
  struct sockaddr_in client_addr;
} rig_t;

static void init_cfg(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
  cfg->enable_dlna = 1;
  cfg->ssdp_ttl = 1;
  cfg->ssdp_interval_s = 3600.0;
  cfg->ssdp_max_age_s = 1800;
  memcpy(cfg->dlna_host, HOST, sizeof HOST);
}

static int loopback_socket(struct sockaddr_in *bound) {
  struct timeval tv = {0, 200000};
  socklen_t len = sizeof *bound;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);

  ck_assert_int_ge(fd, 0);
  memset(bound, 0, sizeof *bound);
  bound->sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &bound->sin_addr);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)bound, sizeof *bound), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)bound, &len), 0);
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  return fd;
}

static void rig_open(rig_t *r) {
  struct sockaddr_in server_addr;

  r->server = loopback_socket(&server_addr);
  r->client = loopback_socket(&r->client_addr);
}

static void rig_close(rig_t *r) {
  close(r->server);
  close(r->client);
}

static void rig_search(rig_t *r, const config_t *cfg, const char *st_line) {
  char uuid[37];
  char req[256];

  ssdp_device_uuid(cfg, uuid);
  snprintf(req, sizeof req, "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\n%s\r\n", st_line);
  ssdp_handle_msearch(r->server, req, (struct sockaddr *)&r->client_addr, sizeof r->client_addr, cfg, uuid);
}

static int rig_recv(rig_t *r, char *buf, size_t sz) {
  ssize_t n = recv(r->client, buf, sz - 1, 0);

  if (n < 0) return 0;
  buf[n] = '\0';
  return 1;
}

START_TEST(msearch_header_finds_names_case_insensitively) {
  const char *headers = "HOST: 239.255.255.250:1900\r\nman: \"ssdp:discover\"\r\nSt:   ssdp:all\r\n\r\nST: ignored\r\n";
  char out[64];

  ck_assert_int_eq(ssdp_msearch_header(headers, "ST", out, sizeof out), 1);
  ck_assert_str_eq(out, "ssdp:all");
  ck_assert_int_eq(ssdp_msearch_header(headers, "MAN", out, sizeof out), 1);
  ck_assert_str_eq(out, "\"ssdp:discover\"");
}
END_TEST

START_TEST(msearch_header_misses_are_reported) {
  char out[64];

  ck_assert_int_eq(ssdp_msearch_header("HOST: x\r\n\r\nST: after-blank\r\n", "ST", out, sizeof out), 0);
  ck_assert_int_eq(ssdp_msearch_header("STX: x\r\n", "ST", out, sizeof out), 0);
  ck_assert_int_eq(ssdp_msearch_header("ST: no-line-end", "ST", out, sizeof out), 0);
  ck_assert_int_eq(ssdp_msearch_header("", "ST", out, sizeof out), 0);
}
END_TEST

START_TEST(msearch_header_truncates_to_the_buffer) {
  char out[5];

  ck_assert_int_eq(ssdp_msearch_header("ST: abcdefgh\r\n", "ST", out, sizeof out), 1);
  ck_assert_str_eq(out, "abcd");
}
END_TEST

START_TEST(device_uuid_is_a_stable_version_4_uuid) {
  config_t cfg;
  char a[37];
  char b[37];

  init_cfg(&cfg);
  ssdp_device_uuid(&cfg, a);
  ssdp_device_uuid(&cfg, b);
  ck_assert_str_eq(a, b);
  ck_assert_uint_eq(strlen(a), 36u);
  ck_assert_int_eq(a[8], '-');
  ck_assert_int_eq(a[13], '-');
  ck_assert_int_eq(a[18], '-');
  ck_assert_int_eq(a[23], '-');
  ck_assert_int_eq(a[14], '4');
  ck_assert_ptr_nonnull(strchr("89ab", a[19]));
}
END_TEST

START_TEST(search_all_answers_for_every_type) {
  config_t cfg;
  rig_t r;
  char buf[1024];
  int replies = 0;

  init_cfg(&cfg);
  rig_open(&r);
  rig_search(&r, &cfg, "ST: ssdp:all\r\n");
  while (rig_recv(&r, buf, sizeof buf)) {
    ck_assert_int_eq(strncmp(buf, "HTTP/1.1 200 OK", 15), 0);
    ck_assert_ptr_nonnull(strstr(buf, "LOCATION: http://127.0.0.1:9080/dlna/desc.xml"));
    ck_assert_ptr_nonnull(strstr(buf, "CACHE-CONTROL: max-age=1800"));
    replies++;
  }
  ck_assert_int_eq(replies, 5);
  rig_close(&r);
}
END_TEST

START_TEST(search_for_a_type_answers_once) {
  config_t cfg;
  rig_t r;
  char buf[1024];

  init_cfg(&cfg);
  rig_open(&r);
  rig_search(&r, &cfg, "ST: urn:schemas-upnp-org:service:ContentDirectory:1\r\n");
  ck_assert_int_eq(rig_recv(&r, buf, sizeof buf), 1);
  ck_assert_ptr_nonnull(strstr(buf, "ST: urn:schemas-upnp-org:service:ContentDirectory:1"));
  ck_assert_int_eq(rig_recv(&r, buf, sizeof buf), 0);
  rig_close(&r);
}
END_TEST

START_TEST(search_for_the_device_uuid_answers_once) {
  config_t cfg;
  rig_t r;
  char uuid[37];
  char line[96];
  char buf[1024];

  init_cfg(&cfg);
  ssdp_device_uuid(&cfg, uuid);
  snprintf(line, sizeof line, "ST: uuid:%s\r\n", uuid);
  rig_open(&r);
  rig_search(&r, &cfg, line);
  ck_assert_int_eq(rig_recv(&r, buf, sizeof buf), 1);
  ck_assert_ptr_nonnull(strstr(buf, line + 4));
  ck_assert_int_eq(rig_recv(&r, buf, sizeof buf), 0);
  rig_close(&r);
}
END_TEST

START_TEST(unmatched_requests_get_no_reply) {
  static const char *const lines[] = {"ST: urn:unknown:device:Nothing:1\r\n", "X-Other: 1\r\n"};
  config_t cfg;
  rig_t r;
  char uuid[37];
  char buf[1024];

  init_cfg(&cfg);
  ssdp_device_uuid(&cfg, uuid);
  rig_open(&r);
  rig_search(&r, &cfg, lines[_i]);
  ck_assert_int_eq(rig_recv(&r, buf, sizeof buf), 0);
  rig_close(&r);
}
END_TEST

START_TEST(other_datagrams_are_ignored) {
  static const char *const msgs[] = {"NOTIFY * HTTP/1.1\r\nST: ssdp:all\r\n\r\n", "M-SEARCH * HTTP/1.1", "M-SEARCH"};
  config_t cfg;
  rig_t r;
  char uuid[37];
  char buf[1024];

  init_cfg(&cfg);
  ssdp_device_uuid(&cfg, uuid);
  rig_open(&r);
  ssdp_handle_msearch(r.server, msgs[_i], (struct sockaddr *)&r.client_addr, sizeof r.client_addr, &cfg, uuid);
  ck_assert_int_eq(rig_recv(&r, buf, sizeof buf), 0);
  rig_close(&r);
}
END_TEST

START_TEST(start_and_stop_are_idempotent) {
  config_t cfg;

  init_cfg(&cfg);
  ssdp_stop();
  ssdp_start(&cfg);
  ssdp_stop();
  ssdp_stop();
}
END_TEST

START_TEST(disabled_dlna_starts_nothing) {
  config_t cfg;

  init_cfg(&cfg);
  cfg.enable_dlna = 0;
  ssdp_start(&cfg);
  ssdp_stop();
}
END_TEST

static Suite *ssdp_suite(void) {
  Suite *s = suite_create("dipixy_ssdp");
  TCase *tc = tcase_create("core");

  tcase_set_timeout(tc, 30);
  tcase_add_test(tc, msearch_header_finds_names_case_insensitively);
  tcase_add_test(tc, msearch_header_misses_are_reported);
  tcase_add_test(tc, msearch_header_truncates_to_the_buffer);
  tcase_add_test(tc, device_uuid_is_a_stable_version_4_uuid);
  tcase_add_test(tc, disabled_dlna_starts_nothing);
  tcase_add_test(tc, search_all_answers_for_every_type);
  tcase_add_test(tc, search_for_a_type_answers_once);
  tcase_add_test(tc, search_for_the_device_uuid_answers_once);
  tcase_add_loop_test(tc, unmatched_requests_get_no_reply, 0, 2);
  tcase_add_loop_test(tc, other_datagrams_are_ignored, 0, 3);
  tcase_add_test(tc, start_and_stop_are_idempotent);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(ssdp_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
