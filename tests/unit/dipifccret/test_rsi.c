/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>

#include "dipifccret/run/run.h"

#define SSRC 0xABCD0001u
#define NTP_SEC 0x01020304u
#define NTP_FRAC 0x05060708u
#define MAX_AGE 15

typedef struct {
  channel_table_t *t;
  channel_t *c;
  rsi_pacer_ctx_t pc;
} fx_t;

static uint32_t rd32(const unsigned char *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t rd16(const unsigned char *p) {
  return (uint16_t)((p[0] << 8) | p[1]);
}

static void fx_open(fx_t *fx, int known_ssrc, double nominal_bps) {
  unsigned char addr[4] = {239, 1, 1, 1};
  unsigned char payload[188];

  memset(fx, 0, sizeof *fx);
  fx->t = channel_table_new(4, 8, 0);
  ck_assert_ptr_nonnull(fx->t);
  fx->c = channel_lookup(fx->t, AF_INET, addr, sizeof addr, 5000);
  ck_assert_ptr_nonnull(fx->c);
  if (known_ssrc) {
    memset(payload, 0x47, sizeof payload);
    channel_store(fx->t, fx->c, SSRC, 1, 0, 0, payload, sizeof payload);
  }
  atomic_store(&fx->c->nominal_bps, nominal_bps);
  fx->pc.channels = fx->t;
  fx->pc.addr[0] = 192;
  fx->pc.addr[1] = 0;
  fx->pc.addr[2] = 2;
  fx->pc.addr[3] = 9;
  fx->pc.port = 4321;
}

static void fx_close(fx_t *fx) {
  channel_table_free(fx->t);
}

static size_t report(fx_t *fx, unsigned char *pkt, size_t cap) {
  return rsi_build_report(&fx->pc, fx->c, NTP_SEC, NTP_FRAC, MAX_AGE, pkt, cap);
}

static void check_header(const unsigned char *pkt, size_t n) {
  ck_assert_uint_eq(pkt[0], 0x80u);
  ck_assert_uint_eq(pkt[1], 208u);
  ck_assert_uint_eq(((size_t)rd16(pkt + 2) + 1) * 4, n);
  ck_assert_uint_eq(rd32(pkt + 4), SSRC);
  ck_assert_uint_eq(rd32(pkt + 8), SSRC);
  ck_assert_uint_eq(rd32(pkt + 12), NTP_SEC);
  ck_assert_uint_eq(rd32(pkt + 16), NTP_FRAC);
}

START_TEST(address_report_has_srbt0_then_bandwidth) {
  fx_t fx;
  unsigned char pkt[RSI_PKT_MAX];
  size_t n;

  fx_open(&fx, 1, 8000000.0);
  n = report(&fx, pkt, sizeof pkt);
  ck_assert_uint_eq(n, 20u + 8u + 8u);
  check_header(pkt, n);
  ck_assert_uint_eq(pkt[20], 0u);
  ck_assert_uint_eq(rd16(pkt + 22), 4321u);
  ck_assert_uint_eq(pkt[24], 192u);
  ck_assert_uint_eq(pkt[27], 9u);
  ck_assert_uint_eq(pkt[28], 11u);
  ck_assert_uint_eq(rd32(pkt + 32), (uint32_t)(400.0 * 65536.0));
  fx_close(&fx);
}
END_TEST

START_TEST(hostname_report_uses_srbt2_instead_of_the_address) {
  fx_t fx;
  unsigned char pkt[RSI_PKT_MAX];
  size_t n;

  fx_open(&fx, 1, 0.0);
  fx.pc.hostname = "fcc.example";
  fx.pc.hostname_len = strlen(fx.pc.hostname);
  n = report(&fx, pkt, sizeof pkt);
  ck_assert_uint_gt(n, 20u);
  check_header(pkt, n);
  ck_assert_uint_eq(pkt[20], 2u);
  ck_assert_mem_eq(pkt + 24, "fcc.example", 11);
  ck_assert_uint_eq(n, 20u + 4u + 12u);
  fx_close(&fx);
}
END_TEST

START_TEST(oversized_hostname_makes_the_report_unreportable) {
  fx_t fx;
  unsigned char pkt[RSI_PKT_MAX];
  static char name[300];

  fx_open(&fx, 1, 0.0);
  memset(name, 'h', sizeof name - 1);
  fx.pc.hostname = name;
  fx.pc.hostname_len = sizeof name - 1;
  ck_assert_uint_eq(report(&fx, pkt, sizeof pkt), 0u);
  fx_close(&fx);
}
END_TEST

START_TEST(unknown_ssrc_is_not_reported) {
  fx_t fx;
  unsigned char pkt[RSI_PKT_MAX];

  fx_open(&fx, 0, 1000.0);
  ck_assert_uint_eq(report(&fx, pkt, sizeof pkt), 0u);
  fx_close(&fx);
}
END_TEST

START_TEST(bandwidth_is_omitted_for_unknown_or_out_of_range_rates) {
  fx_t fx;
  unsigned char pkt[RSI_PKT_MAX];

  fx_open(&fx, 1, 0.0);
  ck_assert_uint_eq(report(&fx, pkt, sizeof pkt), 28u);
  atomic_store(&fx.c->nominal_bps, 2.0e9);
  ck_assert_uint_eq(report(&fx, pkt, sizeof pkt), 28u);
  fx_close(&fx);
}
END_TEST

START_TEST(resolve_by_port_announces_the_channels_own_port) {
  fx_t fx;
  unsigned char pkt[RSI_PKT_MAX];

  fx_open(&fx, 1, 0.0);
  fx.pc.resolve_by_port = 1;
  fx.pc.resolve_base_port = 30000;
  ck_assert_uint_gt(report(&fx, pkt, sizeof pkt), 0u);
  ck_assert_uint_eq(rd16(pkt + 22), (uint16_t)(30000 + fx.c->resolve_slot));
  fx_close(&fx);
}
END_TEST

START_TEST(collisions_are_reported_until_they_age_out) {
  fx_t fx;
  unsigned char pkt[RSI_PKT_MAX];
  struct sockaddr_in a;
  struct sockaddr_in b;
  size_t n;

  fx_open(&fx, 1, 0.0);
  memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(1111);
  inet_pton(AF_INET, "10.0.0.1", &a.sin_addr);
  b = a;
  b.sin_port = htons(2222);
  channel_hned_seen(fx.c, 0x7777, (struct sockaddr *)&a, sizeof a, NULL, 0);
  channel_hned_seen(fx.c, 0x7777, (struct sockaddr *)&b, sizeof b, NULL, 0);

  n = report(&fx, pkt, sizeof pkt);
  ck_assert_uint_eq(n, 20u + 8u + 8u);
  check_header(pkt, n);
  ck_assert_uint_eq(pkt[28], 8u);
  ck_assert_uint_eq(rd32(pkt + 32), 0x7777u);

  fx.c->hned_collisions[0].last_detected = time(NULL) - 3600;
  ck_assert_uint_eq(report(&fx, pkt, sizeof pkt), 28u);
  fx_close(&fx);
}
END_TEST

START_TEST(report_fails_when_the_buffer_cannot_hold_the_first_sub_report) {
  fx_t fx;
  unsigned char pkt[RSI_PKT_MAX];

  fx_open(&fx, 1, 0.0);
  ck_assert_uint_eq(report(&fx, pkt, 24), 0u);
  fx_close(&fx);
}
END_TEST

static Suite *rsi_suite(void) {
  Suite *s = suite_create("dipifccret_rsi");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, address_report_has_srbt0_then_bandwidth);
  tcase_add_test(tc, hostname_report_uses_srbt2_instead_of_the_address);
  tcase_add_test(tc, oversized_hostname_makes_the_report_unreportable);
  tcase_add_test(tc, unknown_ssrc_is_not_reported);
  tcase_add_test(tc, bandwidth_is_omitted_for_unknown_or_out_of_range_rates);
  tcase_add_test(tc, resolve_by_port_announces_the_channels_own_port);
  tcase_add_test(tc, collisions_are_reported_until_they_age_out);
  tcase_add_test(tc, report_fails_when_the_buffer_cannot_hold_the_first_sub_report);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(rsi_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
