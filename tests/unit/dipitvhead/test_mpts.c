/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <check.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/sys/signal.h"
#include "lib/mux/mpts.h"

#include "dipitvhead/tvhead/mpts/priv.h"

#include "psi_fixture.h"
#include "stderr_capture.h"

#define RIG_MAX 3
#define COLLECT_MAX 16
#define RR_MAX_INPUTS 16
#define STREAM_PAD_PACKETS 20
#define PENDING_PACKETS 1

typedef struct {
  config_t cfg;
  mpts_program_t progs[RIG_MAX];
  tv_slot_ctx_t slots[RIG_MAX];
  void *slot_ptrs[RIG_MAX];
  psi_pat_entry_t entries[RIG_MAX];
  input_metrics_t stats[RIG_MAX];
  retryset_t *rs;
  mpts_t *mpts;
  out_ctx_t out;
  ts_metrics_t tsm;
  mpts_tick_t tk;
  unsigned n;
} rig_t;

static void rig_init(rig_t *g, unsigned n) {
  memset(g, 0, sizeof *g);
  g->n = n;
  g->cfg.n_inputs = n;
  g->cfg.tsid = 1;
  g->cfg.onid = 1;
  for (unsigned i = 0; i < n; i++) {
    g->cfg.inputs[i].sid = i + 1;
    g->cfg.inputs[i].sdt_mode = TABLE_DROP;
    g->cfg.inputs[i].input.kind = SRC_STDIN;
    g->slots[i].cfg = &g->cfg;
    g->slots[i].input = &g->cfg.inputs[i];
    g->slot_ptrs[i] = &g->slots[i];
    g->entries[i].program_number = i + 1;
    g->entries[i].pmt_pid = 0x1000 + i;
  }
  g->rs = retryset_new(n, g->slot_ptrs, NULL, &tv_retry_ops, 0);
  g->mpts = mpts_new(1, 1, "", g->entries, n, &mpts_program_ops);
  ck_assert_ptr_nonnull(g->rs);
  ck_assert_ptr_nonnull(g->mpts);
  g->tk.rs = g->rs;
  g->tk.cfg = &g->cfg;
  g->tk.progs = g->progs;
  g->tk.mpts = g->mpts;
  g->tk.input_stats = g->stats;
  g->tk.out = &g->out;
  g->tk.tsm = &g->tsm;
}

static void rig_free(rig_t *g) {
  for (unsigned i = 0; i < g->n; i++) program_reset(&g->progs[i]);
  mpts_free(g->mpts);
  retryset_free(g->rs);
}

static void rig_attach_program(rig_t *g, unsigned i, psi_t *psi) {
  out_program_pids_t pids;

  out_program_pids(i, &pids);
  g->progs[i].psi = psi;
  g->progs[i].rx = remux_new(&g->cfg, &g->cfg.inputs[i], psi, &pids, 0);
  ck_assert_ptr_nonnull(g->progs[i].rx);
}

static int stdin_pipe(void) {
  int p[2];

  ck_assert_int_eq(pipe(p), 0);
  ck_assert_int_ge(dup2(p[0], STDIN_FILENO), 0);
  close(p[0]);
  ck_assert_int_eq(fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL, 0) | O_NONBLOCK), 0);
  return p[1];
}

static void write_stream(int wfd, const unsigned char (*head)[188], unsigned n_head, unsigned total) {
  unsigned char *buf = malloc((size_t)total * 188);

  ck_assert_ptr_nonnull(buf);
  for (unsigned i = 0; i < total; i++) {
    if (i < n_head) memcpy(buf + (size_t)i * 188, head[i], 188);
    else fixture_ts_packet(buf + (size_t)i * 188, 0x1FFF, (unsigned char)i, 0xFF);
  }
  ck_assert_int_eq((int)write(wfd, buf, (size_t)total * 188), (int)((size_t)total * 188));
  free(buf);
}

static tvsrc_t *open_stdin_source(const rig_t *g) {
  net_err_reason_t reason = NET_ERR_COUNT;
  tvsrc_t *src = tvsrc_open(&g->cfg, &g->cfg.inputs[0], &reason);

  ck_assert_ptr_nonnull(src);
  return src;
}

