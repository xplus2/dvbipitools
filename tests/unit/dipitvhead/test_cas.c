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

#include "dipitvhead/cas/cas.h"

#include "lib/scrambler/cissa.h"
#include "lib/scrambler/csa2.h"
#include "psi_fixture.h"
#include "stderr_capture.h"

static void build_pcr_packet(unsigned char pkt[188], int with_pcr, uint64_t base, unsigned ext) {
  memset(pkt, 0xFF, 188);
  pkt[0] = 0x47;
  pkt[1] = 0x00;
  pkt[2] = 0x40;
  if (!with_pcr) {
    pkt[3] = 0x10; /* payload only, no adaptation field */
    return;
  }
  pkt[3] = 0x30; /* adaptation field + payload */
  pkt[4] = 183;  /* adaptation_field_length: room for flags+PCR+stuffing */
  pkt[5] = 0x10; /* PCR_flag */
  pkt[6] = (unsigned char)(base >> 25);
  pkt[7] = (unsigned char)(base >> 17);
  pkt[8] = (unsigned char)(base >> 9);
  pkt[9] = (unsigned char)(base >> 1);
  pkt[10] = (unsigned char)(((base & 1) << 7) | 0x7E | ((ext >> 8) & 1));
  pkt[11] = (unsigned char)ext;
}

START_TEST(parse_pcr_recovers_known_value) {
  unsigned char pkt[188];
  uint64_t pcr27;
  build_pcr_packet(pkt, 1, 12345678ULL, 150);
  ck_assert_int_eq(cas_parse_pcr(pkt, &pcr27), 1);
  ck_assert_uint_eq(pcr27, 12345678ULL * 300 + 150);
}
END_TEST

START_TEST(parse_pcr_zero_extension) {
  unsigned char pkt[188];
  uint64_t pcr27;
  build_pcr_packet(pkt, 1, 1, 0);
  ck_assert_int_eq(cas_parse_pcr(pkt, &pcr27), 1);
  ck_assert_uint_eq(pcr27, 300ULL);
}
END_TEST

START_TEST(parse_pcr_no_adaptation_field) {
  unsigned char pkt[188];
  uint64_t pcr27;
  build_pcr_packet(pkt, 0, 0, 0);
  ck_assert_int_eq(cas_parse_pcr(pkt, &pcr27), 0);
}
END_TEST

START_TEST(parse_pcr_flag_not_set) {
  unsigned char pkt[188];
  uint64_t pcr27;
  build_pcr_packet(pkt, 1, 999, 1);
  pkt[5] = 0x00; /* clear PCR_flag */
  ck_assert_int_eq(cas_parse_pcr(pkt, &pcr27), 0);
}
END_TEST

START_TEST(parse_pcr_adaptation_field_too_short) {
  unsigned char pkt[188];
  uint64_t pcr27;
  build_pcr_packet(pkt, 1, 999, 1);
  pkt[4] = 1; /* only the flags byte, no room for the 6-byte PCR field */
  ck_assert_int_eq(cas_parse_pcr(pkt, &pcr27), 0);
}
END_TEST

START_TEST(pcr_plausible_normal_delta_accepted) {
  double delta_s;
  uint64_t last27 = 27000000ULL * 10; /* 10s */
  uint64_t new27 = 27000000ULL * 11;  /* 11s: 1s later */
  ck_assert_int_eq(cas_pcr_plausible(last27, new27, 1.0, &delta_s), 1);
  ck_assert_double_eq_tol(delta_s, 1.0, 0.01);
}
END_TEST

START_TEST(pcr_plausible_wraparound_accepted) {
  double delta_s;
  uint64_t last27 = CAS_PCR_MODULUS - 27000000ULL; /* 1s before wrap */
  uint64_t new27 = 27000000ULL;                    /* 1s after wrap: 2s total elapsed */
  ck_assert_int_eq(cas_pcr_plausible(last27, new27, 2.0, &delta_s), 1);
  ck_assert_double_eq_tol(delta_s, 2.0, 0.01);
}
END_TEST

START_TEST(pcr_plausible_rejects_huge_discontinuity) {
  double delta_s;
  uint64_t last27 = 27000000ULL * 10;
  uint64_t new27 = 27000000ULL * 10000; /* implies ~9990s elapsed */
  ck_assert_int_eq(cas_pcr_plausible(last27, new27, 1.0, &delta_s), 0);
}
END_TEST

