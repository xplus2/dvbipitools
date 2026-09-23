/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/ioutil.h"
#include "dipimetrics/render.h"
#include "dipimetrics/store.h"

static store_slot_t *add_slot(store_t *st, metrics_component_t component, const char *id, double received_mono) {
  store_slot_t *s = &st->slots[0];
  memset(s, 0, sizeof *s);
  s->used = 1;
  s->valid = 1;
  s->version = METRICS_PROTO_VERSION;
  s->component = component;
  bufcpy(s->metrics_id, sizeof s->metrics_id, id);
  s->received_mono = received_mono;
  return s;
}

static void begin_body(metrics_writer_t *w) {
  metrics_hdr_t hdr;
  memset(&hdr, 0, sizeof hdr);
  hdr.proto_version = METRICS_PROTO_VERSION;
  hdr.component = METRICS_COMPONENT_SDS;
  bufcpy(hdr.metrics_id, sizeof hdr.metrics_id, "x");
  ck_assert_int_eq(metrics_writer_begin(w, &hdr), 0);
}

static void append_body(store_slot_t *s, const metrics_writer_t *w) {
  size_t n = w->len - METRICS_HDR_LEN;
  s->live.data = realloc(s->live.data, s->live.len + n);
  ck_assert_ptr_nonnull(s->live.data);
  memcpy(s->live.data + s->live.len, w->buf + METRICS_HDR_LEN, n);
  s->live.len += n;
}

static void add_entry(store_slot_t *s, metrics_id_t id, const char *label, uint64_t value) {
  metrics_writer_t w;
  begin_body(&w);
  ck_assert_int_eq(metrics_writer_put(&w, id, label, value), 0);
  append_body(s, &w);
}

START_TEST(empty_store_renders_only_self_metrics) {
  store_t st;
  char *out;
  size_t len;
  memset(&st, 0, sizeof st);

  render_openmetrics(&st, 0.0, &out, &len);
  ck_assert(strstr(out, "dvbipi_metrics_instances 0") != NULL);
  ck_assert(strstr(out, "dvbipi_metrics_snapshots_received_total 0") != NULL);
  ck_assert(strstr(out, "dvbipi_metrics_snapshots_rejected_total{reason=\"malformed\"} 0") != NULL);
  ck_assert(strstr(out, "dvbipi_metrics_http_requests_total{status=\"200\"} 0") != NULL);
  /* no exporter-supplied family should appear with nothing tracked */
  ck_assert(strstr(out, "dvbipi_sds_services") == NULL);
  ck_assert(strstr(out, "# EOF\n") != NULL);
  free(out);
  store_free(&st);
}
END_TEST

START_TEST(family_present_only_with_live_samples) {
  store_t st;
  store_slot_t *s;
  char *out;
  size_t len;
  memset(&st, 0, sizeof st);

  s = add_slot(&st, METRICS_COMPONENT_SDS, "inst1", 5.0);
  add_entry(s, METRICS_ID_SDS_SERVICES, NULL, 3);

  render_openmetrics(&st, 10.0, &out, &len);
  ck_assert(strstr(out, "# TYPE dvbipi_sds_services gauge") != NULL);
  ck_assert(strstr(out, "dvbipi_sds_services{component=\"sds\",headend_id=\"inst1\"} 3") != NULL);
  /* a family with zero live samples must not appear at all */
  ck_assert(strstr(out, "dvbipi_sds_service_providers") == NULL);
  free(out);
  store_free(&st);
}
END_TEST

START_TEST(headend_info_uses_version_label_and_info_type) {
  store_t st;
  store_slot_t *s;
  char *out;
  size_t len;
  memset(&st, 0, sizeof st);

  s = add_slot(&st, METRICS_COMPONENT_TVHEAD, "inst1", 5.0);
  add_entry(s, METRICS_ID_HEADEND_INFO, "2.2.0", 1);

  render_openmetrics(&st, 10.0, &out, &len);
  ck_assert(strstr(out, "# TYPE dvbipi_headend_info info") != NULL);
  ck_assert(strstr(out, "version=\"2.2.0\"") != NULL);
  free(out);
  store_free(&st);
}
END_TEST

START_TEST(composite_input_reason_label_is_split) {
  store_t st;
  store_slot_t *s;
  const char composite[] = {'i', '0', METRICS_LABEL_SEP, 'd', 'n', 's', '\0'};
  char *out;
  size_t len;
  memset(&st, 0, sizeof st);

  s = add_slot(&st, METRICS_COMPONENT_TVHEAD, "inst1", 5.0);
  add_entry(s, METRICS_ID_INPUT_ERRORS_TOTAL, composite, 2);

  render_openmetrics(&st, 10.0, &out, &len);
  ck_assert(strstr(out, "input=\"i0\",reason=\"dns\"") != NULL);
  free(out);
  store_free(&st);
}
END_TEST