START_TEST(round_robin_serves_every_input_first_exactly_once_per_cycle) {
  unsigned n = (unsigned)_i + 1;
  unsigned first_count[RR_MAX_INPUTS];
  unsigned rr = 0;

  memset(first_count, 0, sizeof first_count);
  for (unsigned tick = 0; tick < n; tick++) {
    unsigned seen = 0;

    for (unsigned k = 0; k < n; k++) {
      unsigned idx = mpts_service_index(rr, k, n);

      ck_assert_uint_lt(idx, n);
      ck_assert_uint_eq((seen >> idx) & 1u, 0u);
      seen |= 1u << idx;
      if (k == 0) first_count[idx]++;
    }
    ck_assert_uint_eq(seen, (1u << n) - 1u);
    rr = mpts_advance_rr(rr, n);
  }
  for (unsigned i = 0; i < n; i++) ck_assert_uint_eq(first_count[i], 1u);
  ck_assert_uint_eq(rr, 0u);
}
END_TEST

START_TEST(round_robin_tolerates_zero_inputs) {
  ck_assert_uint_eq(mpts_service_index(0, 0, 0), 0u);
  ck_assert_uint_eq(mpts_advance_rr(3, 0), 0u);
}
END_TEST

typedef struct {
  double now;
  int have_cas;
  int expect_due;
} cat_step_t;

static const cat_step_t cat_steps[] = {
    {0.0, 0, 1},
    {0.0625, 0, 0},
    {0.125, 0, 1},
    {0.1875, 1, 0},
    {0.375, 1, 0},
    {0.375, 0, 1},
    {0.4375, 0, 0},
    {0.5, 0, 1},
};

START_TEST(source_cat_passthrough_follows_its_interval_and_yields_to_own_cas) {
  double last = -1.0;

  for (size_t i = 0; i < sizeof cat_steps / sizeof cat_steps[0]; i++) {
    int due = mpts_cat_due(cat_steps[i].have_cas, cat_steps[i].now, &last);

    ck_assert_msg(due == cat_steps[i].expect_due, "step %zu at %.2f: due %d", i, cat_steps[i].now, due);
  }
}
END_TEST

typedef struct {
  unsigned char pkts[COLLECT_MAX][188];
  int count;
} collect_t;

static void collect_cb(void *ctx, const unsigned char *pkt188) {
  collect_t *c = ctx;

  if (c->count < COLLECT_MAX) memcpy(c->pkts[c->count], pkt188, 188);
  c->count++;
}

static void feed_eit(rig_t *g, unsigned i, unsigned service_id, size_t body_len) {
  unsigned char pkts[8][188];
  unsigned char section[1024];
  size_t n = fixture_eit_packets(service_id, 0, body_len, pkts, section);

  for (size_t k = 0; k < n; k++) remux_feed(g->progs[i].rx, 0.0, pkts[k], collect_cb, &(collect_t){{{0}}, 0}, NULL);
}

static const unsigned char *section_start(const collect_t *c, int idx) {
  const unsigned char *pkt = c->pkts[idx];
  size_t off = ((pkt[3] >> 4) & 0x3) == 3 ? 5 + (size_t)pkt[4] : 4;

  return pkt + off + 1;
}

static unsigned emitted_service(const collect_t *c, int idx) {
  const unsigned char *sec = section_start(c, idx);

  return ((unsigned)sec[3] << 8) | sec[4];
}

static int starts_section(const collect_t *c, int idx) {
  return (c->pkts[idx][1] & 0x40) != 0;
}

START_TEST(eit_drain_finishes_a_program_before_starting_the_next) {
  rig_t g;
  collect_t col;
  int busy = -1;
  unsigned char cc = 0;

  rig_init(&g, 2);
  rig_attach_program(&g, 0, build_psi_with_program(101, 0x0101, 0x1B, 0x0101));
  rig_attach_program(&g, 1, build_psi_with_program(102, 0x0101, 0x1B, 0x0101));
  feed_eit(&g, 0, 101, 200);
  feed_eit(&g, 1, 102, 20);
  memset(&col, 0, sizeof col);

  mpts_emit_eit(g.progs, 2, &busy, &cc, collect_cb, &col);
  ck_assert_int_eq(col.count, 1);
  ck_assert_int_eq(busy, 0);
  mpts_emit_eit(g.progs, 2, &busy, &cc, collect_cb, &col);
  ck_assert_int_eq(col.count, 2);
  ck_assert_int_eq(busy, -1);
  mpts_emit_eit(g.progs, 2, &busy, &cc, collect_cb, &col);
  ck_assert_int_eq(col.count, 3);
  ck_assert_int_eq(busy, -1);
  ck_assert_int_eq(starts_section(&col, 0), 1);
  ck_assert_int_eq(starts_section(&col, 1), 0);
  ck_assert_int_eq(starts_section(&col, 2), 1);
  ck_assert_uint_eq(emitted_service(&col, 0), 1u);
  ck_assert_uint_eq(emitted_service(&col, 2), 2u);
  mpts_emit_eit(g.progs, 2, &busy, &cc, collect_cb, &col);
  ck_assert_int_eq(col.count, 3);
  ck_assert_int_eq(busy, -1);
  rig_free(&g);
}
END_TEST

