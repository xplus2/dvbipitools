/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lib/helper/log.h"
#include "lib/helper/signal.h"

#include "../input/inputset.h"
#include "../input/source.h"
#include "../mux/tspacketizer.h"
#include "priv.h"

#define MPTS_POLL_MAX_MS 100
#define MPTS_MAX_FRAMES_PER_TICK 32 /* per input, per tick. caps one input's backlog delaying others */

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

typedef struct {
  inputset_t *is;
  mpts_t *mpts;
  cas_t *cas;
  const config_t *cfg;
  tspacketizer_t **tsps;
  meta_state_t *metas;
  uint64_t *samples_total;
  int *was_connected;
  input_metrics_t *input_stats;
  unsigned long long *last_synced_bytes;
  radio_metrics_t *rm;
  out_ctx_t *out;
  int metrics_on;
  double now;
  time_t now_t;
  const unsigned *pfd_slot;
  const struct pollfd *pfds;
  nfds_t npfd;
} mpts_tick_t;

/* processes one input slot for this poll tick: reads up to MPTS_MAX_FRAMES_PER_TICK frames,
   feeds the packetizer, updates metrics. -1: fatal (tspacketizer_new() OOM), caller must abort */
static int process_input_slot(mpts_tick_t *tk, unsigned i) {
  source_t *src;
  int connected_now;
  int ready = 0;

  src = inputset_source(tk->is, i);
  connected_now = src != NULL;

  if (connected_now && !tk->was_connected[i]) {
    tk->samples_total[i] = 0;
    tk->last_synced_bytes[i] = 0;
  }
  tk->was_connected[i] = connected_now;
  if (!src)
    return 0;

  /* only read a slot poll() actually reported ready. source_next_frame() can block for several seconds
     (http_read()'s SO_RCVTIMEO) on a connected-but-currently-silent source, which would otherwise stall other inputs */
  for (unsigned pfd_i = 0; pfd_i < tk->npfd; pfd_i++)
    if (tk->pfd_slot[pfd_i] == i && (tk->pfds[pfd_i].revents & (POLLIN | POLLERR | POLLHUP))) {
      ready = 1;
      break;
    }
  if (!ready)
    return 0;

  unsigned frames_this_visit = 0;
  while (frames_this_visit < MPTS_MAX_FRAMES_PER_TICK) {
    source_frame_t f;
    uint64_t pts;
    net_err_reason_t reason = NET_ERR_OTHER;
    int r = source_next_frame(src, &f, &reason);

    if (r == 0)
      break;
    if (r < 0) {
      input_metrics_note_read(tk->metrics_on ? &tk->input_stats[i] : NULL, -1, reason);
      if (tk->metrics_on && reason == NET_ERR_FORMAT)
        tk->rm->framing_errors_total++;
      if (tk->metrics_on)
        tk->input_stats[i].up = 0;
      inputset_mark_down(tk->is, i, tk->now_t);
      mpts_set_program(tk->mpts, i, NULL);
      break;
    }
    frames_this_visit++;
    if (tk->metrics_on) {
      tk->input_stats[i].last_data_time = (double)time(NULL);
      tk->rm->frames_total[f.codec]++;
    }

    if (!tk->tsps[i]) {
      tspacketizer_cfg_t tc;
      tc.tsid = tk->cfg->tsid;
      tc.onid = tk->cfg->onid;
      tc.sid = inputset_sid(tk->is, i);
      tc.stream_type = f.stream_type;
      tc.network_name = "";
      tc.service_name = inputset_service_name(tk->is, i);
      tc.pmt_pid = inputset_pmt_pid(tk->is, i);
      tc.audio_pid = inputset_audio_pid(tk->is, i);
      tc.standalone = 0;
      tk->tsps[i] = tspacketizer_new(&tc);
      if (!tk->tsps[i])
        return -1;
      if (tk->cas)
        tspacketizer_set_cas(tk->tsps[i], tk->cas);
      log_line("input %u (%s): codec detected: %s, %u Hz", i, inputset_service_name(tk->is, i), codec_name(f.codec), f.sample_rate);
    }
    mpts_set_program(tk->mpts, i, tk->tsps[i]);

    if (tk->metas[i].dirty) {
      tspacketizer_set_metadata(tk->tsps[i], tk->metas[i].artist, tk->metas[i].title);
      tk->metas[i].dirty = 0;
      log_line("input %u (%s): now playing: %s%s%s", i, inputset_service_name(tk->is, i), tk->metas[i].artist,
                (tk->metas[i].artist[0] && tk->metas[i].title[0]) ? " - " : "", tk->metas[i].title);
    }

    pts = tk->samples_total[i] * 90000ULL / f.sample_rate;
    tk->samples_total[i] += f.samples;
    tk->out->cur_pts = pts;
    tspacketizer_feed(tk->tsps[i], pts, tk->now, f.data, f.len, packet_cb, tk->out);
  }
  if (tk->metrics_on) {
    unsigned long long sb = source_bytes_total(src);
    if (sb > tk->last_synced_bytes[i]) {
      tk->input_stats[i].bytes_total += sb - tk->last_synced_bytes[i];
      tk->last_synced_bytes[i] = sb;
    }
  }
  return 0;
}