START_TEST(headend_id_label_is_escaped) {
  store_t st;
  store_slot_t *s;
  char *out;
  size_t len;
  memset(&st, 0, sizeof st);

  s = add_slot(&st, METRICS_COMPONENT_SDS, "weird\"name\\x", 5.0);
  add_entry(s, METRICS_ID_SDS_SERVICES, NULL, 1);

  render_openmetrics(&st, 10.0, &out, &len);
  ck_assert(strstr(out, "headend_id=\"weird\\\"name\\\\x\"") != NULL);
  free(out);
  store_free(&st);
}
END_TEST

START_TEST(snapshot_age_reflects_now_minus_received) {
  store_t st;
  store_slot_t *s;
  char *out;
  size_t len;
  memset(&st, 0, sizeof st);

  s = add_slot(&st, METRICS_COMPONENT_SDS, "inst1", 5.0);
  add_entry(s, METRICS_ID_SDS_SERVICES, NULL, 1);

  render_openmetrics(&st, 12.5, &out, &len);
  ck_assert(strstr(out, "# TYPE dvbipi_metrics_snapshot_age_seconds gauge") != NULL);
  ck_assert(strstr(out, "dvbipi_metrics_snapshot_age_seconds{component=\"sds\",headend_id=\"inst1\"} 7.500") != NULL);
  free(out);
  store_free(&st);
}
END_TEST

START_TEST(output_ends_with_eof_marker) {
  store_t st;
  store_slot_t *s;
  char *out;
  size_t len;
  memset(&st, 0, sizeof st);

  s = add_slot(&st, METRICS_COMPONENT_SDS, "inst1", 5.0);
  add_entry(s, METRICS_ID_SDS_SERVICES, NULL, 1);

  render_openmetrics(&st, 10.0, &out, &len);
  ck_assert_uint_ge(len, 6u);
  ck_assert_str_eq(out + len - 6, "# EOF\n");
  free(out);
  store_free(&st);
}
END_TEST

START_TEST(self_metrics_reflect_stats_and_instance_count) {
  store_t st;
  char *out;
  size_t len;
  memset(&st, 0, sizeof st);

  st.slots[0].used = 1;
  st.slots[0].valid = 1;
  st.slots[1].used = 1;
  st.slots[1].valid = 1;
  st.stats.snapshots_received_total = 7;
  st.stats.snapshots_rejected_malformed = 1;
  st.stats.snapshots_rejected_stale = 2;
  st.stats.snapshots_rejected_full = 3;
  st.stats.snapshots_rejected_version = 4;
  st.stats.snapshots_rejected_toolarge = 6;
  st.stats.snapshots_incomplete = 8;
  st.stats.parts_orphaned = 10;
  st.stats.http_requests_200 = 9;
  st.stats.http_requests_404 = 5;

  render_openmetrics(&st, 0.0, &out, &len);
  ck_assert(strstr(out, "dvbipi_metrics_instances 2") != NULL);
  ck_assert(strstr(out, "dvbipi_metrics_snapshots_received_total 7") != NULL);
  ck_assert(strstr(out, "dvbipi_metrics_snapshots_rejected_total{reason=\"malformed\"} 1") != NULL);
  ck_assert(strstr(out, "dvbipi_metrics_snapshots_rejected_total{reason=\"stale\"} 2") != NULL);
  ck_assert(strstr(out, "dvbipi_metrics_snapshots_rejected_total{reason=\"full\"} 3") != NULL);
  ck_assert(strstr(out, "dvbipi_metrics_snapshots_rejected_total{reason=\"version\"} 4") != NULL);
  ck_assert(strstr(out, "dvbipi_metrics_snapshots_rejected_total{reason=\"toolarge\"} 6") != NULL);
  ck_assert(strstr(out, "dvbipi_metrics_snapshots_incomplete_total 8") != NULL);
  ck_assert(strstr(out, "dvbipi_metrics_parts_orphaned_total 10") != NULL);
  ck_assert(strstr(out, "dvbipi_metrics_http_requests_total{status=\"200\"} 9") != NULL);
  ck_assert(strstr(out, "dvbipi_metrics_http_requests_total{status=\"404\"} 5") != NULL);
  free(out);
  store_free(&st);
}
END_TEST

