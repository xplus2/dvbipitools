/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "aacprobe_frames.h"
#include "lib/demux/aacprobe/aacprobe.h"
#include "lib/demux/aacprobe/priv.h"
#include "lib/demux/escodec/escodec.h"

#define SFI_24K 6
#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

static aacprobe_result_t run(const aac_test_frame_t *f, size_t n, unsigned *fed) {
  aacprobe_t p;
  aacprobe_init(&p, SFI_24K, 48000);
  for (size_t i = 0; i < 64; i++) {
    const aac_test_frame_t *cur = &f[i % n];
    if (fed) *fed = (unsigned)i + 1;
    if (aacprobe_feed(&p, cur->data, cur->len)) break;
  }
  ck_assert_int_eq(p.done, 1);
  return p.result;
}

START_TEST(he_aac_frames_show_sbr) {
  unsigned fed;
  ck_assert_int_eq(run(he_frames, COUNT(he_frames), &fed), AACPROBE_SBR);
  ck_assert_uint_eq(fed, 1); /* stereo core, no PS to wait for */
}
END_TEST

START_TEST(he_aac_v2_frames_show_ps) {
  ck_assert_int_eq(run(hev2_frames, COUNT(hev2_frames), NULL), AACPROBE_SBR_PS);
}
END_TEST

START_TEST(plain_lc_frames_show_no_sbr) {
  ck_assert_int_eq(run(lc_frames, COUNT(lc_frames), NULL), AACPROBE_LC);
}
END_TEST

START_TEST(unknown_rate_index_is_final_at_once) {
  aacprobe_t p;
  aacprobe_init(&p, 13, 0);
  ck_assert_int_eq(aacprobe_feed(&p, he_frames[0].data, he_frames[0].len), 1);
  ck_assert_int_eq(p.result, AACPROBE_UNDECIDED);
}
END_TEST

START_TEST(garbage_gives_up_undecided) {
  static const unsigned char junk[64] = {0xFF, 0xFF, 0xFF};
  aacprobe_t p;
  int done = 0;
  aacprobe_init(&p, SFI_24K, 48000);
  for (int i = 0; i < 40 && !done; i++) done = aacprobe_feed(&p, junk, sizeof junk);
  ck_assert_int_eq(done, 1);
  ck_assert_int_eq(p.result, AACPROBE_UNDECIDED);
}
END_TEST

START_TEST(sbr_on_a_rate_without_tables_stays_undecided) {
  aacprobe_t p;
  aacprobe_init(&p, SFI_24K, 12345);
  for (int i = 0; i < 40 && !p.done; i++) aacprobe_feed(&p, hev2_frames[i % COUNT(hev2_frames)].data, hev2_frames[i % COUNT(hev2_frames)].len);
  ck_assert_int_eq(p.sbr_seen, 1);
  ck_assert_int_eq(p.result, AACPROBE_UNDECIDED);
}
END_TEST

static void feed_all(const aac_test_frame_t *f, size_t n, const unsigned char *data, size_t len) {
  aacprobe_t p;
  aacprobe_init(&p, SFI_24K, 48000);
  for (size_t i = 0; i < n; i++) aacprobe_feed(&p, f[i].data, f[i].len);
  p.done = 0;
  aacprobe_feed(&p, data, len);
}

START_TEST(truncated_frames_stay_in_bounds) {
  const aac_test_frame_t *f = &hev2_frames[1];
  for (size_t n = 0; n <= f->len; n++) {
    unsigned char *copy = malloc(n ? n : 1); /* exact size for sanitizers */
    ck_assert_ptr_nonnull(copy);
    memcpy(copy, f->data, n);
    feed_all(hev2_frames, 1, copy, n);
    free(copy);
  }
}
END_TEST

START_TEST(bit_flips_stay_in_bounds) {
  uint32_t lfsr = 0xACE1u;
  const aac_test_frame_t *sets[] = {&he_frames[1], &hev2_frames[1], &lc_frames[1]};
  for (int i = 0; i < 6000; i++) {
    const aac_test_frame_t *f = sets[i % 3];
    unsigned char *copy = malloc(f->len);
    ck_assert_ptr_nonnull(copy);
    memcpy(copy, f->data, f->len);
    for (int k = 0; k < 4; k++) {
      lfsr = (lfsr >> 1) ^ (-(lfsr & 1u) & 0xD0000001u);
      copy[lfsr % f->len] ^= (unsigned char)(1u << ((lfsr >> 16) & 7));
    }
    feed_all(hev2_frames, 2, copy, f->len);
    free(copy);
  }
}
END_TEST

START_TEST(huffman_books_decode_every_code) {
  for (size_t b = 0; b < 11; b++) {
    const aacprobe_book_t *book = &aacprobe_book_spectral[b];
    for (unsigned i = 0; i < book->n; i++) {
      const aacprobe_vlc_t *v = &book->vlc[i];
      unsigned char buf[4] = {0};
      uint32_t left = v->code << (32 - v->len);
      br_t r = {buf, sizeof buf, 0, 0};
      for (int k = 0; k < 4; k++) buf[k] = (unsigned char)(left >> (24 - 8 * k));
      ck_assert_int_eq(aacprobe_huff(&r, book), v->sym);
      ck_assert_uint_eq(r.bit, v->len);
    }
  }
}
END_TEST

