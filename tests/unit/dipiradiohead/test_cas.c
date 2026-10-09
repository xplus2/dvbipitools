/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dipiradiohead/cas/cas.h"

START_TEST(ninetyk_to_ms_exact_multiple) {
  uint64_t rem = 0;
  ck_assert_uint_eq(cas_90k_to_ms(90000, &rem), 1000u);
  ck_assert_uint_eq(rem, 0u);
}
END_TEST

START_TEST(ninetyk_to_ms_carries_remainder) {
  uint64_t rem = 0;
  ck_assert_uint_eq(cas_90k_to_ms(2090, &rem), 23u); /* 2090/90 = 23 r 20 */
  ck_assert_uint_eq(rem, 20u);
}
END_TEST

START_TEST(ninetyk_to_ms_no_drift_over_many_calls) {
  uint64_t rem = 0;
  unsigned long total_ms = 0;
  int i;
  /* 90x989 ticks = 89010 = 989ms exactly; drift would show up as leftover rem */
  for (i = 0; i < 90; i++)
    total_ms += cas_90k_to_ms(989, &rem);
  ck_assert_uint_eq(total_ms, 989u);
  ck_assert_uint_eq(rem, 0u);
}
END_TEST

START_TEST(ninetyk_to_ms_zero_delta) {
  uint64_t rem = 42;
  ck_assert_uint_eq(cas_90k_to_ms(0, &rem), 0u);
  ck_assert_uint_eq(rem, 42u);
}
END_TEST

START_TEST(cas_start_rejects_zero_audio_pids) {
  config_t cfg;
  memset(&cfg, 0, sizeof cfg);
  ck_assert_ptr_null(cas_start(&cfg, NULL, 0));
}
END_TEST

START_TEST(cas_start_rejects_too_many_audio_pids) {
  unsigned pids[64];
  config_t cfg;
  size_t i;
  memset(&cfg, 0, sizeof cfg);
  for (i = 0; i < 64; i++)
    pids[i] = 0x0100 + (unsigned)i;
  ck_assert_ptr_null(cas_start(&cfg, pids, 64));
}
END_TEST

#define AUDIO_PID 0x0100u

static void init_cas_cfg(config_t *cfg, unsigned n_vendors) {
  memset(cfg, 0, sizeof *cfg);
  cfg->cas_algo = CAS_ALGO_CSA2;
  cfg->cas_cp_duration_ms = 10000;
  cfg->n_cas_vendors = n_vendors;
  for (unsigned i = 0; i < n_vendors && i < ARGS_MAX_CAS_VENDORS; i++) {
    cas_vendor_t *v = &cfg->cas_vendors[i];

    strcpy(v->ecmg_host, "127.0.0.1");
    v->ecmg_port = 1;
    v->super_cas_id = ((0x4A75u + i) << 16) | 0x0001u;
    v->ecm_id = 1;
    v->ecm_pid = 0x1FF0 + 2 * i;
    v->emm_pid = 0x1FF1 + 2 * i;
  }
}

typedef struct {
  const char *name;
  unsigned n_vendors;
  int biss1;
  int biss2;
  int biss2_ca;
  int started;
} dispatch_case_t;

static const dispatch_case_t dispatch_cases[] = {
    {"biss1 fixed key", 0, 1, 0, 0, 1},
    {"biss2 fixed key", 0, 0, 1, 0, 1},
    {"biss2 ca without key material", 0, 0, 0, 1, 0},
    {"single vendor", 1, 0, 0, 0, 1},
    {"multi vendor", 3, 0, 0, 0, 1},
};

START_TEST(cas_start_dispatches_each_mode_and_round_trips) {
  const dispatch_case_t *c = &dispatch_cases[_i];
  config_t cfg;
  unsigned pids[] = {AUDIO_PID, AUDIO_PID + 1};
  cas_t *cas;

  init_cas_cfg(&cfg, c->n_vendors);
  cfg.biss1_enabled = c->biss1;
  cfg.biss2_enabled = c->biss2;
  cfg.biss2_ca_enabled = c->biss2_ca;
  cfg.biss2_ca_receivers_dir = "/nonexistent/receivers";
  cas = cas_start(&cfg, pids, 2);
  if (!c->started) {
    ck_assert_msg(cas == NULL, "%s: started", c->name);
    return;
  }
  ck_assert_msg(cas != NULL, "%s: not started", c->name);
  ck_assert_msg(cas_vendor_count(cas) == c->n_vendors, "%s: %zu vendors", c->name, cas_vendor_count(cas));
  ck_assert_int_eq(cas_failed(cas), 0);
  cas_stop(cas);
}
END_TEST

