/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <string.h>

#include "lib/demux/crc32.h"

#include "dipitvhead/mux/scte35stamp.h"

#define MOD33 ((uint64_t)1 << 33)

typedef struct {
  unsigned char pk[40][188];
  int n;
} sink_t;

static void emit(void *ctx, unsigned char *pkt) {
  sink_t *s = ctx;
  memcpy(s->pk[s->n++], pkt, 188);
}

static unsigned build_section(unsigned char *s, uint64_t adj, int encrypted, unsigned algo, unsigned body_len) {
  unsigned total = 21 + body_len + 4;
  uint32_t crc;
  memset(s, 0x5A, total);
  s[0] = 0xFC;
  s[1] = (unsigned char)(0x30 | (((total - 3) >> 8) & 0x0F));
  s[2] = (unsigned char)(total - 3);
  s[3] = 0;
  s[4] = (unsigned char)(((encrypted ? 1 : 0) << 7) | ((algo & 0x3F) << 1) | ((adj >> 32) & 1));
  s[5] = (unsigned char)(adj >> 24);
  s[6] = (unsigned char)(adj >> 16);
  s[7] = (unsigned char)(adj >> 8);
  s[8] = (unsigned char)adj;
  s[9] = 0xFF;
  s[10] = 0xFF;
  s[11] = 0xF0;
  s[12] = 5;
  s[13] = 0x06;
  s[14] = 0xFE;
  s[15] = 0;
  s[16] = 0;
  s[17] = 0;
  s[18] = 0;
  s[19] = (unsigned char)(body_len >> 8);
  s[20] = (unsigned char)body_len;
  crc = crc32_mpeg(s, total - 4);
  s[total - 4] = (unsigned char)(crc >> 24);
  s[total - 3] = (unsigned char)(crc >> 16);
  s[total - 2] = (unsigned char)(crc >> 8);
  s[total - 1] = (unsigned char)crc;
  return total;
}

static int packetize(unsigned char pk[][188], const unsigned char *sec, unsigned total, unsigned af_len, unsigned char cc0) {
  unsigned pos = 0;
  int n = 0;
  while (pos < total || n == 0) {
    unsigned char *p = pk[n];
    unsigned st = 4;
    unsigned room;
    memset(p, 0xFF, 188);
    p[0] = 0x47;
    p[1] = (unsigned char)((n == 0 ? 0x40 : 0x00) | 0x01);
    p[2] = 0x20;
    p[3] = (unsigned char)(0x10 | ((cc0 + n) & 0x0F));
    if (n == 0 && af_len) {
      p[3] |= 0x20;
      p[4] = (unsigned char)(af_len - 1);
      p[5] = 0;
      st = 4 + af_len;
    }
    if (n == 0) p[st++] = 0;
    room = 188 - st;
    if (room > total - pos) room = total - pos;
    memcpy(p + st, sec + pos, room);
    pos += room;
    n++;
  }
  return n;
}

static unsigned collect(unsigned char *out, const sink_t *s, unsigned total) {
  unsigned pos = 0;
  for (int i = 0; i < s->n; i++) {
    unsigned st = 4;
    unsigned take;
    if ((s->pk[i][3] >> 4) & 2) st += 1 + s->pk[i][4];
    if (i == 0) st++;
    take = 188 - st;
    if (take > total - pos) take = total - pos;
    memcpy(out + pos, s->pk[i] + st, take);
    pos += take;
  }
  return pos;
}

static uint64_t adj_of(const unsigned char *s) {
  return ((uint64_t)(s[4] & 1) << 32) | ((uint64_t)s[5] << 24) | ((uint64_t)s[6] << 16) | ((uint64_t)s[7] << 8) | s[8];
}

static void feed_all(scte35stamp_t *st, sink_t *sink, unsigned char pk[][188], int n, int64_t delta) {
  for (int i = 0; i < n; i++) scte35stamp_feed(st, pk[i], delta, emit, sink);
}

START_TEST(single_packet_section_is_patched_with_a_valid_crc) {
  unsigned char sec[4096];
  unsigned char pk[30][188];
  unsigned char got[4096];
  scte35stamp_t st;
  sink_t sink = {.n = 0};
  unsigned total = build_section(sec, 1000, 0, 0, 20);
  int n = packetize(pk, sec, total, 0, 3);
  scte35stamp_init(&st);
  ck_assert_int_eq(n, 1);
  feed_all(&st, &sink, pk, n, 90000);
  ck_assert_int_eq(sink.n, 1);
  ck_assert_uint_eq(collect(got, &sink, total), total);
  ck_assert_uint_eq(adj_of(got), 91000u);
  ck_assert_uint_eq(crc32_mpeg(got, total), 0u);
  ck_assert_uint_eq((unsigned)st.patched, 1u);
}
END_TEST

