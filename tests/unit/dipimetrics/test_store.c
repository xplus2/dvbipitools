/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "lib/helper/ioutil.h"
#include "dipimetrics/store.h"

static void make_hdr(metrics_hdr_t *hdr, metrics_component_t component, const char *id, uint64_t process_start, uint64_t sequence) {
  memset(hdr, 0, sizeof *hdr);
  hdr->proto_version = METRICS_PROTO_VERSION;
  hdr->component = component;
  bufcpy(hdr->metrics_id, sizeof hdr->metrics_id, id);
  hdr->process_start_time = process_start;
  hdr->sequence = sequence;
  hdr->snapshot_time = 1000;
}

static size_t build_snapshot(unsigned char *buf, metrics_component_t component, const char *id, uint64_t process_start, uint64_t sequence, uint64_t value) {
  metrics_writer_t w;
  metrics_hdr_t hdr;
  size_t len;
  make_hdr(&hdr, component, id, process_start, sequence);
  metrics_writer_begin(&w, &hdr);
  metrics_writer_put(&w, METRICS_ID_SDS_SERVICES, NULL, value);
  len = metrics_writer_finish(&w);
  memcpy(buf, w.buf, len);
  return len;
}

typedef struct {
  unsigned char parts[METRICS_MAX_PARTS][METRICS_MAX_SNAPSHOT_BYTES];
  size_t lens[METRICS_MAX_PARTS];
  unsigned n;
} capture_t;

static capture_t g_cap;

static int capture_part(void *ctx, const unsigned char *buf, size_t len) {
  capture_t *c = ctx;
  memcpy(c->parts[c->n], buf, len);
  c->lens[c->n++] = len;
  return 0;
}

static void build_parts(uint64_t sequence, unsigned entries) {
  metrics_writer_t w;
  metrics_hdr_t hdr;
  size_t len;
  make_hdr(&hdr, METRICS_COMPONENT_TVHEAD, "inst1", 100, sequence);
  g_cap.n = 0;
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  w.flush = capture_part;
  w.flush_ctx = &g_cap;
  for (unsigned i = 0; i < entries; i++) {
    char label[16];
    label[0] = 'l';
    uint_to_str(label + 1, i);
    ck_assert_int_eq(metrics_writer_put(&w, METRICS_ID_INPUT_BYTES_TOTAL, label, (uint64_t)i * 1000003u), 0);
  }
  len = metrics_writer_finish(&w);
  ck_assert_uint_gt(len, 0u);
  capture_part(&g_cap, w.buf, len);
}

static unsigned count_entries(const store_slot_t *slot) {
  metrics_reader_t r;
  metrics_id_t id;
  const char *label;
  size_t label_len;
  uint64_t value;
  unsigned n = 0;
  metrics_reader_init_body(&r, slot->version, slot->live.data, slot->live.len);
  while (metrics_reader_next_ref(&r, &id, &label, &label_len, &value) == 1)
    n++;
  return n;
}

static uint64_t first_value(const store_slot_t *slot) {
  metrics_reader_t r;
  metrics_id_t id;
  const char *label;
  size_t label_len;
  uint64_t value = 0;
  metrics_reader_init_body(&r, slot->version, slot->live.data, slot->live.len);
  ck_assert_int_eq(metrics_reader_next_ref(&r, &id, &label, &label_len, &value), 1);
  return value;
}

static store_slot_t *only_used_slot(store_t *st) {
  store_slot_t *found = NULL;
  for (int i = 0; i < STORE_MAX_INSTANCES; i++)
    if (st->slots[i].used) {
      ck_assert_ptr_null(found);
      found = &st->slots[i];
    }
  return found;
}