START_TEST(pcr_plausible_rejects_backward_jump) {
  double delta_s;
  uint64_t last27 = 27000000ULL * 100;
  uint64_t new27 = 27000000ULL * 50; /* wraparound-"corrects" to a huge forward delta */
  ck_assert_int_eq(cas_pcr_plausible(last27, new27, 1.0, &delta_s), 0);
}
END_TEST

START_TEST(pcr_plausible_rejects_zero_wall_delta) {
  double delta_s;
  uint64_t last27 = 27000000ULL * 10;
  uint64_t new27 = 27000000ULL * 11;
  ck_assert_int_eq(cas_pcr_plausible(last27, new27, 0.0, &delta_s), 0);
}
END_TEST

static void set_es(out_es_t *es, psi_es_t *psi_es, unsigned out_pid, pid_class_t cls) {
  memset(psi_es, 0, sizeof *psi_es);
  psi_es->cls = cls;
  es->out_pid = out_pid;
  es->src = psi_es;
}

START_TEST(resolve_pids_explicit_only) {
  config_t cfg;
  unsigned out[16];
  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pid_count = 2;
  cfg.cas_pids[0] = 0x0104;
  cfg.cas_pids[1] = 0x0106;
  ck_assert_uint_eq(cas_resolve_pids(&cfg, NULL, 0, out, 16), 2);
  ck_assert_uint_eq(out[0], 0x0104);
  ck_assert_uint_eq(out[1], 0x0106);
}
END_TEST

START_TEST(resolve_pids_video_keyword) {
  config_t cfg;
  psi_es_t pe[2];
  out_es_t es[2];
  unsigned out[16];
  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pids_video = 1;
  set_es(&es[0], &pe[0], 0x0100, PID_VIDEO);
  set_es(&es[1], &pe[1], 0x0101, PID_AUDIO);
  ck_assert_uint_eq(cas_resolve_pids(&cfg, es, 2, out, 16), 1);
  ck_assert_uint_eq(out[0], 0x0100);
}
END_TEST

START_TEST(resolve_pids_audio_keyword_multiple) {
  config_t cfg;
  psi_es_t pe[3];
  out_es_t es[3];
  unsigned out[16];
  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pids_audio = 1;
  set_es(&es[0], &pe[0], 0x0100, PID_VIDEO);
  set_es(&es[1], &pe[1], 0x0101, PID_AUDIO);
  set_es(&es[2], &pe[2], 0x0102, PID_AUDIO);
  ck_assert_uint_eq(cas_resolve_pids(&cfg, es, 3, out, 16), 2);
  ck_assert_uint_eq(out[0], 0x0101);
  ck_assert_uint_eq(out[1], 0x0102);
}
END_TEST

START_TEST(resolve_pids_lcevc_keyword_not_implied_by_video_or_audio) {
  config_t cfg;
  psi_es_t pe[3];
  out_es_t es[3];
  unsigned out[16];
  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pids_video = 1;
  cfg.cas_pids_audio = 1;
  set_es(&es[0], &pe[0], 0x0100, PID_VIDEO);
  set_es(&es[1], &pe[1], 0x0101, PID_AUDIO);
  set_es(&es[2], &pe[2], 0x0102, PID_LCEVC);
  ck_assert_uint_eq(cas_resolve_pids(&cfg, es, 3, out, 16), 2);
  ck_assert_uint_eq(out[0], 0x0100);
  ck_assert_uint_eq(out[1], 0x0101);
}
END_TEST

START_TEST(resolve_pids_lcevc_keyword) {
  config_t cfg;
  psi_es_t pe[3];
  out_es_t es[3];
  unsigned out[16];
  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pids_lcevc = 1;
  set_es(&es[0], &pe[0], 0x0100, PID_VIDEO);
  set_es(&es[1], &pe[1], 0x0101, PID_AUDIO);
  set_es(&es[2], &pe[2], 0x0102, PID_LCEVC);
  ck_assert_uint_eq(cas_resolve_pids(&cfg, es, 3, out, 16), 1);
  ck_assert_uint_eq(out[0], 0x0102);
}
END_TEST

START_TEST(resolve_pids_mixed_explicit_and_keyword_dedupes) {
  config_t cfg;
  psi_es_t pe[2];
  out_es_t es[2];
  unsigned out[16];
  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pid_count = 1;
  cfg.cas_pids[0] = 0x0100; /* same as the video out_pid below */
  cfg.cas_pids_video = 1;
  set_es(&es[0], &pe[0], 0x0100, PID_VIDEO);
  set_es(&es[1], &pe[1], 0x0101, PID_AUDIO);
  ck_assert_uint_eq(cas_resolve_pids(&cfg, es, 2, out, 16), 1);
  ck_assert_uint_eq(out[0], 0x0100);
}
END_TEST

