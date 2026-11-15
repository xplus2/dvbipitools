/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lib/helper/log.h"
#include "lib/sys/signal.h"

#include "../input/inputset.h"
#include "../input/source.h"
#include "../mux/tspacketizer.h"
#include "priv.h"
#include "lib/helper/dateutil.h"

/* lib/mux/mpts.c is tool-agnostic (shared with dipitvhead). these adapt our concrete types to its void*-based ops vtable. */
static int mpts_program_get_sdt_info(void *ctx, psi_sdt_entry_t *out) {
  return tspacketizer_get_sdt_info((tspacketizer_t *)ctx, out);
}
static size_t mpts_program_build_eit(void *ctx, unsigned char *out, size_t cap) {
  return tspacketizer_build_eit((tspacketizer_t *)ctx, out, cap);
}
static int mpts_program_eit_pending(const void *ctx) {
  return tspacketizer_eit_pending((const tspacketizer_t *)ctx);
}
static const mpts_program_ops_t mpts_program_ops = {mpts_program_get_sdt_info, mpts_program_build_eit, mpts_program_eit_pending};

uint32_t compute_ready_mask(const unsigned *pfd_slot, const struct pollfd *pfds, nfds_t npfd) {
  uint32_t mask = 0;
  for (unsigned pfd_i = 0; pfd_i < npfd; pfd_i++) {
    if (pfds[pfd_i].revents & (POLLIN | POLLERR | POLLHUP)) mask |= 1u << pfd_slot[pfd_i];
  }
  return mask;
}

static void sync_input_bytes(mpts_tick_t *tk, const source_t *src, unsigned i) {
  unsigned long long sb;

  if (!tk->metrics_on) return;
  sb = source_bytes_total(src);
  if (sb > tk->last_synced_bytes[i]) {
    tk->input_stats[i].bytes_total += sb - tk->last_synced_bytes[i];
    tk->last_synced_bytes[i] = sb;
  }
}

/* stream_type 0: empty PMT until first frame sets codec. NULL on OOM */
static tspacketizer_t *program_new(const config_t *cfg, const inputset_t *is, cas_t *cas, unsigned i, unsigned stream_type, unsigned aac_profile_level) {
  tspacketizer_cfg_t tc;
  tspacketizer_t *t;
  memset(&tc, 0, sizeof tc);
  tc.tsid = cfg->tsid;
  tc.onid = cfg->onid;
  tc.sid = inputset_sid(is, i);
  tc.stream_type = stream_type;
  tc.aac_profile_level = aac_profile_level;
  tc.network_name = "";
  tc.service_name = inputset_service_name(is, i);
  tc.provider_name = inputset_provider_name(is, i);
  tc.pmt_pid = inputset_pmt_pid(is, i);
  tc.audio_pid = inputset_audio_pid(is, i);
  tc.standalone = 0;
  t = tspacketizer_new(&tc);
  if (t && cas) tspacketizer_set_cas(t, cas);
  return t;
}

static void slot_down(mpts_tick_t *tk, source_t *src, unsigned i, net_err_reason_t reason) {
  input_metrics_note_read(tk->metrics_on ? &tk->input_stats[i] : NULL, -1, reason);
  if (tk->metrics_on) tk->rm->framing_errors_total += source_take_resyncs(src) + (reason == NET_ERR_FORMAT);
  if (tk->metrics_on) tk->input_stats[i].up = 0;
  sync_input_bytes(tk, src, i);
  inputset_mark_down(tk->is, i, tk->now_t);
  mpts_set_program(tk->mpts, i, NULL);
}

/* processes one input slot for this tick: reads up to RADIOHEAD_MAX_FRAMES_PER_TICK frames,
   feeds packetizer, updates metrics. -1: fatal (tspacketizer_new() OOM), caller must abort */
