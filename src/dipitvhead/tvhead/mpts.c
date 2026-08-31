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

int tvhead_run_mpts(const config_t *cfg, metrics_exporter_t *mx) {
  unsigned n = cfg->n_inputs;
  tv_slot_ctx_t slot_ctxs[ARGS_MAX_INPUTS];
  void *slot_ctx_ptrs[ARGS_MAX_INPUTS] = {0};
  psi_pat_entry_t entries[ARGS_MAX_INPUTS];
  mpts_program_t *progs;
  retryset_t *rs = NULL;
  mpts_t *mpts = NULL;
  mcast_t *outmc = NULL;
  out_ctx_t out;
  cas_t *cas = NULL;
  int cas_wanted = cfg->cas_algo != CAS_ALGO_NONE || cfg->biss2_enabled || cfg->biss1_enabled || cfg->biss2_ca_enabled;
  int cas_needs_discovery = cas_wanted && (cfg->cas_pids_video || cfg->cas_pids_audio);
  double cas_gate_deadline = 0.0;
  unsigned char eit_cc = 0;
  int eit_busy = -1; /* program idx mid-section on shared EIT pid, -1: none */
  unsigned char cat_cc = 0;
  double last_cat = -1.0;
  unsigned rr_start = 0;
  double run_start, last_stat = 0;
  int rc = 0;
  input_metrics_t input_stats[ARGS_MAX_INPUTS]; /* outlives mpts_program_t's per-reconnect memset */
  ts_metrics_t tsm;
  int metrics_on = metrics_exporter_enabled(mx);
  ts_metrics_t *tsm_p = metrics_on ? &tsm : NULL;

  memset(input_stats, 0, sizeof input_stats);
  memset(&tsm, 0, sizeof tsm);

  memset(&out, 0, sizeof out);

  progs = calloc(n, sizeof *progs);
  if (!progs)
    return 1;

  for (unsigned i = 0; i < n; i++) {
    out_program_pids_t pids;

    slot_ctxs[i].cfg = cfg;
    slot_ctxs[i].input = &cfg->inputs[i];
    slot_ctxs[i].im = metrics_on ? &input_stats[i] : NULL;
    slot_ctx_ptrs[i] = &slot_ctxs[i];
    out_program_pids(i, &pids);
    entries[i].program_number = cfg->inputs[i].sid;
    entries[i].pmt_pid = pids.pmt_pid;
  }

  rs = retryset_new(n, slot_ctx_ptrs, NULL, &tv_retry_ops, cfg->error_retry_s);
  mpts = rs ? mpts_new(cfg->tsid, cfg->onid, cfg->nit_text, entries, n, &mpts_program_ops) : NULL;
  if (!rs || !mpts) {
    rc = 1;
    goto done;
  }

  if (cfg->mcast_port) {
    outmc = mcast_open_send(cfg->family, cfg->mcast_group, cfg->mcast_port, cfg->iface_out, (int)cfg->ttl);
    if (!outmc) {
      rc = 1;
      goto done;
    }
    out.mc = outmc;
    out.rtp = cfg->rtp;
    if (cfg->rtp) {
      out.rtph = rtpheader_new();
      if (!out.rtph) {
        rc = 1;
        goto done;
      }
    }
  }
  if (cfg->n_rist > 0) {
    out.rist = tvhead_rist_open(cfg);
    if (!out.rist) {
      rc = 1;
      goto done;
    }
  }
  if (cfg->n_srt > 0) {
    out.srt = tvhead_srt_open(cfg);
    if (!out.srt) {
      rc = 1;
      goto done;
    }
  }
  out.pacer = bitrate_pacer_new(cfg->bitrate_kbps ? (double)cfg->bitrate_kbps * 1000.0 : 0.0, cfg->stuff, cfg->burst_limit);
  if (!out.pacer) {
    log_line("bitrate pacer setup failed");
    rc = 1;
    goto done;
  }

  if (cas_wanted && !cas_needs_discovery) {
    cas = cas_start_multi(cfg, NULL, NULL, 0);
    if (!cas) {
      log_line("cas: failed to start");
      rc = 1;
      goto done;
    }
    tvhead_mpts_set_cas(mpts, cas);
  }

  run_start = mono_seconds();
  if (cas_needs_discovery)
    cas_gate_deadline = run_start + CAS_KEYWORD_DISCOVERY_TIMEOUT_S;

  while (!signal_stop_requested()) {
    struct pollfd pfds[ARGS_MAX_INPUTS];
    unsigned pfd_slot[ARGS_MAX_INPUTS];
    nfds_t npfd = 0;
    double now;
    time_t now_t, deadline;
    int timeout_ms = MPTS_POLL_MAX_MS;

    deadline = retryset_next_deadline(rs);
    if (deadline != RETRYSET_NEVER) {
      long remain_s = (long)(deadline - time(NULL));
      int remain_ms = remain_s <= 0 ? 0 : (int)(remain_s * 1000);
      if (remain_ms < timeout_ms)
        timeout_ms = remain_ms;
    }
    for (unsigned i = 0; i < n; i++) {
      int fd = poll_fd_for_input(rs, i, &pfds[npfd].events);
      if (fd < 0)
        continue;
      pfds[npfd].fd = fd;
      pfds[npfd].revents = 0;
      pfd_slot[npfd] = i;
      npfd++;
    }
    poll(npfd ? pfds : NULL, npfd, timeout_ms);
    if (signal_stop_requested())
      break;

    tvhead_srt_service(&out);

    now = mono_seconds();
    now_t = time(NULL);
    for (unsigned i = 0; i < n; i++)
      retryset_service(rs, i, now_t);

    {
      mpts_tick_t tk;
      tk.rs = rs;
      tk.cfg = cfg;
      tk.progs = progs;
      tk.mpts = mpts;
      tk.cas = cas;
      tk.input_stats = input_stats;
      tk.metrics_on = metrics_on;
      tk.out = &out;
      tk.tsm = tsm_p;
      tk.now = now;
      tk.now_t = now_t;

      for (unsigned k = 0; k < n; k++) {
        tvsrc_t *src;

        unsigned i = (rr_start + k) % n;
        src = retryset_result(rs, i);
        if (!src)
          continue;
        if (!input_poll_ready(i, pfd_slot, pfds, npfd))
          continue;

        if (!progs[i].rx)
          discover_input(&tk, i, src);
        else
          feed_input(&tk, i, src);
      }
    }
    rr_start = n ? (rr_start + 1) % n : 0;

    if (eit_busy >= 0 && progs[eit_busy].rx && remux_eit_pending(progs[eit_busy].rx)) {
      remux_emit_eit(progs[eit_busy].rx, OUT_PID_EIT, &eit_cc, 1, packet_cb, &out);
      if (!remux_eit_mid_section(progs[eit_busy].rx))
        eit_busy = -1;
    } else {
      eit_busy = -1;
      for (unsigned i = 0; i < n; i++) {
        if (!progs[i].rx || !remux_eit_pending(progs[i].rx))
          continue;
        remux_emit_eit(progs[i].rx, OUT_PID_EIT, &eit_cc, 1, packet_cb, &out);
        if (remux_eit_mid_section(progs[i].rx)) {
          eit_busy = (int)i;
          break;
        }
      }
    }

    if (!cas && now - last_cat >= INTERVAL_CAT_S) {
      last_cat = now;
      emit_source_cat_passthrough(progs, n, &cat_cc, tsm_p, packet_cb, &out);
    }

    if (cas_needs_discovery && !cas && check_cas_discovery_gate(cfg, progs, n, mpts, cas_gate_deadline, &cas) != 0) {
      rc = 1;
      goto done;
    }

    mpts_tick(mpts, now, packet_cb, &out);
    if (cas) {
      cas_wall_tick(cas, now);
      if (cas_failed(cas)) {
        log_line("cas: fatal error, stopping");
        rc = 1;
        goto done;
      }
      if (signal_reload_requested())
        cas_reload_receivers(cas);
    }
    {
      int stuff_n = bitrate_stuff_due(out.pacer);
      for (unsigned k = 0; k < (unsigned)stuff_n; k++)
        send_null_packet(&out);
    }
    if (cfg->verbose && now - last_stat >= 1.0) {
      fprintf(stderr, "\r%.0fs, %llu TS packets\033[K", now - run_start, out.packets);
      fflush(stderr);
      last_stat = now;
    }
    {
      unsigned active = 0;
      for (unsigned i = 0; i < n; i++)
        if (progs[i].rx)
          active++;
      emit_metrics(mx, now, &out, n, active, input_stats, n, tsm_p, cas);
    }
  }

done:
  if (cas)
    cas_flush(cas, packet_cb, &out);
  flush_batch(&out);
  for (unsigned i = 0; progs && i < n; i++)
    program_reset(&progs[i]);
  free(progs);
  if (mpts)
    mpts_free(mpts);
  if (rs)
    retryset_free(rs);
  if (cas)
    cas_stop(cas);
  if (out.rtph)
    rtpheader_free(out.rtph);
  if (out.rist)
    ristout_close(out.rist);
  if (out.srt)
    srtsink_close(out.srt);
  if (out.pacer)
    bitrate_pacer_free(out.pacer);
  if (outmc)
    mcast_close(outmc);

  if (cfg->verbose && log_stderr_is_tty())
    fputc('\n', stderr);
  if (rc == 0)
    log_line("stopped.");
  return rc ? 1 : 0;
}