START_TEST(eit_drain_abandons_a_program_that_disappeared_mid_section) {
  rig_t g;
  collect_t col;
  int busy = -1;
  unsigned char cc = 0;

  rig_init(&g, 2);
  rig_attach_program(&g, 0, build_psi_with_program(101, 0x0101, 0x1B, 0x0101));
  rig_attach_program(&g, 1, build_psi_with_program(102, 0x0101, 0x1B, 0x0101));
  feed_eit(&g, 0, 101, 200);
  feed_eit(&g, 1, 102, 20);
  memset(&col, 0, sizeof col);

  mpts_emit_eit(g.progs, 2, &busy, &cc, collect_cb, &col);
  ck_assert_int_eq(busy, 0);
  program_reset(&g.progs[0]);
  mpts_emit_eit(g.progs, 2, &busy, &cc, collect_cb, &col);
  ck_assert_int_eq(col.count, 2);
  ck_assert_int_eq(busy, -1);
  ck_assert_uint_eq(emitted_service(&col, 1), 2u);
  rig_free(&g);
}
END_TEST

START_TEST(eit_drain_with_nothing_pending_emits_nothing) {
  rig_t g;
  collect_t col;
  int busy = 1;
  unsigned char cc = 0;

  rig_init(&g, 2);
  rig_attach_program(&g, 0, build_psi_with_program(101, 0x0101, 0x1B, 0x0101));
  memset(&col, 0, sizeof col);
  mpts_emit_eit(g.progs, 2, &busy, &cc, collect_cb, &col);
  ck_assert_int_eq(col.count, 0);
  ck_assert_int_eq(busy, -1);
  rig_free(&g);
}
END_TEST

START_TEST(discovery_builds_a_remux_once_pat_and_pmt_arrive) {
  rig_t g;
  tvsrc_t *src;
  unsigned char pkts[2][188];
  int wfd;

  rig_init(&g, 1);
  wfd = stdin_pipe();
  src = open_stdin_source(&g);
  fixture_programme_packets(101, 0x0101, 0x1B, 0x0101, pkts);
  write_stream(wfd, (const unsigned char (*)[188])pkts, 2, STREAM_PAD_PACKETS);
  for (int i = 0; i < 10 && !g.progs[0].rx; i++) discover_input(&g.tk, 0, src);
  ck_assert_ptr_nonnull(g.progs[0].rx);
  ck_assert_ptr_nonnull(g.progs[0].psi);
  tvsrc_close(src);
  close(wfd);
  rig_free(&g);
}
END_TEST

START_TEST(discovery_gives_up_after_the_timeout) {
  rig_t g;
  tvsrc_t *src;
  capture_t cap;
  char log[2048];
  int wfd;

  rig_init(&g, 1);
  wfd = stdin_pipe();
  src = open_stdin_source(&g);
  write_stream(wfd, NULL, 0, PENDING_PACKETS);
  discover_input(&g.tk, 0, src);
  ck_assert_ptr_nonnull(g.progs[0].psi);
  ck_assert_ptr_null(g.progs[0].rx);
  write_stream(wfd, NULL, 0, PENDING_PACKETS);
  g.progs[0].discover_start -= DISCOVERY_TIMEOUT_S + 1.0;
  capture_begin(&cap);
  discover_input(&g.tk, 0, src);
  capture_end(&cap, log, sizeof log);
  ck_assert_ptr_null(g.progs[0].psi);
  ck_assert_ptr_null(g.progs[0].rx);
  ck_assert_ptr_nonnull(strstr(log, "no live PMT found"));
  tvsrc_close(src);
  close(wfd);
  rig_free(&g);
}
END_TEST

