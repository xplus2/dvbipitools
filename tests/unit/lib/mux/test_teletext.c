/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
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
    unsigned char c = 0x20;
    if (i < tlen) c = (unsigned char)text[i];
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

#define CONCURRENT_TTX_THREADS 16

typedef struct {
  int calls;
  ttx_cue_t cue;
} thread_capture_t;

static void thread_capture_cb(void *ctx, const ttx_cue_t *cue) {
  thread_capture_t *c = ctx;
  c->calls++;
  c->cue = *cue;
}

static void *ttx_new_from_thread(void *arg) {
  thread_capture_t *c = arg;
  unsigned char pes[64];
  size_t n;
  ttx_t *t = ttx_new(777, "eng", 0, thread_capture_cb, c);
  if (!t) return (void *)(intptr_t)1;
  n = build_ttx_pes(pes, 777, 1, "HELLO");
  ttx_pes(t, 1, 0, pes, n);
  ttx_flush(t);
  ttx_free(t);
  return (void *)(intptr_t)(c->calls == 1 && !strncmp(c->cue.text, "HELLO", 5) ? 0 : 1);
}

/* unham_init() (the hamming-decode table build) is lazily run on first ttx_new(),
   from a pthread_once - exercises that every concurrent caller gets a fully built
   table and a working decode, none racing on the table build itself */
START_TEST(ttx_new_is_safe_under_concurrent_first_use) {
  pthread_t th[CONCURRENT_TTX_THREADS];
  thread_capture_t caps[CONCURRENT_TTX_THREADS];
  void *res;

  memset(caps, 0, sizeof caps);
  for (int i = 0; i < CONCURRENT_TTX_THREADS; i++)
    ck_assert_int_eq(pthread_create(&th[i], NULL, ttx_new_from_thread, &caps[i]), 0);
  for (int i = 0; i < CONCURRENT_TTX_THREADS; i++) {
    ck_assert_int_eq(pthread_join(th[i], &res), 0);
    ck_assert_ptr_eq(res, (void *)0);
  }
}
END_TEST

#define MAX_LOG_CUES 8

static ttx_cue_t g_log[MAX_LOG_CUES];
static int g_nlog;

static void log_cb(void *ctx, const ttx_cue_t *cue) {
  (void)ctx;
  if (g_nlog < MAX_LOG_CUES) g_log[g_nlog] = *cue;
  g_nlog++;
}

static void feed_row(ttx_t *t, unsigned page, const char *text, uint64_t pts_ms) {
  unsigned char pes[64];
  size_t n = build_ttx_pes(pes, page, 1, text);

  ttx_pes(t, 1, pts_ms * 90, pes, n);
}

typedef struct {
  const char *lang;
  const char *expect;
} nat_case_t;

static const nat_case_t nat_cases[] = {
  {"deu", "#$\xC2\xA7\xC3\x84\xC3\x96\xC3\x9C^_\xC2\xB0\xC3\xA4\xC3\xB6\xC3\xBC\xC3\x9F"},
  {"fra", "\xC3\xA9\xC3\xAF\xC3\xA0\xC3\xAB\xC3\xAA\xC3\xB9\xC3\xAE#\xC3\xA8\xC3\xA2\xC3\xB4\xC3\xBB\xC3\xA7"},
  {"ita", "\xC2\xA3$\xC3\xA9\xC2\xB0\xC3\xA7\xE2\x86\x92\xE2\x86\x91#\xC3\xB9\xC3\xA0\xC3\xB2\xC3\xA8\xC3\xAC"},
  {"swe", "#\xC2\xA4\xC3\x89\xC3\x84\xC3\x96\xC3\x85\xC3\x9C_\xC3\xA9\xC3\xA4\xC3\xB6\xC3\xA5\xC3\xBC"},
  {"spa", "\xC3\xA7$\xC2\xA1\xC3\xA1\xC3\xA9\xC3\xAD\xC3\xB3\xC3\xBA\xC2\xBF\xC3\xBC\xC3\xB1\xC3\xA8\xC3\xA0"},
  {"ces", "#u\xC4\x8D\xC5\xA5\xC5\xBE\xC3\xBD\xC3\xAD\xC5\x99\xC3\xA9\xC3\xA1\xC4\x9B\xC3\xBA\xC5\xA1"},
  {"eng", "\xC2\xA3$@\xE2\x86\x90\xC2\xBD\xE2\x86\x92\xE2\x86\x91#\xE2\x80\x95\xC2\xBC\xE2\x80\x96\xC2\xBE\xC3\xB7"},
  {NULL, "\xC2\xA3$@\xE2\x86\x90\xC2\xBD\xE2\x86\x92\xE2\x86\x91#\xE2\x80\x95\xC2\xBC\xE2\x80\x96\xC2\xBE\xC3\xB7"},
  {"xxx", "\xC2\xA3$@\xE2\x86\x90\xC2\xBD\xE2\x86\x92\xE2\x86\x91#\xE2\x80\x95\xC2\xBC\xE2\x80\x96\xC2\xBE\xC3\xB7"},
};

