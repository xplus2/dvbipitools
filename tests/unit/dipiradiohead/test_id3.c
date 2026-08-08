/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dipiradiohead/input/id3.h"

typedef struct {
  char artist[256];
  char title[256];
  int calls;
} capture_t;

static void on_meta(void *ctx, const char *artist, const char *title) {
  capture_t *c = ctx;
  snprintf(c->artist, sizeof c->artist, "%s", artist);
  snprintf(c->title, sizeof c->title, "%s", title);
  c->calls++;
}

static void put_syncsafe(unsigned char *out, unsigned v) {
  out[0] = (unsigned char)((v >> 21) & 0x7F);
  out[1] = (unsigned char)((v >> 14) & 0x7F);
  out[2] = (unsigned char)((v >> 7) & 0x7F);
  out[3] = (unsigned char)(v & 0x7F);
}

/* minimal ID3v2.4 tag: one TIT2, one TPE1, syncsafe frame sizes */
static size_t build_tag(unsigned char *out,
                         const unsigned char *title_body, size_t title_len,
                         const unsigned char *artist_body, size_t artist_len) {
  size_t n = 0;
  unsigned body_size;

  out[n++] = 'I'; out[n++] = 'D'; out[n++] = '3';
  out[n++] = 4; /* version 2.4 */
  out[n++] = 0; /* revision */
  out[n++] = 0; /* flags */
  n += 4; /* tag size, patched below */

  memcpy(out + n, "TIT2", 4); n += 4;
  put_syncsafe(out + n, (unsigned)title_len); n += 4;
  out[n++] = 0; out[n++] = 0;
  memcpy(out + n, title_body, title_len); n += title_len;

  memcpy(out + n, "TPE1", 4); n += 4;
  put_syncsafe(out + n, (unsigned)artist_len); n += 4;
  out[n++] = 0; out[n++] = 0;
  memcpy(out + n, artist_body, artist_len); n += artist_len;

  body_size = (unsigned)(n - 10);
  put_syncsafe(out + 6, body_size);
  return n;
}

START_TEST(id3_is_tag_and_tag_size) {
  unsigned char hdr[10] = {'I', 'D', '3', 4, 0, 0, 0, 0, 0, 10};
  ck_assert_int_eq(id3_is_tag(hdr, sizeof hdr), 1);
  ck_assert_uint_eq(id3_tag_size(hdr, sizeof hdr), 20u); /* 10 header + syncsafe body 10 */
  ck_assert_int_eq(id3_is_tag((const unsigned char *)"XYZ", 3), 0);
}
END_TEST

START_TEST(id3_iso8859_1_converts_to_utf8) {
  unsigned char title_body[] = {0x00, 'C', 'a', 'f', 0xE9}; /* Latin-1 "Caf\xE9" == "Caf" + e-acute */
  unsigned char artist_body[] = {0x00, 'X'};
  unsigned char tag[128];
  size_t n = build_tag(tag, title_body, sizeof title_body, artist_body, sizeof artist_body);
  capture_t cap;
  id3_t *c;

  memset(&cap, 0, sizeof cap);
  c = id3_new(on_meta, &cap);
  id3_consume(c, tag, n);
  ck_assert_int_eq(cap.calls, 1);
  ck_assert_str_eq(cap.title, "Caf\xC3\xA9"); /* UTF-8 for U+00E9 */
  id3_free(c);
}
END_TEST

START_TEST(id3_utf8_passthrough_unchanged) {
  unsigned char title_body[] = {0x03, 'C', 'a', 'f', 0xC3, 0xA9}; /* already UTF-8 */
  unsigned char artist_body[] = {0x03, 'X'};
  unsigned char tag[128];
  size_t n = build_tag(tag, title_body, sizeof title_body, artist_body, sizeof artist_body);
  capture_t cap;
  id3_t *c;

  memset(&cap, 0, sizeof cap);
  c = id3_new(on_meta, &cap);
  id3_consume(c, tag, n);
  ck_assert_str_eq(cap.title, "Caf\xC3\xA9");
  id3_free(c);
}
END_TEST