START_TEST(discovery_resets_the_slot_when_remux_setup_fails) {
  rig_t g;
  tvsrc_t *src;
  unsigned char pkts[2][188];
  capture_t cap;
  char log[4096];
  int wfd;

  rig_init(&g, 1);
  g.cfg.inputs[0].strip_mask = TVSTRIP_DATA;
  wfd = stdin_pipe();
  src = open_stdin_source(&g);
  fixture_programme_packets(101, 0x0101, 0x06, 0x0101, pkts);
  write_stream(wfd, (const unsigned char (*)[188])pkts, 2, STREAM_PAD_PACKETS);
  capture_begin(&cap);
  for (int i = 0; i < 10; i++) discover_input(&g.tk, 0, src);
  capture_end(&cap, log, sizeof log);
  ck_assert_ptr_null(g.progs[0].rx);
  ck_assert_ptr_null(g.progs[0].psi);
  ck_assert_ptr_nonnull(strstr(log, "remux setup failed"));
  tvsrc_close(src);
  close(wfd);
  rig_free(&g);
}
END_TEST

#define BACKLOG_PACKETS 320
#define FEED_CHUNK_BYTES (32 * 188)

START_TEST(feed_splits_a_large_read_across_calls) {
  rig_t g;
  tvsrc_t *src;
  read_backlog_t *bl;
  size_t total;
  int wfd;
  int calls = 0;

  rig_init(&g, 1);
  rig_attach_program(&g, 0, build_psi_with_pcr(0x0101));
  wfd = stdin_pipe();
  src = open_stdin_source(&g);
  write_stream(wfd, NULL, 0, BACKLOG_PACKETS);
  bl = &g.progs[0].backlog;

  feed_input(&g.tk, 0, src);
  ck_assert_uint_eq(bl->len, 0u);
  feed_input(&g.tk, 0, src);
  total = bl->len;
  ck_assert_uint_gt(total, (size_t)FEED_CHUNK_BYTES);
  ck_assert_uint_eq(bl->off, (size_t)FEED_CHUNK_BYTES);
  while (bl->len) {
    feed_input(&g.tk, 0, src);
    calls++;
    ck_assert_int_lt(calls, 20);
  }
  ck_assert_int_eq((size_t)calls, (total + FEED_CHUNK_BYTES - 1) / FEED_CHUNK_BYTES - 1);
  ck_assert_uint_eq(bl->off, 0u);
  tvsrc_close(src);
  close(wfd);
  rig_free(&g);
}
END_TEST

START_TEST(feed_keeps_state_when_the_source_has_nothing_yet) {
  rig_t g;
  tvsrc_t *src;
  int wfd;

  rig_init(&g, 1);
  rig_attach_program(&g, 0, build_psi_with_pcr(0x0101));
  wfd = stdin_pipe();
  src = open_stdin_source(&g);
  write_stream(wfd, NULL, 0, PENDING_PACKETS);
  feed_input(&g.tk, 0, src);
  ck_assert_ptr_nonnull(g.progs[0].rx);
  ck_assert_uint_eq(g.progs[0].backlog.len, 0u);
  tvsrc_close(src);
  close(wfd);
  rig_free(&g);
}
END_TEST

START_TEST(feed_resets_the_program_when_the_source_ends) {
  rig_t g;
  tvsrc_t *src;
  int wfd;

  rig_init(&g, 1);
  rig_attach_program(&g, 0, build_psi_with_pcr(0x0101));
  wfd = stdin_pipe();
  src = open_stdin_source(&g);
  close(wfd);
  feed_input(&g.tk, 0, src);
  ck_assert_ptr_null(g.progs[0].rx);
  ck_assert_ptr_null(g.progs[0].psi);
  tvsrc_close(src);
  rig_free(&g);
}
END_TEST

static void gate_cfg(rig_t *g) {
  g->cfg.cas_algo = CAS_ALGO_CSA2;
  g->cfg.cas_cp_duration_ms = 10000;
  g->cfg.cas_pids_video = 1;
  g->cfg.n_cas_vendors = 1;
  strcpy(g->cfg.cas_vendors[0].ecmg_host, "127.0.0.1");
  g->cfg.cas_vendors[0].ecmg_port = 1;
  g->cfg.cas_vendors[0].super_cas_id = 0x4A750001;
  g->cfg.cas_vendors[0].ecm_id = 1;
  g->cfg.cas_vendors[0].ecm_pid = 0x1FF0;
  g->cfg.cas_vendors[0].emm_pid = 0x1FF1;
}

