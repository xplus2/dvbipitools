/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/fec2022.h"
#include "lib/mux/fec2022.h"
#include "lib/mux/rtpheader.h"

#define L 4u
#define D 3u
#define N (L * D)

static size_t drain_all(fec2022_dec_t *dc, unsigned char *out, size_t cap) {
  size_t off = 0;
  size_t n;
  while ((n = fec2022_dec_drain(dc, out + off, cap - off)) > 0) off += n;
  return off;
}

static void build_source(unsigned char *out, uint16_t seq, uint32_t ts, unsigned char fill) {
  out[0] = 0x80;
  out[1] = 33;
  out[2] = (unsigned char)(seq >> 8);
  out[3] = (unsigned char)seq;
  out[4] = (unsigned char)(ts >> 24);
  out[5] = (unsigned char)(ts >> 16);
  out[6] = (unsigned char)(ts >> 8);
  out[7] = (unsigned char)ts;
  out[8] = 0x42;
  out[9] = 0x42;
  out[10] = 0x42;
  out[11] = 0x42;
  memset(out + 12, fill, 188);
}

START_TEST(fec2022_enc_new_rejects_bad_params) {
  fec2022_dec_t *dc;

  ck_assert_ptr_null(fec2022_enc_new(0, 3, 96));
  ck_assert_ptr_null(fec2022_enc_new(4, 0, 96));
  ck_assert_ptr_null(fec2022_enc_new(FEC2022_MAX_L + 1, 1, 96));
  ck_assert_ptr_null(fec2022_enc_new(40, 11, 96));
  dc = fec2022_dec_new(FEC2022_MAX_L, 1);
  ck_assert_ptr_nonnull(dc);
  fec2022_dec_free(dc);
  ck_assert_ptr_null(fec2022_dec_new(0, 3));
  ck_assert_ptr_null(fec2022_dec_new(40, 11));
}
END_TEST

START_TEST(fec2022_parse_ld_accepts_and_rejects) {
  unsigned l;
  unsigned d;

  ck_assert_int_eq(fec2022_parse_ld("10:5", &l, &d), 0);
  ck_assert_uint_eq(l, 10u);
  ck_assert_uint_eq(d, 5u);
  ck_assert_int_eq(fec2022_parse_ld("40:10", &l, &d), 0);
  ck_assert_int_eq(fec2022_parse_ld("41:1", &l, &d), -1);
  ck_assert_int_eq(fec2022_parse_ld("40:11", &l, &d), -1);
  ck_assert_int_eq(fec2022_parse_ld("0:5", &l, &d), -1);
  ck_assert_int_eq(fec2022_parse_ld("5:0", &l, &d), -1);
  ck_assert_int_eq(fec2022_parse_ld("5", &l, &d), -1);
  ck_assert_int_eq(fec2022_parse_ld("5:5x", &l, &d), -1);
  ck_assert_int_eq(fec2022_parse_ld("x:5", &l, &d), -1);
}
END_TEST

START_TEST(fec2022_round_trip_no_loss) {
  fec2022_enc_t *e = fec2022_enc_new(L, D, 96);
  fec2022_dec_t *dc = fec2022_dec_new(L, D);
  unsigned char src[N][200];
  unsigned char repair[N][FEC2022_MAX_REPAIR];
  size_t repair_len[N];
  unsigned char out[N * FEC2022_MAX_PKT];
  size_t off = 0;

  ck_assert_ptr_nonnull(e);
  ck_assert_ptr_nonnull(dc);

  for (unsigned i = 0; i < N; i++) {
    build_source(src[i], (uint16_t)i, 1000 + i, (unsigned char)i);
    repair_len[i] = fec2022_enc_feed(e, src[i], 200, 5000 + i, repair[i], sizeof repair[i]);
  }
  for (unsigned i = 0; i < N; i++)
    if (repair_len[i] > 0)
      ck_assert_uint_eq(repair_len[i], 28u + 188u);

  for (unsigned i = 0; i < N; i++) {
    fec2022_dec_source(dc, src[i], 200);
    if (repair_len[i] > 0)
      fec2022_dec_repair(dc, repair[i], repair_len[i]);
  }
  off = drain_all(dc, out, sizeof out);

  ck_assert_uint_eq(off, (size_t)N * 200);
  for (unsigned i = 0; i < N; i++)
    ck_assert_mem_eq(out + i * 200, src[i], 200);

  fec2022_enc_free(e);
  fec2022_dec_free(dc);
}
END_TEST

