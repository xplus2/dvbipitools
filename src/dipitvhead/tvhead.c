/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lib/demux/psi.h"
#include "lib/demux/tspack.h"
#include "lib/log.h"
#include "lib/metrics/export.h"
#include "lib/mux/mpts.h"
#include "lib/mux/rtpheader.h"
#include "lib/net/multicast.h"
#include "lib/net/retryset.h"
#include "lib/signal.h"

#include "cas/cas.h"
#include "input/source.h"
#include "mux/bitrate.h"
#include "mux/remux.h"
#include "tvhead.h"
#include "version.h"

/* how long to watch PAT-listed PMT candidates before giving up */
#define DISCOVERY_TIMEOUT_S 8.0
#define TS_PER_DGRAM 7

/* multi-input: --cas-pids-video/audio need every program's own live ES discovery to resolve,
   but CAS is one shared ECMG/CW session. wait up to this long for every configured input to be discovered,
   then fail fast (naming which ones aren't ready) rather than start with a partial pid list or block forever.
   explicit numeric --cas-pids needs none of this: it starts eagerly at t=0, same as dipiradiohead's scheme. */
#define CAS_KEYWORD_DISCOVERY_TIMEOUT_S 15.0
#define MPTS_POLL_MAX_MS 100
#define MPTS_READ_CHUNK_BYTES (32 * 188) /* per program, per tick - caps one input's backlog from delaying the rest */

static int psi_cb(void *v, const unsigned char *pkt) {
  psi_feed((psi_t *)v, pkt);
  return 0;
}

static void print_program_list(const psi_t *psi) {
  int n, i;
  const psi_program_t *p = psi_pat_programs(psi, &n);
  log_line("PAT: %d program(s) found", n);
  for (i = 0; i < n; i++)
    log_line("  program %u, PMT pid 0x%x", p[i].program_number, p[i].pmt_pid);
}

static void print_discovered(const psi_t *psi) {
  int n, i;
  const psi_es_t *es = psi_es(psi, &n);
  log_line_ansi("locked: program \e[0;33m%u\e[0m (PMT pid 0x%x, PCR pid 0x%x)", psi_program_number(psi), psi_pmt_pid(psi), psi_pcr_pid(psi));
  for (i = 0; i < n; i++) {
    const psi_es_t *e = &es[i];
    if (e->lang[0])
      log_line("  pid 0x%x: %s (%s) lang=%s", e->pid, pid_class_name(e->cls), codec_name(e->codec), e->lang);
    else
      log_line("  pid 0x%x: %s (%s)", e->pid, pid_class_name(e->cls), codec_name(e->codec));
  }
  if (*psi_service_name(psi))
    log_line("  SDT: service=\"%s\" provider=\"%s\"", psi_service_name(psi), psi_provider_name(psi));
  if (*psi_network_name(psi))
    log_line("  NIT: network=\"%s\"", psi_network_name(psi));
}

typedef struct {
  tspack_t pz;
  int listed, checked_pmt_pid;
} discover_state_t;

/* one non-blocking increment of discovery. 1 ready, 0 still pending, -1 hard error or -p pid
   not in PAT. caller tracks its own timeout and calls psi_select_pmt_pid() up front if wanted. */
static int discover_step(discover_state_t *ds, tvsrc_t *src, const dipitvhead_input_t *input, psi_t *psi, input_metrics_t *im) {
  unsigned char buf[65536];
  net_err_reason_t reason = NET_ERR_OTHER;
  ssize_t n = tvsrc_read(src, buf, sizeof buf, &reason);

  input_metrics_note_read(im, n, reason);
  if (n < 0)
    return -1;
  if (n > 0)
    tspack_feed(&ds->pz, buf, (size_t)n, psi_cb, psi);

  if (psi_have_pat(psi) && !ds->listed) {
    print_program_list(psi);
    ds->listed = 1;
  }
  if (input->pmt_pid && ds->listed && !ds->checked_pmt_pid) {
    int cnt, k, found = 0;
    const psi_program_t *p = psi_pat_programs(psi, &cnt);
    for (k = 0; k < cnt; k++)
      if (p[k].pmt_pid == input->pmt_pid) {
        found = 1;
        break;
      }
    if (!found) {
      log_line("-p 0x%x not present in PAT", input->pmt_pid);
      return -1;
    }
    ds->checked_pmt_pid = 1;
  }
  if (psi_ready(psi))
    return 1;
  return 0;
}