START_TEST(multi_packet_section_is_held_until_complete_then_emitted_in_order) {
  unsigned char sec[4096];
  unsigned char pk[30][188];
  unsigned char got[4096];
  scte35stamp_t st;
  sink_t sink = {.n = 0};
  unsigned total = build_section(sec, 500, 0, 0, 400);
  int n = packetize(pk, sec, total, 0, 0);
  ck_assert_int_eq(n, 3);
  scte35stamp_init(&st);
  scte35stamp_feed(&st, pk[0], 7, emit, &sink);
  scte35stamp_feed(&st, pk[1], 7, emit, &sink);
  ck_assert_int_eq(sink.n, 0);
  scte35stamp_feed(&st, pk[2], 7, emit, &sink);
  ck_assert_int_eq(sink.n, 3);
  for (int i = 0; i < 3; i++) ck_assert_uint_eq(sink.pk[i][3] & 0x0F, (unsigned)i);
  ck_assert_uint_eq(collect(got, &sink, total), total);
  ck_assert_uint_eq(adj_of(got), 507u);
  ck_assert_uint_eq(crc32_mpeg(got, total), 0u);
  ck_assert_mem_eq(got + 9, sec + 9, total - 9 - 4);
}
END_TEST

START_TEST(adjustment_wraps_at_33_bits_in_both_directions) {
  unsigned char sec[4096];
  unsigned char pk[30][188];
  unsigned char got[4096];
  scte35stamp_t st;
  sink_t sink;
  unsigned total = build_section(sec, MOD33 - 10, 0, 0, 20);
  int n = packetize(pk, sec, total, 0, 0);
  scte35stamp_init(&st);
  sink.n = 0;
  feed_all(&st, &sink, pk, n, 25);
  collect(got, &sink, total);
  ck_assert_uint_eq(adj_of(got), 15u);
  ck_assert_uint_eq(crc32_mpeg(got, total), 0u);
  total = build_section(sec, 5, 0, 0, 20);
  n = packetize(pk, sec, total, 0, 0);
  sink.n = 0;
  feed_all(&st, &sink, pk, n, -20);
  collect(got, &sink, total);
  ck_assert_uint_eq(adj_of(got), MOD33 - 15);
  ck_assert_uint_eq(crc32_mpeg(got, total), 0u);
}
END_TEST

START_TEST(encryption_bits_are_preserved_and_the_crc_stays_valid) {
  unsigned char sec[4096];
  unsigned char pk[30][188];
  unsigned char got[4096];
  scte35stamp_t st;
  sink_t sink = {.n = 0};
  unsigned total = build_section(sec, 77, 1, 0x2A, 30);
  int n = packetize(pk, sec, total, 0, 0);
  scte35stamp_init(&st);
  feed_all(&st, &sink, pk, n, 1000);
  collect(got, &sink, total);
  ck_assert_uint_eq(got[4] & 0xFE, sec[4] & 0xFE);
  ck_assert_uint_eq(adj_of(got), 1077u);
  ck_assert_uint_eq(crc32_mpeg(got, total), 0u);
}
END_TEST

START_TEST(zero_delta_reproduces_the_original_section) {
  unsigned char sec[4096];
  unsigned char pk[30][188];
  unsigned char orig[30][188];
  scte35stamp_t st;
  sink_t sink = {.n = 0};
  unsigned total = build_section(sec, 4242, 0, 0, 300);
  int n = packetize(pk, sec, total, 0, 2);
  memcpy(orig, pk, sizeof orig);
  scte35stamp_init(&st);
  feed_all(&st, &sink, pk, n, 0);
  ck_assert_int_eq(sink.n, n);
  for (int i = 0; i < n; i++) ck_assert_mem_eq(sink.pk[i], orig[i], 188);
}
END_TEST

START_TEST(first_packet_adaptation_field_is_handled) {
  unsigned char sec[4096];
  unsigned char pk[30][188];
  unsigned char got[4096];
  scte35stamp_t st;
  sink_t sink = {.n = 0};
  unsigned total = build_section(sec, 10, 0, 0, 200);
  int n = packetize(pk, sec, total, 12, 0);
  scte35stamp_init(&st);
  feed_all(&st, &sink, pk, n, 90);
  ck_assert_int_eq(sink.n, n);
  collect(got, &sink, total);
  ck_assert_uint_eq(adj_of(got), 100u);
  ck_assert_uint_eq(crc32_mpeg(got, total), 0u);
}
END_TEST

