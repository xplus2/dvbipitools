/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/metrics/export.h"

START_TEST(disabled_without_metrics_id) {
  metrics_exporter_t mx;
  metrics_exporter_init(&mx, METRICS_COMPONENT_TVHEAD, NULL, NULL, 0);
  ck_assert_int_eq(metrics_exporter_enabled(&mx), 0);
  ck_assert_int_eq(metrics_exporter_due(&mx, 100.0), 0);
  {
    metrics_writer_t w;
    ck_assert_int_eq(metrics_exporter_begin(&mx, &w, "1.0"), -1);
  }
  /* note_error must be safe (no-op) even while disabled */
  metrics_exporter_note_error(&mx, NET_ERR_DNS);
  metrics_exporter_close(&mx);
}
END_TEST

START_TEST(disabled_on_empty_metrics_id) {
  metrics_exporter_t mx;
  metrics_exporter_init(&mx, METRICS_COMPONENT_TVHEAD, "", "/tmp/whatever.sock", 5.0);
  ck_assert_int_eq(metrics_exporter_enabled(&mx), 0);
}
END_TEST

START_TEST(default_socket_path_used_when_null) {
  metrics_exporter_t mx;
  metrics_exporter_init(&mx, METRICS_COMPONENT_SDS, "inst1", NULL, 5.0);
  ck_assert_int_eq(metrics_exporter_enabled(&mx), 1);
  ck_assert_str_eq(mx.dest.sun_path, METRICS_DEFAULT_SOCK_PATH);
  metrics_exporter_close(&mx);
}
END_TEST

START_TEST(overridden_socket_path_used) {
  metrics_exporter_t mx;
  metrics_exporter_init(&mx, METRICS_COMPONENT_SDS, "inst1", "/tmp/custom-metrics.sock", 5.0);
  ck_assert_int_eq(metrics_exporter_enabled(&mx), 1);
  ck_assert_str_eq(mx.dest.sun_path, "/tmp/custom-metrics.sock");
  metrics_exporter_close(&mx);
}
END_TEST

START_TEST(due_gates_on_interval) {
  metrics_exporter_t mx;
  metrics_exporter_init(&mx, METRICS_COMPONENT_SDS, "inst1", "/tmp/custom-metrics.sock", 5.0);
  ck_assert_int_eq(metrics_exporter_due(&mx, 100.0), 1);
  ck_assert_int_eq(metrics_exporter_due(&mx, 101.0), 0);
  ck_assert_int_eq(metrics_exporter_due(&mx, 104.9), 0);
  ck_assert_int_eq(metrics_exporter_due(&mx, 105.0), 1);
  metrics_exporter_close(&mx);
}
END_TEST

START_TEST(send_to_missing_collector_is_nonblocking_and_counts_dropped) {
  metrics_exporter_t mx;
  metrics_writer_t w;
  metrics_exporter_init(&mx, METRICS_COMPONENT_SDS, "inst1", "/tmp/dvbipitools-test-no-such-collector.sock", 5.0);
  ck_assert_int_eq(metrics_exporter_enabled(&mx), 1);
  ck_assert_int_eq(metrics_exporter_begin(&mx, &w, "1.0"), 0);
  metrics_exporter_send(&mx, &w);
  ck_assert_uint_eq(mx.snapshots_dropped, 1u);
  metrics_exporter_close(&mx);
}
END_TEST

START_TEST(sequence_increases_monotonically) {
  metrics_exporter_t mx;
  metrics_writer_t w;
  metrics_reader_t r;
  metrics_hdr_t hdr;
  int i;

  metrics_exporter_init(&mx, METRICS_COMPONENT_SDS, "inst1", "/tmp/dvbipitools-test-no-such-collector.sock", 5.0);
  for (i = 1; i <= 3; i++) {
    ck_assert_int_eq(metrics_exporter_begin(&mx, &w, "1.0"), 0);
    ck_assert_int_eq(metrics_reader_init(&r, w.buf, w.len, &hdr), 0);
    ck_assert_uint_eq(hdr.sequence, (uint64_t)i);
  }
  metrics_exporter_close(&mx);
}
END_TEST