START_TEST(grouped_entries_share_label_and_render_each_value) {
  store_t st;
  store_slot_t *s;
  metrics_writer_t w;
  char *out;
  size_t len;
  memset(&st, 0, sizeof st);

  s = add_slot(&st, METRICS_COMPONENT_TVHEAD, "inst1", 5.0);
  begin_body(&w);
  ck_assert_int_eq(metrics_writer_put(&w, METRICS_ID_INPUT_UP, "i0", 1), 0);
  ck_assert_int_eq(metrics_writer_put(&w, METRICS_ID_INPUT_BYTES_TOTAL, "i0", 5000000000ULL), 0);
  ck_assert_int_eq(metrics_writer_put(&w, METRICS_ID_INPUT_UP, "i1", 0), 0);
  append_body(s, &w);

  render_openmetrics(&st, 10.0, &out, &len);
  ck_assert(strstr(out, "dvbipi_input_up{component=\"tvhead\",headend_id=\"inst1\",input=\"i0\"} 1") != NULL);
  ck_assert(strstr(out, "dvbipi_input_up{component=\"tvhead\",headend_id=\"inst1\",input=\"i1\"} 0") != NULL);
  ck_assert(strstr(out, "dvbipi_input_bytes_total{component=\"tvhead\",headend_id=\"inst1\",input=\"i0\"} 5000000000") != NULL);
  free(out);
  store_free(&st);
}
END_TEST

START_TEST(ts_series_carry_direction_and_stream_when_labeled) {
  store_t st;
  store_slot_t *s;
  char *out;
  size_t len;
  const char table_label[] = {'o', 'u', 't', 'p', 'u', 't', '0', METRICS_LABEL_SEP, 'p', 'a', 't', '\0'};
  const char pid_label[] = {'i', 'n', 'p', 'u', 't', '0', METRICS_LABEL_SEP, '2', '5', '6', '\0'};
  const char svc_label[] = {'o', 'u', 't', 'p', 'u', 't', '0', METRICS_LABEL_SEP, '7', '\0'};
  memset(&st, 0, sizeof st);

  s = add_slot(&st, METRICS_COMPONENT_TVHEAD, "inst1", 5.0);
  add_entry(s, METRICS_ID_TS_CONTINUITY_ERRORS_TOTAL, "input2", 17);
  add_entry(s, METRICS_ID_TS_PACKETS_TOTAL, NULL, 9);
  add_entry(s, METRICS_ID_TS_TABLE_CRC_ERRORS_TOTAL, table_label, 3);
  add_entry(s, METRICS_ID_TS_PID_PACKETS_TOTAL, pid_label, 41);
  add_entry(s, METRICS_ID_TS_SERVICE_SCRAMBLED_PACKETS_TOTAL, svc_label, 5);

  render_openmetrics(&st, 10.0, &out, &len);
  ck_assert(strstr(out, "dvbipi_ts_continuity_errors_total{component=\"tvhead\",headend_id=\"inst1\",direction=\"input\",stream=\"input2\"} 17") != NULL);
  ck_assert(strstr(out, "dvbipi_ts_packets_total{component=\"tvhead\",headend_id=\"inst1\"} 9") != NULL);
  ck_assert(strstr(out, "dvbipi_ts_table_crc_errors_total{component=\"tvhead\",headend_id=\"inst1\",direction=\"output\",stream=\"output0\",table=\"pat\"} 3") != NULL);
  ck_assert(strstr(out, "dvbipi_ts_pid_packets_total{component=\"tvhead\",headend_id=\"inst1\",direction=\"input\",stream=\"input0\",pid=\"256\"} 41") != NULL);
  ck_assert(strstr(out, "dvbipi_ts_service_scrambled_packets_total{component=\"tvhead\",headend_id=\"inst1\",direction=\"output\",stream=\"output0\",service=\"7\"} 5") != NULL);
  free(out);
  store_free(&st);
}
END_TEST

static Suite *render_suite(void) {
  Suite *s = suite_create("dipimetrics_render");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, empty_store_renders_only_self_metrics);
  tcase_add_test(tc, family_present_only_with_live_samples);
  tcase_add_test(tc, headend_info_uses_version_label_and_info_type);
  tcase_add_test(tc, composite_input_reason_label_is_split);
  tcase_add_test(tc, headend_id_label_is_escaped);
  tcase_add_test(tc, snapshot_age_reflects_now_minus_received);
  tcase_add_test(tc, output_ends_with_eof_marker);
  tcase_add_test(tc, self_metrics_reflect_stats_and_instance_count);
  tcase_add_test(tc, grouped_entries_share_label_and_render_each_value);
  tcase_add_test(tc, ts_series_carry_direction_and_stream_when_labeled);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(render_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
