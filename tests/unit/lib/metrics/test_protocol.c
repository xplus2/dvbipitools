/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/ioutil.h"
#include "lib/metrics/protocol.h"

static void make_hdr(metrics_hdr_t *hdr, const char *id) {
  memset(hdr, 0, sizeof *hdr);
  hdr->proto_version = METRICS_PROTO_VERSION;
  hdr->component = METRICS_COMPONENT_TVHEAD;
  bufcpy(hdr->metrics_id, sizeof hdr->metrics_id, id);
  hdr->process_start_time = 1000;
  hdr->sequence = 7;
  hdr->snapshot_time = 2000;
}

START_TEST(roundtrip_basic) {
  metrics_writer_t w;
  metrics_reader_t r;
  metrics_hdr_t hdr, out_hdr;
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  uint64_t value;

  make_hdr(&hdr, "instance1");
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  ck_assert_int_eq(metrics_writer_put(&w, METRICS_ID_OUTPUT_PACKETS_TOTAL, NULL, 42), 0);
  ck_assert_int_eq(metrics_writer_put(&w, METRICS_ID_INPUT_UP, "i0", 1), 0);
  ck_assert_uint_gt(metrics_writer_finish(&w), 0u);

  ck_assert_int_eq(metrics_reader_init(&r, w.buf, w.len, &out_hdr), 0);
  ck_assert_int_eq(out_hdr.component, METRICS_COMPONENT_TVHEAD);
  ck_assert_str_eq(out_hdr.metrics_id, "instance1");
  ck_assert_uint_eq(out_hdr.process_start_time, 1000u);
  ck_assert_uint_eq(out_hdr.sequence, 7u);
  ck_assert_uint_eq(out_hdr.snapshot_time, 2000u);
  ck_assert_uint_eq(out_hdr.part, 0u);
  ck_assert_uint_eq(out_hdr.flags, (unsigned)METRICS_FLAG_LAST);

  ck_assert_int_eq(metrics_reader_next(&r, &id, label, sizeof label, &value), 1);
  ck_assert_int_eq(id, METRICS_ID_OUTPUT_PACKETS_TOTAL);
  ck_assert_str_eq(label, "");
  ck_assert_uint_eq(value, 42u);

  ck_assert_int_eq(metrics_reader_next(&r, &id, label, sizeof label, &value), 1);
  ck_assert_int_eq(id, METRICS_ID_INPUT_UP);
  ck_assert_str_eq(label, "i0");
  ck_assert_uint_eq(value, 1u);

  ck_assert_int_eq(metrics_reader_next(&r, &id, label, sizeof label, &value), 0);
}
END_TEST

START_TEST(begin_rejects_bad_version) {
  metrics_writer_t w;
  metrics_hdr_t hdr;
  make_hdr(&hdr, "x");
  hdr.proto_version = METRICS_PROTO_VERSION + 1;
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), -1);
  ck_assert_uint_eq(metrics_writer_finish(&w), 0u);
}
END_TEST

START_TEST(begin_rejects_empty_metrics_id) {
  metrics_writer_t w;
  metrics_hdr_t hdr;
  make_hdr(&hdr, "");
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), -1);
}
END_TEST

START_TEST(put_overflow_makes_writer_unusable) {
  metrics_writer_t w;
  metrics_hdr_t hdr;
  int i, saw_overflow = 0;
  make_hdr(&hdr, "x");
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  for (i = 0; i < 3000; i++) {
    if (metrics_writer_put(&w, METRICS_ID_OUTPUT_PACKETS_TOTAL, "some-label", (uint64_t)i)) {
      saw_overflow = 1;
      break;
    }
  }
  ck_assert_int_eq(saw_overflow, 1);
  ck_assert_uint_eq(metrics_writer_finish(&w), 0u);
  /* stays unusable, doesn't resurrect on a further put */
  ck_assert_int_eq(metrics_writer_put(&w, METRICS_ID_OUTPUT_PACKETS_TOTAL, NULL, 1), -1);
}
END_TEST

START_TEST(put_truncates_oversized_label) {
  metrics_writer_t w;
  metrics_reader_t r;
  metrics_hdr_t hdr, out_hdr;
  metrics_id_t id;
  char oversized[METRICS_LABEL_MAX + 50];
  char label[METRICS_LABEL_MAX + 1];
  uint64_t value;

  memset(oversized, 'a', sizeof oversized - 1);
  oversized[sizeof oversized - 1] = '\0';

  make_hdr(&hdr, "x");
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  ck_assert_int_eq(metrics_writer_put(&w, METRICS_ID_ERRORS_TOTAL, oversized, 1), 0);
  ck_assert_int_eq(metrics_reader_init(&r, w.buf, w.len, &out_hdr), 0);
  ck_assert_int_eq(metrics_reader_next(&r, &id, label, sizeof label, &value), 1);
  ck_assert_uint_eq(strlen(label), (unsigned)METRICS_LABEL_MAX);
}
END_TEST