int process_input_slot(mpts_tick_t *tk, unsigned i) {
  source_t *src;
  int connected_now;
  int ready = 0;
  net_err_reason_t reason = NET_ERR_OTHER;

  src = inputset_source(tk->is, i);
  connected_now = src != NULL;
  if (connected_now && !tk->was_connected[i]) {
    tk->timeline[i] = 0;
    if (tk->tsps[i]) tspacketizer_mark_discontinuity(tk->tsps[i]);
    tk->last_synced_bytes[i] = 0;
    tk->pace_deadline[i] = tk->now;
  }
  tk->was_connected[i] = connected_now;
  if (!src) return 0;
  ready = (int)((tk->ready_mask >> i) & 1u);
  if (!ready) ready = source_has_buffered(src);
  if (!ready) {
    /* silent peer, socket open: fd never readable */
    if (source_stalled(src, &reason)) slot_down(tk, src, i, reason);
    return 0;
  }

  unsigned frames_this_visit = 0;
  while (frames_this_visit < RADIOHEAD_MAX_FRAMES_PER_TICK && tk->pace_deadline[i] <= tk->now + RADIOHEAD_PACE_TOLERANCE_S) {
    source_frame_t f;
    uint64_t pts;
    int r = source_next_frame(src, &f, &reason);
    if (r == 0) break;
    if (r < 0) {
      slot_down(tk, src, i, reason);
      return 0;
    }
    frames_this_visit++;
    if (source_take_resumed(src) && tk->pace_deadline[i] < tk->now) tk->pace_deadline[i] = tk->now;
    if (tk->metrics_on) {
      tk->input_stats[i].last_data_time = (double)time(NULL);
      tk->rm->frames_total[f.codec]++;
    }
    if (!tk->tsps[i]) {
      tk->tsps[i] = program_new(tk->cfg, tk->is, tk->cas, i, f.stream_type, f.aac_profile_level);
      if (!tk->tsps[i]) return -1;
      log_line_ansi("input \e[1;30m%u\e[0m (\e[1;30m%s\e[0m): codec detected: \e[1;30m%s\e[0m, \e[1;30m%u\e[0m Hz", i, inputset_service_name(tk->is, i), source_codec_name(f.codec), f.sample_rate);
    } else {
      int had_codec = tspacketizer_has_codec(tk->tsps[i]);
      if (tspacketizer_set_codec(tk->tsps[i], f.stream_type, f.aac_profile_level))
        log_line_ansi("input \e[1;30m%u\e[0m (\e[1;30m%s\e[0m): codec %s: \e[1;30m%s\e[0m, \e[1;30m%u\e[0m Hz", i, inputset_service_name(tk->is, i), had_codec ? "changed" : "detected", source_codec_name(f.codec), f.sample_rate);
    }
    mpts_set_program(tk->mpts, i, tk->tsps[i]);
    pace_reanchor(&tk->pace_deadline[i], tk->now, tk->tsps[i]);

    if (tk->metas[i].dirty) {
      int accepted = tspacketizer_set_metadata(tk->tsps[i], tk->metas[i].artist, tk->metas[i].title);
      tk->metas[i].dirty = 0;
      if (accepted) log_line_ansi("input \e[1;30m%u\e[0m (\e[1;30m%s\e[0m): now: \e[0;36m%s%s%s\e[0m", i, inputset_service_name(tk->is, i), tk->metas[i].artist, (tk->metas[i].artist[0] && tk->metas[i].title[0]) ? " - " : "", tk->metas[i].title);
    }
    pts = timeline_pts(tk->timeline[i]);
    timeline_add(&tk->timeline[i], f.samples, f.sample_rate);
    tk->pace_deadline[i] += (double)f.samples / (double)f.sample_rate;
    tspacketizer_feed(tk->tsps[i], pts, (uint32_t)((uint64_t)f.samples * 90000ULL / f.sample_rate), tk->now, f.data, f.len, tk->out->insp ? &packet_cb_inspect : &packet_cb, tk->out);
  }
  sync_input_bytes(tk, src, i);
  if (tk->metrics_on) tk->rm->framing_errors_total += source_take_resyncs(src);
  return 0;
}

