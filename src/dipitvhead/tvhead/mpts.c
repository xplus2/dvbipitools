/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/log.h"
#include "lib/mux/mpts.h"
#include "lib/mux/psi_build.h"
#include "lib/mux/tspacket_write.h"
#include "lib/net/retryset.h"
#include "lib/signal.h"
#include "priv.h"

#define INTERVAL_CAT_S 0.1 /* matches remux.c's own INTERVAL_PAT_PMT_S cadence */

/* how long to wait for every configured input to be discovered when
   --cas-pids-video/audio need every program's own live ES discovery to resolve,
   before failing fast (naming which ones aren't ready) rather than starting with
   a partial pid list or blocking forever. explicit numeric --cas-pids needs none
   of this: it starts eagerly at t=0, same as dipiradiohead's scheme. */
#define CAS_KEYWORD_DISCOVERY_TIMEOUT_S 15.0
#define MPTS_POLL_MAX_MS 100
#define MPTS_READ_CHUNK_BYTES (32 * 188) /* per program, per tick; caps one input's backlog from delaying rest */

/* mpts.c is tool-agnostic (shared with dipiradiohead); these adapt our concrete types to its
   void*-based ops vtables. dipitvhead's EIT is real passthrough, not synthesized, and rides its
   own pid merged directly by this file (remux_emit_eit()); never through mpts_t; build_eit/
   eit_pending are no-ops here. */
static int mpts_program_get_sdt_info(void *ctx, psi_sdt_entry_t *out) {
  return remux_get_sdt_info((remux_t *)ctx, out);
}
static size_t mpts_program_build_eit(void *ctx, unsigned char *out, size_t cap) {
  (void)ctx;
  (void)out;
  (void)cap;
  return 0;
}
static int mpts_program_eit_pending(const void *ctx) {
  (void)ctx;
  return 0;
}
static const mpts_program_ops_t mpts_program_ops = {mpts_program_get_sdt_info, mpts_program_build_eit, mpts_program_eit_pending};

static size_t mpts_cas_build_cat(void *ctx, unsigned char *out, size_t cap) { return cas_build_cat((cas_t *)ctx, out, cap); }
static int mpts_cas_ecm_due(void *ctx, size_t vendor_idx, double now_s, unsigned char *out, size_t cap, size_t *out_len) { return cas_vendor_ecm_due((cas_t *)ctx, vendor_idx, now_s, out, cap, out_len); }
static int mpts_cas_next_emm(void *ctx, size_t vendor_idx, unsigned char *out, size_t cap, size_t *out_len) { return cas_vendor_next_emm((cas_t *)ctx, vendor_idx, out, cap, out_len); }
static const mpts_cas_ops_t mpts_cas_ops = {mpts_cas_build_cat, mpts_cas_ecm_due, mpts_cas_next_emm};

static void tvhead_mpts_set_cas(mpts_t *mpts, cas_t *cas) {
  mpts_cas_vendor_pid_t vendors[MPTS_MAX_CAS_VENDORS];
  size_t n = cas_vendor_count(cas);
  if (n > MPTS_MAX_CAS_VENDORS)
    n = MPTS_MAX_CAS_VENDORS;
  for (size_t i = 0; i < n; i++) {
    vendors[i].ecm_pid = cas_vendor_ecm_pid(cas, i);
    vendors[i].emm_pid = cas_vendor_emm_pid(cas, i);
  }
  mpts_set_cas(mpts, cas, &mpts_cas_ops, vendors, n);
}

typedef struct {
  const config_t *cfg;
  const dipitvhead_input_t *input;
  input_metrics_t *im;
} tv_slot_ctx_t;

/* open_step() only gets opening handle, not slot_ctx; wrap it to carry im too */
typedef struct {
  tvsrc_open_t *o;
  input_metrics_t *im;
} tv_opening_t;

