/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/helper/log.h"
#include "lib/net/retryset.h"
#include "lib/helper/signal.h"

#include "mpts/priv.h"

#define INTERVAL_CAT_S 0.1 /* matches remux.c's own INTERVAL_PAT_PMT_S cadence */
#define MPTS_POLL_MAX_MS 100

typedef struct {
  unsigned n;
  tv_slot_ctx_t slot_ctxs[ARGS_MAX_INPUTS];
  void *slot_ctx_ptrs[ARGS_MAX_INPUTS];
  psi_pat_entry_t entries[ARGS_MAX_INPUTS];
  mpts_program_t *progs;
  retryset_t *rs;
  mpts_t *mpts;
  out_ctx_t out;
  cas_t *cas;
  int cas_wanted;
  int cas_needs_discovery;
  double cas_gate_deadline;
  unsigned char eit_cc;
  int eit_busy; /* program idx mid-section on shared EIT pid, -1: none */
  unsigned char cat_cc;
  double last_cat;
  unsigned rr_start;
  double run_start;
  double last_stat;
  int rc;
  input_metrics_t input_stats[ARGS_MAX_INPUTS]; /* outlives mpts_program_t's per-reconnect memset */
  ts_metrics_t tsm;
  int metrics_on;
  ts_metrics_t *tsm_p;
} mpts_run_ctx_t;

static int mpts_setup(const config_t *cfg, const metrics_exporter_t *mx, mpts_run_ctx_t *c) {
  c->n = cfg->n_inputs;
  c->cas_wanted = cfg->cas_algo != CAS_ALGO_NONE || cfg->biss2_enabled || cfg->biss1_enabled || cfg->biss2_ca_enabled;
  c->cas_needs_discovery = c->cas_wanted && (cfg->cas_pids_video || cfg->cas_pids_audio);
  c->eit_busy = -1;
  c->last_cat = -1.0;
  c->metrics_on = metrics_exporter_enabled(mx);
  c->tsm_p = c->metrics_on ? &c->tsm : NULL;

  c->progs = calloc(c->n, sizeof *c->progs);
  if (!c->progs) {
    c->rc = 1;
    return -1;
  }
  for (unsigned i = 0; i < c->n; i++) {
    out_program_pids_t pids;
    c->slot_ctxs[i].cfg = cfg;
    c->slot_ctxs[i].input = &cfg->inputs[i];
    c->slot_ctxs[i].im = c->metrics_on ? &c->input_stats[i] : NULL;
    c->slot_ctx_ptrs[i] = &c->slot_ctxs[i];
    out_program_pids(i, &pids);
    c->entries[i].program_number = cfg->inputs[i].sid;
    c->entries[i].pmt_pid = pids.pmt_pid;
  }

  c->rs = retryset_new(c->n, c->slot_ctx_ptrs, NULL, &tv_retry_ops, cfg->error_retry_s);
  c->mpts = c->rs ? mpts_new(cfg->tsid, cfg->onid, cfg->nit_text, c->entries, c->n, &mpts_program_ops) : NULL;
  if (!c->rs || !c->mpts) {
    c->rc = 1;
    return -1;
  }

  if (tvhead_output_open(cfg, &c->out)) {
    c->rc = 1;
    return -1;
  }
  c->out.pacer = bitrate_pacer_new(cfg->bitrate_kbps ? (double)cfg->bitrate_kbps * 1000.0 : 0.0, cfg->stuff, cfg->burst_limit);
  if (!c->out.pacer) {
    log_line("bitrate pacer setup failed");
    c->rc = 1;
    return -1;
  }

  if (c->cas_wanted && !c->cas_needs_discovery) {
    c->cas = cas_start_multi(cfg, NULL, NULL, 0);
    if (!c->cas) {
      log_line("cas: failed to start");
      c->rc = 1;
      return -1;
    }
    tvhead_mpts_set_cas(c->mpts, c->cas);
  }

  c->run_start = mono_seconds();
  if (c->cas_needs_discovery) c->cas_gate_deadline = c->run_start + CAS_KEYWORD_DISCOVERY_TIMEOUT_S;
  return 0;
}