START_TEST(reader_rejects_short_buffer) {
  metrics_reader_t r;
  metrics_hdr_t hdr;
  unsigned char buf[METRICS_HDR_LEN - 1];
  memset(buf, 0, sizeof buf);
  ck_assert_int_eq(metrics_reader_init(&r, buf, sizeof buf, &hdr), -1);
}
END_TEST

START_TEST(reader_rejects_bad_version) {
  metrics_writer_t w;
  metrics_reader_t r;
  metrics_hdr_t hdr, out_hdr;
  make_hdr(&hdr, "x");
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  w.buf[0] = METRICS_PROTO_VERSION + 1;
  ck_assert_int_eq(metrics_reader_init(&r, w.buf, w.len, &out_hdr), -1);
}
END_TEST

START_TEST(reader_rejects_unknown_component) {
  metrics_writer_t w;
  metrics_reader_t r;
  metrics_hdr_t hdr, out_hdr;
  make_hdr(&hdr, "x");
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  w.buf[1] = 99;
  ck_assert_int_eq(metrics_reader_init(&r, w.buf, w.len, &out_hdr), -1);
}
END_TEST

START_TEST(reader_rejects_truncated_entry) {
  metrics_writer_t w;
  metrics_reader_t r;
  metrics_hdr_t hdr, out_hdr;
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  uint64_t value;

  make_hdr(&hdr, "x");
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  ck_assert_int_eq(metrics_writer_put(&w, METRICS_ID_OUTPUT_PACKETS_TOTAL, "lbl", 5), 0);
  ck_assert_int_eq(metrics_reader_init(&r, w.buf, w.len - 3, &out_hdr), 0);
  ck_assert_int_eq(metrics_reader_next(&r, &id, label, sizeof label, &value), -1);
}
END_TEST

START_TEST(reader_skips_unknown_ids_by_length) {
  /* a decoder that doesn't recognize a given id still advances correctly,
     since the entry length is self-describing (append-only wire compat) */
  metrics_writer_t w;
  metrics_reader_t r;
  metrics_hdr_t hdr, out_hdr;
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  uint64_t value;

  make_hdr(&hdr, "x");
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  ck_assert_int_eq(metrics_writer_put(&w, (metrics_id_t)9999, "future", 123), 0);
  ck_assert_int_eq(metrics_writer_put(&w, METRICS_ID_OUTPUT_PACKETS_TOTAL, NULL, 55), 0);

  ck_assert_int_eq(metrics_reader_init(&r, w.buf, w.len, &out_hdr), 0);
  ck_assert_int_eq(metrics_reader_next(&r, &id, label, sizeof label, &value), 1);
  ck_assert_int_eq((int)id, 9999);
  ck_assert_int_eq(metrics_reader_next(&r, &id, label, sizeof label, &value), 1);
  ck_assert_int_eq(id, METRICS_ID_OUTPUT_PACKETS_TOTAL);
  ck_assert_uint_eq(value, 55u);
}
END_TEST

START_TEST(consecutive_same_label_shares_group) {
  metrics_writer_t w;
  metrics_hdr_t hdr;
  make_hdr(&hdr, "x");
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  ck_assert_int_eq(metrics_writer_put(&w, METRICS_ID_INPUT_UP, "i0", 1), 0);
  ck_assert_int_eq(metrics_writer_put(&w, METRICS_ID_INPUT_BYTES_TOTAL, "i0", 2), 0);
  ck_assert_int_eq(metrics_writer_put(&w, METRICS_ID_INPUT_UP, "i1", 1), 0);
  ck_assert_uint_eq(w.buf[METRICS_HDR_LEN], 2u);
  ck_assert_uint_eq(w.buf[METRICS_HDR_LEN + 1 + 2], 2u);
  ck_assert_uint_eq(w.len, METRICS_HDR_LEN + (2 + 2 + 2 * 3) + (2 + 2 + 3));
}
END_TEST

