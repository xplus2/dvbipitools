/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "dipitvhead/input/source.h"

static void wait_ms(int ms) {
  struct timespec ts = {0, (long)ms * 1000000L};
  nanosleep(&ts, NULL);
}

START_TEST(udp_kind_opens_a_real_socket_and_reads_datagrams) {
  config_t cfg;
  dipitvhead_input_t in;
  tvsrc_t *s;
  net_err_reason_t reason = NET_ERR_OTHER;
  int sock;
  struct sockaddr_in dst;
  unsigned char pkt[188];
  unsigned char rbuf[512];
  ssize_t n;
  int i;

  memset(&cfg, 0, sizeof cfg);
  memset(&in, 0, sizeof in);
  in.input.kind = SRC_UDP;
  in.input.family = AF_INET;
  strcpy(in.input.group, "239.7.9.31");
  in.input.port = 15311;

  s = tvsrc_open(&cfg, &in, &reason);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_ge(tvsrc_fd(s), 0);

  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  sock = socket(AF_INET, SOCK_DGRAM, 0);
  memset(&dst, 0, sizeof dst);
  dst.sin_family = AF_INET;
  dst.sin_port = htons(15311);
  inet_pton(AF_INET, "239.7.9.31", &dst.sin_addr);

  n = -1;
  for (i = 0; i < 20 && n < 0; i++) {
    sendto(sock, pkt, sizeof pkt, 0, (const struct sockaddr *)&dst, sizeof dst);
    wait_ms(20);
    n = tvsrc_read(s, rbuf, sizeof rbuf, &reason);
  }
  close(sock);

  ck_assert_int_eq(n, (ssize_t)sizeof pkt);
  ck_assert_mem_eq(rbuf, pkt, sizeof pkt);

  tvsrc_close(s);
}
END_TEST

START_TEST(stdin_kind_maps_to_the_stdin_fd) {
  config_t cfg;
  dipitvhead_input_t in;
  tvsrc_t *s;
  net_err_reason_t reason = NET_ERR_OTHER;

  memset(&cfg, 0, sizeof cfg);
  memset(&in, 0, sizeof in);
  in.input.kind = SRC_STDIN;

  s = tvsrc_open(&cfg, &in, &reason);
  ck_assert_ptr_nonnull(s);
  ck_assert_int_eq(tvsrc_fd(s), STDIN_FILENO);

  tvsrc_close(s);
}
END_TEST

START_TEST(http_kind_with_unreachable_host_fails_to_open) {
  config_t cfg;
  dipitvhead_input_t in;
  tvsrc_t *s;
  net_err_reason_t reason = NET_ERR_OTHER;

  memset(&cfg, 0, sizeof cfg);
  memset(&in, 0, sizeof in);
  in.input.kind = SRC_HTTP;
  strcpy(in.input.http.host, "127.0.0.1");
  in.input.http.port = 1; /* nothing listens on port 1 */
  strcpy(in.input.http.path, "/");

  s = tvsrc_open(&cfg, &in, &reason);
  ck_assert_ptr_null(s);
}
END_TEST

static Suite *source_suite(void) {
  Suite *s = suite_create("dipitvhead_source");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 10);
  tcase_add_test(tc, udp_kind_opens_a_real_socket_and_reads_datagrams);
  tcase_add_test(tc, stdin_kind_maps_to_the_stdin_fd);
  tcase_add_test(tc, http_kind_with_unreachable_host_fails_to_open);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(source_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
