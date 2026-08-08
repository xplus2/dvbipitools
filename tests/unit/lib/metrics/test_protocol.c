/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/metrics/protocol.h"

static void make_hdr(metrics_hdr_t *hdr, const char *id) {
  memset(hdr, 0, sizeof *hdr);
  hdr->proto_version = METRICS_PROTO_VERSION;
  hdr->component = METRICS_COMPONENT_TVHEAD;
  snprintf(hdr->metrics_id, sizeof hdr->metrics_id, "%s", id);
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
  for (i = 0; i < 1000; i++) {
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
  /* chop off the last 3 bytes of the one entry - now a truncated value field */
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