START_TEST(resolve_pids_default_video_and_audio) {
  config_t cfg;
  psi_es_t pe[2];
  out_es_t es[2];
  unsigned out[16];
  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pids_video = 1;
  cfg.cas_pids_audio = 1;
  set_es(&es[0], &pe[0], 0x0100, PID_VIDEO);
  set_es(&es[1], &pe[1], 0x0101, PID_AUDIO);
  ck_assert_uint_eq(cas_resolve_pids(&cfg, es, 2, out, 16), 2);
  ck_assert_uint_eq(out[0], 0x0100);
  ck_assert_uint_eq(out[1], 0x0101);
}
END_TEST

START_TEST(resolve_pids_caps_at_limit) {
  config_t cfg;
  psi_es_t pe[3];
  out_es_t es[3];
  unsigned out[2];
  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pids_audio = 1;
  set_es(&es[0], &pe[0], 0x0101, PID_AUDIO);
  set_es(&es[1], &pe[1], 0x0102, PID_AUDIO);
  set_es(&es[2], &pe[2], 0x0103, PID_AUDIO);
  ck_assert_uint_eq(cas_resolve_pids(&cfg, es, 3, out, 2), 2);
}
END_TEST

START_TEST(resolve_pids_nothing_requested_yields_none) {
  config_t cfg;
  unsigned out[16];
  memset(&cfg, 0, sizeof cfg);
  ck_assert_uint_eq(cas_resolve_pids(&cfg, NULL, 0, out, 16), 0);
}
END_TEST

START_TEST(resolve_pids_exceeds_old_16_limit) {
  config_t cfg;
  psi_es_t pe[20];
  out_es_t es[20];
  unsigned out[CAS_CORE_MAX_PIDS];
  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pids_audio = 1;
  for (int i = 0; i < 20; i++)
    set_es(&es[i], &pe[i], (unsigned)(0x0100 + i), PID_AUDIO);
  ck_assert_uint_eq(cas_resolve_pids(&cfg, es, 20, out, CAS_CORE_MAX_PIDS), 20);
}
END_TEST

START_TEST(resolve_pids_multi_across_programs) {
  config_t cfg;
  psi_es_t pe0[2];
  psi_es_t pe1[2];
  out_es_t es0[2];
  out_es_t es1[2];
  const out_es_t *es_lists[2];
  int es_counts[2];
  unsigned out[16];

  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pids_video = 1;
  cfg.cas_pids_audio = 1;
  set_es(&es0[0], &pe0[0], 0x1100, PID_VIDEO);
  set_es(&es0[1], &pe0[1], 0x1101, PID_AUDIO);
  set_es(&es1[0], &pe1[0], 0x1200, PID_VIDEO);
  set_es(&es1[1], &pe1[1], 0x1201, PID_AUDIO);
  es_lists[0] = es0;
  es_lists[1] = es1;
  es_counts[0] = 2;
  es_counts[1] = 2;

  ck_assert_uint_eq(cas_resolve_pids_multi(&cfg, es_lists, es_counts, 2, out, 16), 4);
  ck_assert_uint_eq(out[0], 0x1100u);
  ck_assert_uint_eq(out[1], 0x1101u);
  ck_assert_uint_eq(out[2], 0x1200u);
  ck_assert_uint_eq(out[3], 0x1201u);
}
END_TEST

START_TEST(resolve_pids_multi_dedupes_across_programs) {
  config_t cfg;
  psi_es_t pe0[1];
  psi_es_t pe1[1];
  out_es_t es0[1];
  out_es_t es1[1];
  const out_es_t *es_lists[2];
  int es_counts[2];
  unsigned out[16];

  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pid_count = 1;
  cfg.cas_pids[0] = 0x1300; /* same pid program 1 also resolves via --cas-pids-video */
  cfg.cas_pids_video = 1;
  set_es(&es0[0], &pe0[0], 0x1400, PID_VIDEO);
  set_es(&es1[0], &pe1[0], 0x1300, PID_VIDEO);
  es_lists[0] = es0;
  es_lists[1] = es1;
  es_counts[0] = 1;
  es_counts[1] = 1;

  ck_assert_uint_eq(cas_resolve_pids_multi(&cfg, es_lists, es_counts, 2, out, 16), 2);
  ck_assert_uint_eq(out[0], 0x1300u);
  ck_assert_uint_eq(out[1], 0x1400u);
}
END_TEST

