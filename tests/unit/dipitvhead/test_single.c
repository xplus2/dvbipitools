/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <arpa/inet.h>
#include <check.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "lib/sys/signal.h"
#include "lib/metrics/export.h"

#include "dipitvhead/tvhead/priv.h"

#include "psi_fixture.h"
#include "stderr_capture.h"

static double now_s(void) {
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static unsigned free_udp_port(void) {
  struct sockaddr_in addr;
  socklen_t alen = sizeof addr;
  int fd = socket(AF_INET, SOCK_DGRAM, 0);
  unsigned port;

  ck_assert_int_ge(fd, 0);
  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  ck_assert_int_eq(bind(fd, (struct sockaddr *)&addr, sizeof addr), 0);
  ck_assert_int_eq(getsockname(fd, (struct sockaddr *)&addr, &alen), 0);
  port = ntohs(addr.sin_port);
  close(fd);
  return port;
}

static void init_single_cfg(config_t *cfg) {
  memset(cfg, 0, sizeof *cfg);
  cfg->n_inputs = 1;
  cfg->inputs[0].sid = 1;
  cfg->inputs[0].sdt_mode = TABLE_DROP;
}

static void set_unreachable_http_input(config_t *cfg) {
  cfg->inputs[0].input.kind = SRC_HTTP;
  strcpy(cfg->inputs[0].input.http.host, "127.0.0.1");
  cfg->inputs[0].input.http.port = 1;
  strcpy(cfg->inputs[0].input.http.path, "/");
}

static void init_disabled_exporter(metrics_exporter_t *mx) {
  memset(mx, 0, sizeof *mx);
  metrics_exporter_init(mx, METRICS_COMPONENT_TVHEAD, NULL, NULL, 0.0);
}

START_TEST(input_open_failure_without_retry_ends_the_run) {
  config_t cfg;
  metrics_exporter_t mx;
  double start = now_s();

  init_single_cfg(&cfg);
  set_unreachable_http_input(&cfg);
  cfg.error_retry_s = 0;
  init_disabled_exporter(&mx);
  ck_assert_int_eq(tvhead_run_single(&cfg, &mx), 1);
  ck_assert_msg(now_s() - start < 2.0, "run did not end promptly");
}
END_TEST

typedef struct {
  double delay_s;
} stopper_t;

static void *stop_after_delay(void *arg) {
  const stopper_t *s = arg;
  struct timespec ts = {(time_t)s->delay_s, (long)((s->delay_s - (double)(time_t)s->delay_s) * 1e9)};

  nanosleep(&ts, NULL);
  kill(getpid(), SIGINT);
  return NULL;
}

START_TEST(input_open_failure_with_retry_keeps_trying_until_stopped) {
  config_t cfg;
  metrics_exporter_t mx;
  stopper_t stopper = {0.4};
  pthread_t th;
  capture_t cap;
  char log[4096];
  double start;
  int rc;

  init_single_cfg(&cfg);
  set_unreachable_http_input(&cfg);
  cfg.error_retry_s = 1;
  init_disabled_exporter(&mx);
  signals_install();
  ck_assert_int_eq(pthread_create(&th, NULL, stop_after_delay, &stopper), 0);
  capture_begin(&cap);
  start = now_s();
  rc = tvhead_run_single(&cfg, &mx);
  capture_end(&cap, log, sizeof log);
  pthread_join(th, NULL);
  ck_assert_int_eq(rc, 0);
  ck_assert_msg(now_s() - start >= 0.3, "run ended before the stop request");
  ck_assert_ptr_nonnull(strstr(log, "input error, retrying in 1s"));
}
END_TEST

typedef struct {
  const char *group;
  unsigned port;
  atomic_int stop;
  unsigned char pkts[2][188];
} announcer_t;

static void *announce_pat_pmt(void *arg) {
  const announcer_t *a = arg;
  mcast_t *m = mcast_open_send(AF_INET, a->group, a->port, NULL, 1);

  if (!m) return NULL;
  while (!atomic_load(&a->stop)) {
    struct timespec ts = {0, 40 * 1000000L};

    mcast_send(m, a->pkts[0], 188);
    mcast_send(m, a->pkts[1], 188);
    nanosleep(&ts, NULL);
  }
  mcast_close(m);
  return NULL;
}

START_TEST(cas_setup_failure_skips_the_output_loop) {
  config_t cfg;
  metrics_exporter_t mx;
  announcer_t ann;
  pthread_t th;
  capture_t cap;
  char log[8192];
  int rc;

  init_single_cfg(&cfg);
  cfg.inputs[0].input.kind = SRC_UDP;
  cfg.inputs[0].input.family = AF_INET;
  strcpy(cfg.inputs[0].input.group, "239.7.9.72");
  cfg.inputs[0].input.port = free_udp_port();
  cfg.error_retry_s = 0;
  cfg.cas_algo = CAS_ALGO_CSA2;
  cfg.cas_cp_duration_ms = 10000;
  init_disabled_exporter(&mx);

  memset(&ann, 0, sizeof ann);
  ann.group = cfg.inputs[0].input.group;
  ann.port = cfg.inputs[0].input.port;
  fixture_pat_pmt_packets(0x0100, ann.pkts);
  ck_assert_int_eq(pthread_create(&th, NULL, announce_pat_pmt, &ann), 0);
  capture_begin(&cap);
  rc = tvhead_run_single(&cfg, &mx);
  capture_end(&cap, log, sizeof log);
  atomic_store(&ann.stop, 1);
  pthread_join(th, NULL);
  ck_assert_int_eq(rc, 1);
  ck_assert_ptr_nonnull(strstr(log, "cas setup failed"));
  ck_assert_ptr_null(strstr(log, "no live PMT found"));
}
END_TEST

static void init_csa_cfg(config_t *cfg, pcr_mode_t mode) {
  init_single_cfg(cfg);
  cfg->pcr_mode = mode;
  cfg->cas_algo = CAS_ALGO_CSA2;
  cfg->cas_cp_duration_ms = 10000;
  cfg->cas_pid_count = 1;
  cfg->cas_pids[0] = 0x0100;
  cfg->n_cas_vendors = 1;
  strcpy(cfg->cas_vendors[0].ecmg_host, "127.0.0.1");
  cfg->cas_vendors[0].ecmg_port = 1;
  cfg->cas_vendors[0].super_cas_id = 0x4A750001u;
  cfg->cas_vendors[0].ecm_id = 1;
  cfg->cas_vendors[0].ecm_pid = 0x1FF0;
  cfg->cas_vendors[0].emm_pid = 0x1FF1;
}

static remux_t *single_remux(const config_t *cfg, unsigned src_pcr_pid, psi_t **psi_out) {
  unsigned char pkts[2][188];
  out_program_pids_t pids;
  remux_t *r;

  *psi_out = psi_new();
  fixture_programme_packets(1, src_pcr_pid, 0x1B, 0x0100, pkts);
  psi_feed(*psi_out, pkts[0]);
  psi_feed(*psi_out, pkts[1]);
  out_program_pids(0, &pids);
  r = remux_new(cfg, &cfg->inputs[0], *psi_out, &pids, 1);
  ck_assert_ptr_nonnull(r);
  return r;
}

START_TEST(cas_follows_the_source_pcr_unless_the_pcr_is_regenerated) {
  config_t cfg;
  psi_t *psi;
  remux_t *r;
  cas_t *c;

  init_csa_cfg(&cfg, PCR_MODE_PRESERVE);
  r = single_remux(&cfg, 0x0100, &psi);
  c = tvhead_single_cas_start(&cfg, psi, r);
  ck_assert_ptr_nonnull(c);
  ck_assert_uint_eq(cas_pcr_pid(c), remux_pcr_pid_out(r));
  cas_stop(c);
  remux_free(r);
  psi_free(psi);

  init_csa_cfg(&cfg, PCR_MODE_REBASE);
  r = single_remux(&cfg, 0x0100, &psi);
  c = tvhead_single_cas_start(&cfg, psi, r);
  ck_assert_ptr_nonnull(c);
  ck_assert_uint_eq(cas_pcr_pid(c), remux_pcr_pid_out(r));
  cas_stop(c);
  remux_free(r);
  psi_free(psi);

  init_csa_cfg(&cfg, PCR_MODE_REGENERATE);
  r = single_remux(&cfg, 0x0100, &psi);
  c = tvhead_single_cas_start(&cfg, psi, r);
  ck_assert_ptr_nonnull(c);
  ck_assert_uint_eq(cas_pcr_pid(c), 0x1FFFu);
  cas_stop(c);
  remux_free(r);
  psi_free(psi);
}
END_TEST

START_TEST(regenerate_starts_the_cas_even_when_the_source_declares_no_pcr_pid) {
  config_t cfg;
  psi_t *psi;
  remux_t *r;
  cas_t *c;

  init_csa_cfg(&cfg, PCR_MODE_PRESERVE);
  r = single_remux(&cfg, 0x1FFF, &psi);
  ck_assert_ptr_null(tvhead_single_cas_start(&cfg, psi, r));
  remux_free(r);
  psi_free(psi);

  init_csa_cfg(&cfg, PCR_MODE_REGENERATE);
  r = single_remux(&cfg, 0x1FFF, &psi);
  c = tvhead_single_cas_start(&cfg, psi, r);
  ck_assert_ptr_nonnull(c);
  ck_assert_int_eq(cas_failed(c), 0);
  cas_stop(c);
  remux_free(r);
  psi_free(psi);
}
END_TEST

static Suite *single_suite(void) {
  Suite *s = suite_create("dipitvhead_single");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 20);
  tcase_add_test(tc, input_open_failure_without_retry_ends_the_run);
  tcase_add_test(tc, input_open_failure_with_retry_keeps_trying_until_stopped);
  tcase_add_test(tc, cas_setup_failure_skips_the_output_loop);
  tcase_add_test(tc, cas_follows_the_source_pcr_unless_the_pcr_is_regenerated);
  tcase_add_test(tc, regenerate_starts_the_cas_even_when_the_source_declares_no_pcr_pid);
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(single_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