int radiohead_run_mpts(const config_t *cfg, metrics_exporter_t *mx) {
  mcast_t *mc = NULL;
  out_ctx_t out;
  meta_state_t metas[RADIOHEAD_MAX_INPUTS];
  void *meta_ctxs[RADIOHEAD_MAX_INPUTS];
  tspacketizer_t *tsps[RADIOHEAD_MAX_INPUTS];
  uint64_t samples_total[RADIOHEAD_MAX_INPUTS];
  int was_connected[RADIOHEAD_MAX_INPUTS];
  psi_pat_entry_t entries[RADIOHEAD_MAX_INPUTS];
  input_metrics_t input_stats[RADIOHEAD_MAX_INPUTS];
  unsigned long long last_synced_bytes[RADIOHEAD_MAX_INPUTS];
  radio_metrics_t rm;
  int metrics_on = metrics_exporter_enabled(mx);
  unsigned n = cfg->n_inputs;
  inputset_t *is = NULL;
  mpts_t *mpts = NULL;
  cas_t *cas = NULL;
  double start, last_stat = 0;
  int rc = 0;

  memset(&out, 0, sizeof out);
  memset(metas, 0, sizeof metas);
  memset(tsps, 0, sizeof tsps);
  memset(samples_total, 0, sizeof samples_total);
  memset(was_connected, 0, sizeof was_connected);
  memset(input_stats, 0, sizeof input_stats);
  memset(last_synced_bytes, 0, sizeof last_synced_bytes);
  memset(&rm, 0, sizeof rm);

  if (cfg->mcast_port) {
    mc = mcast_open_send(cfg->family, cfg->mcast_group, cfg->mcast_port, cfg->iface, (int)cfg->ttl);
    if (!mc)
      return 1;
    out.mc = mc;
    out.rtp = cfg->rtp;
    if (cfg->rtp) {
      out.rtph = rtpheader_new();
      if (!out.rtph) {
        mcast_close(mc);
        return 1;
      }
    }
  }
  if (cfg->n_rist > 0) {
    out.rist = radiohead_rist_open(cfg);
    if (!out.rist) {
      if (out.rtph)
        rtpheader_free(out.rtph);
      if (mc)
        mcast_close(mc);
      return 1;
    }
  }
  if (cfg->n_srt > 0) {
    out.srt = radiohead_srt_open(cfg);
    if (!out.srt) {
      if (out.rist)
        ristout_close(out.rist);
      if (out.rtph)
        rtpheader_free(out.rtph);
      if (mc)
        mcast_close(mc);
      return 1;
    }
  }

  for (unsigned i = 0; i < n; i++) {
    meta_ctxs[i] = &metas[i];
    metas[i].rm = metrics_on ? &rm : NULL;
  }
  is = inputset_new(cfg, meta_cb, meta_ctxs, metrics_on ? input_stats : NULL);
  if (!is) {
    rc = 1;
    goto done;
  }
  for (unsigned i = 0; i < n; i++) {
    entries[i].program_number = inputset_sid(is, i);
    entries[i].pmt_pid = inputset_pmt_pid(is, i);
  }
  mpts = mpts_new(cfg->tsid, cfg->onid, cfg->nit_text, entries, n, &mpts_program_ops);
  if (!mpts) {
    rc = 1;
    goto done;
  }

  if (cfg->cas_algo != CAS_ALGO_NONE || cfg->biss2_enabled || cfg->biss1_enabled || cfg->biss2_ca_enabled) {
    unsigned audio_pids[RADIOHEAD_MAX_INPUTS];
    for (unsigned i = 0; i < n; i++)
      audio_pids[i] = inputset_audio_pid(is, i);
    cas = cas_start(cfg, audio_pids, n);
    if (!cas) {
      log_line("cas: failed to start");
      rc = 1;
      goto done;
    }
    radiohead_mpts_set_cas(mpts, cas);
  }

  start = mono_seconds();
  unsigned rr_start = 0;
  while (!signal_stop_requested()) {
    struct pollfd pfds[RADIOHEAD_MAX_INPUTS];
    unsigned pfd_slot[RADIOHEAD_MAX_INPUTS];
    nfds_t npfd = 0;
    double now;
    time_t now_t, deadline;
    int timeout_ms = MPTS_POLL_MAX_MS;

    deadline = inputset_next_deadline(is);
    if (deadline != INPUTSET_NEVER) {
      long remain_s = (long)(deadline - time(NULL));
      int remain_ms = remain_s <= 0 ? 0 : (int)(remain_s * 1000);
      if (remain_ms < timeout_ms)
        timeout_ms = remain_ms;
    }
    for (unsigned i = 0; i < n; i++) {
      int fd = inputset_poll_fd(is, i);
      if (fd < 0)
        continue;
      pfds[npfd].fd = fd;
      pfds[npfd].events = inputset_poll_events(is, i);
      pfds[npfd].revents = 0;
      pfd_slot[npfd] = i;
      npfd++;
    }
    poll(npfd ? pfds : NULL, npfd, timeout_ms);
    if (signal_stop_requested())
      break;

    radiohead_srt_service(&out);

    now = mono_seconds();
    now_t = time(NULL);
    for (unsigned i = 0; i < n; i++)
      inputset_service(is, i, now_t);

    {
      mpts_tick_t tk;
      tk.is = is;
      tk.mpts = mpts;
      tk.cas = cas;
      tk.cfg = cfg;
      tk.tsps = tsps;
      tk.metas = metas;
      tk.samples_total = samples_total;
      tk.was_connected = was_connected;
      tk.input_stats = input_stats;
      tk.last_synced_bytes = last_synced_bytes;
      tk.rm = &rm;
      tk.out = &out;
      tk.metrics_on = metrics_on;
      tk.now = now;
      tk.now_t = now_t;
      tk.pfd_slot = pfd_slot;
      tk.pfds = pfds;
      tk.npfd = npfd;

      for (unsigned k = 0; k < n; k++) {
        unsigned i = (rr_start + k) % n;
        if (process_input_slot(&tk, i) != 0) {
          rc = 1;
          goto done;
        }
      }
    }
    if (n)
      rr_start = (rr_start + 1) % n;

    mpts_tick(mpts, now, packet_cb, &out);
    if (cas) {
      cas_clock_tick(cas, (uint64_t)(now * 90000.0));
      if (cas_failed(cas)) {
        log_line("cas: fatal, stopping");
        rc = 1;
        goto done;
      }
      if (signal_reload_requested())
        cas_reload_receivers(cas);
    }
    if (cfg->verbose && now - last_stat >= 1.0) {
      fprintf(stderr, "\r%.0fs, %llu TS packets\033[K", now - start, out.packets);
      fflush(stderr);
      last_stat = now;
    }
    {
      unsigned active = 0;
      for (unsigned i = 0; i < n; i++)
        if (tsps[i])
          active++;
      emit_metrics(mx, now, &out, n, active, input_stats, n, &rm, cas);
    }
  }

done:
  if (cas)
    cas_flush(cas, packet_cb, &out);
  flush_batch(&out);
  for (unsigned i = 0; i < n; i++)
    if (tsps[i])
      tspacketizer_free(tsps[i]);
  if (mpts)
    mpts_free(mpts);
  if (is)
    inputset_free(is);
  if (cas)
    cas_stop(cas);
  if (out.rtph)
    rtpheader_free(out.rtph);
  if (out.rist)
    ristout_close(out.rist);
  if (out.srt)
    srtsink_close(out.srt);
  if (mc)
    mcast_close(mc);

  if (cfg->verbose && log_stderr_is_tty())
    fputc('\n', stderr);
  if (rc == 0)
    log_line("stopped.");
  return rc;
}