START_TEST(resolve_pids_multi_caps_across_programs) {
  config_t cfg;
  psi_es_t pe0[3];
  psi_es_t pe1[3];
  out_es_t es0[3];
  out_es_t es1[3];
  const out_es_t *es_lists[2];
  int es_counts[2];
  unsigned out[4];

  memset(&cfg, 0, sizeof cfg);
  cfg.cas_pids_audio = 1;
  for (int i = 0; i < 3; i++) {
    set_es(&es0[i], &pe0[i], (unsigned)(0x1500 + i), PID_AUDIO);
    set_es(&es1[i], &pe1[i], (unsigned)(0x1600 + i), PID_AUDIO);
  }
  es_lists[0] = es0;
  es_lists[1] = es1;
  es_counts[0] = 3;
  es_counts[1] = 3;

  ck_assert_uint_eq(cas_resolve_pids_multi(&cfg, es_lists, es_counts, 2, out, 4), 4);
}
END_TEST

static void init_cas_cfg(config_t *cfg, unsigned n_vendors) {
  memset(cfg, 0, sizeof *cfg);
  cfg->cas_algo = CAS_ALGO_CSA2;
  cfg->cas_cp_duration_ms = 10000;
  cfg->cas_pid_count = 1;
  cfg->cas_pids[0] = 0x0100;
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
  unsigned cp_duration_ms;
  size_t pid_count;
} bad_group_case_t;

static const bad_group_case_t bad_group_cases[] = {
    {"zero crypto period", 0, 1},
    {"no pids to scramble", 10000, 0},
};

START_TEST(start_multi_rejects_unusable_group_configuration) {
  const bad_group_case_t *c = &bad_group_cases[_i];
  config_t cfg;
  psi_es_t pe;
  out_es_t es;
  const out_es_t *lists[1];
  int counts[1] = {1};

  init_cas_cfg(&cfg, 2);
  cfg.cas_cp_duration_ms = c->cp_duration_ms;
  cfg.cas_pid_count = c->pid_count;
  set_es(&es, &pe, 0x0100, PID_VIDEO);
  lists[0] = &es;
  ck_assert_msg(cas_start_multi(&cfg, lists, counts, 1) == NULL, "%s: accepted", c->name);
}
END_TEST

START_TEST(start_multi_limits_started_vendors_to_the_group_maximum) {
  config_t cfg;
  psi_es_t pe;
  out_es_t es;
  const out_es_t *lists[1];
  int counts[1] = {1};
  cas_t *c;

  init_cas_cfg(&cfg, ARGS_MAX_CAS_VENDORS + 1);
  set_es(&es, &pe, 0x0100, PID_VIDEO);
  lists[0] = &es;
  c = cas_start_multi(&cfg, lists, counts, 1);
  ck_assert_ptr_nonnull(c);
  ck_assert_uint_eq(cas_vendor_count(c), (size_t)ARGS_MAX_CAS_VENDORS);
  cas_stop(c);
}
END_TEST

START_TEST(start_single_requires_a_pcr_and_resolvable_pids) {
  config_t cfg;
  psi_es_t pe;
  out_es_t es;
  psi_t *no_pcr = build_psi_with_pcr(0x1FFF);
  psi_t *with_pcr = build_psi_with_pcr(0x0100);
  cas_t *c;

  init_cas_cfg(&cfg, 1);
  set_es(&es, &pe, 0x0100, PID_VIDEO);
  ck_assert_ptr_null(cas_start(&cfg, no_pcr, &es, 1, 0x0100));

  cfg.cas_pid_count = 0;
  ck_assert_ptr_null(cas_start(&cfg, with_pcr, &es, 1, 0x0100));

  cfg.cas_pid_count = 1;
  c = cas_start(&cfg, with_pcr, &es, 1, 0x0100);
  ck_assert_ptr_nonnull(c);
  ck_assert_uint_eq(cas_pcr_pid(c), 0x0100u);
  ck_assert_int_eq(cas_failed(c), 0);
  cas_stop(c);
  psi_free(no_pcr);
  psi_free(with_pcr);
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
  psi_es_t pe;
  out_es_t es;
  const out_es_t *lists[1];
  int counts[1] = {1};
  unsigned port;
  int blocker = bind_listener(&port);
  cas_t *c;

  init_cas_cfg(&cfg, 1);
  cfg.cas_vendors[0].emmg_port = port;
  set_es(&es, &pe, 0x0100, PID_VIDEO);
  lists[0] = &es;
  c = cas_start_multi(&cfg, lists, counts, 1);
  ck_assert_ptr_nonnull(c);
  ck_assert_int_eq(cas_failed(c), 0);
  cas_wall_tick(c, 1.0);
  ck_assert_int_eq(cas_failed(c), 1);
  cas_stop(c);
  close(blocker);
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
  psi_es_t pe;
  out_es_t es;
  const out_es_t *lists[1];
  int counts[1] = {1};
  unsigned char pkt[188];
  emit_count_t emitted = {0};
  cas_t *c;

  init_cas_cfg(&cfg, 1);
  set_es(&es, &pe, 0x0100, PID_VIDEO);
  lists[0] = &es;
  cas_flush(NULL, count_emit, &emitted);
  cas_stop(NULL);
  ck_assert_uint_eq(emitted.count, 0u);

  c = cas_start_multi(&cfg, lists, counts, 1);
  ck_assert_ptr_nonnull(c);
  cas_flush(c, count_emit, &emitted);
  cas_flush(c, count_emit, &emitted);
  ck_assert_uint_eq(emitted.count, 0u);

  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = 0x01;
  pkt[2] = 0x00;
  pkt[3] = 0x10;
  for (int i = 0; i < 3; i++) cas_scramble_packet(c, 0x0100, 1.0 + i, pkt, count_emit, &emitted);
  cas_flush(c, count_emit, &emitted);
  cas_flush(c, count_emit, &emitted);
  ck_assert_uint_eq(emitted.count, 3u);
  cas_stop(c);
}
END_TEST

