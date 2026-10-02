/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dipiradiohead/input/id3.h"
#include "lib/sys/ioutil.h"

typedef struct {
  char artist[256];
  char title[256];
  int calls;
} capture_t;

static void on_meta(void *ctx, const char *artist, const char *title) {
  capture_t *c = ctx;
  bufcpy(c->artist, sizeof c->artist, artist);
  bufcpy(c->title, sizeof c->title, title);
  c->calls++;
}

static void put_syncsafe(unsigned char *out, unsigned v) {
  out[0] = (unsigned char)((v >> 21) & 0x7F);
  out[1] = (unsigned char)((v >> 14) & 0x7F);
  out[2] = (unsigned char)((v >> 7) & 0x7F);
  out[3] = (unsigned char)(v & 0x7F);
}

typedef struct {
  unsigned version;
  unsigned flags;
  const unsigned char *ext;
  size_t ext_len;
  size_t footer_len;
} tag_opts_t;

static void put_frame_size(unsigned char *out, unsigned version, unsigned v) {
  if (version >= 4) {
    put_syncsafe(out, v);
    return;
  }
  out[0] = (unsigned char)(v >> 24);
  out[1] = (unsigned char)(v >> 16);
  out[2] = (unsigned char)(v >> 8);
  out[3] = (unsigned char)v;
}

static size_t build_tag_opts(unsigned char *out, const tag_opts_t *o,
                             const unsigned char *title_body, size_t title_len,
                             const unsigned char *artist_body, size_t artist_len) {
  size_t n = 0;
  unsigned body_size;

  out[n++] = 'I'; out[n++] = 'D'; out[n++] = '3';
  out[n++] = (unsigned char)o->version;
  out[n++] = 0; /* revision */
  out[n++] = (unsigned char)o->flags;
  n += 4; /* tag size, patched below */

  if (o->ext_len) memcpy(out + n, o->ext, o->ext_len);
  n += o->ext_len;

  memcpy(out + n, "TIT2", 4); n += 4;
  put_frame_size(out + n, o->version, (unsigned)title_len); n += 4;
  out[n++] = 0; out[n++] = 0;
  memcpy(out + n, title_body, title_len); n += title_len;

  memcpy(out + n, "TPE1", 4); n += 4;
  put_frame_size(out + n, o->version, (unsigned)artist_len); n += 4;
  out[n++] = 0; out[n++] = 0;
  memcpy(out + n, artist_body, artist_len); n += artist_len;

  body_size = (unsigned)(n - 10);
  put_syncsafe(out + 6, body_size);
  memset(out + n, 0, o->footer_len);
  n += o->footer_len;
  return n;
}