/* 1 ready, 0 timeout, -1 hard error or -p pid not in PAT */
static int discover(tvsrc_t *src, const dipitvhead_input_t *input, psi_t *psi, input_metrics_t *im) {
  discover_state_t ds;
  double start = mono_seconds();

  memset(&ds, 0, sizeof ds);
  if (input->pmt_pid)
    psi_select_pmt_pid(psi, input->pmt_pid);

  while (!signal_stop_requested()) {
    int r = discover_step(&ds, src, input, psi, im);
    if (r)
      return r;
    if (mono_seconds() - start >= DISCOVERY_TIMEOUT_S)
      return 0;
  }
  return -1;
}

typedef struct {
  mcast_t *mc;
  int rtp;
  rtpheader_t *rtph;
  bitrate_pacer_t *pacer;
  unsigned char batch[12 + TS_PER_DGRAM * 188]; /* [0,12): RTP header headroom, unused if !rtp */
  int batch_count;
  int had_error;
  unsigned long long packets;
  unsigned long long errors;
} out_ctx_t;

/* pace/account once per datagram, not per packet - keeps burst_limit's sleep off the per-packet path */
static void flush_batch(out_ctx_t *o) {
  size_t n = (size_t)o->batch_count * 188;

  if (o->batch_count == 0)
    return;
  bitrate_pace(o->pacer);
  if (o->rtp) {
    rtpheader_build(o->rtph, (uint32_t)(mono_seconds() * 90000.0), o->batch, 12);
    if (mcast_send(o->mc, o->batch, 12 + n) < 0) {
      o->had_error = 1;
      o->errors++;
    }
  } else if (mcast_send(o->mc, o->batch + 12, n) < 0) {
    o->had_error = 1;
    o->errors++;
  }
  bitrate_account_n(o->pacer, (unsigned)o->batch_count);
  o->batch_count = 0;
}

static void packet_cb(void *ctx, const unsigned char *pkt188) {
  out_ctx_t *o = ctx;
  memcpy(o->batch + 12 + (size_t)o->batch_count * 188, pkt188, 188);
  o->batch_count++;
  o->packets++;
  if (o->batch_count == TS_PER_DGRAM)
    flush_batch(o);
}

static void send_null_packet(out_ctx_t *o) {
  unsigned char pkt[188];
  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = 0x1F;
  pkt[2] = 0xFF;
  pkt[3] = 0x10;
  packet_cb(o, pkt);
}

typedef struct {
  remux_t *rx;
  out_ctx_t *out;
  double now;
  ts_metrics_t *tsm;
} feed_ctx_t;

static int remux_cb(void *v, const unsigned char *pkt) {
  feed_ctx_t *f = v;
  remux_feed(f->rx, f->now, pkt, packet_cb, f->out, f->tsm);
  return 0;
}

/* common + output + input + TS-integrity + CAS metrics, on mx's own interval
   (metrics_exporter_due gates/no-ops when disabled) */