START_TEST(add_pids_extends_the_scrambled_set_at_runtime) {
  config_t cfg;
  psi_es_t pe;
  out_es_t es;
  const out_es_t *lists[1];
  int counts[1] = {1};
  unsigned extra[2] = {0x0200, 0x0100};
  unsigned char pkt[188];
  emit_count_t emitted = {0};
  cas_metrics_t before;
  cas_metrics_t mid;
  cas_metrics_t after;
  cas_t *c;

  init_cas_cfg(&cfg, 1);
  set_es(&es, &pe, 0x0100, PID_VIDEO);
  lists[0] = &es;
  c = cas_start_multi(&cfg, lists, counts, 1);
  ck_assert_ptr_nonnull(c);

  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = 0x02;
  pkt[2] = 0x00;
  pkt[3] = 0x10;
  cas_get_metrics(c, &before);
  cas_scramble_packet(c, 0x0200, 1.0, pkt, count_emit, &emitted);
  cas_get_metrics(c, &mid);
  ck_assert_uint_eq(mid.unexpected_clear_packets_total, before.unexpected_clear_packets_total);

  ck_assert_uint_eq(cas_add_pids(c, extra, 2), 0u);
  cas_scramble_packet(c, 0x0200, 2.0, pkt, count_emit, &emitted);
  cas_get_metrics(c, &after);
  ck_assert_uint_gt(after.unexpected_clear_packets_total, mid.unexpected_clear_packets_total);
  cas_flush(c, count_emit, &emitted);
  cas_stop(c);
}
END_TEST

START_TEST(add_pids_reports_pids_that_do_not_fit) {
  config_t cfg;
  psi_es_t pe;
  out_es_t es;
  const out_es_t *lists[1];
  int counts[1] = {1};
  unsigned pids[CAS_CORE_MAX_PIDS + 2];
  cas_t *c;

  init_cas_cfg(&cfg, 1);
  set_es(&es, &pe, 0x0100, PID_VIDEO);
  lists[0] = &es;
  c = cas_start_multi(&cfg, lists, counts, 1);
  ck_assert_ptr_nonnull(c);
  for (unsigned i = 0; i < CAS_CORE_MAX_PIDS + 2; i++) pids[i] = 0x0300 + i;
  ck_assert_uint_eq(cas_add_pids(c, pids, CAS_CORE_MAX_PIDS + 2), 3u);
  cas_stop(c);
}
END_TEST

