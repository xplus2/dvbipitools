/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "../metrics_sink.h"
#include "dipifccret/run/run.h"

#define SSRC 0xABCD0001u

static void noop_mc(const channel_t *c, const unsigned char *pkt, size_t len, int dscp, void *user) {
  (void)c;
  (void)pkt;
  (void)len;
  (void)dscp;
  (void)user;
}

static void noop_uni(int fd, const struct sockaddr *to, socklen_t tolen, const unsigned char *pkt, size_t len, int dscp, void *user) {
  (void)fd;
  (void)to;
  (void)tolen;
  (void)pkt;
  (void)len;
  (void)dscp;
  (void)user;
}

static channel_t *add_channel(channel_table_t *t, unsigned last_octet, uint16_t seq) {
  unsigned char addr[4] = {239, 1, 1, 0};
  unsigned char payload[188];
  channel_t *c;

  addr[3] = (unsigned char)last_octet;
  c = channel_lookup(t, AF_INET, addr, sizeof addr, 5000);
  ck_assert_ptr_nonnull(c);
  memset(payload, 0x47, sizeof payload);
  channel_store(t, c, SSRC + last_octet, seq, 0, 0, payload, sizeof payload);
  return c;
}

START_TEST(push_reports_channels_only_when_ret_and_fcc_are_off) {
  sink_t s;
  channel_table_t *t = channel_table_new(4, 8, 8);
  metrics_ctx_t mc;
  seen_t seen;
  uint64_t v = 99;

  sink_open(&s, METRICS_COMPONENT_FCCRET, "fcc1", 5.0);
  add_channel(t, 1, 1);
  add_channel(t, 2, 1);
  mc = (metrics_ctx_t){.mx = &s.mx, .channels = t};
  fccret_push_metrics(&mc);
  ck_assert_int_eq(sink_read(&s, &seen), 1);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_FCC_CHANNELS_ACTIVE, &v), 1);
  ck_assert_uint_eq(v, 2u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_FCC_RET_CLIENTS_ACTIVE, NULL), 0);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_FCC_BURSTS_ACTIVE, NULL), 0);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_FCC_NACKS_TOTAL, NULL), 0);
  sink_close(&s);
  channel_table_free(t);
}
END_TEST

START_TEST(push_reports_ret_and_burst_values) {
  sink_t s;
  channel_table_t *t = channel_table_new(4, 8, 8);
  ret_ctx_t *ret = ret_ctx_new(t, 99, 4, noop_mc, noop_uni, NULL);
  burst_table_t *bt = burst_table_new(2);
  channel_t *c = add_channel(t, 1, 5);
  struct sockaddr_in client;
  rtcp_nack_t nack;
  burst_t *b;
  burst_table_nack_result_t res;
  metrics_ctx_t mc;
  seen_t seen;
  uint64_t v;

  sink_open(&s, METRICS_COMPONENT_FCCRET, "fcc1", 5.0);
  memset(&client, 0, sizeof client);
  client.sin_family = AF_INET;
  client.sin_port = htons(4444);
  inet_pton(AF_INET, "127.0.0.1", &client.sin_addr);
  memset(&nack, 0, sizeof nack);
  nack.sender_ssrc = 1;
  nack.media_ssrc = SSRC + 1;
  nack.entry_count = 1;
  nack.entry[0].pid = 5;
  ret_handle_nack(ret, &nack, -1, (struct sockaddr *)&client, sizeof client);
  ck_assert_uint_eq(ret_ctx_active_clients(ret), 1u);

  atomic_store(&c->nominal_bps, 1000000.0);
  b = burst_new(c, 2.0, 0, 99);
  ck_assert_ptr_nonnull(burst_table_claim(bt, (struct sockaddr *)&client, sizeof client, -1, b));
  ck_assert_int_eq(burst_table_note_nack(bt, (struct sockaddr *)&client, sizeof client, 2, &res), 1);
  burst_table_note_bytes_sent(bt, 777);

  mc = (metrics_ctx_t){.mx = &s.mx, .channels = t, .ret = ret, .bursts = bt};
  fccret_push_metrics(&mc);
  ck_assert_int_eq(sink_read(&s, &seen), 1);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_FCC_CHANNELS_ACTIVE, &v), 1);
  ck_assert_uint_eq(v, 1u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_FCC_RET_CLIENTS_ACTIVE, &v), 1);
  ck_assert_uint_eq(v, 1u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_FCC_BURSTS_ACTIVE, &v), 1);
  ck_assert_uint_eq(v, 1u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_FCC_BYTES_RETRANSMITTED_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 777u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_FCC_NACKS_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 1u);
  ck_assert_int_eq(seen_has(&seen, METRICS_ID_FCC_CONGESTION_ADAPTATIONS_TOTAL, &v), 1);
  ck_assert_uint_eq(v, 1u);

  sink_close(&s);
  burst_table_free(bt);
  ret_ctx_free(ret);
  channel_table_free(t);
}
END_TEST

START_TEST(push_only_sends_when_the_interval_elapsed_and_exporter_enabled) {
  sink_t s;
  channel_table_t *t = channel_table_new(4, 8, 8);
  metrics_ctx_t mc;
  metrics_exporter_t off;
  seen_t seen;

  sink_open(&s, METRICS_COMPONENT_FCCRET, "fcc1", 1000.0);
  mc = (metrics_ctx_t){.mx = &s.mx, .channels = t};
  fccret_push_metrics(&mc);
  ck_assert_int_eq(sink_read(&s, &seen), 1);
  fccret_push_metrics(&mc);
  ck_assert_int_eq(sink_read(&s, &seen), 0);

  metrics_exporter_init(&off, METRICS_COMPONENT_FCCRET, NULL, NULL, 0);
  mc.mx = &off;
  fccret_push_metrics(&mc);
  ck_assert_int_eq(sink_read(&s, &seen), 0);
  sink_close(&s);
  channel_table_free(t);
}
END_TEST

static Suite *metrics_suite(void) {
  Suite *s = suite_create("dipifccret_metrics");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, push_reports_channels_only_when_ret_and_fcc_are_off);
  tcase_add_test(tc, push_reports_ret_and_burst_values);
  tcase_add_test(tc, push_only_sends_when_the_interval_elapsed_and_exporter_enabled);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(metrics_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
