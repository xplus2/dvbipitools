/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "dipiradiohead/input/icy.h"

typedef struct {
  int calls;
  char artist[512];
  char title[512];
} meta_capture_t;

static void meta_cb(void *ctx, const char *artist, const char *title) {
  meta_capture_t *m = ctx;
  m->calls++;
  strncpy(m->artist, artist, sizeof m->artist - 1);
  m->artist[sizeof m->artist - 1] = '\0';
  strncpy(m->title, title, sizeof m->title - 1);
  m->title[sizeof m->title - 1] = '\0';
}

static size_t build_meta_block(unsigned char *out, const char *streamtitle_tag) {
  size_t taglen = strlen(streamtitle_tag);
  size_t n16 = (taglen + 15) / 16;
  size_t block_len = n16 * 16;
  out[0] = (unsigned char)n16;
  memset(out + 1, 0, block_len);
  memcpy(out + 1, streamtitle_tag, taglen);
  return 1 + block_len;
}

START_TEST(metaint_zero_passes_through_unmodified) {
  icy_t *c = icy_new(0, NULL, NULL);
  unsigned char in[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  unsigned char out[8] = {0};
  size_t w = icy_feed(c, in, sizeof in, out, sizeof out);
  ck_assert_uint_eq(w, sizeof in);
  ck_assert_mem_eq(out, in, sizeof in);
  icy_free(c);
}

START_TEST(pure_audio_below_metaint_passes_through_unmodified) {
  icy_t *c = icy_new(100, NULL, NULL);
  unsigned char in[10];
  unsigned char out[10] = {0};
  for (int i = 0; i < 10; i++) in[i] = (unsigned char)(i + 1);
  size_t w = icy_feed(c, in, sizeof in, out, sizeof out);
  ck_assert_uint_eq(w, 10);
  ck_assert_mem_eq(out, in, 10);
  icy_free(c);
}

START_TEST(metaint_boundary_with_zero_length_meta_strips_only_length_byte) {
  icy_t *c = icy_new(4, NULL, NULL);
  unsigned char in[6] = {0xAA, 0xAA, 0xAA, 0xAA, 0x00, 0xBB};
  unsigned char out[6] = {0};
  size_t w = icy_feed(c, in, sizeof in, out, sizeof out);
  ck_assert_uint_eq(w, 5);
  ck_assert_mem_eq(out, "\xAA\xAA\xAA\xAA\xBB", 5);
  icy_free(c);
}

START_TEST(metadata_block_is_stripped_and_callback_fires) {
  icy_t *c;
  meta_capture_t m;
  unsigned char meta[256];
  unsigned char in[512];
  unsigned char out[512] = {0};
  size_t meta_len, off = 0;
  size_t w;

  memset(&m, 0, sizeof m);
  c = icy_new(4, meta_cb, &m);

  memcpy(in + off, "\xAA\xAA\xAA\xAA", 4);
  off += 4;
  meta_len = build_meta_block(meta, "StreamTitle='Artist Name - Song Title';junk=1;");
  memcpy(in + off, meta, meta_len);
  off += meta_len;
  memcpy(in + off, "\xBB\xBB", 2);
  off += 2;

  w = icy_feed(c, in, off, out, sizeof out);
  ck_assert_uint_eq(w, 6);
  ck_assert_mem_eq(out, "\xAA\xAA\xAA\xAA\xBB\xBB", 6);
  ck_assert_int_eq(m.calls, 1);
  ck_assert_str_eq(m.artist, "Artist Name");
  ck_assert_str_eq(m.title, "Song Title");

  icy_free(c);
}

START_TEST(metadata_without_separator_goes_entirely_to_title) {
  icy_t *c;
  meta_capture_t m;
  unsigned char meta[256];
  unsigned char in[256];
  unsigned char out[256] = {0};
  size_t meta_len, off = 0;

  memset(&m, 0, sizeof m);
  c = icy_new(2, meta_cb, &m);

  memcpy(in + off, "\xAA\xAA", 2);
  off += 2;
  meta_len = build_meta_block(meta, "StreamTitle='Just A Title';");
  memcpy(in + off, meta, meta_len);
  off += meta_len;

  icy_feed(c, in, off, out, sizeof out);
  ck_assert_int_eq(m.calls, 1);
  ck_assert_str_eq(m.artist, "");
  ck_assert_str_eq(m.title, "Just A Title");

  icy_free(c);
}

START_TEST(repeated_identical_title_only_fires_callback_once) {
  icy_t *c;
  meta_capture_t m;
  unsigned char meta[256];
  unsigned char in[256];
  unsigned char out[256] = {0};
  size_t meta_len;

  memset(&m, 0, sizeof m);
  c = icy_new(2, meta_cb, &m);
  meta_len = build_meta_block(meta, "StreamTitle='Same - Song';");

  memcpy(in, "\xAA\xAA", 2);
  memcpy(in + 2, meta, meta_len);
  icy_feed(c, in, 2 + meta_len, out, sizeof out);
  ck_assert_int_eq(m.calls, 1);

  memcpy(in, "\xAA\xAA", 2);
  memcpy(in + 2, meta, meta_len);
  icy_feed(c, in, 2 + meta_len, out, sizeof out);
  ck_assert_int_eq(m.calls, 1);

  icy_free(c);
}

START_TEST(state_carries_across_calls_when_boundary_splits_mid_call) {
  icy_t *c = icy_new(10, NULL, NULL);
  unsigned char in1[6] = {1, 2, 3, 4, 5, 6};
  unsigned char in2[8] = {7, 8, 9, 10, 0x00, 11, 12, 13};
  unsigned char out1[6] = {0};
  unsigned char out2[8] = {0};
  size_t w1, w2;

  w1 = icy_feed(c, in1, sizeof in1, out1, sizeof out1);
  ck_assert_uint_eq(w1, 6);
  ck_assert_mem_eq(out1, in1, 6);

  w2 = icy_feed(c, in2, sizeof in2, out2, sizeof out2);
  ck_assert_uint_eq(w2, 7);
  ck_assert_mem_eq(out2, "\x07\x08\x09\x0A\x0B\x0C\x0D", 7);

  icy_free(c);
}

START_TEST(output_capped_below_inlen_drops_excess_audio_without_desync) {
  icy_t *c = icy_new(100, NULL, NULL);
  unsigned char in[10];
  unsigned char out[4] = {0};
  size_t w;
  for (int i = 0; i < 10; i++) in[i] = (unsigned char)(i + 1);

  w = icy_feed(c, in, sizeof in, out, sizeof out);
  ck_assert_uint_eq(w, 4);
  ck_assert_mem_eq(out, "\x01\x02\x03\x04", 4);

  {
    unsigned char in2[1] = {99};
    unsigned char out2[90] = {0};
    size_t w2;
    unsigned char pad[90];
    memset(pad, 0x55, sizeof pad);
    w2 = icy_feed(c, pad, sizeof pad, out2, sizeof out2);
    ck_assert_uint_eq(w2, 90);
    w2 = icy_feed(c, in2, sizeof in2, out2, sizeof out2);
    ck_assert_uint_eq(w2, 0);
  }

  icy_free(c);
}

static Suite *icy_suite(void) {
  Suite *s = suite_create("dipiradiohead_icy");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, metaint_zero_passes_through_unmodified);
  tcase_add_test(tc, pure_audio_below_metaint_passes_through_unmodified);
  tcase_add_test(tc, metaint_boundary_with_zero_length_meta_strips_only_length_byte);
  tcase_add_test(tc, metadata_block_is_stripped_and_callback_fires);
  tcase_add_test(tc, metadata_without_separator_goes_entirely_to_title);
  tcase_add_test(tc, repeated_identical_title_only_fires_callback_once);
  tcase_add_test(tc, state_carries_across_calls_when_boundary_splits_mid_call);
  tcase_add_test(tc, output_capped_below_inlen_drops_excess_audio_without_desync);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(icy_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