START_TEST(ttx_maps_national_character_subsets_by_language) {
  const nat_case_t *c = &nat_cases[_i];
  ttx_t *t = ttx_new(777, c->lang, 0, log_cb, NULL);

  g_nlog = 0;
  feed_row(t, 777, "#$@[\\]^_`{|}~", 1000);
  ttx_flush(t);
  ck_assert_int_eq(g_nlog, 1);
  ck_assert_str_eq(g_log[0].text, c->expect);
  ttx_free(t);
}
END_TEST

START_TEST(ttx_turns_control_codes_into_spaces_and_trims_row_edges) {
  ttx_t *t = ttx_new(777, "eng", 0, log_cb, NULL);

  g_nlog = 0;
  feed_row(t, 777, "\x01  AB\x02" "CD   ", 1000);
  ttx_flush(t);
  ck_assert_int_eq(g_nlog, 1);
  ck_assert_str_eq(g_log[0].text, "AB CD");
  ttx_free(t);
}
END_TEST

START_TEST(ttx_joins_rows_of_one_group_and_drops_repeated_rows) {
  ttx_t *t = ttx_new(777, "eng", 0, log_cb, NULL);

  g_nlog = 0;
  feed_row(t, 777, "FIRST", 1000);
  feed_row(t, 777, "SECOND", 1100);
  feed_row(t, 777, "FIRST", 1200);
  feed_row(t, 777, "   ", 1250);
  ttx_flush(t);
  ck_assert_int_eq(g_nlog, 1);
  ck_assert_str_eq(g_log[0].text, "FIRST\nSECOND");
  ttx_free(t);
}
END_TEST

START_TEST(ttx_splits_groups_on_a_gap_and_ignores_carousel_repeats) {
  ttx_t *t = ttx_new(777, "eng", 0, log_cb, NULL);

  g_nlog = 0;
  feed_row(t, 777, "ONE", 1000);
  feed_row(t, 777, "TWO", 1600);
  feed_row(t, 777, "TWO", 2400);
  ttx_flush(t);
  ck_assert_int_eq(g_nlog, 2);
  ck_assert_str_eq(g_log[0].text, "ONE");
  ck_assert_int_eq((int)g_log[0].start_ms, 1000);
  ck_assert_int_eq((int)g_log[0].end_ms, 1600);
  ck_assert_str_eq(g_log[1].text, "TWO");
  ck_assert_int_eq((int)g_log[1].start_ms, 1600);
  ck_assert_int_eq((int)g_log[1].end_ms, 2800);
  ttx_free(t);
}
END_TEST

START_TEST(ttx_caps_long_cues_and_holds_short_ones_to_the_minimum) {
  ttx_t *t = ttx_new(777, "eng", 0, log_cb, NULL);

  g_nlog = 0;
  feed_row(t, 777, "LONG", 1000);
  feed_row(t, 777, "NEXT", 20000);
  ttx_flush(t);
  ck_assert_int_eq(g_nlog, 2);
  ck_assert_int_eq((int)g_log[0].end_ms, 6000);
  ck_assert_int_eq((int)(g_log[1].end_ms - g_log[1].start_ms), 1200);
  ttx_free(t);
}
END_TEST