START_TEST(gate_starts_cas_once_every_program_is_discovered) {
  rig_t g;
  cas_t *cas = NULL;

  rig_init(&g, 2);
  gate_cfg(&g);
  rig_attach_program(&g, 0, build_psi_with_program(101, 0x0101, 0x1B, 0x0101));
  rig_attach_program(&g, 1, build_psi_with_program(102, 0x0101, 0x1B, 0x0101));
  ck_assert_int_eq(check_cas_discovery_gate(&g.cfg, g.progs, 2, g.mpts, mono_seconds() + 60.0, &cas), 0);
  ck_assert_ptr_nonnull(cas);
  ck_assert_uint_eq(cas_vendor_count(cas), 1u);
  cas_stop(cas);
  rig_free(&g);
}
END_TEST

START_TEST(gate_waits_before_the_deadline) {
  rig_t g;
  cas_t *cas = NULL;

  rig_init(&g, 2);
  gate_cfg(&g);
  rig_attach_program(&g, 0, build_psi_with_program(101, 0x0101, 0x1B, 0x0101));
  ck_assert_int_eq(check_cas_discovery_gate(&g.cfg, g.progs, 2, g.mpts, mono_seconds() + 60.0, &cas), 0);
  ck_assert_ptr_null(cas);
  rig_free(&g);
}
END_TEST

START_TEST(gate_fails_past_the_deadline_and_names_the_stragglers) {
  rig_t g;
  cas_t *cas = NULL;
  capture_t cap;
  char log[4096];
  int rc;

  rig_init(&g, 3);
  gate_cfg(&g);
  rig_attach_program(&g, 0, build_psi_with_program(101, 0x0101, 0x1B, 0x0101));
  g.progs[1].psi = psi_new();
  capture_begin(&cap);
  rc = check_cas_discovery_gate(&g.cfg, g.progs, 3, g.mpts, mono_seconds() - 1.0, &cas);
  capture_end(&cap, log, sizeof log);
  ck_assert_int_eq(rc, -1);
  ck_assert_ptr_null(cas);
  ck_assert_ptr_nonnull(strstr(log, "still discovering"));
  ck_assert_ptr_nonnull(strstr(log, "not connected"));
  rig_free(&g);
}
END_TEST

START_TEST(gate_reports_a_cas_that_cannot_start) {
  rig_t g;
  cas_t *cas = NULL;
  capture_t cap;
  char log[4096];
  int rc;

  rig_init(&g, 1);
  gate_cfg(&g);
  g.cfg.cas_pids_video = 0;
  g.cfg.cas_pid_count = 0;
  rig_attach_program(&g, 0, build_psi_with_program(101, 0x0101, 0x1B, 0x0101));
  capture_begin(&cap);
  rc = check_cas_discovery_gate(&g.cfg, g.progs, 1, g.mpts, mono_seconds() + 60.0, &cas);
  capture_end(&cap, log, sizeof log);
  ck_assert_int_eq(rc, -1);
  ck_assert_ptr_null(cas);
  ck_assert_ptr_nonnull(strstr(log, "cas: failed to start"));
  rig_free(&g);
}
END_TEST

static psi_t *programme_with_emm(unsigned program, unsigned sys_id, unsigned emm_pid) {
  psi_t *psi = build_psi_with_program(program, 0x0101, 0x1B, 0x0101);

  if (emm_pid) {
    unsigned char desc[6] = {0x09, 4, (unsigned char)(sys_id >> 8), (unsigned char)sys_id, (unsigned char)(0xE0 | (emm_pid >> 8)), (unsigned char)emm_pid};
    unsigned char pkt[188];

    fixture_cat_packet(desc, sizeof desc, pkt);
    psi_feed(psi, pkt);
  }
  return psi;
}