static void emit_metrics(metrics_exporter_t *mx, double now, const out_ctx_t *out, unsigned configured_services, unsigned active_services,
                          const input_metrics_t *inputs, unsigned n_inputs, const ts_metrics_t *tsm, cas_t *cas) {
  metrics_writer_t w;

  if (!metrics_exporter_due(mx, now))
    return;
  if (metrics_exporter_begin(mx, &w, TOOL_VERSION))
    return;
  metrics_writer_put(&w, METRICS_ID_OUTPUT_PACKETS_TOTAL, NULL, out->packets);
  metrics_writer_put(&w, METRICS_ID_OUTPUT_BYTES_TOTAL, NULL, out->packets * 188ULL);
  metrics_writer_put(&w, METRICS_ID_OUTPUT_ERRORS_TOTAL, NULL, out->errors);
  metrics_writer_put(&w, METRICS_ID_CONFIGURED_SERVICES, NULL, configured_services);
  metrics_writer_put(&w, METRICS_ID_ACTIVE_SERVICES, NULL, active_services);
  metrics_writer_put(&w, METRICS_ID_TV_SOURCE_PROGRAM_UP, NULL, active_services > 0 ? 1 : 0);
  metrics_writer_put_inputs(&w, inputs, n_inputs);
  metrics_writer_put(&w, METRICS_ID_TS_PACKETS_TOTAL, NULL, tsm->ts_packets);
  metrics_writer_put(&w, METRICS_ID_TS_SYNC_ERRORS_TOTAL, NULL, tsm->ts_sync_errors);
  metrics_writer_put(&w, METRICS_ID_TS_CONTINUITY_ERRORS_TOTAL, NULL, tsm->ts_continuity_errors);
  metrics_writer_put(&w, METRICS_ID_TS_DISCONTINUITIES_TOTAL, NULL, tsm->ts_discontinuities);
  metrics_writer_put(&w, METRICS_ID_PCR_DISCONTINUITIES_TOTAL, NULL, tsm->pcr_discontinuities);
  {
    psi_table_t t;
    for (t = 0; t < PSI_TABLE_COUNT; t++) {
      metrics_writer_put(&w, METRICS_ID_PSI_SECTIONS_TOTAL, psi_table_name(t), tsm->psi_sections_total[t]);
      metrics_writer_put(&w, METRICS_ID_PSI_ERRORS_TOTAL, psi_table_name(t), tsm->psi_errors_total[t]);
    }
  }
  metrics_writer_put(&w, METRICS_ID_TV_SOURCE_PMT_UPDATES_TOTAL, NULL, tsm->pmt_updates_total);
  metrics_writer_put(&w, METRICS_ID_TV_REMUX_PACKETS_TOTAL, NULL, tsm->remux_packets_total);
  metrics_writer_put(&w, METRICS_ID_TV_REMUX_DROPPED_PACKETS_TOTAL, NULL, tsm->remux_dropped_packets_total);
  metrics_writer_put(&w, METRICS_ID_TV_AIT_SECTIONS_TOTAL, NULL, tsm->ait_sections_total);
  if (cas) {
    cas_metrics_t cm;
    size_t i, n = cas_vendor_count(cas);
    cas_get_metrics(cas, &cm);
    metrics_writer_put(&w, METRICS_ID_CAS_SCRAMBLED_PACKETS_TOTAL, NULL, cm.scrambled_packets_total);
    metrics_writer_put(&w, METRICS_ID_CAS_UNEXPECTED_CLEAR_PACKETS_TOTAL, NULL, cm.unexpected_clear_packets_total);
    for (i = 0; i < n; i++) {
      char label[16];
      cas_metrics_t vm;
      cas_vendor_metrics(cas, i, &vm);
      snprintf(label, sizeof label, "0x%08x", cas_vendor_super_cas_id(cas, i));
      metrics_writer_put(&w, METRICS_ID_CAS_ECMG_CONNECTED, label, vm.ecmg_connected ? 1 : 0);
      metrics_writer_put(&w, METRICS_ID_CAS_EMMG_CLIENTS, label, vm.emmg_clients);
      metrics_writer_put(&w, METRICS_ID_CAS_CRYPTOPERIOD_TRANSITIONS_TOTAL, label, vm.cryptoperiod_transitions_total);
      metrics_writer_put(&w, METRICS_ID_CAS_ECM_TOTAL, label, vm.ecm_total);
      metrics_writer_put(&w, METRICS_ID_CAS_ECM_ERRORS_TOTAL, label, vm.ecm_errors_total);
      metrics_writer_put(&w, METRICS_ID_CAS_EMM_TOTAL, label, vm.emm_total);
    }
  }
  metrics_exporter_send(mx, &w);
}

