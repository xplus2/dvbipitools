/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dipimetrics/store.h"

static size_t build_snapshot(unsigned char *buf, metrics_component_t component, const char *id, uint64_t process_start, uint64_t sequence, uint64_t value) {
  metrics_writer_t w;
  metrics_hdr_t hdr;
  memset(&hdr, 0, sizeof hdr);
  hdr.proto_version = METRICS_PROTO_VERSION;
  hdr.component = component;
  snprintf(hdr.metrics_id, sizeof hdr.metrics_id, "%s", id);
  hdr.process_start_time = process_start;
  hdr.sequence = sequence;
  hdr.snapshot_time = 1000;
  metrics_writer_begin(&w, &hdr);
  metrics_writer_put(&w, METRICS_ID_SDS_SERVICES, NULL, value);
  memcpy(buf, w.buf, w.len);
  return w.len;
}

static store_slot_t *only_used_slot(store_t *st) {
  int i;
  store_slot_t *found = NULL;
  for (i = 0; i < STORE_MAX_INSTANCES; i++)
    if (st->slots[i].used) {
      ck_assert_ptr_null(found);
      found = &st->slots[i];
    }
  return found;
}

START_TEST(valid_snapshot_creates_slot) {
  store_t st;
  unsigned char buf[METRICS_MAX_SNAPSHOT_BYTES];
  size_t len = build_snapshot(buf, METRICS_COMPONENT_SDS, "inst1", 100, 1, 5);
  store_slot_t *slot;

  store_init(&st);
  store_ingest(&st, buf, len, 10.0, 0);

  slot = only_used_slot(&st);
  ck_assert_ptr_nonnull(slot);
  ck_assert_int_eq(slot->component, METRICS_COMPONENT_SDS);
  ck_assert_str_eq(slot->metrics_id, "inst1");
  ck_assert_uint_eq(slot->sequence, 1u);
  ck_assert_int_eq(slot->entry_count, 1);
  ck_assert_int_eq(slot->entries[0].id, METRICS_ID_SDS_SERVICES);
  ck_assert_uint_eq(slot->entries[0].value, 5u);
  ck_assert(slot->received_mono == 10.0);
}
END_TEST

START_TEST(malformed_datagram_creates_no_slot) {
  store_t st;
  unsigned char garbage[20];
  memset(garbage, 0xAA, sizeof garbage);

  store_init(&st);
  store_ingest(&st, garbage, sizeof garbage, 1.0, 0);
  ck_assert_ptr_null(only_used_slot(&st));
}
END_TEST

START_TEST(oversized_datagram_rejected) {
  store_t st;
  unsigned char buf[METRICS_MAX_SNAPSHOT_BYTES + 1];
  memset(buf, 0, sizeof buf);

  store_init(&st);
  store_ingest(&st, buf, sizeof buf, 1.0, 0);
  ck_assert_ptr_null(only_used_slot(&st));
}
END_TEST

START_TEST(stale_sequence_is_dropped) {
  store_t st;
  unsigned char buf[METRICS_MAX_SNAPSHOT_BYTES];
  size_t len;
  store_slot_t *slot;

  store_init(&st);
  len = build_snapshot(buf, METRICS_COMPONENT_SDS, "inst1", 100, 5, 1);
  store_ingest(&st, buf, len, 10.0, 0);
  len = build_snapshot(buf, METRICS_COMPONENT_SDS, "inst1", 100, 3, 2);
  store_ingest(&st, buf, len, 20.0, 0);

  slot = only_used_slot(&st);
  ck_assert_ptr_nonnull(slot);
  ck_assert_uint_eq(slot->sequence, 5u); /* the stale seq=3 update never applied */
  ck_assert_uint_eq(slot->entries[0].value, 1u);
  ck_assert(slot->received_mono == 10.0); /* not bumped by the rejected datagram */
}
END_TEST

START_TEST(equal_sequence_is_dropped) {
  store_t st;
  unsigned char buf[METRICS_MAX_SNAPSHOT_BYTES];
  size_t len;
  store_slot_t *slot;

  store_init(&st);
  len = build_snapshot(buf, METRICS_COMPONENT_SDS, "inst1", 100, 5, 1);
  store_ingest(&st, buf, len, 10.0, 0);
  len = build_snapshot(buf, METRICS_COMPONENT_SDS, "inst1", 100, 5, 99);
  store_ingest(&st, buf, len, 20.0, 0);

  slot = only_used_slot(&st);
  ck_assert_uint_eq(slot->entries[0].value, 1u);
}
END_TEST