static store_slot_t *only_valid_slot(store_t *st) {
  store_slot_t *found = NULL;
  for (int i = 0; i < STORE_MAX_INSTANCES; i++)
    if (st->slots[i].valid) {
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
  ck_assert_int_eq(slot->valid, 1);
  ck_assert_uint_eq(count_entries(slot), 1u);
  ck_assert_uint_eq(first_value(slot), 5u);
  ck_assert(slot->received_mono == 10.0);
  ck_assert_uint_eq(st.stats.snapshots_received_total, 1u);
  store_free(&st);
}
END_TEST

START_TEST(malformed_datagram_creates_no_slot) {
  store_t st;
  unsigned char garbage[20];
  memset(garbage, 0, sizeof garbage);
  garbage[0] = METRICS_PROTO_VERSION;

  store_init(&st);
  store_ingest(&st, garbage, sizeof garbage, 1.0, 0);
  ck_assert_ptr_null(only_used_slot(&st));
  ck_assert_uint_eq(st.stats.snapshots_rejected_malformed, 1u);
  ck_assert_uint_eq(st.stats.snapshots_received_total, 0u);
  store_free(&st);
}
END_TEST

START_TEST(oversized_datagram_rejected) {
  store_t st;
  unsigned char buf[METRICS_MAX_SNAPSHOT_BYTES + 1];
  memset(buf, 0, sizeof buf);

  store_init(&st);
  store_ingest(&st, buf, sizeof buf, 1.0, 0);
  ck_assert_ptr_null(only_used_slot(&st));
  ck_assert_uint_eq(st.stats.snapshots_rejected_malformed, 1u);
  store_free(&st);
}
END_TEST

START_TEST(unsupported_version_is_rejected) {
  store_t st;
  unsigned char buf[METRICS_MAX_SNAPSHOT_BYTES];
  size_t len = build_snapshot(buf, METRICS_COMPONENT_SDS, "inst1", 100, 1, 5);
  buf[0] = METRICS_PROTO_VERSION + 1;

  store_init(&st);
  store_ingest(&st, buf, len, 1.0, 0);
  ck_assert_ptr_null(only_used_slot(&st));
  ck_assert_uint_eq(st.stats.snapshots_rejected_version, 1u);
  ck_assert_uint_eq(st.stats.snapshots_received_total, 0u);
  store_free(&st);
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
  ck_assert_uint_eq(first_value(slot), 1u);
  ck_assert(slot->received_mono == 10.0); /* not bumped by the rejected datagram */
  ck_assert_uint_eq(st.stats.snapshots_rejected_stale, 1u);
  store_free(&st);
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
  ck_assert_uint_eq(first_value(slot), 1u);
  store_free(&st);
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
  ck_assert_uint_eq(first_value(slot), 77u);
  store_free(&st);
}
END_TEST

START_TEST(distinct_component_same_id_are_separate_instances) {
  store_t st;
  unsigned char buf[METRICS_MAX_SNAPSHOT_BYTES];
  size_t len;
  int count = 0;

  store_init(&st);
  len = build_snapshot(buf, METRICS_COMPONENT_SDS, "shared-name", 100, 1, 1);
  store_ingest(&st, buf, len, 10.0, 0);
  len = build_snapshot(buf, METRICS_COMPONENT_BCG, "shared-name", 100, 1, 2);
  store_ingest(&st, buf, len, 10.0, 0);

  for (int i = 0; i < STORE_MAX_INSTANCES; i++)
    if (st.slots[i].used)
      count++;
  ck_assert_int_eq(count, 2);
  store_free(&st);
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
    id[0] = 'i';
    uint_to_str(id + 1, (unsigned)i);
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
  ck_assert_uint_eq(st.stats.snapshots_rejected_full, 1u);
  store_free(&st);
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
  store_free(&st);
}
END_TEST

START_TEST(v1_snapshot_is_accepted) {
  store_t st;
  unsigned char buf[METRICS_MAX_SNAPSHOT_BYTES];
  size_t len = build_snapshot(buf, METRICS_COMPONENT_SDS, "inst1", 100, 1, 0);
  store_slot_t *slot;

  memset(buf + METRICS_HDR_LEN, 0, len - METRICS_HDR_LEN);
  len = METRICS_HDR_LEN;
  buf[0] = METRICS_PROTO_V1;
  buf[2] = 0;
  buf[3] = 0;
  buf[len++] = 0;
  buf[len++] = (unsigned char)METRICS_ID_SDS_SERVICES;
  buf[len++] = 0;
  for (int i = 0; i < 7; i++)
    buf[len++] = 0;
  buf[len++] = 9;

  store_init(&st);
  store_ingest(&st, buf, len, 10.0, 0);
  slot = only_valid_slot(&st);
  ck_assert_ptr_nonnull(slot);
  ck_assert_uint_eq(slot->version, (unsigned)METRICS_PROTO_V1);
  ck_assert_uint_eq(first_value(slot), 9u);
  store_free(&st);
}
END_TEST

START_TEST(malformed_v2_body_is_rejected) {
  store_t st;
  unsigned char buf[METRICS_MAX_SNAPSHOT_BYTES];
  size_t len = build_snapshot(buf, METRICS_COMPONENT_SDS, "inst1", 100, 1, 5);

  buf[METRICS_HDR_LEN + 1] = 0;
  store_init(&st);
  store_ingest(&st, buf, len, 1.0, 0);
  ck_assert_ptr_null(only_used_slot(&st));
  ck_assert_uint_eq(st.stats.snapshots_rejected_malformed, 1u);
  store_free(&st);
}
END_TEST

START_TEST(multipart_commits_only_when_last_part_arrives) {
  store_t st;
  store_slot_t *slot;

  build_parts(1, 2000);
  ck_assert_uint_gt(g_cap.n, 2u);
  store_init(&st);
  for (unsigned i = 0; i + 1 < g_cap.n; i++) {
    store_ingest(&st, g_cap.parts[i], g_cap.lens[i], 10.0, 0);
    ck_assert_ptr_null(only_valid_slot(&st));
  }
  store_ingest(&st, g_cap.parts[g_cap.n - 1], g_cap.lens[g_cap.n - 1], 10.0, 0);
  slot = only_valid_slot(&st);
  ck_assert_ptr_nonnull(slot);
  ck_assert_uint_eq(count_entries(slot), 2000u);
  ck_assert_uint_eq(st.stats.snapshots_received_total, 1u);
  ck_assert_uint_eq(st.stats.snapshots_incomplete, 0u);
  store_free(&st);
}
END_TEST

START_TEST(lost_part_keeps_previous_snapshot) {
  store_t st;
  unsigned char buf[METRICS_MAX_SNAPSHOT_BYTES];
  size_t len = build_snapshot(buf, METRICS_COMPONENT_TVHEAD, "inst1", 100, 1, 5);
  store_slot_t *slot;

  store_init(&st);
  store_ingest(&st, buf, len, 10.0, 0);
  build_parts(2, 2000);
  ck_assert_uint_gt(g_cap.n, 2u);
  store_ingest(&st, g_cap.parts[0], g_cap.lens[0], 20.0, 0);
  for (unsigned i = 2; i < g_cap.n; i++)
    store_ingest(&st, g_cap.parts[i], g_cap.lens[i], 20.0, 0);

  slot = only_valid_slot(&st);
  ck_assert_ptr_nonnull(slot);
  ck_assert_uint_eq(slot->sequence, 1u);
  ck_assert_uint_eq(count_entries(slot), 1u);
  ck_assert_uint_eq(st.stats.snapshots_incomplete, 1u);
  ck_assert_uint_gt(st.stats.parts_orphaned, 0u);
  store_free(&st);
}
END_TEST

START_TEST(newer_sequence_supersedes_staging) {
  store_t st;
  store_slot_t *slot;

  store_init(&st);
  build_parts(2, 2000);
  store_ingest(&st, g_cap.parts[0], g_cap.lens[0], 10.0, 0);
  build_parts(3, 2000);
  for (unsigned i = 0; i < g_cap.n; i++)
    store_ingest(&st, g_cap.parts[i], g_cap.lens[i], 11.0, 0);

  slot = only_valid_slot(&st);
  ck_assert_ptr_nonnull(slot);
  ck_assert_uint_eq(slot->sequence, 3u);
  ck_assert_uint_eq(count_entries(slot), 2000u);
  ck_assert_uint_eq(st.stats.snapshots_incomplete, 1u);
  store_free(&st);
}
END_TEST

START_TEST(orphan_part_creates_no_slot) {
  store_t st;

  build_parts(1, 2000);
  store_init(&st);
  store_ingest(&st, g_cap.parts[1], g_cap.lens[1], 10.0, 0);
  ck_assert_ptr_null(only_used_slot(&st));
  ck_assert_uint_eq(st.stats.parts_orphaned, 1u);
  store_free(&st);
}
END_TEST

START_TEST(oversized_snapshot_is_dropped) {
  store_t st;
  metrics_writer_t w;
  metrics_hdr_t hdr;
  unsigned n = 0;

  store_init(&st);
  make_hdr(&hdr, METRICS_COMPONENT_TVHEAD, "inst1", 100, 1);
  g_cap.n = 0;
  ck_assert_int_eq(metrics_writer_begin(&w, &hdr), 0);
  w.flush = capture_part;
  w.flush_ctx = &g_cap;
  while (g_cap.n < METRICS_MAX_PARTS - 2) {
    char label[METRICS_LABEL_MAX + 1];
    char num[16];
    size_t nl = uint_to_str(num, n++);
    memset(label, 'a', METRICS_LABEL_MAX);
    label[METRICS_LABEL_MAX] = '\0';
    memcpy(label, num, nl);
    ck_assert_int_eq(metrics_writer_put(&w, METRICS_ID_INPUT_BYTES_TOTAL, label, 1), 0);
  }
  for (unsigned i = 0; i < g_cap.n; i++)
    store_ingest(&st, g_cap.parts[i], g_cap.lens[i], 10.0, 0);
  ck_assert_uint_eq(st.stats.snapshots_rejected_toolarge, 1u);
  ck_assert_ptr_null(only_used_slot(&st));
  store_free(&st);
}
END_TEST

START_TEST(reap_frees_slot_with_only_staged_parts) {
  store_t st;

  build_parts(1, 2000);
  store_init(&st);
  store_ingest(&st, g_cap.parts[0], g_cap.lens[0], 10.0, 0);
  ck_assert_ptr_nonnull(only_used_slot(&st));
  ck_assert_ptr_null(only_valid_slot(&st));
  store_reap_expired(&st, 50.0, 30.0);
  ck_assert_ptr_null(only_used_slot(&st));
  store_free(&st);
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
  tcase_add_test(tc, unsupported_version_is_rejected);
  tcase_add_test(tc, stale_sequence_is_dropped);
  tcase_add_test(tc, equal_sequence_is_dropped);
  tcase_add_test(tc, process_restart_accepted_despite_lower_sequence);
  tcase_add_test(tc, distinct_component_same_id_are_separate_instances);
  tcase_add_test(tc, store_full_drops_new_instance);
  tcase_add_test(tc, reap_expired_frees_silent_slot);
  tcase_add_test(tc, v1_snapshot_is_accepted);
  tcase_add_test(tc, malformed_v2_body_is_rejected);
  tcase_add_test(tc, multipart_commits_only_when_last_part_arrives);
  tcase_add_test(tc, lost_part_keeps_previous_snapshot);
  tcase_add_test(tc, newer_sequence_supersedes_staging);
  tcase_add_test(tc, orphan_part_creates_no_slot);
  tcase_add_test(tc, oversized_snapshot_is_dropped);
  tcase_add_test(tc, reap_frees_slot_with_only_staged_parts);
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