/* steady-state: read, remux, send, until stop/hard error. returns 0 clean stop, -1 error */
static int run_output(tvsrc_t *src, remux_t *rx, out_ctx_t *out, const config_t *cfg, cas_t *cas, metrics_exporter_t *mx, input_metrics_t *im,
                       ts_metrics_t *tsm) {
  unsigned char buf[65536];
  tspack_t pz;
  feed_ctx_t fc;
  double start = mono_seconds(), last_stat = 0;
  net_err_reason_t reason;

  memset(&pz, 0, sizeof pz);
  fc.rx = rx;
  fc.out = out;
  fc.tsm = tsm;

  while (!signal_stop_requested()) {
    int stuff_n, k;
    double now;
    ssize_t n;
    reason = NET_ERR_OTHER;
    n = tvsrc_read(src, buf, sizeof buf, &reason);
    input_metrics_note_read(im, n, reason);
    if (n < 0) {
      cas_flush(cas, packet_cb, out);
      return -1;
    }
    now = mono_seconds();
    if (n > 0) {
      fc.now = now;
      tspack_feed(&pz, buf, (size_t)n, remux_cb, &fc);
    }
    if (out->had_error) {
      cas_flush(cas, packet_cb, out);
      return -1;
    }
    if (cas && cas_failed(cas)) {
      log_line("cas: fatal error, stopping");
      cas_flush(cas, packet_cb, out);
      return -1;
    }
    stuff_n = bitrate_stuff_due(out->pacer);
    for (k = 0; k < stuff_n; k++)
      send_null_packet(out);
    if (out->had_error) {
      cas_flush(cas, packet_cb, out);
      return -1;
    }
    if (cfg->verbose && now - last_stat >= 1.0) {
      fprintf(stderr, "\r%.0fs, %llu TS packets\033[K", now - start, out->packets);
      fflush(stderr);
      last_stat = now;
    }
    emit_metrics(mx, now, out, 1, 1, im, 1, tsm, cas);
  }
  cas_flush(cas, packet_cb, out);
  return 0;
}

static int tvhead_run_single(const config_t *cfg, metrics_exporter_t *mx) {
  int rc = 0;
  mcast_t *outmc;
  out_ctx_t out;
  input_metrics_t im;
  ts_metrics_t tsm;
  int metrics_on = metrics_exporter_enabled(mx);
  input_metrics_t *im_p = metrics_on ? &im : NULL;
  ts_metrics_t *tsm_p = metrics_on ? &tsm : NULL;

  memset(&out, 0, sizeof out);
  memset(&im, 0, sizeof im);
  memset(&tsm, 0, sizeof tsm);
  outmc = mcast_open_send(cfg->family, cfg->mcast_group, cfg->mcast_port, cfg->iface_out, (int)cfg->ttl);
  if (!outmc)
    return 1;
  out.mc = outmc;
  out.rtp = cfg->rtp;
  if (cfg->rtp) {
    out.rtph = rtpheader_new();
    if (!out.rtph) {
      mcast_close(outmc);
      return 1;
    }
  }

  while (!signal_stop_requested()) {
    net_err_reason_t reason = NET_ERR_OTHER;
    tvsrc_t *src = tvsrc_open(cfg, &cfg->inputs[0], &reason);
    psi_t *psi;
    int r;

    if (!src) {
      if (metrics_on) {
        im.up = 0;
        im.errors_total[reason]++;
      }
      if (cfg->error_retry_s <= 0) {
        rc = 1;
        break;
      }
      log_line("input error, retrying in %lds", cfg->error_retry_s);
      sleep_interruptible(cfg->error_retry_s);
      continue;
    }
    if (metrics_on) {
      if (im.seen_open)
        im.reconnects_total++;
      im.seen_open = 1;
      im.up = 1;
    }

    psi = psi_new();
    if (!psi) {
      tvsrc_close(src);
      rc = 1;
      break;
    }

    r = discover(src, &cfg->inputs[0], psi, im_p);
    if (r == 1) {
      out_program_pids_t pids;
      remux_t *rx;

      out_program_pids(0, &pids);
      rx = remux_new(cfg, &cfg->inputs[0], psi, &pids, 1);
      if (!rx) {
        log_line("remux setup failed");
        r = -1;
      } else {
        out.pacer = bitrate_pacer_new(cfg->bitrate_kbps ? (double)cfg->bitrate_kbps * 1000.0 : 0.0, cfg->stuff, cfg->burst_limit);
        if (!out.pacer) {
          log_line("bitrate pacer setup failed");
          remux_free(rx);
          r = -1;
        } else {
          int cas_wanted = cfg->cas_algo != CAS_ALGO_NONE;
          cas_t *cas = NULL;
          if (cas_wanted) {
            int es_count;
            const out_es_t *es = remux_es(rx, &es_count);
            cas = cas_start(cfg, psi, es, es_count, remux_pcr_pid_out(rx));
            if (cas)
              remux_set_cas(rx, cas);
            else
              log_line("cas setup failed");
          }
          if (cas_wanted && !cas) {
            r = -1;
          } else {
            print_discovered(psi);
            run_output(src, rx, &out, cfg, cas, mx, im_p, tsm_p);
            if (cas)
              cas_stop(cas);
          }
          bitrate_pacer_free(out.pacer);
          out.pacer = NULL;
          remux_free(rx);
        }
      }
    } else if (r == 0) {
      log_line("no live PMT found within %.0fs (use -p to select one, or check the source)", DISCOVERY_TIMEOUT_S);
    }
    psi_free(psi);
    tvsrc_close(src);
    if (metrics_on)
      im.up = 0;
    if (signal_stop_requested() || out.had_error)
      break;
    if (cfg->error_retry_s <= 0) {
      rc = 1;
      break;
    }
    log_line("retrying in %lds", cfg->error_retry_s);
    sleep_interruptible(cfg->error_retry_s);
  }

  flush_batch(&out);
  if (out.rtph)
    rtpheader_free(out.rtph);
  mcast_close(outmc);

  if (cfg->verbose && log_stderr_is_tty())
    fputc('\n', stderr);
  if (rc == 0 && !out.had_error)
    log_line("stopped.");
  return (rc || out.had_error) ? 1 : 0;
}