START_TEST(process_restart_accepted_despite_lower_sequence) {
  store_t st;
  unsigned char buf[METRICS_MAX_SNAPSHOT_BYTES];
  size_t len;
  store_slot_t *slot;

  store_init(&st);
  len = build_snapshot(buf, METRICS_COMPONENT_SDS, "inst1", 100, 50, 1);
  store_ingest(&st, buf, len, 10.0, 0);
  /* different process_start_time (restart), sequence resets to 1 - still accepted */
  len = build_snapshot(buf, METRICS_COMPONENT_SDS, "inst1", 200, 1, 77);
  store_ingest(&st, buf, len, 20.0, 0);

  slot = only_used_slot(&st);
  ck_assert_ptr_nonnull(slot);
  ck_assert_uint_eq(slot->process_start_time, 200u);
  ck_assert_uint_eq(slot->sequence, 1u);
  ck_assert_uint_eq(slot->entries[0].value, 77u);
}
END_TEST

START_TEST(distinct_component_same_id_are_separate_instances) {
  store_t st;
  unsigned char buf[METRICS_MAX_SNAPSHOT_BYTES];
  size_t len;
  int count = 0, i;

  store_init(&st);
  len = build_snapshot(buf, METRICS_COMPONENT_SDS, "shared-name", 100, 1, 1);
  store_ingest(&st, buf, len, 10.0, 0);
  len = build_snapshot(buf, METRICS_COMPONENT_BCG, "shared-name", 100, 1, 2);
  store_ingest(&st, buf, len, 10.0, 0);

  for (i = 0; i < STORE_MAX_INSTANCES; i++)
    if (st.slots[i].used)
      count++;
  ck_assert_int_eq(count, 2);
}
END_TEST

START_TEST(store_full_drops_new_instance) {
  store_t st;
  unsigned char buf[METRICS_MAX_SNAPSHOT_BYTES];
  int i, used = 0;
  char id[METRICS_ID_MAX];

  store_init(&st);
  for (i = 0; i < STORE_MAX_INSTANCES; i++) {
    size_t len;
    snprintf(id, sizeof id, "inst%d", i);
    len = build_snapshot(buf, METRICS_COMPONENT_SDS, id, 100, 1, (uint64_t)i);
    store_ingest(&st, buf, len, 10.0, 0);
  }
  for (i = 0; i < STORE_MAX_INSTANCES; i++)
    if (st.slots[i].used)
      used++;
  ck_assert_int_eq(used, STORE_MAX_INSTANCES);

  {
    size_t len = build_snapshot(buf, METRICS_COMPONENT_SDS, "one-too-many", 100, 1, 999);
    store_ingest(&st, buf, len, 10.0, 0);
  }
  used = 0;
  for (i = 0; i < STORE_MAX_INSTANCES; i++) {
    if (st.slots[i].used) {
      used++;
      ck_assert_str_ne(st.slots[i].metrics_id, "one-too-many");
    }
  }
  ck_assert_int_eq(used, STORE_MAX_INSTANCES);
}
END_TEST

START_TEST(reap_expired_frees_silent_slot) {
  store_t st;
  unsigned char buf[METRICS_MAX_SNAPSHOT_BYTES];
  size_t len = build_snapshot(buf, METRICS_COMPONENT_SDS, "inst1", 100, 1, 1);

  store_init(&st);
  store_ingest(&st, buf, len, 10.0, 0);
  ck_assert_ptr_nonnull(only_used_slot(&st));

  store_reap_expired(&st, 20.0, 30.0); /* only 10s elapsed, under 30s expiry */
  ck_assert_ptr_nonnull(only_used_slot(&st));

  store_reap_expired(&st, 41.0, 30.0); /* 31s elapsed, past 30s expiry */
  ck_assert_ptr_null(only_used_slot(&st));
}
END_TEST

START_TEST(component_name_covers_all_known_and_unknown) {
  ck_assert_str_eq(metrics_component_name(METRICS_COMPONENT_TVHEAD), "tvhead");
  ck_assert_str_eq(metrics_component_name(METRICS_COMPONENT_RADIOHEAD), "radiohead");
  ck_assert_str_eq(metrics_component_name(METRICS_COMPONENT_SDS), "sds");
  ck_assert_str_eq(metrics_component_name(METRICS_COMPONENT_BCG), "bcg");
  ck_assert_str_eq(metrics_component_name((metrics_component_t)0), "unknown");
}
END_TEST

static Suite *store_suite(void) {
  Suite *s = suite_create("dipimetrics_store");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, valid_snapshot_creates_slot);
  tcase_add_test(tc, malformed_datagram_creates_no_slot);
  tcase_add_test(tc, oversized_datagram_rejected);
  tcase_add_test(tc, stale_sequence_is_dropped);
  tcase_add_test(tc, equal_sequence_is_dropped);
  tcase_add_test(tc, process_restart_accepted_despite_lower_sequence);
  tcase_add_test(tc, distinct_component_same_id_are_separate_instances);
  tcase_add_test(tc, store_full_drops_new_instance);
  tcase_add_test(tc, reap_expired_frees_silent_slot);
  tcase_add_test(tc, component_name_covers_all_known_and_unknown);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(store_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