static void mpts_run_loop(const config_t *cfg, metrics_exporter_t *mx, mpts_run_ctx_t *c) {
  unsigned n = c->n;

  while (!signal_stop_requested()) {
    struct pollfd pfds[ARGS_MAX_INPUTS];
    unsigned pfd_slot[ARGS_MAX_INPUTS];
    nfds_t npfd = 0;
    double now;
    time_t now_t, deadline;
    int timeout_ms = MPTS_POLL_MAX_MS;

    deadline = retryset_next_deadline(c->rs);
    if (deadline != RETRYSET_NEVER) {
      long remain_s = (long)(deadline - time(NULL));
      int remain_ms = remain_s <= 0 ? 0 : (int)(remain_s * 1000);
      if (remain_ms < timeout_ms) timeout_ms = remain_ms;
    }
    for (unsigned i = 0; i < n; i++) {
      int fd = poll_fd_for_input(c->rs, i, &pfds[npfd].events);
      if (fd < 0) continue;
      pfds[npfd].fd = fd;
      pfds[npfd].revents = 0;
      pfd_slot[npfd] = i;
      npfd++;
    }
    poll(npfd ? pfds : NULL, npfd, timeout_ms);
    if (signal_stop_requested()) break;
    tvhead_srt_service(&c->out);
    flush_batch_if_stale(&c->out);
    now = mono_seconds();
    now_t = time(NULL);
    for (unsigned i = 0; i < n; i++) retryset_service(c->rs, i, now_t);

    {
      mpts_tick_t tk;
      tk.rs = c->rs;
      tk.cfg = cfg;
      tk.progs = c->progs;
      tk.mpts = c->mpts;
      tk.cas = c->cas;
      tk.input_stats = c->input_stats;
      tk.metrics_on = c->metrics_on;
      tk.out = &c->out;
      tk.tsm = c->tsm_p;
      tk.now = now;
      tk.now_t = now_t;

      for (unsigned k = 0; k < n; k++) {
        tvsrc_t *src;
        unsigned i = (c->rr_start + k) % n;
        src = retryset_result(c->rs, i);
        if (!src) continue;
        if (!input_poll_ready(i, pfd_slot, pfds, npfd)) continue;
        if (!c->progs[i].rx) discover_input(&tk, i, src);
        else feed_input(&tk, i, src);
      }
    }
    c->rr_start = n ? (c->rr_start + 1) % n : 0;

    if (c->eit_busy >= 0 && c->progs[c->eit_busy].rx && remux_eit_pending(c->progs[c->eit_busy].rx)) {
      remux_emit_eit(c->progs[c->eit_busy].rx, OUT_PID_EIT, &c->eit_cc, 1, packet_cb, &c->out);
      if (!remux_eit_mid_section(c->progs[c->eit_busy].rx)) c->eit_busy = -1;
    } else {
      c->eit_busy = -1;
      for (unsigned i = 0; i < n; i++) {
        if (!c->progs[i].rx || !remux_eit_pending(c->progs[i].rx)) continue;
        remux_emit_eit(c->progs[i].rx, OUT_PID_EIT, &c->eit_cc, 1, packet_cb, &c->out);
        if (remux_eit_mid_section(c->progs[i].rx)) {
          c->eit_busy = (int)i;
          break;
        }
      }
    }

    if (!c->cas && now - c->last_cat >= INTERVAL_CAT_S) {
      c->last_cat = now;
      emit_source_cat_passthrough(c->progs, n, &c->cat_cc, c->tsm_p, packet_cb, &c->out);
    }

    if (c->cas_needs_discovery && !c->cas && check_cas_discovery_gate(cfg, c->progs, n, c->mpts, c->cas_gate_deadline, &c->cas) != 0) {
      c->rc = 1;
      return;
    }

    mpts_tick(c->mpts, now, packet_cb, &c->out);
    if (c->cas) {
      cas_wall_tick(c->cas, now);
      if (cas_failed(c->cas)) {
        log_line("cas: fatal error, stopping");
        c->rc = 1;
        return;
      }
      if (signal_reload_requested()) cas_reload_receivers(c->cas);
    }
    {
      int stuff_n = bitrate_stuff_due(c->out.pacer);
      for (unsigned k = 0; k < (unsigned)stuff_n; k++) send_null_packet(&c->out);
    }
    if (cfg->verbose && now - c->last_stat >= 1.0) {
      fprintf(stderr, "\r%.0fs, %llu TS packets\033[K", now - c->run_start, c->out.packets);
      fflush(stderr);
      c->last_stat = now;
    }
    {
      unsigned active = 0;
      for (unsigned i = 0; i < n; i++) if (c->progs[i].rx) active++;
      emit_metrics(mx, now, &c->out, n, active, c->input_stats, n, c->tsm_p, c->cas);
    }
  }
}

static void mpts_teardown(const config_t *cfg, mpts_run_ctx_t *c) {
  if (c->cas) cas_flush(c->cas, packet_cb, &c->out);
  flush_batch(&c->out);
  for (unsigned i = 0; c->progs && i < c->n; i++) program_reset(&c->progs[i]);
  free(c->progs);
  if (c->mpts) mpts_free(c->mpts);
  if (c->rs) retryset_free(c->rs);
  if (c->cas) cas_stop(c->cas);
  tvhead_output_close(&c->out);
  if (cfg->verbose && log_stderr_is_tty()) fputc('\n', stderr);
  if (c->rc == 0) log_line("stopped.");
}

int tvhead_run_mpts(const config_t *cfg, metrics_exporter_t *mx) {
  mpts_run_ctx_t c;
  memset(&c, 0, sizeof c);
  if (mpts_setup(cfg, mx, &c) == 0) mpts_run_loop(cfg, mx, &c);
  mpts_teardown(cfg, &c);
  return c.rc ? 1 : 0;
}