START_TEST(errors_total_reflects_note_error_by_reason) {
  metrics_exporter_t mx;
  metrics_writer_t w;
  metrics_reader_t r;
  metrics_hdr_t hdr;
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  uint64_t value;
  int seen_dns = 0, seen_timeout = 0;

  metrics_exporter_init(&mx, METRICS_COMPONENT_SDS, "inst1", "/tmp/dvbipitools-test-no-such-collector.sock", 5.0);
  metrics_exporter_note_error(&mx, NET_ERR_DNS);
  metrics_exporter_note_error(&mx, NET_ERR_DNS);
  metrics_exporter_note_error(&mx, NET_ERR_TIMEOUT);

  ck_assert_int_eq(metrics_exporter_begin(&mx, &w, "1.0"), 0);
  ck_assert_int_eq(metrics_reader_init(&r, w.buf, w.len, &hdr), 0);
  while (metrics_reader_next(&r, &id, label, sizeof label, &value) == 1) {
    if (id != METRICS_ID_ERRORS_TOTAL)
      continue;
    if (!strcmp(label, "dns")) {
      ck_assert_uint_eq(value, 2u);
      seen_dns = 1;
    } else if (!strcmp(label, "timeout")) {
      ck_assert_uint_eq(value, 1u);
      seen_timeout = 1;
    }
  }
  ck_assert_int_eq(seen_dns, 1);
  ck_assert_int_eq(seen_timeout, 1);
  metrics_exporter_close(&mx);
}
END_TEST

START_TEST(input_metrics_note_read_accumulates) {
  input_metrics_t im;
  memset(&im, 0, sizeof im);

  input_metrics_note_read(&im, 100, NET_ERR_OTHER);
  ck_assert_uint_eq(im.bytes_total, 100u);
  ck_assert(im.last_data_time > 0.0);

  input_metrics_note_read(&im, -1, NET_ERR_TIMEOUT);
  ck_assert_uint_eq(im.errors_total[NET_ERR_TIMEOUT], 1u);
  ck_assert_uint_eq(im.bytes_total, 100u); /* unchanged on error */

  input_metrics_note_read(&im, 0, NET_ERR_OTHER);
  ck_assert_uint_eq(im.bytes_total, 100u); /* n==0 no-op */

  /* NULL im must be a safe no-op (disabled-exporter fast path) */
  input_metrics_note_read(NULL, 50, NET_ERR_OTHER);
}
END_TEST

START_TEST(put_inputs_labels_and_skips_zero_error_reasons) {
  input_metrics_t inputs[2];
  metrics_writer_t w;
  metrics_hdr_t hdr;
  metrics_reader_t r;
  metrics_id_t id;
  char label[METRICS_LABEL_MAX + 1];
  uint64_t value;
  int error_entries = 0;

  memset(inputs, 0, sizeof inputs);
  inputs[0].up = 1;
  inputs[0].bytes_total = 10;
  input_metrics_note_read(&inputs[1], -1, NET_ERR_CONNECT);

  memset(&hdr, 0, sizeof hdr);
  hdr.proto_version = METRICS_PROTO_VERSION;
  hdr.component = METRICS_COMPONENT_TVHEAD;
  snprintf(hdr.metrics_id, sizeof hdr.metrics_id, "inst1");
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  metrics_writer_put_inputs(&w, inputs, 2);

  ck_assert_int_eq(metrics_reader_init(&r, w.buf, w.len, &hdr), 0);
  while (metrics_reader_next(&r, &id, label, sizeof label, &value) == 1) {
    if (id == METRICS_ID_INPUT_UP && !strcmp(label, "i0"))
      ck_assert_uint_eq(value, 1u);
    if (id == METRICS_ID_INPUT_ERRORS_TOTAL) {
      error_entries++;
      ck_assert(strstr(label, "i1") == label);
      ck_assert(strchr(label, METRICS_LABEL_SEP) != NULL);
    }
  }
  /* only i1's one nonzero reason produced an entry, not all NET_ERR_COUNT * 2 */
  ck_assert_int_eq(error_entries, 1);
}
END_TEST

static Suite *export_suite(void) {
  Suite *s = suite_create("metrics_export");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, disabled_without_metrics_id);
  tcase_add_test(tc, disabled_on_empty_metrics_id);
  tcase_add_test(tc, default_socket_path_used_when_null);
  tcase_add_test(tc, overridden_socket_path_used);
  tcase_add_test(tc, due_gates_on_interval);
  tcase_add_test(tc, send_to_missing_collector_is_nonblocking_and_counts_dropped);
  tcase_add_test(tc, sequence_increases_monotonically);
  tcase_add_test(tc, errors_total_reflects_note_error_by_reason);
  tcase_add_test(tc, input_metrics_note_read_accumulates);
  tcase_add_test(tc, put_inputs_labels_and_skips_zero_error_reasons);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(export_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