START_TEST(varint_values_roundtrip) {
  static const uint64_t vals[] = {0, 1, 127, 128, 16383, 16384, 0xFFFFFFFFULL, 1ULL << 63, UINT64_MAX};
  metrics_writer_t w;
  metrics_reader_t r;
  metrics_hdr_t hdr;
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  uint64_t value;

  make_hdr(&hdr, "x");
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  for (size_t i = 0; i < sizeof vals / sizeof *vals; i++)
    ck_assert_int_eq(metrics_writer_put(&w, METRICS_ID_OUTPUT_PACKETS_TOTAL, NULL, vals[i]), 0);
  ck_assert_int_eq(metrics_reader_init(&r, w.buf, w.len, &hdr), 0);
  for (size_t i = 0; i < sizeof vals / sizeof *vals; i++) {
    ck_assert_int_eq(metrics_reader_next(&r, &id, label, sizeof label, &value), 1);
    ck_assert_uint_eq(value, vals[i]);
  }
  ck_assert_int_eq(metrics_reader_next(&r, &id, label, sizeof label, &value), 0);
}
END_TEST

typedef struct {
  unsigned char parts[METRICS_MAX_PARTS][METRICS_MAX_SNAPSHOT_BYTES];
  size_t lens[METRICS_MAX_PARTS];
  unsigned n;
  unsigned fail_at;
} capture_t;

static capture_t g_cap;

static int capture_part(void *ctx, const unsigned char *buf, size_t len) {
  capture_t *c = ctx;
  if (c->n == c->fail_at)
    return -1;
  memcpy(c->parts[c->n], buf, len);
  c->lens[c->n++] = len;
  return 0;
}

static void put_unique(metrics_writer_t *w, unsigned n, int *rc) {
  for (unsigned i = 0; i < n; i++) {
    char label[METRICS_LABEL_MAX + 1];
    char num[16];
    size_t nl = uint_to_str(num, i);
    memset(label, 'a', METRICS_LABEL_MAX);
    label[METRICS_LABEL_MAX] = '\0';
    memcpy(label, num, nl);
    *rc = metrics_writer_put(w, METRICS_ID_INPUT_BYTES_TOTAL, label, i);
    if (*rc)
      return;
  }
}

START_TEST(overflow_streams_parts_with_flush) {
  metrics_writer_t w;
  metrics_hdr_t hdr;
  metrics_reader_t r;
  metrics_hdr_t out;
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  uint64_t value;
  unsigned total = 0;
  int rc = 0;
  size_t len;

  make_hdr(&hdr, "x");
  memset(&g_cap, 0, sizeof g_cap);
  g_cap.fail_at = (unsigned)-1;
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  w.flush = capture_part;
  w.flush_ctx = &g_cap;
  put_unique(&w, 300, &rc);
  ck_assert_int_eq(rc, 0);
  len = metrics_writer_finish(&w);
  ck_assert_uint_gt(len, 0u);
  capture_part(&g_cap, w.buf, len);
  ck_assert_uint_gt(g_cap.n, 1u);

  for (unsigned p = 0; p < g_cap.n; p++) {
    ck_assert_int_eq(metrics_reader_init(&r, g_cap.parts[p], g_cap.lens[p], &out), 0);
    ck_assert_uint_eq(out.part, p);
    ck_assert_uint_eq(out.sequence, 7u);
    ck_assert_uint_eq(out.flags & METRICS_FLAG_LAST, p + 1 == g_cap.n ? (unsigned)METRICS_FLAG_LAST : 0u);
    while (metrics_reader_next(&r, &id, label, sizeof label, &value) == 1) {
      ck_assert_uint_eq(value, total);
      total++;
    }
  }
  ck_assert_uint_eq(total, 300u);
}
END_TEST

START_TEST(flush_failure_makes_writer_unusable) {
  metrics_writer_t w;
  metrics_hdr_t hdr;
  int rc = 0;

  make_hdr(&hdr, "x");
  memset(&g_cap, 0, sizeof g_cap);
  g_cap.fail_at = 0;
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  w.flush = capture_part;
  w.flush_ctx = &g_cap;
  put_unique(&w, 300, &rc);
  ck_assert_int_eq(rc, -1);
  ck_assert_uint_eq(metrics_writer_finish(&w), 0u);
}
END_TEST