START_TEST(stuffing_after_the_section_is_untouched) {
  unsigned char sec[4096];
  unsigned char pk[30][188];
  scte35stamp_t st;
  sink_t sink = {.n = 0};
  unsigned total = build_section(sec, 10, 0, 0, 20);
  int n = packetize(pk, sec, total, 0, 0);
  scte35stamp_init(&st);
  feed_all(&st, &sink, pk, n, 5);
  for (unsigned i = 5 + total; i < 188; i++) ck_assert_uint_eq(sink.pk[0][i], 0xFFu);
}
END_TEST

START_TEST(other_tables_and_odd_packets_pass_through_untouched) {
  unsigned char sec[4096];
  unsigned char pk[30][188];
  unsigned char orig[188];
  scte35stamp_t st;
  sink_t sink;
  unsigned total = build_section(sec, 10, 0, 0, 20);
  scte35stamp_init(&st);

  sec[0] = 0x74;
  packetize(pk, sec, total, 0, 0);
  memcpy(orig, pk[0], 188);
  sink.n = 0;
  scte35stamp_feed(&st, pk[0], 5, emit, &sink);
  ck_assert_int_eq(sink.n, 1);
  ck_assert_mem_eq(sink.pk[0], orig, 188);

  total = build_section(sec, 10, 0, 0, 20);
  packetize(pk, sec, total, 0, 0);
  pk[0][4] = 3;
  memcpy(orig, pk[0], 188);
  sink.n = 0;
  scte35stamp_feed(&st, pk[0], 5, emit, &sink);
  ck_assert_int_eq(sink.n, 1);
  ck_assert_mem_eq(sink.pk[0], orig, 188);

  packetize(pk, sec, total, 0, 0);
  pk[0][1] &= 0xBF;
  memcpy(orig, pk[0], 188);
  sink.n = 0;
  scte35stamp_feed(&st, pk[0], 5, emit, &sink);
  ck_assert_int_eq(sink.n, 1);
  ck_assert_mem_eq(sink.pk[0], orig, 188);

  packetize(pk, sec, total, 0, 0);
  pk[0][3] |= 0x80;
  memcpy(orig, pk[0], 188);
  sink.n = 0;
  scte35stamp_feed(&st, pk[0], 5, emit, &sink);
  ck_assert_int_eq(sink.n, 1);
  ck_assert_mem_eq(sink.pk[0], orig, 188);

  packetize(pk, sec, total, 0, 0);
  pk[0][3] = 0x20;
  memcpy(orig, pk[0], 188);
  sink.n = 0;
  scte35stamp_feed(&st, pk[0], 5, emit, &sink);
  ck_assert_int_eq(sink.n, 1);
  ck_assert_mem_eq(sink.pk[0], orig, 188);
  ck_assert_uint_eq((unsigned)st.patched, 0u);
}
END_TEST

START_TEST(oversized_section_length_passes_through) {
  unsigned char sec[4096];
  unsigned char pk[30][188];
  unsigned char orig[188];
  scte35stamp_t st;
  sink_t sink = {.n = 0};
  unsigned total = build_section(sec, 10, 0, 0, 20);
  scte35stamp_init(&st);
  packetize(pk, sec, total, 0, 0);
  pk[0][5] = 0x3F;
  pk[0][6] = 0xFF;
  memcpy(orig, pk[0], 188);
  scte35stamp_feed(&st, pk[0], 5, emit, &sink);
  ck_assert_int_eq(sink.n, 1);
  ck_assert_mem_eq(sink.pk[0], orig, 188);
}
END_TEST