static size_t cat_descriptors(const collect_t *c, unsigned char *out, size_t cap) {
  const unsigned char *sec;
  size_t sec_len;
  size_t desc_len;

  ck_assert_int_ge(c->count, 1);
  sec = section_start(c, 0);
  ck_assert_uint_eq(sec[0], 0x01);
  sec_len = (((size_t)sec[1] & 0x0F) << 8) | sec[2];
  desc_len = sec_len - 5 - 4;
  ck_assert_uint_le(desc_len, cap);
  memcpy(out, sec + 8, desc_len);
  return desc_len;
}

START_TEST(cat_passthrough_merges_descriptors_of_programs_that_have_one) {
  rig_t g;
  collect_t col;
  unsigned char cc = 0;
  unsigned char got[64];
  unsigned char want[64];
  size_t want_len = 0;
  size_t got_len;

  rig_init(&g, 3);
  rig_attach_program(&g, 0, programme_with_emm(101, 0x4A75, 0x0300));
  rig_attach_program(&g, 1, programme_with_emm(102, 0, 0));
  rig_attach_program(&g, 2, programme_with_emm(103, 0x0963, 0x0301));
  memset(&col, 0, sizeof col);
  for (unsigned i = 0; i < 3; i++) want_len += remux_source_emm_descriptor(g.progs[i].rx, want + want_len, sizeof want - want_len);
  ck_assert_uint_eq(want_len, 12u);

  emit_source_cat_passthrough(g.progs, 3, &cc, &g.tsm, collect_cb, &col);
  ck_assert_int_eq(col.count, 1);
  ck_assert_uint_eq(g.tsm.psi_sections_total[PSI_TABLE_CAT], 1ULL);
  ck_assert_uint_eq(g.tsm.psi_errors_total[PSI_TABLE_CAT], 0ULL);
  got_len = cat_descriptors(&col, got, sizeof got);
  ck_assert_uint_eq(got_len, want_len);
  ck_assert_mem_eq(got, want, want_len);

  emit_source_cat_passthrough(g.progs, 3, &cc, NULL, collect_cb, &col);
  ck_assert_int_eq(col.count, 2);
  ck_assert_uint_eq(col.pkts[1][3] & 0x0F, (col.pkts[0][3] + 1) & 0x0F);
  rig_free(&g);
}
END_TEST

START_TEST(cat_passthrough_skips_when_no_program_carries_emm) {
  rig_t g;
  collect_t col;
  unsigned char cc = 0;

  rig_init(&g, 2);
  rig_attach_program(&g, 0, programme_with_emm(101, 0, 0));
  rig_attach_program(&g, 1, programme_with_emm(102, 0, 0));
  memset(&col, 0, sizeof col);
  emit_source_cat_passthrough(g.progs, 2, &cc, &g.tsm, collect_cb, &col);
  ck_assert_int_eq(col.count, 0);
  ck_assert_uint_eq(g.tsm.psi_sections_total[PSI_TABLE_CAT], 0ULL);
  rig_free(&g);
}
END_TEST

START_TEST(cat_passthrough_ignores_programs_that_are_down) {
  rig_t g;
  collect_t col;
  unsigned char cc = 0;
  unsigned char got[64];

  rig_init(&g, 2);
  rig_attach_program(&g, 1, programme_with_emm(102, 0x0963, 0x0301));
  memset(&col, 0, sizeof col);
  emit_source_cat_passthrough(g.progs, 2, &cc, &g.tsm, collect_cb, &col);
  ck_assert_int_eq(col.count, 1);
  ck_assert_uint_eq(cat_descriptors(&col, got, sizeof got), 6u);
  rig_free(&g);
}
END_TEST

typedef struct {
  const char *name;
  int cas;
  int cas_pids;
  int cas_discovery;
  int mcast_bad;
  int inspect;
  int want_rc;
} run_case_t;

static const run_case_t run_cases[] = {
  {"plain output", 0, 0, 0, 0, 0, 0},
  {"inspectors enabled", 0, 0, 0, 0, 1, 0},
  {"own cas with explicit pids", 1, 1, 0, 0, 0, 0},
  {"cas waiting for pid discovery", 1, 0, 1, 0, 0, 0},
  {"cas with nothing to scramble", 1, 0, 0, 0, 0, 1},
  {"multicast target that cannot open", 0, 0, 0, 1, 0, 1},
};

