/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/cas/cas_scramble_engine.h"

#define PID_A 0x0100u
#define PID_CLEAR 0x0200u
#define FLUSH_PID 0x1FFFu

typedef struct {
  unsigned count;
  unsigned pid[8];
} sink_t;

static void sink_emit(void *ctx, const unsigned char pkt[188]) {
  sink_t *s = ctx;
  if (s->count < 8) s->pid[s->count] = (unsigned)((pkt[1] & 0x1F) << 8) | pkt[2];
  s->count++;
}

static void make_pkt(unsigned char pkt[188], unsigned pid, int pcr, unsigned af_len) {
  memset(pkt, 0xAA, 188);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)((pid >> 8) & 0x1F);
  pkt[2] = (unsigned char)(pid & 0xFF);
  if (pcr) {
    pkt[3] = 0x30;
    pkt[4] = (unsigned char)af_len;
    pkt[5] = 0x10;
  } else {
    pkt[3] = 0x10;
  }
}

static cas_scramble_engine_t *start_engine(sink_t *s) {
  static const unsigned char cw[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  const unsigned pids[] = {PID_A};
  cas_scramble_engine_t *e = cas_scramble_engine_start(SCRAMBLE_ALGO_CSA2, pids, 1, FLUSH_PID);
  ck_assert_ptr_nonnull(e);
  ck_assert_int_eq(cas_scramble_engine_set_cw(e, SCRAMBLE_PARITY_EVEN, cw, sizeof cw, sink_emit, s), 0);
  return e;
}

static void feed(cas_scramble_engine_t *e, sink_t *s, unsigned pid, int pcr, unsigned af_len) {
  unsigned char pkt[188];
  make_pkt(pkt, pid, pcr, af_len);
  cas_scramble_engine_scramble_packet(e, pid, 1, 1, SCRAMBLE_PARITY_EVEN, 1, 0.0, pkt, sink_emit, s);
}

START_TEST(payload_packet_stays_batched) {
  sink_t s;
  cas_scramble_engine_t *e;
  memset(&s, 0, sizeof s);
  e = start_engine(&s);
  feed(e, &s, PID_A, 0, 0);
  ck_assert_uint_eq(s.count, 0u);
  cas_scramble_engine_flush(e, sink_emit, &s);
  ck_assert_uint_eq(s.count, 1u);
  cas_scramble_engine_stop(e);
}
END_TEST

START_TEST(scrambled_pcr_packet_flushes_batch_in_order) {
  sink_t s;
  cas_scramble_engine_t *e;
  memset(&s, 0, sizeof s);
  e = start_engine(&s);
  feed(e, &s, PID_A, 0, 0);
  feed(e, &s, PID_A, 1, 7);
  ck_assert_uint_eq(s.count, 2u);
  cas_scramble_engine_stop(e);
}
END_TEST

START_TEST(clear_pcr_packet_flushes_batch_in_order) {
  sink_t s;
  cas_scramble_engine_t *e;
  memset(&s, 0, sizeof s);
  e = start_engine(&s);
  feed(e, &s, PID_A, 0, 0);
  feed(e, &s, PID_CLEAR, 1, 7);
  ck_assert_uint_eq(s.count, 2u);
  ck_assert_uint_eq(s.pid[0], PID_A);
  ck_assert_uint_eq(s.pid[1], PID_CLEAR);
  cas_scramble_engine_stop(e);
}
END_TEST

START_TEST(short_adaptation_field_is_not_pcr) {
  sink_t s;
  cas_scramble_engine_t *e;
  memset(&s, 0, sizeof s);
  e = start_engine(&s);
  feed(e, &s, PID_A, 0, 0);
  feed(e, &s, PID_A, 1, 6);
  ck_assert_uint_eq(s.count, 0u);
  cas_scramble_engine_stop(e);
}
END_TEST

static Suite *engine_suite(void) {
  Suite *s = suite_create("cas_scramble_engine");
  TCase *tc = tcase_create("pcr_flush");
  tcase_add_test(tc, payload_packet_stays_batched);
  tcase_add_test(tc, scrambled_pcr_packet_flushes_batch_in_order);
  tcase_add_test(tc, clear_pcr_packet_flushes_batch_in_order);
  tcase_add_test(tc, short_adaptation_field_is_not_pcr);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(engine_suite());
  int failed;
  srunner_run_all(sr, CK_NORMAL);
  failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