START_TEST(band_counts_follow_figure_4_39_and_4_40) {
  aacprobe_sbr_t s;
  ck_assert_int_eq(aacprobe_sbr_bands(&s, 48000, 5, 9, 0, 2, 1, 2), 0);
  ck_assert_uint_gt(s.n_high, 0);
  ck_assert_uint_eq(s.n_low, (s.n_high + 1) / 2);
  ck_assert_uint_ge(s.n_q, 1);
  ck_assert_uint_le(s.n_q, 5);
  ck_assert_int_eq(aacprobe_sbr_bands(&s, 48000, 5, 9, 0, 0, 0, 0), 0);
  ck_assert_uint_eq(s.n_q, 1);
  ck_assert_int_eq(aacprobe_sbr_bands(&s, 48000, 5, 9, 7, 3, 1, 3) == 0 || s.n_q <= 5, 1);
  ck_assert_int_eq(aacprobe_sbr_bands(&s, 12345, 5, 9, 0, 2, 1, 2), -1);
  ck_assert_int_eq(aacprobe_sbr_bands(&s, 48000, 16, 9, 0, 2, 1, 2), -1);
}
END_TEST

START_TEST(asc_reads_plain_lc) {
  static const unsigned char lc[] = {0x11, 0x90};
  asc_info_t a;
  ck_assert_int_eq(asc_parse(lc, sizeof lc, &a), 0);
  ck_assert_uint_eq(a.aot, 2);
  ck_assert_uint_eq(a.rate, 48000);
  ck_assert_uint_eq(a.channels, 2);
  ck_assert_int_eq(a.sbr, -1);
  ck_assert_int_eq(a.ps, -1);
}
END_TEST

START_TEST(asc_reads_hierarchical_signalling) {
  static const unsigned char he[] = {0x2B, 0x11, 0x88, 0x00}; /* AOT 5, 24 kHz, stereo, ext 48 kHz, AOT 2 */
  static const unsigned char v2[] = {0xEB, 0x09, 0x88, 0x00}; /* AOT 29, 24 kHz, mono, ext 48 kHz, AOT 2 */
  asc_info_t a;
  ck_assert_int_eq(asc_parse(he, sizeof he, &a), 0);
  ck_assert_uint_eq(a.aot, 2);
  ck_assert_uint_eq(a.rate, 24000);
  ck_assert_uint_eq(a.ext_rate, 48000);
  ck_assert_int_eq(a.sbr, 1);
  ck_assert_int_eq(a.ps, -1);
  ck_assert_int_eq(asc_parse(v2, sizeof v2, &a), 0);
  ck_assert_int_eq(a.ps, 1);
  ck_assert_uint_eq(a.channels, 1);
}
END_TEST

START_TEST(asc_explicit_config_round_trips) {
  unsigned char out[ASC_EXPLICIT_MAX];
  asc_info_t a;
  size_t n = asc_build_explicit(6, 1, 3, 1, out, sizeof out);
  ck_assert_uint_eq(n, ASC_EXPLICIT_MAX);
  ck_assert_int_eq(out[0] >> 3, 2); /* LC first: backward compatible */
  ck_assert_int_eq(asc_parse(out, n, &a), 0);
  ck_assert_uint_eq(a.aot, 2);
  ck_assert_uint_eq(a.rate, 24000);
  ck_assert_uint_eq(a.ext_rate, 48000);
  ck_assert_int_eq(a.sbr, 1);
  ck_assert_int_eq(a.ps, 1);
  n = asc_build_explicit(6, 2, 3, 0, out, sizeof out);
  ck_assert_int_eq(asc_parse(out, n, &a), 0);
  ck_assert_int_eq(a.sbr, 1);
  ck_assert_int_eq(a.ps, 0);
  ck_assert_uint_eq(asc_build_explicit(6, 2, 3, 0, out, 3), 0);
}
END_TEST

static Suite *aacprobe_suite(void) {
  Suite *s = suite_create("aacprobe");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, he_aac_frames_show_sbr);
  tcase_add_test(tc, he_aac_v2_frames_show_ps);
  tcase_add_test(tc, plain_lc_frames_show_no_sbr);
  tcase_add_test(tc, unknown_rate_index_is_final_at_once);
  tcase_add_test(tc, garbage_gives_up_undecided);
  tcase_add_test(tc, sbr_on_a_rate_without_tables_stays_undecided);
  tcase_add_test(tc, truncated_frames_stay_in_bounds);
  tcase_add_test(tc, bit_flips_stay_in_bounds);
  tcase_add_test(tc, huffman_books_decode_every_code);
  tcase_add_test(tc, band_counts_follow_figure_4_39_and_4_40);
  tcase_add_test(tc, asc_reads_plain_lc);
  tcase_add_test(tc, asc_reads_hierarchical_signalling);
  tcase_add_test(tc, asc_explicit_config_round_trips);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(aacprobe_suite());
  int failed;
  srunner_run_all(sr, CK_NORMAL);
  failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
