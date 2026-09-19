/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dipixy/http3/http3_steer.h"

#define GROUP 4

static int group[GROUP];
static struct sockaddr_in dst;

static void setup(void) {
  socklen_t len = sizeof dst;
  int one = 1;
  memset(&dst, 0, sizeof dst);
  dst.sin_family = AF_INET;
  inet_pton(AF_INET, "127.0.0.1", &dst.sin_addr);
  for (int i = 0; i < GROUP; i++) {
    group[i] = socket(AF_INET, SOCK_DGRAM, 0);
    ck_assert_int_ge(group[i], 0);
    ck_assert_int_eq(setsockopt(group[i], SOL_SOCKET, SO_REUSEPORT, &one, sizeof one), 0);
    ck_assert_int_eq(bind(group[i], (struct sockaddr *)&dst, sizeof dst), 0);
    if (i == 0) ck_assert_int_eq(getsockname(group[0], (struct sockaddr *)&dst, &len), 0);
  }
  ck_assert_int_eq(h3_steer_attach(group[0]), 0);
}

static void teardown(void) {
  for (int i = 0; i < GROUP; i++) close(group[i]);
}

static int send_and_find(const uint8_t *pkt, size_t len) {
  int tx = socket(AF_INET, SOCK_DGRAM, 0);
  fd_set rfds;
  struct timeval tv = {1, 0};
  int maxfd = 0, found = -1;
  ck_assert_int_ge(tx, 0);
  ck_assert_int_eq((int)sendto(tx, pkt, len, 0, (struct sockaddr *)&dst, sizeof dst), (int)len);
  close(tx);
  FD_ZERO(&rfds);
  for (int i = 0; i < GROUP; i++) {
    FD_SET(group[i], &rfds);
    if (group[i] > maxfd) maxfd = group[i];
  }
  ck_assert_int_gt(select(maxfd + 1, &rfds, NULL, NULL, &tv), 0);
  for (int i = 0; i < GROUP; i++) {
    if (!FD_ISSET(group[i], &rfds)) continue;
    uint8_t buf[64];
    ck_assert_int_gt((int)recv(group[i], buf, sizeof buf, 0), 0);
    found = i;
  }
  return found;
}

START_TEST(short_header_routes_by_tag) {
  for (int round = 0; round < 8; round++) {
    for (int tag = 0; tag < GROUP; tag++) {
      uint8_t pkt[32] = {0x40, (uint8_t)(tag >> 8), (uint8_t)tag};
      ck_assert_int_eq(send_and_find(pkt, sizeof pkt), tag);
    }
  }
}
END_TEST

START_TEST(long_header_routes_by_tag) {
  for (int round = 0; round < 8; round++) {
    for (int tag = 0; tag < GROUP; tag++) {
      uint8_t pkt[48] = {0xC0, 0, 0, 0, 1, 16, (uint8_t)(tag >> 8), (uint8_t)tag};
      ck_assert_int_eq(send_and_find(pkt, sizeof pkt), tag);
    }
  }
}
END_TEST

START_TEST(tag_lands_at_cid_start) {
  uint8_t cid[16];
  memset(cid, 0xaa, sizeof cid);
  h3_steer_tag_cid(cid);
  ck_assert_int_eq(cid[0], 0);
  ck_assert_int_eq(cid[1], 0);
  ck_assert_int_eq(cid[2], 0xaa);
}
END_TEST

static Suite *h3_steer_suite(void) {
  Suite *s = suite_create("dipixy_h3_steer");
  TCase *tc = tcase_create("core");
  tcase_add_checked_fixture(tc, setup, teardown);
  tcase_add_test(tc, short_header_routes_by_tag);
  tcase_add_test(tc, long_header_routes_by_tag);
  tcase_add_test(tc, tag_lands_at_cid_start);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(h3_steer_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