START_TEST(id3_utf16_be_no_bom_converts_bmp_char) {
  /* enc 0x02: fixed UTF-16BE, no BOM. "A" + U+2013 (EN DASH) + "B" */
  unsigned char title_body[] = {0x02, 0x00, 'A', 0x20, 0x13, 0x00, 'B'};
  unsigned char artist_body[] = {0x02, 0x00, 'X'};
  unsigned char tag[128];
  size_t n = build_tag(tag, title_body, sizeof title_body, artist_body, sizeof artist_body);
  capture_t cap;
  id3_t *c;

  memset(&cap, 0, sizeof cap);
  c = id3_new(on_meta, &cap);
  id3_consume(c, tag, n);
  ck_assert_str_eq(cap.title, "A\xE2\x80\x93" "B"); /* U+2013 -> E2 80 93 */
  id3_free(c);
}
END_TEST

START_TEST(id3_utf16_le_with_bom_converts) {
  /* enc 0x01, LE BOM (FF FE): "A" + U+2013 + "B", each unit little-endian */
  unsigned char title_body[] = {0x01, 0xFF, 0xFE, 'A', 0x00, 0x13, 0x20, 'B', 0x00};
  unsigned char artist_body[] = {0x01, 0xFF, 0xFE, 'X', 0x00};
  unsigned char tag[128];
  size_t n = build_tag(tag, title_body, sizeof title_body, artist_body, sizeof artist_body);
  capture_t cap;
  id3_t *c;

  memset(&cap, 0, sizeof cap);
  c = id3_new(on_meta, &cap);
  id3_consume(c, tag, n);
  ck_assert_str_eq(cap.title, "A\xE2\x80\x93" "B");
  id3_free(c);
}
END_TEST

START_TEST(id3_utf16_be_with_bom_converts) {
  /* enc 0x01, BE BOM (FE FF) */
  unsigned char title_body[] = {0x01, 0xFE, 0xFF, 0x00, 'A', 0x20, 0x13, 0x00, 'B'};
  unsigned char artist_body[] = {0x01, 0xFE, 0xFF, 0x00, 'X'};
  unsigned char tag[128];
  size_t n = build_tag(tag, title_body, sizeof title_body, artist_body, sizeof artist_body);
  capture_t cap;
  id3_t *c;

  memset(&cap, 0, sizeof cap);
  c = id3_new(on_meta, &cap);
  id3_consume(c, tag, n);
  ck_assert_str_eq(cap.title, "A\xE2\x80\x93" "B");
  id3_free(c);
}
END_TEST

START_TEST(id3_utf16_surrogate_pair_converts_to_4byte_utf8) {
  /* enc 0x02, BE, no BOM: U+1F600 (grinning face) as a surrogate pair */
  unsigned char title_body[] = {0x02, 0xD8, 0x3D, 0xDE, 0x00};
  unsigned char artist_body[] = {0x02, 0x00, 'X'};
  unsigned char tag[128];
  size_t n = build_tag(tag, title_body, sizeof title_body, artist_body, sizeof artist_body);
  capture_t cap;
  id3_t *c;

  memset(&cap, 0, sizeof cap);
  c = id3_new(on_meta, &cap);
  id3_consume(c, tag, n);
  ck_assert_str_eq(cap.title, "\xF0\x9F\x98\x80");
  id3_free(c);
}
END_TEST

START_TEST(id3_consume_dedupes_unchanged_metadata) {
  unsigned char title_body[] = {0x00, 'T'};
  unsigned char artist_body[] = {0x00, 'A'};
  unsigned char tag[128];
  size_t n = build_tag(tag, title_body, sizeof title_body, artist_body, sizeof artist_body);
  capture_t cap;
  id3_t *c;

  memset(&cap, 0, sizeof cap);
  c = id3_new(on_meta, &cap);
  id3_consume(c, tag, n);
  id3_consume(c, tag, n);
  ck_assert_int_eq(cap.calls, 1);
  id3_free(c);
}
END_TEST

static Suite *id3_suite(void) {
  Suite *s = suite_create("id3");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, id3_is_tag_and_tag_size);
  tcase_add_test(tc, id3_iso8859_1_converts_to_utf8);
  tcase_add_test(tc, id3_utf8_passthrough_unchanged);
  tcase_add_test(tc, id3_utf16_be_no_bom_converts_bmp_char);
  tcase_add_test(tc, id3_utf16_le_with_bom_converts);
  tcase_add_test(tc, id3_utf16_be_with_bom_converts);
  tcase_add_test(tc, id3_utf16_surrogate_pair_converts_to_4byte_utf8);
  tcase_add_test(tc, id3_consume_dedupes_unchanged_metadata);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(id3_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
