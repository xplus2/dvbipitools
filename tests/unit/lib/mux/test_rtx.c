/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "lib/demux/rtx.h"
#include "lib/mux/rtx.h"

START_TEST(rtx_build_round_trips_through_rtx_parse) {
  rtx_ctx_t *ctx = rtx_ctx_new();
  unsigned char buf[32];
  static const unsigned char orig[] = {0xDE, 0xAD, 0xBE, 0xEF};
  size_t n;
  rtx_pkt_t out;

  n = rtx_build(ctx, 0x11223344u, 99, 0xAABBCCDDu, 42, orig, sizeof orig, buf, sizeof buf);
  ck_assert_uint_eq(n, 12u + 2u + sizeof orig);

  ck_assert_int_eq(rtx_parse(buf, n, 99, &out), 1);
  ck_assert_uint_eq(out.ssrc, 0x11223344u);
  ck_assert_uint_eq(out.osn, 42u);
  ck_assert_uint_eq(out.payload_len, sizeof orig);
  ck_assert_mem_eq(out.payload, orig, sizeof orig);

  rtx_ctx_free(ctx);
}
END_TEST

START_TEST(rtx_build_increments_seq_independent_of_orig_seq) {
  rtx_ctx_t *ctx = rtx_ctx_new();
  unsigned char b1[16], b2[16];
  rtx_pkt_t o1, o2;

  rtx_build(ctx, 1, 99, 0, 5, NULL, 0, b1, sizeof b1);
  rtx_build(ctx, 1, 99, 0, 5, NULL, 0, b2, sizeof b2); /* same orig_seq both times */
  rtx_parse(b1, 14, 99, &o1);
  rtx_parse(b2, 14, 99, &o2);

  ck_assert_uint_eq(o1.osn, 5u);
  ck_assert_uint_eq(o2.osn, 5u);
  /* the RTX stream's own RTP seq (not exposed by rtx_pkt_t) must still
   * differ between calls - check indirectly via the raw bytes */
  ck_assert(b1[2] != b2[2] || b1[3] != b2[3]);

  rtx_ctx_free(ctx);
}
END_TEST

START_TEST(rtx_build_rejects_small_cap) {
  rtx_ctx_t *ctx = rtx_ctx_new();
  unsigned char buf[13];
  ck_assert_uint_eq(rtx_build(ctx, 1, 99, 0, 0, NULL, 0, buf, sizeof buf), 0u);
  rtx_ctx_free(ctx);
}
END_TEST

/* race test: concurrent rtx_build() callers, TSan-checked, no dup seq */

#define RTX_RACE_THREADS 8
#define RTX_RACE_PER_THREAD 2000

static rtx_ctx_t *g_rtx_race_ctx;
static uint16_t g_rtx_race_seqs[RTX_RACE_THREADS][RTX_RACE_PER_THREAD];

static void *rtx_race_worker(void *arg) {
  long idx = (long)arg;
  int i;
  for (i = 0; i < RTX_RACE_PER_THREAD; i++) {
    unsigned char buf[16];
    rtx_build(g_rtx_race_ctx, 0x99u, 96, 0, 0, NULL, 0, buf, sizeof buf);
    g_rtx_race_seqs[idx][i] = (uint16_t)((buf[2] << 8) | buf[3]);
  }
  return NULL;
}

START_TEST(rtx_build_race_never_duplicates_seq) {
  pthread_t th[RTX_RACE_THREADS];
  static unsigned char seen[65536];
  long i;
  int j;

  g_rtx_race_ctx = rtx_ctx_new();
  memset(seen, 0, sizeof seen);

  for (i = 0; i < RTX_RACE_THREADS; i++)
    pthread_create(&th[i], NULL, rtx_race_worker, (void *)i);
  for (i = 0; i < RTX_RACE_THREADS; i++)
    pthread_join(th[i], NULL);

  for (i = 0; i < RTX_RACE_THREADS; i++)
    for (j = 0; j < RTX_RACE_PER_THREAD; j++) {
      uint16_t seq = g_rtx_race_seqs[i][j];
      ck_assert_uint_eq(seen[seq], 0u);
      seen[seq] = 1;
    }

  rtx_ctx_free(g_rtx_race_ctx);
}
END_TEST

static Suite *rtx_suite(void) {
  Suite *s = suite_create("mux_rtx");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, rtx_build_round_trips_through_rtx_parse);
  tcase_add_test(tc, rtx_build_increments_seq_independent_of_orig_seq);
  tcase_add_test(tc, rtx_build_rejects_small_cap);
  tcase_add_test(tc, rtx_build_race_never_duplicates_seq);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(rtx_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