START_TEST(a_new_section_start_flushes_an_incomplete_one_unpatched) {
  unsigned char sec1[4096];
  unsigned char sec2[4096];
  unsigned char pk1[30][188];
  unsigned char pk2[30][188];
  unsigned char orig1[188];
  unsigned char got[4096];
  scte35stamp_t st;
  sink_t sink = {.n = 0};
  unsigned t1 = build_section(sec1, 100, 0, 0, 400);
  unsigned t2 = build_section(sec2, 200, 0, 0, 20);
  packetize(pk1, sec1, t1, 0, 0);
  packetize(pk2, sec2, t2, 0, 5);
  memcpy(orig1, pk1[0], 188);
  scte35stamp_init(&st);
  scte35stamp_feed(&st, pk1[0], 9, emit, &sink);
  scte35stamp_feed(&st, pk2[0], 9, emit, &sink);
  ck_assert_int_eq(sink.n, 2);
  ck_assert_mem_eq(sink.pk[0], orig1, 188);
  memmove(sink.pk[0], sink.pk[1], 188);
  sink.n = 1;
  collect(got, &sink, t2);
  ck_assert_uint_eq(adj_of(got), 209u);
  ck_assert_uint_eq(crc32_mpeg(got, t2), 0u);
}
END_TEST

START_TEST(continuation_without_a_started_section_passes_through) {
  unsigned char sec[4096];
  unsigned char pk[30][188];
  unsigned char orig[188];
  scte35stamp_t st;
  sink_t sink = {.n = 0};
  unsigned total = build_section(sec, 10, 0, 0, 400);
  packetize(pk, sec, total, 0, 0);
  memcpy(orig, pk[1], 188);
  scte35stamp_init(&st);
  scte35stamp_feed(&st, pk[1], 5, emit, &sink);
  ck_assert_int_eq(sink.n, 1);
  ck_assert_mem_eq(sink.pk[0], orig, 188);
}
END_TEST

START_TEST(flush_emits_a_partial_section_unpatched) {
  unsigned char sec[4096];
  unsigned char pk[30][188];
  unsigned char orig[188];
  scte35stamp_t st;
  sink_t sink = {.n = 0};
  unsigned total = build_section(sec, 10, 0, 0, 400);
  packetize(pk, sec, total, 0, 0);
  memcpy(orig, pk[0], 188);
  scte35stamp_init(&st);
  scte35stamp_feed(&st, pk[0], 5, emit, &sink);
  ck_assert_int_eq(sink.n, 0);
  scte35stamp_flush(&st, emit, &sink);
  ck_assert_int_eq(sink.n, 1);
  ck_assert_mem_eq(sink.pk[0], orig, 188);
  scte35stamp_flush(&st, emit, &sink);
  ck_assert_int_eq(sink.n, 1);
}
END_TEST

START_TEST(consecutive_sections_are_each_patched) {
  unsigned char sec[4096];
  unsigned char pk[30][188];
  unsigned char got[4096];
  scte35stamp_t st;
  sink_t sink = {.n = 0};
  unsigned total;
  int n;
  scte35stamp_init(&st);
  for (int k = 0; k < 3; k++) {
    total = build_section(sec, 100u * (unsigned)(k + 1), 0, 0, 20 + 100u * (unsigned)k);
    n = packetize(pk, sec, total, 0, (unsigned char)k);
    sink.n = 0;
    feed_all(&st, &sink, pk, n, 50);
    ck_assert_int_eq(sink.n, n);
    collect(got, &sink, total);
    ck_assert_uint_eq(adj_of(got), 100u * (unsigned)(k + 1) + 50u);
    ck_assert_uint_eq(crc32_mpeg(got, total), 0u);
  }
  ck_assert_uint_eq((unsigned)st.patched, 3u);
}
END_TEST

static Suite *scte35stamp_suite(void) {
  Suite *s = suite_create("scte35stamp");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, single_packet_section_is_patched_with_a_valid_crc);
  tcase_add_test(tc, multi_packet_section_is_held_until_complete_then_emitted_in_order);
  tcase_add_test(tc, adjustment_wraps_at_33_bits_in_both_directions);
  tcase_add_test(tc, encryption_bits_are_preserved_and_the_crc_stays_valid);
  tcase_add_test(tc, zero_delta_reproduces_the_original_section);
  tcase_add_test(tc, first_packet_adaptation_field_is_handled);
  tcase_add_test(tc, stuffing_after_the_section_is_untouched);
  tcase_add_test(tc, other_tables_and_odd_packets_pass_through_untouched);
  tcase_add_test(tc, oversized_section_length_passes_through);
  tcase_add_test(tc, a_new_section_start_flushes_an_incomplete_one_unpatched);
  tcase_add_test(tc, continuation_without_a_started_section_passes_through);
  tcase_add_test(tc, flush_emits_a_partial_section_unpatched);
  tcase_add_test(tc, consecutive_sections_are_each_patched);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(scte35stamp_suite());
  int failed;
  srunner_run_all(sr, CK_NORMAL);
  failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? 0 : 1;
}