START_TEST(fec2022_recovers_single_loss_per_column) {
  fec2022_enc_t *e = fec2022_enc_new(L, D, 96);
  fec2022_dec_t *dc = fec2022_dec_new(L, D);
  unsigned char src[N][200];
  unsigned char repair[N][FEC2022_MAX_REPAIR];
  size_t repair_len[N];
  unsigned char out[N * FEC2022_MAX_PKT];
  size_t off;
  unsigned dropped_col = 2;
  uint16_t dropped_seq = (uint16_t)(dropped_col + 1u * L);

  ck_assert_ptr_nonnull(e);
  ck_assert_ptr_nonnull(dc);

  for (unsigned i = 0; i < N; i++) {
    build_source(src[i], (uint16_t)i, 1000 + i, (unsigned char)(i + 7));
    repair_len[i] = fec2022_enc_feed(e, src[i], 200, 5000 + i, repair[i], sizeof repair[i]);
  }
  for (unsigned i = 0; i < N; i++) {
    if (i != dropped_seq)
      fec2022_dec_source(dc, src[i], 200);
    if (repair_len[i] > 0)
      fec2022_dec_repair(dc, repair[i], repair_len[i]);
  }
  off = drain_all(dc, out, sizeof out);

  ck_assert_uint_eq(off, (size_t)N * 200);
  for (unsigned i = 0; i < N; i++) ck_assert_mem_eq(out + i * 200, src[i], 200);

  fec2022_enc_free(e);
  fec2022_dec_free(dc);
}
END_TEST

START_TEST(fec2022_drops_double_loss_in_one_column) {
  fec2022_enc_t *e = fec2022_enc_new(L, D, 96);
  fec2022_dec_t *dc = fec2022_dec_new(L, D);
  unsigned char src[N + L][200];
  unsigned char repair[N + L][FEC2022_MAX_REPAIR];
  size_t repair_len[N + L];
  unsigned char out[(N + L) * FEC2022_MAX_PKT];
  size_t off;
  unsigned col = 1;
  uint16_t drop_a = (uint16_t)(col + 0u * L);
  uint16_t drop_b = (uint16_t)(col + 1u * L);

  ck_assert_ptr_nonnull(e);
  ck_assert_ptr_nonnull(dc);

  /* forces the flush isolated tests need */
  for (unsigned i = 0; i < N + L; i++) {
    build_source(src[i], (uint16_t)i, 1000 + i, (unsigned char)i);
    repair_len[i] = fec2022_enc_feed(e, src[i], 200, 5000 + i, repair[i], sizeof repair[i]);
  }
  for (unsigned i = 0; i < N + L; i++) {
    if (i != drop_a && i != drop_b)
      fec2022_dec_source(dc, src[i], 200);
    if (repair_len[i] > 0)
      fec2022_dec_repair(dc, repair[i], repair_len[i]);
  }
  off = drain_all(dc, out, sizeof out);
  ck_assert_uint_eq(off, (size_t)(N - 2) * 200);
  fec2022_enc_free(e);
  fec2022_dec_free(dc);
}
END_TEST

#define WRAP_N 600u

static void wrap_run(unsigned l, unsigned d, int drop, unsigned skew) {
  static unsigned char src[WRAP_N][200];
  static unsigned char repair[WRAP_N][FEC2022_MAX_REPAIR];
  static unsigned char out[WRAP_N * FEC2022_MAX_PKT];
  size_t repair_len[WRAP_N];
  fec2022_enc_t *e = fec2022_enc_new(l, d, 96);
  fec2022_dec_t *dc = fec2022_dec_new(l, d);
  unsigned ld = l * d;
  unsigned start = (65536u - WRAP_N / 2) / ld * ld + skew;
  unsigned drop_i = drop ? WRAP_N / 2 - 3 : WRAP_N;
  size_t got = 0;

  ck_assert_ptr_nonnull(e);
  ck_assert_ptr_nonnull(dc);
  for (unsigned i = 0; i < WRAP_N; i++) {
    build_source(src[i], (uint16_t)(start + i), i, (unsigned char)i);
    repair_len[i] = fec2022_enc_feed(e, src[i], 200, i, repair[i], sizeof repair[i]);
  }
  for (unsigned i = 0; i < WRAP_N; i++) {
    if (i != drop_i) fec2022_dec_source(dc, src[i], 200);
    if (repair_len[i] > 0) fec2022_dec_repair(dc, repair[i], repair_len[i]);
    got += drain_all(dc, out + got, sizeof out - got);
  }
  ck_assert_uint_ge(got / 200, WRAP_N - ld);
  for (size_t i = 0; i < got / 200; i++) ck_assert_mem_eq(out + i * 200, src[i], 200);
  fec2022_enc_free(e);
  fec2022_dec_free(dc);
}

START_TEST(fec2022_seq_wrap_l4d3) { wrap_run(4, 3, 0, 0); }
END_TEST

START_TEST(fec2022_seq_wrap_l4d3_loss) { wrap_run(4, 3, 1, 0); }
END_TEST