/* mpts.c is tool-agnostic (shared with dipiradiohead); these adapt our concrete types to its
   void*-based ops vtables. dipitvhead's EIT is real passthrough, not synthesized, and rides its
   own pid merged directly by this file (remux_emit_eit()) - never through mpts_t, so build_eit/
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
  size_t i, n = cas_vendor_count(cas);
  if (n > MPTS_MAX_CAS_VENDORS)
    n = MPTS_MAX_CAS_VENDORS;
  for (i = 0; i < n; i++) {
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

/* open_step() only gets the opening handle, not slot_ctx - wrap it to carry im too */
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
  /* discovery until rx exists; stays alive after too - rx's out_es_t[].src borrows into it */
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

static int tvhead_run_mpts(const config_t *cfg, metrics_exporter_t *mx) {
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
  int cas_wanted = cfg->cas_algo != CAS_ALGO_NONE;
  int cas_needs_discovery = cas_wanted && (cfg->cas_pids_video || cfg->cas_pids_audio);
  double cas_gate_deadline = 0.0;
  unsigned char eit_cc = 0;
  unsigned i, k, rr_start = 0;
  double run_start, last_stat = 0;
  int rc = 0;
  input_metrics_t input_stats[ARGS_MAX_INPUTS]; /* outlives mpts_program_t's per-reconnect memset */
  ts_metrics_t tsm;
  int metrics_on = metrics_exporter_enabled(mx);
  ts_metrics_t *tsm_p = metrics_on ? &tsm : NULL;

  memset(input_stats, 0, sizeof input_stats);
  memset(&tsm, 0, sizeof tsm);

  progs = calloc(n, sizeof *progs);
  if (!progs)
    return 1;

  for (i = 0; i < n; i++) {
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

  outmc = mcast_open_send(cfg->family, cfg->mcast_group, cfg->mcast_port, cfg->iface_out, (int)cfg->ttl);
  if (!outmc) {
    rc = 1;
    goto done;
  }
  memset(&out, 0, sizeof out);
  out.mc = outmc;
  out.rtp = cfg->rtp;
  if (cfg->rtp) {
    out.rtph = rtpheader_new();
    if (!out.rtph) {
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
    for (i = 0; i < n; i++) {
      int fd = retryset_poll_fd(rs, i);
      short events = retryset_poll_events(rs, i);
      if (fd < 0) {
        tvsrc_t *src = retryset_result(rs, i);
        if (src) {
          fd = tvsrc_fd(src);
          events = POLLIN;
        }
      }
      if (fd < 0)
        continue;
      pfds[npfd].fd = fd;
      pfds[npfd].events = events;
      pfds[npfd].revents = 0;
      pfd_slot[npfd] = i;
      npfd++;
    }
    poll(npfd ? pfds : NULL, npfd, timeout_ms);
    if (signal_stop_requested())
      break;

    now = mono_seconds();
    now_t = time(NULL);
    for (i = 0; i < n; i++)
      retryset_service(rs, i, now_t);

    for (k = 0; k < n; k++) {
      tvsrc_t *src;
      unsigned pfd_i;
      int ready = 0;

      i = (rr_start + k) % n;
      src = retryset_result(rs, i);
      if (!src)
        continue;
      for (pfd_i = 0; pfd_i < npfd; pfd_i++)
        if (pfd_slot[pfd_i] == i && (pfds[pfd_i].revents & (POLLIN | POLLERR | POLLHUP))) {
          ready = 1;
          break;
        }
      if (!ready)
        continue;

      if (!progs[i].rx) {
        int r;

        if (!progs[i].psi) {
          progs[i].psi = psi_new();
          progs[i].discover_start = mono_seconds();
          if (cfg->inputs[i].pmt_pid)
            psi_select_pmt_pid(progs[i].psi, cfg->inputs[i].pmt_pid);
        }
        r = discover_step(&progs[i].ds, src, &cfg->inputs[i], progs[i].psi, metrics_on ? &input_stats[i] : NULL);
        if (r == 0 && mono_seconds() - progs[i].discover_start < DISCOVERY_TIMEOUT_S)
          continue;
        if (r <= 0) {
          if (r == 0)
            log_line("input %u: no live PMT found within %.0fs", i, DISCOVERY_TIMEOUT_S);
          program_reset(&progs[i]);
          retryset_mark_down(rs, i, now_t);
          if (metrics_on)
            input_stats[i].up = 0;
          continue;
        }

        {
          out_program_pids_t pids;
          out_program_pids(i, &pids);
          log_line("input %u:", i);
          print_discovered(progs[i].psi);
          progs[i].rx = remux_new(cfg, &cfg->inputs[i], progs[i].psi, &pids, 0);
        }
        if (!progs[i].rx) {
          log_line("input %u: remux setup failed", i);
          psi_free(progs[i].psi);
          progs[i].psi = NULL;
          retryset_mark_down(rs, i, now_t);
          if (metrics_on)
            input_stats[i].up = 0;
          continue;
        }
        /* cas already running (non-keyword --cas-pids, or a reconnect after it started):
           this new remux_t needs it too, same as the initial attach below for the
           keyword-discovery path. without this, packets never get scrambled. */
        if (cas)
          remux_set_cas(progs[i].rx, cas);
        /* psi stays alive: remux_t's out_es_t[].src borrows pointers into it for its
           entire lifetime (see pmtbuild.h) - freed alongside rx in program_reset() */
        mpts_set_program(mpts, i, progs[i].rx);
        continue; /* steady-state starts next visit */
      }

      {
        feed_ctx_t fc;
        read_backlog_t *bl = &progs[i].backlog;
        size_t remaining = bl->len - bl->off;
        size_t chunk;

        if (remaining == 0) {
          net_err_reason_t reason = NET_ERR_OTHER;
          ssize_t rn = tvsrc_read(src, bl->buf, sizeof bl->buf, &reason);
          input_metrics_note_read(metrics_on ? &input_stats[i] : NULL, rn, reason);
          if (rn < 0) {
            mpts_set_program(mpts, i, NULL);
            program_reset(&progs[i]);
            retryset_mark_down(rs, i, now_t);
            if (metrics_on)
              input_stats[i].up = 0;
            continue;
          }
          if (rn == 0)
            continue;
          bl->len = (size_t)rn;
          bl->off = 0;
          remaining = bl->len;
        }
        chunk = remaining < MPTS_READ_CHUNK_BYTES ? remaining : MPTS_READ_CHUNK_BYTES;
        fc.rx = progs[i].rx;
        fc.out = &out;
        fc.now = now;
        fc.tsm = tsm_p;
        tspack_feed(&progs[i].pz, bl->buf + bl->off, chunk, remux_cb, &fc);
        bl->off += chunk;
        if (bl->off >= bl->len)
          bl->len = bl->off = 0;
        if (out.had_error) {
          rc = 1;
          goto done;
        }
      }
    }
    rr_start = n ? (rr_start + 1) % n : 0;

    for (i = 0; i < n; i++)
      if (progs[i].rx)
        remux_emit_eit(progs[i].rx, OUT_PID_EIT, &eit_cc, 1, packet_cb, &out);

    if (cas_needs_discovery && !cas) {
      unsigned ready_count = 0;
      for (i = 0; i < n; i++)
        if (progs[i].rx)
          ready_count++;
      if (ready_count == n) {
        const out_es_t *es_lists[ARGS_MAX_INPUTS];
        int es_counts[ARGS_MAX_INPUTS];
        for (i = 0; i < n; i++)
          es_lists[i] = remux_es(progs[i].rx, &es_counts[i]);
        cas = cas_start_multi(cfg, es_lists, es_counts, n);
        if (!cas) {
          log_line("cas: failed to start");
          rc = 1;
          goto done;
        }
        tvhead_mpts_set_cas(mpts, cas);
        for (i = 0; i < n; i++)
          remux_set_cas(progs[i].rx, cas);
      } else if (mono_seconds() >= cas_gate_deadline) {
        log_line("cas: --cas-pids-video/--cas-pids-audio need every -i discovered within %.0fs:", CAS_KEYWORD_DISCOVERY_TIMEOUT_S);
        for (i = 0; i < n; i++)
          if (!progs[i].rx)
            log_line("  input %u: %s", i, progs[i].psi ? "still discovering" : "not connected");
        rc = 1;
        goto done;
      }
    }

    mpts_tick(mpts, now, packet_cb, &out);
    if (cas) {
      cas_wall_tick(cas, now);
      if (cas_failed(cas)) {
        log_line("cas: fatal error, stopping");
        rc = 1;
        goto done;
      }
    }
    {
      int stuff_n = bitrate_stuff_due(out.pacer);
      for (k = 0; k < (unsigned)stuff_n; k++)
        send_null_packet(&out);
    }
    if (out.had_error) {
      rc = 1;
      goto done;
    }
    if (cfg->verbose && now - last_stat >= 1.0) {
      fprintf(stderr, "\r%.0fs, %llu TS packets\033[K", now - run_start, out.packets);
      fflush(stderr);
      last_stat = now;
    }
    {
      unsigned active = 0;
      for (i = 0; i < n; i++)
        if (progs[i].rx)
          active++;
      emit_metrics(mx, now, &out, n, active, input_stats, n, tsm_p, cas);
    }
  }

done:
  if (cas)
    cas_flush(cas, packet_cb, &out);
  if (outmc)
    flush_batch(&out);
  for (i = 0; progs && i < n; i++)
    program_reset(&progs[i]);
  free(progs);
  if (mpts)
    mpts_free(mpts);
  if (rs)
    retryset_free(rs);
  if (cas)
    cas_stop(cas);
  if (outmc) {
    if (out.rtph)
      rtpheader_free(out.rtph);
    if (out.pacer)
      bitrate_pacer_free(out.pacer);
    mcast_close(outmc);
  }

  if (cfg->verbose && outmc && log_stderr_is_tty())
    fputc('\n', stderr);
  if (rc == 0 && outmc && !out.had_error)
    log_line("stopped.");
  return (rc || (outmc && out.had_error)) ? 1 : 0;
}

int tvhead_run(const config_t *cfg, metrics_exporter_t *mx) {
  if (cfg->n_inputs > 1)
    return tvhead_run_mpts(cfg, mx);
  return tvhead_run_single(cfg, mx);
}