START_TEST(metrics_are_zero_for_missing_or_unstarted_instances) {
  config_t cfg;
  psi_es_t pe;
  out_es_t es;
  const out_es_t *lists[1];
  int counts[1] = {1};
  cas_metrics_t m;
  cas_t *c;

  memset(&m, 0xAA, sizeof m);
  cas_get_metrics(NULL, &m);
  ck_assert_uint_eq(m.ecm_total, 0u);
  ck_assert_int_eq(m.ecmg_connected, 0);

  init_cas_cfg(&cfg, 2);
  set_es(&es, &pe, 0x0100, PID_VIDEO);
  lists[0] = &es;
  c = cas_start_multi(&cfg, lists, counts, 1);
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
  psi_es_t pe;
  out_es_t es;
  const out_es_t *lists[1];
  int counts[1] = {1};
  unsigned char out[128];
  unsigned char ecm[64];
  size_t ecm_len = 0;
  size_t prog_len;
  size_t cat_len;
  cas_t *c;

  init_cas_cfg(&cfg, 2);
  set_es(&es, &pe, 0x0100, PID_VIDEO);
  lists[0] = &es;
  c = cas_start_multi(&cfg, lists, counts, 1);
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
  psi_es_t pe;
  out_es_t es;
  const out_es_t *lists[1];
  int counts[1] = {1};
  cas_t *c;

  cas_reload_receivers(NULL);
  init_cas_cfg(&cfg, 1);
  set_es(&es, &pe, 0x0100, PID_VIDEO);
  lists[0] = &es;
  c = cas_start_multi(&cfg, lists, counts, 1);
  ck_assert_ptr_nonnull(c);
  cas_reload_receivers(c);
  ck_assert_int_eq(cas_failed(c), 0);
  cas_stop(c);
}
END_TEST

static size_t count_substr(const char *hay, const char *needle) {
  size_t n = 0;
  size_t step = strlen(needle);

  for (const char *p = strstr(hay, needle); p; p = strstr(p + step, needle)) n++;
  return n;
}

START_TEST(pcr_ticks_start_the_clock_and_flag_discontinuities) {
  config_t cfg;
  psi_es_t pe;
  out_es_t es;
  psi_t *with_pcr = build_psi_with_pcr(0x0100);
  unsigned char no_pcr[188];
  unsigned char first[188];
  unsigned char next[188];
  unsigned char jump[188];
  capture_t cap;
  char log[4096];
  cas_t *c;

  init_cas_cfg(&cfg, 1);
  set_es(&es, &pe, 0x0100, PID_VIDEO);
  build_pcr_packet(no_pcr, 0, 0, 0);
  build_pcr_packet(first, 1, 1000000, 0);
  build_pcr_packet(next, 1, 1009000, 0);
  build_pcr_packet(jump, 1, 1009000 + 90000ULL * 500, 0);
  c = cas_start(&cfg, with_pcr, &es, 1, 0x0100);
  ck_assert_ptr_nonnull(c);

  capture_begin(&cap);
  cas_pcr_tick(c, 0x0200, first);
  cas_pcr_tick(c, 0x0100, no_pcr);
  cas_pcr_tick(c, 0x0100, first);
  cas_pcr_tick(c, 0x0100, next);
  cas_pcr_tick(c, 0x0100, jump);
  capture_end(&cap, log, sizeof log);
  ck_assert_uint_eq(count_substr(log, "first PCR observed"), 1u);
  ck_assert_uint_eq(count_substr(log, "PCR discontinuity"), 1u);
  ck_assert_int_eq(cas_failed(c), 0);
  cas_stop(c);
  psi_free(with_pcr);
}
END_TEST

START_TEST(wall_ticks_advance_the_clock_only_forward) {
  config_t cfg;
  psi_es_t pe;
  out_es_t es;
  const out_es_t *lists[1];
  int counts[1] = {1};
  unsigned char pcr[188];
  cas_t *c;

  init_cas_cfg(&cfg, 1);
  set_es(&es, &pe, 0x0100, PID_VIDEO);
  lists[0] = &es;
  build_pcr_packet(pcr, 1, 1000000, 0);
  c = cas_start_multi(&cfg, lists, counts, 1);
  ck_assert_ptr_nonnull(c);
  cas_wall_tick(c, 1.0);
  cas_wall_tick(c, 2.0);
  cas_wall_tick(c, 2.0);
  cas_wall_tick(c, 1.5);
  cas_pcr_tick(c, 0x1FFF, pcr);
  ck_assert_uint_eq(cas_pcr_pid(c), 0x1FFFu);
  ck_assert_int_eq(cas_failed(c), 0);
  cas_stop(c);
}
END_TEST

typedef struct {
  const char *name;
  int biss1;
  int emit_esw;
  int multi;
} biss_case_t;

static const biss_case_t biss_cases[] = {
    {"biss1 single", 1, 0, 0},
    {"biss1 multi", 1, 0, 1},
    {"biss2 single", 0, 0, 0},
    {"biss2 multi", 0, 0, 1},
    {"biss2 with esw single", 0, 1, 0},
    {"biss2 with esw multi", 0, 1, 1},
};