START_TEST(fec2022_seq_wrap_l10d10) { wrap_run(10, 10, 0, 0); }
END_TEST

START_TEST(fec2022_seq_wrap_l10d10_loss) { wrap_run(10, 10, 1, 0); }
END_TEST

START_TEST(fec2022_seq_wrap_unaligned_loss) { wrap_run(10, 10, 1, 7); }
END_TEST

START_TEST(fec2022_seq_wrap_unaligned_l4d3_loss) { wrap_run(4, 3, 1, 5); }
END_TEST

#define STREAM_PKTS (3 * N)
#define PKT_LEN 200u

typedef struct {
  unsigned char src[STREAM_PKTS][PKT_LEN];
  unsigned char repair[STREAM_PKTS][FEC2022_MAX_REPAIR];
  size_t repair_len[STREAM_PKTS];
} stream_t;

static void make_stream(stream_t *st, unsigned first_seq, unsigned count) {
  fec2022_enc_t *e = fec2022_enc_new(L, D, 96);

  ck_assert_ptr_nonnull(e);
  ck_assert_uint_le(count, (unsigned)STREAM_PKTS);
  for (unsigned i = 0; i < count; i++) {
    build_source(st->src[i], (uint16_t)(first_seq + i), 1000 + i, (unsigned char)(i + 3));
    st->repair_len[i] = fec2022_enc_feed(e, st->src[i], PKT_LEN, 5000 + i, st->repair[i], sizeof st->repair[i]);
  }
  fec2022_enc_free(e);
}

static stream_t g_stream;
static stream_t g_restart;

START_TEST(fec2022_dec_source_rejects_bad_lengths) {
  fec2022_dec_t *dc = fec2022_dec_new(L, D);
  unsigned char pkt[FEC2022_MAX_PKT + 8];

  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x80;
  ck_assert_int_eq(fec2022_dec_source(dc, pkt, 11), -1);
  ck_assert_int_eq(fec2022_dec_source(dc, pkt, FEC2022_MAX_PKT + 1), -1);
  ck_assert_int_eq(fec2022_dec_source(dc, pkt, 12), 0);
  fec2022_dec_free(dc);
}
END_TEST

START_TEST(fec2022_dec_drain_keeps_packets_that_do_not_fit) {
  fec2022_dec_t *dc = fec2022_dec_new(L, D);
  unsigned char out[N * FEC2022_MAX_PKT];
  unsigned char small[10];

  make_stream(&g_stream, 0, N);
  ck_assert_uint_eq(fec2022_dec_drain(dc, out, sizeof out), 0u);
  for (unsigned i = 0; i < N; i++) {
    fec2022_dec_source(dc, g_stream.src[i], PKT_LEN);
    if (g_stream.repair_len[i]) fec2022_dec_repair(dc, g_stream.repair[i], g_stream.repair_len[i]);
  }
  ck_assert_uint_eq(fec2022_dec_drain(dc, small, sizeof small), 0u);
  ck_assert_uint_eq(drain_all(dc, out, sizeof out), (size_t)N * PKT_LEN);
  fec2022_dec_free(dc);
}
END_TEST

START_TEST(fec2022_dec_ignores_duplicate_and_late_packets) {
  fec2022_dec_t *dc = fec2022_dec_new(L, D);
  unsigned char out[3 * N * FEC2022_MAX_PKT];
  size_t got = 0;

  make_stream(&g_stream, 0, 2 * N);
  for (unsigned i = 0; i < 2 * N; i++) {
    fec2022_dec_source(dc, g_stream.src[i], PKT_LEN);
    fec2022_dec_source(dc, g_stream.src[i], PKT_LEN);
    if (g_stream.repair_len[i]) {
      fec2022_dec_repair(dc, g_stream.repair[i], g_stream.repair_len[i]);
      fec2022_dec_repair(dc, g_stream.repair[i], g_stream.repair_len[i]);
    }
    got += drain_all(dc, out, sizeof out);
  }
  fec2022_dec_source(dc, g_stream.src[0], PKT_LEN);
  fec2022_dec_source(dc, g_stream.src[5], PKT_LEN);
  if (g_stream.repair_len[N - 1]) fec2022_dec_repair(dc, g_stream.repair[N - 1], g_stream.repair_len[N - 1]);
  got += drain_all(dc, out, sizeof out);
  ck_assert_uint_eq(got, (size_t)2 * N * PKT_LEN);
  fec2022_dec_free(dc);
}
END_TEST