static int bind_listener(unsigned *port_out) {
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;
  int fd = socket(AF_INET, SOCK_STREAM, 0);

  ck_assert_int_ge(fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(listen(fd, 1), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  *port_out = ntohs(addr.sin_port);
  return fd;
}

START_TEST(cas_reports_failure_when_the_emmg_port_is_taken) {
  config_t cfg;
  unsigned pids[] = {AUDIO_PID};
  unsigned port;
  int blocker = bind_listener(&port);
  cas_t *c;

  init_cas_cfg(&cfg, 1);
  cfg.cas_vendors[0].emmg_port = port;
  c = cas_start(&cfg, pids, 1);
  ck_assert_ptr_nonnull(c);
  ck_assert_int_eq(cas_failed(c), 0);
  cas_clock_tick(c, 0);
  ck_assert_int_eq(cas_failed(c), 1);
  cas_stop(c);
  close(blocker);
}
END_TEST

START_TEST(clock_tick_is_safe_for_every_engine_and_clock_step) {
  config_t cfg;
  unsigned pids[] = {AUDIO_PID};
  cas_t *c;

  init_cas_cfg(&cfg, 1);
  c = cas_start(&cfg, pids, 1);
  ck_assert_ptr_nonnull(c);
  cas_clock_tick(c, 90000);
  cas_clock_tick(c, 90000 + 2090);
  cas_clock_tick(c, 90000 + 2090);
  cas_clock_tick(c, 0);
  cas_clock_tick(c, 2090);
  ck_assert_int_eq(cas_failed(c), 0);
  cas_stop(c);

  init_cas_cfg(&cfg, 0);
  cfg.biss1_enabled = 1;
  c = cas_start(&cfg, pids, 1);
  ck_assert_ptr_nonnull(c);
  cas_clock_tick(c, 90000);
  cas_clock_tick(c, 180000);
  ck_assert_int_eq(cas_failed(c), 0);
  cas_stop(c);
}
END_TEST

typedef struct {
  unsigned count;
} emit_count_t;

static void count_emit(void *ctx, const unsigned char pkt[188]) {
  (void)pkt;
  ((emit_count_t *)ctx)->count++;
}

START_TEST(flush_and_stop_are_safe_in_every_order) {
  config_t cfg;
  unsigned pids[] = {AUDIO_PID};
  unsigned char pkt[188];
  emit_count_t emitted = {0};
  cas_t *c;

  init_cas_cfg(&cfg, 1);
  cas_flush(NULL, count_emit, &emitted);
  cas_stop(NULL);
  ck_assert_uint_eq(emitted.count, 0u);

  c = cas_start(&cfg, pids, 1);
  ck_assert_ptr_nonnull(c);
  cas_flush(c, count_emit, &emitted);
  cas_flush(c, count_emit, &emitted);
  ck_assert_uint_eq(emitted.count, 0u);

  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = (unsigned char)(AUDIO_PID >> 8);
  pkt[2] = (unsigned char)AUDIO_PID;
  pkt[3] = 0x10;
  for (int i = 0; i < 3; i++) cas_scramble_packet(c, AUDIO_PID, 1.0 + i, pkt, count_emit, &emitted);
  cas_flush(c, count_emit, &emitted);
  cas_flush(c, count_emit, &emitted);
  ck_assert_uint_eq(emitted.count, 3u);
  cas_stop(c);
}
END_TEST

START_TEST(metrics_are_zero_for_missing_or_unstarted_instances) {
  config_t cfg;
  unsigned pids[] = {AUDIO_PID};
  cas_metrics_t m;
  cas_t *c;

  memset(&m, 0xAA, sizeof m);
  cas_get_metrics(NULL, &m);
  ck_assert_uint_eq(m.ecm_total, 0u);
  ck_assert_int_eq(m.ecmg_connected, 0);

  init_cas_cfg(&cfg, 2);
  c = cas_start(&cfg, pids, 1);
  ck_assert_ptr_nonnull(c);
  memset(&m, 0xAA, sizeof m);
  cas_vendor_metrics(c, 1, &m);
  ck_assert_int_eq(m.ecmg_connected, 0);
  ck_assert_uint_eq(m.emm_total, 0u);
  ck_assert_uint_eq(m.ecm_errors_total, 0u);
  memset(&m, 0xAA, sizeof m);
  cas_get_metrics(c, &m);
  ck_assert_uint_eq(m.scrambled_packets_total, 0ULL);
  cas_stop(c);
}
END_TEST

START_TEST(vendor_accessors_and_descriptors_describe_each_vendor) {
  config_t cfg;
  unsigned pids[] = {AUDIO_PID};
  unsigned char out[128];
  unsigned char ecm[64];
  size_t ecm_len = 0;
  size_t prog_len;
  size_t cat_len;
  cas_t *c;

  init_cas_cfg(&cfg, 2);
  c = cas_start(&cfg, pids, 1);
  ck_assert_ptr_nonnull(c);
  ck_assert_uint_eq(cas_vendor_count(c), 2u);
  for (size_t i = 0; i < 2; i++) {
    ck_assert_uint_eq(cas_vendor_ecm_pid(c, i), cfg.cas_vendors[i].ecm_pid);
    ck_assert_uint_eq(cas_vendor_emm_pid(c, i), cfg.cas_vendors[i].emm_pid);
    ck_assert_uint_eq(cas_vendor_super_cas_id(c, i), cfg.cas_vendors[i].super_cas_id);
    ck_assert_int_eq(cas_vendor_ecm_due(c, i, 1.0, ecm, sizeof ecm, &ecm_len), -1);
    ck_assert_int_eq(cas_vendor_next_emm(c, i, ecm, sizeof ecm, &ecm_len), -1);
  }

  prog_len = cas_prog_desc(c, out, sizeof out);
  ck_assert_uint_gt(prog_len, 12u);
  for (size_t i = 0; i < 2; i++) {
    const unsigned char *d = out + i * 6;

    ck_assert_uint_eq(d[0], 0x09);
    ck_assert_uint_eq(d[1], 4u);
    ck_assert_uint_eq(((unsigned)d[2] << 8) | d[3], cfg.cas_vendors[i].super_cas_id >> 16);
    ck_assert_uint_eq((((unsigned)d[4] & 0x1F) << 8) | d[5], cfg.cas_vendors[i].ecm_pid);
  }
  ck_assert_uint_eq(out[12], 0x65);
  for (size_t cap = 0; cap < prog_len; cap++) ck_assert_uint_eq(cas_prog_desc(c, out, cap), 0u);

  cat_len = cas_build_cat(c, out, sizeof out);
  ck_assert_uint_gt(cat_len, 0u);
  ck_assert_uint_eq(out[0], 0x01);
  for (size_t cap = 0; cap < cat_len; cap++) ck_assert_uint_eq(cas_build_cat(c, out, cap), 0u);
  cas_stop(c);
}
END_TEST

START_TEST(reload_receivers_is_a_noop_outside_biss_ca_mode) {
  config_t cfg;
  unsigned pids[] = {AUDIO_PID};
  cas_t *c;

  cas_reload_receivers(NULL);
  init_cas_cfg(&cfg, 1);
  c = cas_start(&cfg, pids, 1);
  ck_assert_ptr_nonnull(c);
  cas_reload_receivers(c);
  ck_assert_int_eq(cas_failed(c), 0);
  cas_stop(c);
}
END_TEST

static Suite *cas_suite(void) {
  Suite *s = suite_create("cas");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, ninetyk_to_ms_exact_multiple);
  tcase_add_test(tc, ninetyk_to_ms_carries_remainder);
  tcase_add_test(tc, ninetyk_to_ms_no_drift_over_many_calls);
  tcase_add_test(tc, ninetyk_to_ms_zero_delta);
  tcase_add_test(tc, cas_start_rejects_zero_audio_pids);
  tcase_add_test(tc, cas_start_rejects_too_many_audio_pids);
  tcase_add_loop_test(tc, cas_start_dispatches_each_mode_and_round_trips, 0, (int)(sizeof dispatch_cases / sizeof dispatch_cases[0]));
  tcase_add_test(tc, cas_reports_failure_when_the_emmg_port_is_taken);
  tcase_add_test(tc, clock_tick_is_safe_for_every_engine_and_clock_step);
  tcase_add_test(tc, flush_and_stop_are_safe_in_every_order);
  tcase_add_test(tc, metrics_are_zero_for_missing_or_unstarted_instances);
  tcase_add_test(tc, vendor_accessors_and_descriptors_describe_each_vendor);
  tcase_add_test(tc, reload_receivers_is_a_noop_outside_biss_ca_mode);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(cas_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
