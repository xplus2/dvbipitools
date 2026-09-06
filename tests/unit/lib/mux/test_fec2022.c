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

static Suite *fec2022_suite(void) {
  Suite *s = suite_create("fec2022");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, fec2022_enc_new_rejects_bad_params);
  tcase_add_test(tc, fec2022_parse_ld_accepts_and_rejects);
  tcase_add_test(tc, fec2022_round_trip_no_loss);
  tcase_add_test(tc, fec2022_recovers_single_loss_per_column);
  tcase_add_test(tc, fec2022_drops_double_loss_in_one_column);
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