static cas_t *start_biss(const biss_case_t *bc, config_t *cfg, const psi_t *psi, const out_es_t *es) {
  const out_es_t *lists[1];
  int counts[1] = {1};

  lists[0] = es;
  init_cas_cfg(cfg, 0);
  cfg->biss1_enabled = bc->biss1;
  cfg->biss2_enabled = !bc->biss1;
  cfg->biss2_emit_esw = bc->emit_esw;
  memset(cfg->biss1_cw, 0x11, sizeof cfg->biss1_cw);
  memset(cfg->biss2_sw, 0x22, sizeof cfg->biss2_sw);
  memset(cfg->biss2_esw_id, 0x33, sizeof cfg->biss2_esw_id);
  if (bc->multi) return cas_start_multi(cfg, lists, counts, 1);
  return cas_start(cfg, psi, es, 1, 0x0100);
}

typedef struct {
  unsigned count;
  unsigned char control;
} emit_rec_t;

static void rec_emit(void *ctx, const unsigned char pkt[188]) {
  emit_rec_t *rec = ctx;

  rec->count++;
  rec->control = pkt[3];
}

static int biss_key_usable(const biss_case_t *bc) {
  unsigned char cw[16] = {0};

  if (bc->biss1) {
    csa2_key_t *k = csa2_key_new(cw);

    csa2_key_free(k);
    return k != NULL;
  }
  {
    cissa_key_t *k = cissa_key_new(cw);

    cissa_key_free(k);
    return k != NULL;
  }
}

START_TEST(biss_modes_scramble_with_a_fixed_key) {
  const biss_case_t *bc = &biss_cases[_i];
  config_t cfg;
  psi_es_t pe;
  out_es_t es;
  psi_t *with_pcr = build_psi_with_pcr(0x0100);
  unsigned char pkt[188];
  emit_rec_t emitted = {0, 0};
  cas_t *c;

  set_es(&es, &pe, 0x0100, PID_VIDEO);
  c = start_biss(bc, &cfg, with_pcr, &es);
  if (!biss_key_usable(bc)) {
    ck_assert_msg(c == NULL, "%s: started without a usable key", bc->name);
    psi_free(with_pcr);
    return;
  }
  ck_assert_msg(c != NULL, "%s: not started", bc->name);
  ck_assert_int_eq(cas_failed(c), 0);
  ck_assert_uint_eq(cas_pcr_pid(c), bc->multi ? 0x1FFFu : 0x0100u);
  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = 0x01;
  pkt[2] = 0x00;
  pkt[3] = 0x10;
  cas_wall_tick(c, 1.0);
  cas_pcr_tick(c, cas_pcr_pid(c), pkt);
  cas_scramble_packet(c, 0x0100, 1.0, pkt, rec_emit, &emitted);
  cas_flush(c, rec_emit, &emitted);
  ck_assert_msg(emitted.count == 1u, "%s: %u packets emitted", bc->name, emitted.count);
  ck_assert_int_eq(emitted.control & 0xC0, 0x80);
  cas_stop(c);
  psi_free(with_pcr);
}
END_TEST

START_TEST(biss_modes_refuse_an_empty_scramble_set) {
  const biss_case_t *bc = &biss_cases[_i];
  config_t cfg;
  psi_es_t pe;
  out_es_t es;
  psi_t *with_pcr = build_psi_with_pcr(0x0100);

  set_es(&es, &pe, 0x0100, PID_VIDEO);
  init_cas_cfg(&cfg, 0);
  cfg.cas_pid_count = 0;
  cfg.biss1_enabled = bc->biss1;
  cfg.biss2_enabled = !bc->biss1;
  if (bc->multi) {
    const out_es_t *lists[1] = {&es};
    int counts[1] = {1};

    ck_assert_ptr_null(cas_start_multi(&cfg, lists, counts, 1));
  } else {
    ck_assert_ptr_null(cas_start(&cfg, with_pcr, &es, 1, 0x0100));
  }
  psi_free(with_pcr);
}
END_TEST

typedef struct {
  const char *name;
  int multi;
  int session_given;
  size_t pid_count;
} biss_ca_case_t;

static const biss_ca_case_t biss_ca_cases[] = {
    {"single, fixed session id", 0, 1, 1},
    {"single, random session id", 0, 0, 1},
    {"multi, fixed session id", 1, 1, 1},
    {"multi, random session id", 1, 0, 1},
    {"single, no pids", 0, 1, 0},
    {"multi, no pids", 1, 1, 0},
};