typedef struct {
  const config_t *cfg;
  metrics_exporter_t *mx;
  unsigned n;
  out_ctx_t out;
  meta_state_t metas[RADIOHEAD_MAX_INPUTS];
  void *meta_ctxs[RADIOHEAD_MAX_INPUTS];
  tspacketizer_t *tsps[RADIOHEAD_MAX_INPUTS];
  uint64_t timeline[RADIOHEAD_MAX_INPUTS];
  uint64_t cas_prev_pts[RADIOHEAD_MAX_INPUTS]; /* leading input advance drives the shared CAS clock */
  uint64_t cas_clock;
  double pace_deadline[RADIOHEAD_MAX_INPUTS];
  int was_connected[RADIOHEAD_MAX_INPUTS];
  psi_pat_entry_t entries[RADIOHEAD_MAX_INPUTS];
  input_metrics_t input_stats[RADIOHEAD_MAX_INPUTS];
  unsigned long long last_synced_bytes[RADIOHEAD_MAX_INPUTS];
  radio_metrics_t rm;
  int metrics_on;
  inputset_t *is;
  mpts_t *mpts;
  cas_t *cas;
  tsinspect_t *in_insp[RADIOHEAD_MAX_INPUTS];
  source_insp_t sins[RADIOHEAD_MAX_INPUTS];
  tsinspect_set_t set;
  srtsink_queue_ctx_t qctx;
  unsigned rr_start;
} mpts_run_t;

static ts_packet_cb run_packet_cb(const mpts_run_t *r) { return r->out.insp ? &packet_cb_inspect : &packet_cb; }

static int run_setup(mpts_run_t *r) {
  const config_t *cfg = r->cfg;
  for (unsigned i = 0; i < r->n; i++) {
    r->meta_ctxs[i] = &r->metas[i];
    r->metas[i].rm = r->metrics_on ? &r->rm : NULL;
    r->sins[i].slot = &r->in_insp[i];
    r->sins[i].level = cfg->metrics_inspect_ts;
    r->sins[i].known_pids = cfg->metrics_known_pids;
    r->sins[i].n_known_pids = cfg->metrics_n_known_pids;
  }
  r->is = inputset_new(cfg, meta_cb, r->meta_ctxs, r->metrics_on ? r->input_stats : NULL, r->sins);
  if (!r->is) return -1;
  for (unsigned i = 0; i < r->n; i++) {
    r->entries[i].program_number = inputset_sid(r->is, i);
    r->entries[i].pmt_pid = inputset_pmt_pid(r->is, i);
  }
  r->mpts = mpts_new(cfg->tsid, cfg->onid, cfg->nit_text, r->entries, r->n, &mpts_program_ops);
  if (!r->mpts) return -1;
  if (cfg->cas_algo != CAS_ALGO_NONE || cfg->biss2_enabled || cfg->biss1_enabled || cfg->biss2_ca_enabled) {
    unsigned audio_pids[RADIOHEAD_MAX_INPUTS] = {0};
    for (unsigned i = 0; i < r->n; i++) audio_pids[i] = inputset_audio_pid(r->is, i);
    r->cas = cas_start(cfg, audio_pids, r->n);
    if (!r->cas) {
      log_code_ansi("RS300163", "cas: \e[0;31mfailed to start\e[0m");
      return -1;
    }
    radiohead_mpts_set_cas(r->mpts, r->cas);
  }
  for (unsigned i = 0; i < r->n; i++) {
    r->tsps[i] = program_new(cfg, r->is, r->cas, i, 0, 0);
    if (!r->tsps[i]) return -1;
  }
  return 0;
}