static size_t build_tag(unsigned char *out,
                         const unsigned char *title_body, size_t title_len,
                         const unsigned char *artist_body, size_t artist_len) {
  static const tag_opts_t v24 = {4, 0, NULL, 0, 0};
  return build_tag_opts(out, &v24, title_body, title_len, artist_body, artist_len);
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

START_TEST(id3_v23_frames_use_plain_big_endian_sizes) {
  static const tag_opts_t v23 = {3, 0, NULL, 0, 0};
  unsigned char title_body[130];
  unsigned char artist_body[] = {0x00, 'X'};
  unsigned char tag[512];
  size_t n;
  capture_t cap;
  id3_t *c;

  memset(title_body, 'T', sizeof title_body);
  title_body[0] = 0x00;
  n = build_tag_opts(tag, &v23, title_body, sizeof title_body, artist_body, sizeof artist_body);
  memset(&cap, 0, sizeof cap);
  c = id3_new(on_meta, &cap);
  id3_consume(c, tag, n);
  ck_assert_int_eq(cap.calls, 1);
  ck_assert_uint_eq(strlen(cap.title), 129u);
  ck_assert_str_eq(cap.artist, "X");
  id3_free(c);
}
END_TEST

typedef struct {
  const char *name;
  tag_opts_t opts;
} ext_case_t;

static const unsigned char ext_v3[] = {0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static const unsigned char ext_v4[] = {0x00, 0x00, 0x00, 0x06, 0x01, 0x00};

static const ext_case_t ext_cases[] = {
    {"v2.3 extended header", {3, 0x40, ext_v3, sizeof ext_v3, 0}},
    {"v2.4 extended header", {4, 0x40, ext_v4, sizeof ext_v4, 0}},
};

START_TEST(id3_extended_header_is_skipped) {
  const ext_case_t *ec = &ext_cases[_i];
  unsigned char title_body[] = {0x00, 'T'};
  unsigned char artist_body[] = {0x00, 'A'};
  unsigned char tag[128];
  size_t n = build_tag_opts(tag, &ec->opts, title_body, sizeof title_body, artist_body, sizeof artist_body);
  capture_t cap;
  id3_t *c;

  memset(&cap, 0, sizeof cap);
  c = id3_new(on_meta, &cap);
  id3_consume(c, tag, n);
  ck_assert_msg(cap.calls == 1, "%s: %d callbacks", ec->name, cap.calls);
  ck_assert_str_eq(cap.title, "T");
  ck_assert_str_eq(cap.artist, "A");
  id3_free(c);
}
END_TEST

START_TEST(id3_v24_footer_adds_ten_bytes_to_tag_size) {
  static const tag_opts_t footer = {4, 0x10, NULL, 0, 10};
  static const tag_opts_t plain = {4, 0, NULL, 0, 0};
  unsigned char title_body[] = {0x00, 'T'};
  unsigned char artist_body[] = {0x00, 'A'};
  unsigned char with[128];
  unsigned char without[128];
  size_t n_with = build_tag_opts(with, &footer, title_body, sizeof title_body, artist_body, sizeof artist_body);
  size_t n_without = build_tag_opts(without, &plain, title_body, sizeof title_body, artist_body, sizeof artist_body);
  capture_t cap;
  id3_t *c;

  ck_assert_uint_eq(id3_tag_size(without, n_without), n_without);
  ck_assert_uint_eq(id3_tag_size(with, n_with), n_with);
  ck_assert_uint_eq(n_with, n_without + 10);
  memset(&cap, 0, sizeof cap);
  c = id3_new(on_meta, &cap);
  id3_consume(c, with, n_with);
  ck_assert_int_eq(cap.calls, 1);
  ck_assert_str_eq(cap.title, "T");
  id3_free(c);
}
END_TEST

START_TEST(id3_footer_flag_is_ignored_before_v24) {
  static const tag_opts_t v23_flag = {3, 0x10, NULL, 0, 0};
  unsigned char title_body[] = {0x00, 'T'};
  unsigned char artist_body[] = {0x00, 'A'};
  unsigned char tag[128];
  size_t n = build_tag_opts(tag, &v23_flag, title_body, sizeof title_body, artist_body, sizeof artist_body);

  ck_assert_uint_eq(id3_tag_size(tag, n), n);
}
END_TEST

typedef struct {
  const char *name;
  size_t patch_off;
  unsigned char patch[4];
  size_t taglen_cut;
  int calls;
  const char *title;
  const char *artist;
} malformed_case_t;

static const malformed_case_t malformed_cases[] = {
    {"first frame size overruns tag", 14, {0x00, 0x00, 0x7F, 0x7F}, 0, 0, "", ""},
    {"second frame size overruns tag", 26, {0x00, 0x00, 0x7F, 0x7F}, 0, 1, "T", ""},
    {"tag shorter than header", 0, {0}, 9, 0, "", ""},
    {"truncated inside first frame", 0, {0}, 13, 0, "", ""},
    {"truncated inside second frame", 0, {0}, 33, 1, "T", ""},
};

START_TEST(id3_malformed_tag_is_handled_without_overrun) {
  const malformed_case_t *mc = &malformed_cases[_i];
  unsigned char title_body[] = {0x00, 'T'};
  unsigned char artist_body[] = {0x00, 'A'};
  unsigned char tag[128];
  size_t n = build_tag(tag, title_body, sizeof title_body, artist_body, sizeof artist_body);
  capture_t cap;
  id3_t *c;

  if (mc->patch_off) memcpy(tag + mc->patch_off, mc->patch, sizeof mc->patch);
  if (mc->taglen_cut) n = mc->taglen_cut;
  memset(&cap, 0, sizeof cap);
  c = id3_new(on_meta, &cap);
  id3_consume(c, tag, n);
  ck_assert_msg(cap.calls == mc->calls, "%s: %d callbacks", mc->name, cap.calls);
  ck_assert_msg(strcmp(cap.title, mc->title) == 0, "%s: title '%s'", mc->name, cap.title);
  ck_assert_msg(strcmp(cap.artist, mc->artist) == 0, "%s: artist '%s'", mc->name, cap.artist);
  id3_free(c);
}
END_TEST

START_TEST(id3_extended_header_beyond_tag_end_is_rejected) {
  unsigned char tag[10] = {'I', 'D', '3', 4, 0, 0x40, 0, 0, 0, 0};
  capture_t cap;
  id3_t *c;

  memset(&cap, 0, sizeof cap);
  c = id3_new(on_meta, &cap);
  id3_consume(c, tag, sizeof tag);
  ck_assert_int_eq(cap.calls, 0);
  id3_free(c);
}
END_TEST

typedef struct {
  const char *name;
  unsigned char body[12];
  size_t len;
  const char *want;
} surrogate_case_t;

static const surrogate_case_t surrogate_cases[] = {
    {"lone high surrogate before text", {0x02, 0x00, 'A', 0xD8, 0x3D, 0x00, 'B'}, 7, "A\xEF\xBF\xBD" "B"},
    {"lone high surrogate at end", {0x02, 0x00, 'A', 0xD8, 0x3D}, 5, "A\xEF\xBF\xBD"},
    {"lone low surrogate", {0x02, 0x00, 'A', 0xDC, 0x00, 0x00, 'B'}, 7, "A\xEF\xBF\xBD" "B"},
    {"high surrogate then high surrogate", {0x02, 0xD8, 0x3D, 0xD8, 0x3D, 0x00, 'B'}, 7, "\xEF\xBF\xBD\xEF\xBF\xBD" "B"},
};

START_TEST(id3_lone_utf16_surrogate_becomes_replacement_character) {
  const surrogate_case_t *sc = &surrogate_cases[_i];
  unsigned char artist_body[] = {0x02, 0x00, 'X'};
  unsigned char tag[128];
  size_t n = build_tag(tag, sc->body, sc->len, artist_body, sizeof artist_body);
  capture_t cap;
  id3_t *c;

  memset(&cap, 0, sizeof cap);
  c = id3_new(on_meta, &cap);
  id3_consume(c, tag, n);
  ck_assert_msg(strcmp(cap.title, sc->want) == 0, "%s: title '%s'", sc->name, cap.title);
  id3_free(c);
}
END_TEST

START_TEST(id3_overlong_text_is_truncated_to_the_stack_buffers) {
  unsigned char title_body[301];
  unsigned char artist_body[301];
  unsigned char tag[700];
  size_t n;
  capture_t cap;
  id3_t *c;

  memset(title_body, 'T', sizeof title_body);
  memset(artist_body, 'A', sizeof artist_body);
  title_body[0] = 0x00;
  artist_body[0] = 0x03;
  n = build_tag(tag, title_body, sizeof title_body, artist_body, sizeof artist_body);
  memset(&cap, 0, sizeof cap);
  c = id3_new(on_meta, &cap);
  id3_consume(c, tag, n);
  ck_assert_int_eq(cap.calls, 1);
  ck_assert_uint_eq(strlen(cap.title), 255u);
  ck_assert_uint_eq(strlen(cap.artist), 255u);
  id3_free(c);
}
END_TEST

START_TEST(id3_overlong_multibyte_text_is_cut_on_a_character_boundary) {
  unsigned char title_body[1 + 2 * 200];
  unsigned char artist_body[] = {0x00, 'X'};
  unsigned char tag[700];
  size_t n;
  capture_t cap;
  id3_t *c;

  title_body[0] = 0x03;
  for (size_t i = 0; i < 200; i++) {
    title_body[1 + 2 * i] = 0xC3;
    title_body[2 + 2 * i] = 0xA9;
  }
  n = build_tag(tag, title_body, sizeof title_body, artist_body, sizeof artist_body);
  memset(&cap, 0, sizeof cap);
  c = id3_new(on_meta, &cap);
  id3_consume(c, tag, n);
  ck_assert_uint_eq(strlen(cap.title), 254u);
  ck_assert_int_eq((unsigned char)cap.title[253], 0xA9);
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
  tcase_add_test(tc, id3_v23_frames_use_plain_big_endian_sizes);
  tcase_add_loop_test(tc, id3_extended_header_is_skipped, 0, (int)(sizeof ext_cases / sizeof ext_cases[0]));
  tcase_add_test(tc, id3_v24_footer_adds_ten_bytes_to_tag_size);
  tcase_add_test(tc, id3_footer_flag_is_ignored_before_v24);
  tcase_add_loop_test(tc, id3_malformed_tag_is_handled_without_overrun, 0, (int)(sizeof malformed_cases / sizeof malformed_cases[0]));
  tcase_add_test(tc, id3_extended_header_beyond_tag_end_is_rejected);
  tcase_add_loop_test(tc, id3_lone_utf16_surrogate_becomes_replacement_character, 0, (int)(sizeof surrogate_cases / sizeof surrogate_cases[0]));
  tcase_add_test(tc, id3_overlong_text_is_truncated_to_the_stack_buffers);
  tcase_add_test(tc, id3_overlong_multibyte_text_is_cut_on_a_character_boundary);
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