START_TEST(parts_are_capped) {
  metrics_writer_t w;
  metrics_hdr_t hdr;
  int rc = 0;

  make_hdr(&hdr, "x");
  memset(&g_cap, 0, sizeof g_cap);
  g_cap.fail_at = (unsigned)-1;
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  w.flush = capture_part;
  w.flush_ctx = &g_cap;
  put_unique(&w, 40000, &rc);
  ck_assert_int_eq(rc, -1);
  ck_assert_uint_le(g_cap.n, (unsigned)METRICS_MAX_PARTS);
  ck_assert_uint_eq(metrics_writer_finish(&w), 0u);
}
END_TEST

START_TEST(reader_accepts_v1_entries) {
  unsigned char buf[METRICS_HDR_LEN + 11 + 3 + 3 + 8];
  metrics_writer_t w;
  metrics_reader_t r;
  metrics_hdr_t hdr, out;
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  uint64_t value;
  size_t n = METRICS_HDR_LEN;

  make_hdr(&hdr, "x");
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  memcpy(buf, w.buf, METRICS_HDR_LEN);
  buf[0] = METRICS_PROTO_V1;
  buf[n++] = 0;
  buf[n++] = (unsigned char)METRICS_ID_OUTPUT_PACKETS_TOTAL;
  buf[n++] = 3;
  memcpy(buf + n, "lbl", 3);
  n += 3;
  for (int i = 0; i < 7; i++)
    buf[n++] = 0;
  buf[n++] = 200;

  ck_assert_int_eq(metrics_reader_init(&r, buf, n, &out), 0);
  ck_assert_uint_eq(out.part, 0u);
  ck_assert_uint_eq(out.flags, (unsigned)METRICS_FLAG_LAST);
  ck_assert_int_eq(metrics_reader_next(&r, &id, label, sizeof label, &value), 1);
  ck_assert_str_eq(label, "lbl");
  ck_assert_uint_eq(value, 200u);
  ck_assert_int_eq(metrics_reader_next(&r, &id, label, sizeof label, &value), 0);
}
END_TEST

START_TEST(reader_rejects_malformed_v2_groups) {
  metrics_writer_t w;
  metrics_reader_t r;
  metrics_hdr_t hdr, out;
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  uint64_t value;
  unsigned char body[16];

  make_hdr(&hdr, "x");
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  ck_assert_int_eq(metrics_writer_put(&w, METRICS_ID_OUTPUT_PACKETS_TOTAL, NULL, 1), 0);
  w.buf[METRICS_HDR_LEN + 1] = 0;
  ck_assert_int_eq(metrics_reader_init(&r, w.buf, w.len, &out), 0);
  ck_assert_int_eq(metrics_reader_next(&r, &id, label, sizeof label, &value), -1);

  w.buf[METRICS_HDR_LEN + 1] = 2;
  ck_assert_int_eq(metrics_reader_init(&r, w.buf, w.len, &out), 0);
  ck_assert_int_eq(metrics_reader_next(&r, &id, label, sizeof label, &value), 1);
  ck_assert_int_eq(metrics_reader_next(&r, &id, label, sizeof label, &value), -1);

  memset(body, 0, sizeof body);
  body[1] = 1;
  body[4] = 0x80;
  for (int i = 5; i < 15; i++)
    body[i] = 0x80;
  body[15] = 0x01;
  metrics_reader_init_body(&r, METRICS_PROTO_VERSION, body, sizeof body);
  ck_assert_int_eq(metrics_reader_next(&r, &id, label, sizeof label, &value), -1);
}
END_TEST

static Suite *protocol_suite(void) {
  Suite *s = suite_create("metrics_protocol");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, roundtrip_basic);
  tcase_add_test(tc, begin_rejects_bad_version);
  tcase_add_test(tc, begin_rejects_empty_metrics_id);
  tcase_add_test(tc, put_overflow_makes_writer_unusable);
  tcase_add_test(tc, put_truncates_oversized_label);
  tcase_add_test(tc, reader_rejects_short_buffer);
  tcase_add_test(tc, reader_rejects_bad_version);
  tcase_add_test(tc, reader_rejects_unknown_component);
  tcase_add_test(tc, reader_rejects_truncated_entry);
  tcase_add_test(tc, reader_skips_unknown_ids_by_length);
  tcase_add_test(tc, consecutive_same_label_shares_group);
  tcase_add_test(tc, varint_values_roundtrip);
  tcase_add_test(tc, overflow_streams_parts_with_flush);
  tcase_add_test(tc, flush_failure_makes_writer_unusable);
  tcase_add_test(tc, parts_are_capped);
  tcase_add_test(tc, reader_accepts_v1_entries);
  tcase_add_test(tc, reader_rejects_malformed_v2_groups);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(protocol_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