static int build_poll(const mpts_run_t *r, struct pollfd *pfds, unsigned *pfd_slot, nfds_t *npfd) {
  int timeout_ms = RADIOHEAD_POLL_MAX_MS;
  time_t deadline = inputset_next_deadline(r->is);
  double now_pre;
  *npfd = 0;
  if (deadline != INPUTSET_NEVER) {
    long remain_s = (long)(deadline - (time_t)mono_seconds());
    int remain_ms = remain_s <= 0 ? 0 : (int)(remain_s * 1000);
    if (remain_ms < timeout_ms) timeout_ms = remain_ms;
  }
  now_pre = mono_seconds();
  for (unsigned i = 0; i < r->n; i++) {
    int fd = inputset_poll_fd(r->is, i);
    double pcr_due = r->tsps[i] ? tspacketizer_pcr_next_due(r->tsps[i]) : -1.0;
    if (pcr_due >= 0.0) {
      int wait_ms = pcr_due <= now_pre ? 0 : 1 + (int)((pcr_due - now_pre) * 1000.0);
      if (wait_ms < timeout_ms) timeout_ms = wait_ms;
    }
    if (fd < 0) {
      if (inputset_source(r->is, i) && timeout_ms > RADIOHEAD_IDLE_WAIT_MS) timeout_ms = RADIOHEAD_IDLE_WAIT_MS;
      continue;
    }
    /* sock readable, skip&sleep. avoid poll() spin. */
    if (inputset_source(r->is, i) && r->pace_deadline[i] > now_pre + RADIOHEAD_PACE_TOLERANCE_S) {
      /* ceil, not trunc */
      int wait_ms = 1 + (int)((r->pace_deadline[i] - now_pre - RADIOHEAD_PACE_TOLERANCE_S) * 1000.0);
      if (wait_ms < 1) wait_ms = 1;
      if (wait_ms < timeout_ms) timeout_ms = wait_ms;
      continue;
    }
    pfds[*npfd].fd = fd;
    pfds[*npfd].events = inputset_poll_events(r->is, i);
    pfds[*npfd].revents = 0;
    pfd_slot[(*npfd)++] = i;
  }
  return timeout_ms;
}

static int cas_advance(mpts_run_t *r) {
  uint64_t adv = 0;
  for (unsigned i = 0; i < r->n; i++) {
    uint64_t cur = timeline_pts(r->timeline[i]);
    if (cur > r->cas_prev_pts[i] && cur - r->cas_prev_pts[i] > adv) adv = cur - r->cas_prev_pts[i];
    r->cas_prev_pts[i] = cur;
  }
  if (adv) {
    r->cas_clock += adv;
    cas_clock_tick(r->cas, r->cas_clock);
  }
  return radiohead_cas_service(r->cas);
}

