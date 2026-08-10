/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/cas/biss/ca_engine.h"

static char g_dir[] = "/tmp/biss_ca_engine_test_XXXXXX";

static void noop_emit(void *ctx, const unsigned char pkt[188]) {
  (void)ctx;
  (void)pkt;
}

static void write_receiver_key(const char *dir, const char *name) {
  char path[512], cmd[1024];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  snprintf(cmd, sizeof cmd, "openssl genrsa 2048 2>/dev/null | openssl rsa -pubout -out %s 2>/dev/null", path);
  ck_assert_int_eq(system(cmd), 0);
}

static void setup(void) {
  ck_assert_ptr_nonnull(mkdtemp(g_dir));
  write_receiver_key(g_dir, "r1.pem");
  write_receiver_key(g_dir, "r2.pem");
}

static void teardown(void) {
  char cmd[600];
  snprintf(cmd, sizeof cmd, "rm -rf %s", g_dir);
  system(cmd);
}

static biss_ca_engine_cfg_t base_cfg(const unsigned *pids, size_t pid_count) {
  biss_ca_engine_cfg_t cfg;
  memset(&cfg, 0, sizeof cfg);
  cfg.receivers_dir = g_dir;
  cfg.esid = 0x1234;
  cfg.onid = 1;
  cfg.sw_period_ms = 1000;
  cfg.ecm_pid = 0x1FFA;
  cfg.emm_pid = 0x1FFB;
  cfg.pids = pids;
  cfg.pid_count = pid_count;
  cfg.flush_pid = pids[0];
  return cfg;
}

START_TEST(start_loads_receivers_and_stops_cleanly) {
  unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  biss_ca_engine_t *e = biss_ca_engine_start(&cfg);
  ck_assert_ptr_nonnull(e);
  ck_assert_uint_eq(biss_ca_engine_receiver_count(e), 2u);
  ck_assert_uint_eq(biss_ca_engine_ecm_pid(e), 0x1FFAu);
  ck_assert_uint_eq(biss_ca_engine_emm_pid(e), 0x1FFBu);
  biss_ca_engine_stop(e);
}
END_TEST

START_TEST(start_rejects_empty_receivers_dir) {
  char empty_dir[] = "/tmp/biss_ca_engine_empty_XXXXXX";
  unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg;
  biss_ca_engine_t *e;
  ck_assert_ptr_nonnull(mkdtemp(empty_dir));
  cfg = base_cfg(pids, 1);
  cfg.receivers_dir = empty_dir;
  e = biss_ca_engine_start(&cfg);
  ck_assert_ptr_null(e);
  rmdir(empty_dir);
}
END_TEST

START_TEST(start_rejects_short_sw_period) {
  unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  cfg.sw_period_ms = 999;
  ck_assert_ptr_null(biss_ca_engine_start(&cfg));
}
END_TEST

START_TEST(ecm_and_emm_become_due_and_repeat_rate_limited) {
  unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  biss_ca_engine_t *e = biss_ca_engine_start(&cfg);
  unsigned char buf[4096];
  size_t len = 0;

  ck_assert_int_eq(biss_ca_engine_ecm_due(e, 1.0, buf, sizeof buf, &len), 0);
  ck_assert_uint_gt(len, 0u);
  /* immediate re-poll: not due yet (T_ECM_MIN not elapsed) */
  ck_assert_int_eq(biss_ca_engine_ecm_due(e, 1.01, buf, sizeof buf, &len), -1);
  ck_assert_int_eq(biss_ca_engine_ecm_due(e, 1.2, buf, sizeof buf, &len), 0);

  ck_assert_int_eq(biss_ca_engine_emm_due(e, 1.0, buf, sizeof buf, &len), 0);
  ck_assert_uint_gt(len, 0u);
  ck_assert_int_eq(biss_ca_engine_emm_due(e, 1.05, buf, sizeof buf, &len), -1);
  ck_assert_int_eq(biss_ca_engine_emm_due(e, 1.3, buf, sizeof buf, &len), 0);

  biss_ca_engine_stop(e);
}
END_TEST