START_TEST(run_mpts_sets_up_and_tears_down_for_each_configuration) {
  const run_case_t *c = &run_cases[_i];
  static config_t cfg;
  metrics_exporter_t mx;
  int rc;

  memset(&cfg, 0, sizeof cfg);
  cfg.n_inputs = 1;
  cfg.tsid = 1;
  cfg.onid = 1;
  cfg.inputs[0].sid = 1;
  cfg.inputs[0].sdt_mode = TABLE_DROP;
  cfg.inputs[0].input.kind = SRC_STDIN;
  if (c->inspect) cfg.metrics_inspect_ts = METRICS_INSPECT_TS_BASIC;
  if (c->cas) {
    cfg.cas_algo = CAS_ALGO_CSA2;
    cfg.cas_cp_duration_ms = 10000;
    cfg.n_cas_vendors = 1;
    snprintf(cfg.cas_vendors[0].ecmg_host, sizeof cfg.cas_vendors[0].ecmg_host, "127.0.0.1");
    cfg.cas_vendors[0].ecmg_port = 1;
    cfg.cas_vendors[0].super_cas_id = (0x4A75u << 16) | 1u;
    cfg.cas_vendors[0].ecm_id = 1;
    cfg.cas_vendors[0].ecm_pid = 0x1FF0;
    cfg.cas_vendors[0].emm_pid = 0x1FF1;
    if (c->cas_pids) {
      cfg.cas_pid_count = 1;
      cfg.cas_pids[0] = 0x0100;
    }
    cfg.cas_pids_video = c->cas_discovery;
  }
  if (c->mcast_bad) {
    cfg.family = AF_INET;
    snprintf(cfg.mcast_group, sizeof cfg.mcast_group, "not-an-address");
    cfg.mcast_port = 5000;
  }
  metrics_exporter_init(&mx, METRICS_COMPONENT_TVHEAD, NULL, NULL, 0.0);
  signals_install();
  raise(SIGTERM);
  rc = tvhead_run_mpts(&cfg, &mx);
  ck_assert_msg(rc == c->want_rc, "%s: rc %d", c->name, rc);
}
END_TEST

static Suite *mpts_suite(void) {
  Suite *s = suite_create("dipitvhead_mpts");
  TCase *tc = tcase_create("core");
  tcase_set_timeout(tc, 20);
  tcase_add_loop_test(tc, round_robin_serves_every_input_first_exactly_once_per_cycle, 0, RR_MAX_INPUTS);
  tcase_add_test(tc, round_robin_tolerates_zero_inputs);
  tcase_add_test(tc, source_cat_passthrough_follows_its_interval_and_yields_to_own_cas);
  tcase_add_test(tc, eit_drain_finishes_a_program_before_starting_the_next);
  tcase_add_test(tc, eit_drain_abandons_a_program_that_disappeared_mid_section);
  tcase_add_test(tc, eit_drain_with_nothing_pending_emits_nothing);
  tcase_add_test(tc, discovery_builds_a_remux_once_pat_and_pmt_arrive);
  tcase_add_test(tc, discovery_gives_up_after_the_timeout);
  tcase_add_test(tc, discovery_resets_the_slot_when_remux_setup_fails);
  tcase_add_test(tc, feed_splits_a_large_read_across_calls);
  tcase_add_test(tc, feed_keeps_state_when_the_source_has_nothing_yet);
  tcase_add_test(tc, feed_resets_the_program_when_the_source_ends);
  tcase_add_test(tc, gate_starts_cas_once_every_program_is_discovered);
  tcase_add_test(tc, gate_waits_before_the_deadline);
  tcase_add_test(tc, gate_fails_past_the_deadline_and_names_the_stragglers);
  tcase_add_test(tc, gate_reports_a_cas_that_cannot_start);
  tcase_add_test(tc, cat_passthrough_merges_descriptors_of_programs_that_have_one);
  tcase_add_test(tc, cat_passthrough_skips_when_no_program_carries_emm);
  tcase_add_test(tc, cat_passthrough_ignores_programs_that_are_down);
  tcase_add_loop_test(tc, run_mpts_sets_up_and_tears_down_for_each_configuration, 0, (int)(sizeof run_cases / sizeof run_cases[0]));
  suite_add_tcase(s, tc);
  return s;
}

int main(void) {
  SRunner *sr = srunner_create(mpts_suite());
  srunner_run_all(sr, CK_NORMAL);
  int failed = srunner_ntests_failed(sr);
  srunner_free(sr);
  return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