static int run_pass(mpts_run_t *r, double start, double *last_stat) {
  struct pollfd pfds[RADIOHEAD_MAX_INPUTS];
  unsigned pfd_slot[RADIOHEAD_MAX_INPUTS];
  nfds_t npfd;
  int timeout_ms = build_poll(r, pfds, pfd_slot, &npfd);
  ts_packet_cb cb;
  mpts_tick_t tk;
  unsigned active = 0;
  double now;
  double now_pcr;
  poll(npfd ? pfds : NULL, npfd, timeout_ms);
  if (signal_stop_requested()) return 0;
  radiohead_srt_service(&r->out);
  now = mono_seconds();
  for (unsigned i = 0; i < r->n; i++) inputset_service(r->is, i, (time_t)now);
  tk = (mpts_tick_t){.is = r->is, .mpts = r->mpts, .cas = r->cas, .cfg = r->cfg, .tsps = r->tsps, .metas = r->metas, .timeline = r->timeline, .pace_deadline = r->pace_deadline,
                     .was_connected = r->was_connected, .input_stats = r->input_stats, .last_synced_bytes = r->last_synced_bytes, .rm = &r->rm, .out = &r->out,
                     .metrics_on = r->metrics_on, .now = now, .now_t = (time_t)now, .pfd_slot = pfd_slot, .pfds = pfds, .npfd = npfd,
                     .ready_mask = compute_ready_mask(pfd_slot, pfds, npfd)};
  r->out.cur_pts = (uint64_t)(now * 90000.0);
  for (unsigned k = 0; k < r->n; k++)
    if (process_input_slot(&tk, (r->rr_start + k) % r->n) != 0) return 1;
  if (r->n) r->rr_start = (r->rr_start + 1) % r->n;
  cb = run_packet_cb(r);
  now_pcr = mono_seconds();
  for (unsigned i = 0; i < r->n; i++) tspacketizer_pcr_flush(r->tsps[i], now_pcr, cb, &r->out);
  /* PAT lists all programs: idle ones keep PMT */
  for (unsigned i = 0; i < r->n; i++)
    if (!r->was_connected[i] || now > r->pace_deadline[i] + RADIOHEAD_PACE_TOLERANCE_S) tspacketizer_idle(r->tsps[i], now, cb, &r->out);
  if (r->out.insp) tsinspect_tick(r->out.insp, now);
  mpts_tick(r->mpts, now, cb, &r->out);
  if (r->cas && cas_advance(r)) return 1;
  if (r->cfg->verbose && now - *last_stat >= 1.0) {
    fprintf(stderr, "\r%.0fs, %llu TS packets\033[K", now - start, r->out.packets);
    fflush(stderr);
    *last_stat = now;
  }
  for (unsigned i = 0; i < r->n; i++) active += r->was_connected[i] != 0;
  flush_batch(&r->out); /* each pass: PCR must not wait for full datagram */
  emit_metrics(r->mx, now, &r->out, r->n, active, r->input_stats, r->n, &r->rm, r->cas);
  return 0;
}

static void run_teardown(mpts_run_t *r) {
  if (r->cas) cas_flush(r->cas, run_packet_cb(r), &r->out);
  flush_batch(&r->out);
  for (unsigned i = 0; i < r->n; i++)
    if (r->tsps[i]) tspacketizer_free(r->tsps[i]);
  if (r->mpts) mpts_free(r->mpts);
  if (r->is) inputset_free(r->is);
  if (r->cas) cas_stop(r->cas);
  metrics_exporter_clear_extras(r->mx);
  for (unsigned i = 0; i < r->n; i++) tsinspect_free(r->in_insp[i]);
  radiohead_output_close(&r->out);
}

int radiohead_run_mpts(const config_t *cfg, metrics_exporter_t *mx) {
  mpts_run_t r;
  double start;
  double last_stat = 0;
  int rc = 0;

  memset(&r, 0, sizeof r);
  r.cfg = cfg;
  r.mx = mx;
  r.n = cfg->n_inputs;
  r.metrics_on = metrics_exporter_enabled(mx);
  r.set = (tsinspect_set_t){r.in_insp, cfg->n_inputs, &r.out.insp, 1};
  r.qctx = (srtsink_queue_ctx_t){&r.out.srt, 1, metrics_queue_level(cfg->metrics_inspect_ts)};
  if (radiohead_output_open(cfg, &r.out)) {
    radiohead_output_close(&r.out);
    return 1;
  }
  if (r.out.insp) metrics_exporter_set_extra(mx, tsinspect_set_put, &r.set);
  if (r.out.srt && r.qctx.queue_metrics) metrics_exporter_add_extra(mx, srtsink_put_queue_metrics, &r.qctx);

  if (run_setup(&r)) rc = 1;
  start = mono_seconds();
  while (!rc && !signal_stop_requested()) rc = run_pass(&r, start, &last_stat);

  run_teardown(&r);
  if (cfg->verbose && log_stderr_is_tty()) fputc('\n', stderr);
  if (rc == 0) log_line("stopped.");
  return rc;
}