START_TEST(sw_rotation_changes_ecm_content) {
  unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  biss_ca_engine_t *e = biss_ca_engine_start(&cfg);
  unsigned char pkt[188];
  unsigned char before[4096], after[4096];
  size_t before_len, after_len;

  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(pkt[1] | (1 << 6)); /* pusi */
  pkt[1] = (unsigned char)((pkt[1] & 0xE0) | (0x0100 >> 8));
  pkt[2] = 0x00;

  biss_ca_engine_scramble_packet(e, 0x0100, 0.0, pkt, noop_emit, NULL);
  ck_assert_int_eq(biss_ca_engine_ecm_due(e, 0.0, before, sizeof before, &before_len), 0);

  biss_ca_engine_clock_tick(e, 1000);
  biss_ca_engine_scramble_packet(e, 0x0100, 1.0, pkt, noop_emit, NULL);
  ck_assert_int_eq(biss_ca_engine_ecm_due(e, 1.0, after, sizeof after, &after_len), 0);

  ck_assert_uint_eq(before_len, after_len);
  ck_assert_mem_ne(before, after, before_len);

  biss_ca_engine_stop(e);
}
END_TEST

START_TEST(prog_desc_and_cat_build_nonempty) {
  unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  biss_ca_engine_t *e = biss_ca_engine_start(&cfg);
  unsigned char buf[64];
  size_t n;

  n = biss_ca_engine_prog_desc(e, buf, sizeof buf);
  ck_assert_uint_gt(n, 0u);
  ck_assert_uint_eq(buf[0], 0x09); /* CA_descriptor tag */

  n = biss_ca_engine_build_cat(e, buf, sizeof buf);
  ck_assert_uint_gt(n, 0u);
  ck_assert_uint_eq(buf[0], 0x01); /* CAT table_id */

  biss_ca_engine_stop(e);
}
END_TEST

START_TEST(reload_detects_added_and_removed_receiver) {
  unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  biss_ca_engine_t *e = biss_ca_engine_start(&cfg);
  char path[512];

  /* same 2 files: unchanged */
  ck_assert_int_eq(biss_ca_engine_reload_receivers(e), 0);

  write_receiver_key(g_dir, "r3.pem");
  ck_assert_int_eq(biss_ca_engine_reload_receivers(e), 1);
  ck_assert_uint_eq(biss_ca_engine_receiver_count(e), 3u);

  snprintf(path, sizeof path, "%s/r3.pem", g_dir);
  unlink(path);
  ck_assert_int_eq(biss_ca_engine_reload_receivers(e), 1);
  ck_assert_uint_eq(biss_ca_engine_receiver_count(e), 2u);

  biss_ca_engine_stop(e);
}
END_TEST

START_TEST(force_sk_rotation_makes_next_scramble_rotate) {
  unsigned pids[] = {0x0100};
  biss_ca_engine_cfg_t cfg = base_cfg(pids, 1);
  biss_ca_engine_t *e = biss_ca_engine_start(&cfg);
  unsigned char pkt[188];
  unsigned char before[4096], after[4096];
  size_t before_len, after_len;

  memset(pkt, 0, sizeof pkt);
  pkt[0] = 0x47;

  ck_assert_int_eq(biss_ca_engine_emm_due(e, 0.0, before, sizeof before, &before_len), 0);
  biss_ca_engine_force_sk_rotation(e);
  biss_ca_engine_scramble_packet(e, 0x0100, 1.0, pkt, noop_emit, NULL);
  ck_assert_int_eq(biss_ca_engine_emm_due(e, 1.0, after, sizeof after, &after_len), 0);
  ck_assert_mem_ne(before, after, before_len < after_len ? before_len : after_len);

  biss_ca_engine_stop(e);
}
END_TEST

static Suite *biss_ca_engine_suite(void) {
  Suite *s = suite_create("biss_ca_engine");
  TCase *tc = tcase_create("core");
  tcase_add_checked_fixture(tc, setup, teardown);
  tcase_add_test(tc, start_loads_receivers_and_stops_cleanly);
  tcase_add_test(tc, start_rejects_empty_receivers_dir);
  tcase_add_test(tc, start_rejects_short_sw_period);
  tcase_add_test(tc, ecm_and_emm_become_due_and_repeat_rate_limited);
  tcase_add_test(tc, sw_rotation_changes_ecm_content);
  tcase_add_test(tc, prog_desc_and_cat_build_nonempty);
  tcase_add_test(tc, reload_detects_added_and_removed_receiver);
  tcase_add_test(tc, force_sk_rotation_makes_next_scramble_rotate);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(biss_ca_engine_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