START_TEST(ttx_clamps_a_lead_that_would_start_before_zero) {
  ttx_t *t = ttx_new(777, "eng", 2000, log_cb, NULL);

  g_nlog = 0;
  feed_row(t, 777, "EARLY", 1000);
  ttx_flush(t);
  ck_assert_int_eq(g_nlog, 1);
  ck_assert_int_eq((int)g_log[0].start_ms, 0);
  ck_assert(g_log[0].end_ms > 0);
  ttx_free(t);
}
END_TEST

START_TEST(ttx_keeps_at_most_one_screen_of_rows_per_group) {
  ttx_t *t = ttx_new(777, "eng", 0, log_cb, NULL);
  char row[8];
  int lines = 1;

  g_nlog = 0;
  for (int i = 0; i < 26; i++) {
    snprintf(row, sizeof row, "R%02d", i);
    feed_row(t, 777, row, 1000);
  }
  ttx_flush(t);
  ck_assert_int_eq(g_nlog, 1);
  for (const char *p = g_log[0].text; *p; p++)
    if (*p == '\n') lines++;
  ck_assert_int_eq(lines, 24);
  ttx_free(t);
}
END_TEST

typedef struct {
  const char *name;
  size_t off;
  unsigned char xor_mask;
  unsigned char set;
  int use_set;
} corrupt_case_t;

static const corrupt_case_t corrupt_cases[] = {
  {"data_identifier below range", 0, 0, 0x05, 1},
  {"data_identifier above range", 0, 0, 0x20, 1},
  {"not an ebu subtitle unit", 1, 0, 0x02, 1},
  {"unit shorter than a packet", 2, 0, 0x10, 1},
  {"uncorrectable magazine hamming", 5, 0x03, 0, 0},
};

START_TEST(ttx_ignores_malformed_data_units) {
  const corrupt_case_t *c = &corrupt_cases[_i];
  ttx_t *t = ttx_new(777, "eng", 0, log_cb, NULL);
  unsigned char pes[64];
  size_t n = build_ttx_pes(pes, 777, 1, "TEXT");

  g_nlog = 0;
  if (c->use_set) pes[c->off] = c->set;
  else pes[c->off] ^= c->xor_mask;
  ttx_pes(t, 1, 90000, pes, n);
  ttx_flush(t);
  ck_assert_msg(g_nlog == 0, "%s: cue emitted", c->name);
  ttx_free(t);
}
END_TEST

START_TEST(ttx_survives_truncated_and_empty_input_and_missing_pts) {
  ttx_t *t = ttx_new(777, "eng", 0, log_cb, NULL);
  unsigned char pes[64];
  size_t n = build_ttx_pes(pes, 777, 1, "TEXT");

  g_nlog = 0;
  ttx_pes(t, 1, 90000, pes, 0);
  ttx_pes(t, 1, 90000, pes, 1);
  ttx_pes(t, 1, 90000, pes, n - 5);
  ttx_flush(t);
  ck_assert_int_eq(g_nlog, 0);
  ttx_free(t);
  t = ttx_new(777, "eng", 0, log_cb, NULL);
  ttx_pes(t, 0, 0, pes, n);
  ttx_flush(t);
  ck_assert_int_eq(g_nlog, 1);
  ck_assert_int_eq((int)g_log[0].start_ms, 0);
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
  tcase_add_test(tc, ttx_new_is_safe_under_concurrent_first_use);
  tcase_add_loop_test(tc, ttx_maps_national_character_subsets_by_language, 0, (int)(sizeof nat_cases / sizeof nat_cases[0]));
  tcase_add_test(tc, ttx_turns_control_codes_into_spaces_and_trims_row_edges);
  tcase_add_test(tc, ttx_joins_rows_of_one_group_and_drops_repeated_rows);
  tcase_add_test(tc, ttx_splits_groups_on_a_gap_and_ignores_carousel_repeats);
  tcase_add_test(tc, ttx_caps_long_cues_and_holds_short_ones_to_the_minimum);
  tcase_add_test(tc, ttx_clamps_a_lead_that_would_start_before_zero);
  tcase_add_test(tc, ttx_keeps_at_most_one_screen_of_rows_per_group);
  tcase_add_loop_test(tc, ttx_ignores_malformed_data_units, 0, (int)(sizeof corrupt_cases / sizeof corrupt_cases[0]));
  tcase_add_test(tc, ttx_survives_truncated_and_empty_input_and_missing_pts);
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