START_TEST(biss_ca_refuses_unusable_start_conditions) {
  const biss_ca_case_t *bc = &biss_ca_cases[_i];
  config_t cfg;
  psi_es_t pe;
  out_es_t es;
  const out_es_t *lists[1];
  int counts[1] = {1};
  psi_t *with_pcr = build_psi_with_pcr(0x0100);
  cas_t *c;

  set_es(&es, &pe, 0x0100, PID_VIDEO);
  lists[0] = &es;
  init_cas_cfg(&cfg, 0);
  cfg.cas_pid_count = bc->pid_count;
  cfg.biss2_ca_enabled = 1;
  cfg.biss2_ca_receivers_dir = "/nonexistent/dvbipitools-receivers";
  cfg.biss2_ca_session_id_given = bc->session_given;
  cfg.biss2_ca_session_id = 0x1234;
  c = bc->multi ? cas_start_multi(&cfg, lists, counts, 1) : cas_start(&cfg, with_pcr, &es, 1, 0x0100);
  ck_assert_msg(c == NULL, "%s: started", bc->name);
  psi_free(with_pcr);
}
END_TEST

static Suite *cas_suite(void) {
  Suite *s = suite_create("cas");
  TCase *tc = tcase_create("core");
  tcase_add_test(tc, parse_pcr_recovers_known_value);
  tcase_add_test(tc, parse_pcr_zero_extension);
  tcase_add_test(tc, parse_pcr_no_adaptation_field);
  tcase_add_test(tc, parse_pcr_flag_not_set);
  tcase_add_test(tc, parse_pcr_adaptation_field_too_short);
  tcase_add_test(tc, pcr_plausible_normal_delta_accepted);
  tcase_add_test(tc, pcr_plausible_wraparound_accepted);
  tcase_add_test(tc, pcr_plausible_rejects_huge_discontinuity);
  tcase_add_test(tc, pcr_plausible_rejects_backward_jump);
  tcase_add_test(tc, pcr_plausible_rejects_zero_wall_delta);
  tcase_add_test(tc, resolve_pids_explicit_only);
  tcase_add_test(tc, resolve_pids_video_keyword);
  tcase_add_test(tc, resolve_pids_audio_keyword_multiple);
  tcase_add_test(tc, resolve_pids_lcevc_keyword_not_implied_by_video_or_audio);
  tcase_add_test(tc, resolve_pids_lcevc_keyword);
  tcase_add_test(tc, resolve_pids_mixed_explicit_and_keyword_dedupes);
  tcase_add_test(tc, resolve_pids_default_video_and_audio);
  tcase_add_test(tc, resolve_pids_caps_at_limit);
  tcase_add_test(tc, resolve_pids_nothing_requested_yields_none);
  tcase_add_test(tc, resolve_pids_exceeds_old_16_limit);
  tcase_add_test(tc, resolve_pids_multi_across_programs);
  tcase_add_test(tc, resolve_pids_multi_dedupes_across_programs);
  tcase_add_test(tc, resolve_pids_multi_caps_across_programs);
  tcase_add_loop_test(tc, start_multi_rejects_unusable_group_configuration, 0, (int)(sizeof bad_group_cases / sizeof bad_group_cases[0]));
  tcase_add_test(tc, start_multi_limits_started_vendors_to_the_group_maximum);
  tcase_add_test(tc, start_single_requires_a_pcr_and_resolvable_pids);
  tcase_add_test(tc, cas_reports_failure_when_the_emmg_port_is_taken);
  tcase_add_test(tc, flush_and_stop_are_safe_in_every_order);
  tcase_add_test(tc, add_pids_extends_the_scrambled_set_at_runtime);
  tcase_add_test(tc, add_pids_reports_pids_that_do_not_fit);
  tcase_add_test(tc, metrics_are_zero_for_missing_or_unstarted_instances);
  tcase_add_test(tc, vendor_accessors_and_descriptors_describe_each_vendor);
  tcase_add_test(tc, reload_receivers_is_a_noop_outside_biss_ca_mode);
  tcase_add_test(tc, pcr_ticks_start_the_clock_and_flag_discontinuities);
  tcase_add_test(tc, wall_ticks_advance_the_clock_only_forward);
  tcase_add_loop_test(tc, biss_modes_scramble_with_a_fixed_key, 0, (int)(sizeof biss_cases / sizeof biss_cases[0]));
  tcase_add_loop_test(tc, biss_modes_refuse_an_empty_scramble_set, 0, (int)(sizeof biss_cases / sizeof biss_cases[0]));
  tcase_add_loop_test(tc, biss_ca_refuses_unusable_start_conditions, 0, (int)(sizeof biss_ca_cases / sizeof biss_ca_cases[0]));
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