static void *tv_open_start(void *ctx) {
  tv_slot_ctx_t *sc = ctx;
  net_err_reason_t reason = NET_ERR_OTHER;
  tv_opening_t *w = calloc(1, sizeof *w);
  if (!w)
    return NULL;
  w->im = sc->im;
  w->o = tvsrc_open_async_start(sc->cfg, sc->input, &reason);
  if (!w->o) {
    if (sc->im)
      sc->im->errors_total[reason]++;
    free(w);
    return NULL;
  }
  return w;
}
static int tv_open_poll_fd(const void *o) { return tvsrc_open_async_poll_fd(((const tv_opening_t *)o)->o); }
static short tv_open_poll_events(const void *o) { return tvsrc_open_async_poll_events(((const tv_opening_t *)o)->o); }
static retryset_open_state_t tv_open_step(void *o) {
  tv_opening_t *w = o;
  net_err_reason_t reason = NET_ERR_OTHER;
  switch (tvsrc_open_async_step(w->o, &reason)) {
  case TVSRC_OPEN_DONE:
    if (w->im) {
      if (w->im->seen_open)
        w->im->reconnects_total++;
      w->im->seen_open = 1;
      w->im->up = 1;
    }
    return RETRYSET_OPEN_DONE;
  case TVSRC_OPEN_ERROR:
    if (w->im)
      w->im->errors_total[reason]++;
    return RETRYSET_OPEN_ERROR;
  default:
    return RETRYSET_OPEN_PENDING;
  }
}
static void *tv_open_take(void *o) {
  tv_opening_t *w = o;
  tvsrc_t *r = tvsrc_open_async_take(w->o);
  free(w);
  return r;
}
static void tv_open_free(void *o) {
  tv_opening_t *w = o;
  if (w) {
    tvsrc_open_async_free(w->o);
    free(w);
  }
}
static int tv_result_fd(const void *r) { return tvsrc_fd(r); }
static void tv_result_close(void *r) { tvsrc_close((tvsrc_t *)r); }
static const retryset_ops_t tv_retry_ops = {tv_open_start,  tv_open_poll_fd, tv_open_poll_events, tv_open_step,
                                             tv_open_take,   tv_open_free,    tv_result_fd,        tv_result_close};

typedef struct {
  unsigned char buf[65536];
  size_t len, off; /* off..len: not yet fed to tspack_feed() this read */
} read_backlog_t;

typedef struct {
  /* discovery until rx exists; stays alive after too; rx's out_es_t[].src borrows into it */
  psi_t *psi;
  discover_state_t ds;
  double discover_start;

  /* steady state, live once discovered */
  remux_t *rx;
  tspack_t pz;
  read_backlog_t backlog;
} mpts_program_t;

static void program_reset(mpts_program_t *p) {
  if (p->rx)
    remux_free(p->rx);
  if (p->psi)
    psi_free(p->psi);
  memset(p, 0, sizeof *p);
}

/* resolves the fd/events to poll for this input: its retry-connect fd, or once connected,
   its live source fd (POLLIN only) */
static int poll_fd_for_input(retryset_t *rs, unsigned i, short *events_out) {
  int fd = retryset_poll_fd(rs, i);
  *events_out = retryset_poll_events(rs, i);
  if (fd < 0) {
    tvsrc_t *src = retryset_result(rs, i);
    if (src) {
      fd = tvsrc_fd(src);
      *events_out = POLLIN;
    }
  }
  return fd;
}

static int input_poll_ready(unsigned i, const unsigned *pfd_slot, const struct pollfd *pfds, nfds_t npfd) {
  for (unsigned pfd_i = 0; pfd_i < npfd; pfd_i++)
    if (pfd_slot[pfd_i] == i && (pfds[pfd_i].revents & (POLLIN | POLLERR | POLLHUP)))
      return 1;
  return 0;
}

