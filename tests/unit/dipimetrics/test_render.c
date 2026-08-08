/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dipimetrics/render.h"
#include "dipimetrics/store.h"

static store_slot_t *add_slot(store_t *st, metrics_component_t component, const char *id, double received_mono) {
  store_slot_t *s = &st->slots[0];
  memset(s, 0, sizeof *s);
  s->used = 1;
  s->component = component;
  snprintf(s->metrics_id, sizeof s->metrics_id, "%s", id);
  s->received_mono = received_mono;
  return s;
}

static void add_entry(store_slot_t *s, metrics_id_t id, const char *label, uint64_t value) {
  stored_entry_t *e = &s->entries[s->entry_count++];
  e->id = id;
  e->label[0] = '\0';
  if (label)
    snprintf(e->label, sizeof e->label, "%s", label);
  e->value = value;
}

START_TEST(empty_store_renders_only_eof) {
  store_t st;
  char *out;
  size_t len;
  memset(&st, 0, sizeof st);

  render_openmetrics(&st, 0.0, &out, &len);
  ck_assert_str_eq(out, "# EOF\n");
  free(out);
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
  ck_assert(strstr(out, "dvbipi_sds_services{component=\"sds\",instance=\"inst1\"} 3") != NULL);
  /* a family with zero live samples must not appear at all */
  ck_assert(strstr(out, "dvbipi_sds_service_providers") == NULL);
  free(out);
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
}
END_TEST

START_TEST(composite_input_reason_label_is_split) {
  store_t st;
  store_slot_t *s;
  char composite[16];
  char *out;
  size_t len;
  memset(&st, 0, sizeof st);

  snprintf(composite, sizeof composite, "i0%cdns", METRICS_LABEL_SEP);

  s = add_slot(&st, METRICS_COMPONENT_TVHEAD, "inst1", 5.0);
  add_entry(s, METRICS_ID_INPUT_ERRORS_TOTAL, composite, 2);

  render_openmetrics(&st, 10.0, &out, &len);
  ck_assert(strstr(out, "input=\"i0\",reason=\"dns\"") != NULL);
  free(out);
}
END_TEST

START_TEST(instance_label_is_escaped) {
  store_t st;
  store_slot_t *s;
  char *out;
  size_t len;
  memset(&st, 0, sizeof st);

  s = add_slot(&st, METRICS_COMPONENT_SDS, "weird\"name\\x", 5.0);
  add_entry(s, METRICS_ID_SDS_SERVICES, NULL, 1);

  render_openmetrics(&st, 10.0, &out, &len);
  ck_assert(strstr(out, "instance=\"weird\\\"name\\\\x\"") != NULL);
  free(out);
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
  ck_assert(strstr(out, "dvbipi_metrics_snapshot_age_seconds{component=\"sds\",instance=\"inst1\"} 7.500") != NULL);
  free(out);
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
}
END_TEST

static Suite *render_suite(void) {
  Suite *s = suite_create("dipimetrics_render");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, empty_store_renders_only_eof);
  tcase_add_test(tc, family_present_only_with_live_samples);
  tcase_add_test(tc, headend_info_uses_version_label_and_info_type);
  tcase_add_test(tc, composite_input_reason_label_is_split);
  tcase_add_test(tc, instance_label_is_escaped);
  tcase_add_test(tc, snapshot_age_reflects_now_minus_received);
  tcase_add_test(tc, output_ends_with_eof_marker);
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