START_TEST(fec2022_dec_repair_ignores_malformed_and_foreign_geometry) {
  fec2022_dec_t *dc = fec2022_dec_new(L, D);
  fec2022_enc_t *other = fec2022_enc_new(5, D, 96);
  unsigned char foreign[5 * D][PKT_LEN];
  unsigned char frep[FEC2022_MAX_REPAIR];
  unsigned char out[N * FEC2022_MAX_PKT];
  size_t flen = 0;
  size_t got;
  unsigned dropped = 6;

  ck_assert_ptr_nonnull(other);
  make_stream(&g_stream, 0, N);
  for (unsigned i = 0; i < 5 * D; i++) {
    build_source(foreign[i], (uint16_t)i, 1000 + i, 9);
    flen = fec2022_enc_feed(other, foreign[i], PKT_LEN, 5000 + i, frep, sizeof frep);
    if (flen) fec2022_dec_repair(dc, frep, flen);
  }
  fec2022_enc_free(other);
  fec2022_dec_repair(dc, g_stream.repair[N - 1], 10);
  fec2022_dec_repair(dc, g_stream.repair[N - 1], 20);
  for (unsigned i = 0; i < N; i++)
    if (i != dropped) fec2022_dec_source(dc, g_stream.src[i], PKT_LEN);
  got = drain_all(dc, out, sizeof out);
  ck_assert_uint_lt(got, (size_t)N * PKT_LEN);
  fec2022_dec_free(dc);
}
END_TEST

START_TEST(fec2022_dec_resynchronises_when_the_stream_restarts) {
  fec2022_dec_t *dc = fec2022_dec_new(L, D);
  unsigned char out[3 * N * FEC2022_MAX_PKT];
  size_t got;

  make_stream(&g_stream, 0, N);
  make_stream(&g_restart, 1001, N);
  for (unsigned i = 0; i < N; i++) {
    fec2022_dec_source(dc, g_stream.src[i], PKT_LEN);
    if (g_stream.repair_len[i]) fec2022_dec_repair(dc, g_stream.repair[i], g_stream.repair_len[i]);
  }
  for (unsigned i = 0; i < N; i++) {
    if (g_restart.repair_len[i]) fec2022_dec_repair(dc, g_restart.repair[i], g_restart.repair_len[i]);
    fec2022_dec_source(dc, g_restart.src[i], PKT_LEN);
  }
  got = drain_all(dc, out, sizeof out);
  ck_assert_uint_ge(got, (size_t)N * PKT_LEN);
  fec2022_dec_free(dc);
}
END_TEST

START_TEST(fec2022_dec_drops_released_packets_while_the_ready_queue_is_full) {
  fec2022_dec_t *dc = fec2022_dec_new(L, D);
  unsigned char out[3 * N * FEC2022_MAX_PKT];
  size_t got;

  make_stream(&g_stream, 0, 3 * N);
  for (unsigned i = 0; i < 3 * N; i++) {
    fec2022_dec_source(dc, g_stream.src[i], PKT_LEN);
    if (g_stream.repair_len[i]) fec2022_dec_repair(dc, g_stream.repair[i], g_stream.repair_len[i]);
  }
  got = drain_all(dc, out, sizeof out);
  ck_assert_uint_gt(got, 0u);
  ck_assert_uint_le(got, (size_t)N * PKT_LEN);
  fec2022_dec_free(dc);
}
END_TEST

static Suite *fec2022_suite(void) {
  Suite *s = suite_create("fec2022");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, fec2022_enc_new_rejects_bad_params);
  tcase_add_test(tc, fec2022_parse_ld_accepts_and_rejects);
  tcase_add_test(tc, fec2022_round_trip_no_loss);
  tcase_add_test(tc, fec2022_recovers_single_loss_per_column);
  tcase_add_test(tc, fec2022_drops_double_loss_in_one_column);
  tcase_add_test(tc, fec2022_seq_wrap_l4d3);
  tcase_add_test(tc, fec2022_seq_wrap_l4d3_loss);
  tcase_add_test(tc, fec2022_seq_wrap_l10d10);
  tcase_add_test(tc, fec2022_seq_wrap_l10d10_loss);
  tcase_add_test(tc, fec2022_seq_wrap_unaligned_loss);
  tcase_add_test(tc, fec2022_seq_wrap_unaligned_l4d3_loss);
  tcase_add_test(tc, fec2022_dec_source_rejects_bad_lengths);
  tcase_add_test(tc, fec2022_dec_drain_keeps_packets_that_do_not_fit);
  tcase_add_test(tc, fec2022_dec_ignores_duplicate_and_late_packets);
  tcase_add_test(tc, fec2022_dec_repair_ignores_malformed_and_foreign_geometry);
  tcase_add_test(tc, fec2022_dec_resynchronises_when_the_stream_restarts);
  tcase_add_test(tc, fec2022_dec_drops_released_packets_while_the_ready_queue_is_full);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(fec2022_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