typedef struct {
  retryset_t *rs;
  const config_t *cfg;
  mpts_program_t *progs;
  mpts_t *mpts;
  cas_t *cas;
  input_metrics_t *input_stats;
  int metrics_on;
  out_ctx_t *out;
  ts_metrics_t *tsm;
  double now;
  time_t now_t;
} mpts_tick_t;

/* runs (or continues) PSI discovery for input i until a remux_t can be built. always
   settles this visit (never falls through to feed_input() in the same tick) */
static void discover_input(mpts_tick_t *tk, unsigned i, tvsrc_t *src) {
  int r;

  if (!tk->progs[i].psi) {
    tk->progs[i].psi = psi_new();
    if (!tk->progs[i].psi) {
      log_line("input %u: out of memory allocating psi state, retrying next poll", i);
      return;
    }
    tk->progs[i].discover_start = mono_seconds();
    if (tk->cfg->inputs[i].pmt_pid)
      psi_select_pmt_pid(tk->progs[i].psi, tk->cfg->inputs[i].pmt_pid);
  }
  r = discover_step(&tk->progs[i].ds, src, &tk->cfg->inputs[i], tk->progs[i].psi, tk->metrics_on ? &tk->input_stats[i] : NULL);
  if (r == 0 && mono_seconds() - tk->progs[i].discover_start < DISCOVERY_TIMEOUT_S)
    return;
  if (r <= 0) {
    if (r == 0)
      log_line("input %u: no live PMT found within %.0fs", i, DISCOVERY_TIMEOUT_S);
    program_reset(&tk->progs[i]);
    retryset_mark_down(tk->rs, i, tk->now_t);
    if (tk->metrics_on)
      tk->input_stats[i].up = 0;
    return;
  }

  {
    out_program_pids_t pids;
    out_program_pids(i, &pids);
    log_line("input %u:", i);
    print_discovered(tk->progs[i].psi);
    tk->progs[i].rx = remux_new(tk->cfg, &tk->cfg->inputs[i], tk->progs[i].psi, &pids, 0);
  }
  if (!tk->progs[i].rx) {
    log_line("input %u: remux setup failed", i);
    psi_free(tk->progs[i].psi);
    tk->progs[i].psi = NULL;
    retryset_mark_down(tk->rs, i, tk->now_t);
    if (tk->metrics_on)
      tk->input_stats[i].up = 0;
    return;
  }
  /* cas already running (non-keyword --cas-pids, or a reconnect after it started):
     this new remux_t needs it too, same as initial attach below for
     keyword-discovery path. without this, packets never get scrambled. */
  if (tk->cas)
    remux_set_cas(tk->progs[i].rx, tk->cas);
  /* psi stays alive: remux_t's out_es_t[].src borrows pointers into it for its
     entire lifetime (see pmtbuild.h); freed alongside rx in program_reset() */
  mpts_set_program(tk->mpts, i, tk->progs[i].rx);
}

/* feeds already-buffered/newly-read bytes for input i's live remux_t into tspack_feed() */
static void feed_input(mpts_tick_t *tk, unsigned i, tvsrc_t *src) {
  feed_ctx_t fc;
  read_backlog_t *bl = &tk->progs[i].backlog;
  size_t remaining = bl->len - bl->off;
  size_t chunk;

  if (remaining == 0) {
    net_err_reason_t reason = NET_ERR_OTHER;
    ssize_t rn = tvsrc_read(src, bl->buf, sizeof bl->buf, &reason);
    input_metrics_note_read(tk->metrics_on ? &tk->input_stats[i] : NULL, rn, reason);
    if (rn < 0) {
      mpts_set_program(tk->mpts, i, NULL);
      program_reset(&tk->progs[i]);
      retryset_mark_down(tk->rs, i, tk->now_t);
      if (tk->metrics_on)
        tk->input_stats[i].up = 0;
      return;
    }
    if (rn == 0)
      return;
    bl->len = (size_t)rn;
    bl->off = 0;
    remaining = bl->len;
  }
  chunk = remaining < MPTS_READ_CHUNK_BYTES ? remaining : MPTS_READ_CHUNK_BYTES;
  fc.rx = tk->progs[i].rx;
  fc.out = tk->out;
  fc.now = tk->now;
  fc.tsm = tk->tsm;
  tspack_feed(&tk->progs[i].pz, bl->buf + bl->off, chunk, remux_cb, &fc);
  bl->off += chunk;
  if (bl->off >= bl->len)
    bl->len = bl->off = 0;
}

