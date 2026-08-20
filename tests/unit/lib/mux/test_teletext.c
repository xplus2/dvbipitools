/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "lib/mux/teletext.h"

static unsigned char rev8_local(unsigned char b) {
  b = (unsigned char)((b >> 4) | (b << 4));
  b = (unsigned char)(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
  b = (unsigned char)(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
  return b;
}

/* hamming 8/4 encoder, like teletext.c unham_init table-build (w/o final rev8) @see EN 300 706 annex A */
static unsigned char hamm_raw(unsigned d) {
  unsigned D1 = d & 1, D2 = (d >> 1) & 1, D3 = (d >> 2) & 1, D4 = (d >> 3) & 1;
  unsigned P1 = D1 ^ D2 ^ D4, P2 = D1 ^ D3 ^ D4, P3 = D2 ^ D3 ^ D4;
  unsigned c = P1 | (P2 << 1) | (D1 << 2) | (P3 << 3) | (D2 << 4) | (D3 << 5) | (D4 << 6);
  unsigned ones = 0;
  for (int k = 0; k < 7; k++)
    ones += (c >> k) & 1;
  c |= (ones & 1) << 7;
  return (unsigned char)c;
}

/* builds one EN 300 472 teletext PES payload with a single packet: magazine
   derived from `page`, packet number `pkt`, 40-column row `text` (padded with spaces, truncated to 40 chars) */
static size_t build_ttx_pes(unsigned char *out, unsigned page, unsigned pkt, const char *text) {
  unsigned mag = (page / 100) & 0x07;
  unsigned d0 = (mag & 0x07) | ((pkt & 1) << 3);
  unsigned d1 = (pkt >> 1) & 0x0F;
  size_t n = 0;
  size_t tlen = strlen(text);

  out[n++] = 0x10;             /* data_identifier */
  out[n++] = 0x03;             /* data_unit_id: EBU teletext subtitle */
  out[n++] = 0x2A;             /* data_unit_length = 42 */
  out[n++] = 0xFF;             /* framing code, unused by the decoder */
  out[n++] = 0xFF;             /* reserved, unused */
  out[n++] = rev8_local(hamm_raw(d0)); /* mpag byte 1 (magazine + pkt bit0) */
  out[n++] = rev8_local(hamm_raw(d1)); /* mpag byte 2 (pkt bits 1-4) */
  for (size_t i = 0; i < 40; i++) {
    unsigned char c = (i < tlen) ? (unsigned char)text[i] : 0x20;
    out[n++] = rev8_local(c);
  }
  return n;
}

static int g_calls;
static ttx_cue_t g_cue;

static void capture_cb(void *ctx, const ttx_cue_t *cue) {
  (void)ctx;
  g_calls++;
  g_cue = *cue;
}

START_TEST(ttx_decodes_one_row_and_emits_cue_on_flush) {
  ttx_t *t = ttx_new(777, "eng", 0, capture_cb, NULL);
  unsigned char pes[64];
  size_t n = build_ttx_pes(pes, 777, 1, "HELLO");
  g_calls = 0;
  ttx_pes(t, 1, 90000ULL, pes, n); /* pts_90k=90000 -> 1000ms */
  ck_assert_int_eq(g_calls, 0);    /* nothing emitted until flush/next group */
  ttx_flush(t);
  ck_assert_int_eq(g_calls, 1);
  ck_assert_str_eq(g_cue.text, "HELLO");
  ck_assert(g_cue.start_ms >= 0);
  ck_assert(g_cue.end_ms > g_cue.start_ms);
  ttx_free(t);
}
END_TEST

START_TEST(ttx_ignores_packets_on_a_different_magazine) {
  ttx_t *t = ttx_new(777, "eng", 0, capture_cb, NULL); /* magazine 7 */
  unsigned char pes[64];
  size_t n = build_ttx_pes(pes, 100, 1, "WRONG MAG"); /* page 100 -> magazine 1 */
  g_calls = 0;
  ttx_pes(t, 1, 90000ULL, pes, n);
  ttx_flush(t);
  ck_assert_int_eq(g_calls, 0);
  ttx_free(t);
}
END_TEST

START_TEST(ttx_skips_the_page_ident_row) {
  ttx_t *t = ttx_new(777, "eng", 0, capture_cb, NULL);
  unsigned char pes[64];
  /* ident rows start with the page number and are boundaries, not text */
  size_t n = build_ttx_pes(pes, 777, 0, "777 12:00:00");
  g_calls = 0;
  ttx_pes(t, 1, 90000ULL, pes, n);
  ttx_flush(t);
  ck_assert_int_eq(g_calls, 0); /* the ident row itself never becomes a cue */
  ttx_free(t);
}
END_TEST

START_TEST(ttx_lead_ms_shifts_cue_times_earlier) {
  ttx_t *t = ttx_new(777, "eng", 300 /* lead_ms */, capture_cb, NULL);
  unsigned char pes[64];
  size_t n = build_ttx_pes(pes, 777, 1, "HELLO");
  g_calls = 0;
  ttx_pes(t, 1, 90000ULL, pes, n); /* 1000ms */
  ttx_flush(t);
  ck_assert_int_eq(g_calls, 1);
  ck_assert_int_eq((int)g_cue.start_ms, 700); /* 1000 - 300 lead */
  ttx_free(t);
}
END_TEST

static Suite *teletext_suite(void) {
  Suite *s = suite_create("teletext");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, ttx_decodes_one_row_and_emits_cue_on_flush);
  tcase_add_test(tc, ttx_ignores_packets_on_a_different_magazine);
  tcase_add_test(tc, ttx_skips_the_page_ident_row);
  tcase_add_test(tc, ttx_lead_ms_shifts_cue_times_earlier);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(teletext_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