/* once every input's rx exists (all keyword-CAS pids resolved), starts CAS with the full
   pid list. past the gate deadline without that, fails fast naming the stragglers.
   0: ok (*cas_out set if cas just started). -1: fatal, caller must abort */
static int check_cas_discovery_gate(const config_t *cfg, mpts_program_t *progs, unsigned n, mpts_t *mpts,
                                     double cas_gate_deadline, cas_t **cas_out) {
  unsigned ready_count = 0;
  for (unsigned i = 0; i < n; i++)
    if (progs[i].rx)
      ready_count++;
  if (ready_count == n) {
    const out_es_t *es_lists[ARGS_MAX_INPUTS];
    int es_counts[ARGS_MAX_INPUTS];
    cas_t *cas;
    for (unsigned i = 0; i < n; i++)
      es_lists[i] = remux_es(progs[i].rx, &es_counts[i]);
    cas = cas_start_multi(cfg, es_lists, es_counts, n);
    if (!cas) {
      log_line("cas: failed to start");
      return -1;
    }
    tvhead_mpts_set_cas(mpts, cas);
    for (unsigned i = 0; i < n; i++)
      remux_set_cas(progs[i].rx, cas);
    *cas_out = cas;
  } else if (mono_seconds() >= cas_gate_deadline) {
    log_line("cas: --cas-pids-video/--cas-pids-audio need every -i discovered within %.0fs:", CAS_KEYWORD_DISCOVERY_TIMEOUT_S);
    for (unsigned i = 0; i < n; i++)
      if (!progs[i].rx)
        log_line("  input %u: %s", i, progs[i].psi ? "still discovering" : "not connected");
    return -1;
  }
  return 0;
}

/* EMM passthrough: mux-wide CAT, merged from each program's descriptor.
   exclusive with own CAS (remux_new()). mp version of remux.c send_psi_tables()'s CAT handler */
static void emit_source_cat_passthrough(mpts_program_t *progs, unsigned n, unsigned char *cat_cc, ts_metrics_t *tsm_p, remux_packet_cb cb, void *ctx) {
  unsigned char cat_desc[ARGS_MAX_INPUTS * 6];
  size_t cat_desc_len = 0;
  int have_desc = 0;
  unsigned char sec[4096];
  unsigned char ptr0 = 0x00;
  size_t sl;
  for (unsigned i = 0; i < n; i++) {
    size_t dl;
    if (!progs[i].rx || cat_desc_len + 6 > sizeof cat_desc)
      continue;
    dl = remux_source_emm_descriptor(progs[i].rx, cat_desc + cat_desc_len, sizeof cat_desc - cat_desc_len);
    if (dl) {
      cat_desc_len += dl;
      have_desc = 1;
    }
  }
  if (!have_desc)
    return;

  sl = psi_build_cat(0, cat_desc, cat_desc_len, sec, sizeof sec);
  if (sl) {
    ts_packet_emit(OUT_PID_CAT, cat_cc, &ptr0, sec, sl, 0, 0, cb, ctx);
    if (tsm_p)
      tsm_p->psi_sections_total[PSI_TABLE_CAT]++;
  } else if (tsm_p) {
    tsm_p->psi_errors_total[PSI_TABLE_CAT]++;
  }
}

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

    for (unsigned i = 0; i < n; i++)
      if (progs[i].rx)
        remux_emit_eit(progs[i].rx, OUT_PID_EIT, &eit_cc, 1, packet_cb, &out);

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
