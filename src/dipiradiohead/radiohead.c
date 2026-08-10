/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lib/ioutil.h"
#include "lib/log.h"
#include "lib/metrics/export.h"
#include "lib/net/multicast.h"
#include "lib/signal.h"

#include "input/inputset.h"
#include "input/source.h"
#include "lib/mux/mpts.h"
#include "lib/mux/rtpheader.h"
#include "cas/cas.h"
#include "mux/tspacketizer.h"
#include "radiohead.h"
#include "version.h"

#define TS_PER_DGRAM 7

typedef struct {
  mcast_t *mc;
  int rtp;
  rtpheader_t *rtph;
  uint64_t cur_pts;
  unsigned char batch[12 + TS_PER_DGRAM * 188]; /* [0,12): RTP header headroom, unused if !rtp */
  int batch_count;
  int had_error;
  unsigned long long packets;
  unsigned long long errors;
} out_ctx_t;

/* tool-wide, not per-input - matches the spec's unlabeled radio_* metric names */
typedef struct {
  unsigned long long frames_total[3]; /* indexed by source_codec_t */
  unsigned long long framing_errors_total;
  unsigned long long metadata_updates_total;
} radio_metrics_t;

typedef struct {
  char artist[256], title[256];
  int dirty;
  radio_metrics_t *rm; /* shared, not owned */
} meta_state_t;

static void meta_cb(void *ctx, const char *artist, const char *title) {
  meta_state_t *m = ctx;
  bufcpy(m->artist, sizeof m->artist, artist);
  bufcpy(m->title, sizeof m->title, title);
  m->dirty = 1;
  if (m->rm)
    m->rm->metadata_updates_total++;
}

static void flush_batch(out_ctx_t *o) {
  size_t n = (size_t)o->batch_count * 188;

  if (o->batch_count == 0)
    return;
  if (o->rtp) {
    rtpheader_build(o->rtph, (uint32_t)o->cur_pts, o->batch, 12);
    if (mcast_send(o->mc, o->batch, 12 + n) < 0) {
      o->had_error = 1;
      o->errors++;
    }
  } else if (mcast_send(o->mc, o->batch + 12, n) < 0) {
    o->had_error = 1;
    o->errors++;
  }
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

static const char *codec_name(source_codec_t c) {
  switch (c) {
    case SRC_MPEG_AUDIO:      return "mpeg-audio";
    case SRC_AAC_ADTS:        return "aac-adts";
    case SRC_AAC_LATM:        return "aac-latm";
  }
  return "?";
}

/* common + output + input + CAS + radio metrics, on mx's own interval
   (metrics_exporter_due gates/no-ops when disabled) */
static void emit_metrics(metrics_exporter_t *mx, double now, const out_ctx_t *out, unsigned configured_services, unsigned active_services,
                          const input_metrics_t *inputs, unsigned n_inputs, const radio_metrics_t *rm, cas_t *cas) {
  metrics_writer_t w;
  unsigned c;

  if (!metrics_exporter_due(mx, now))
    return;
  if (metrics_exporter_begin(mx, &w, TOOL_VERSION))
    return;
  metrics_writer_put(&w, METRICS_ID_OUTPUT_PACKETS_TOTAL, NULL, out->packets);
  metrics_writer_put(&w, METRICS_ID_OUTPUT_BYTES_TOTAL, NULL, out->packets * 188ULL);
  metrics_writer_put(&w, METRICS_ID_OUTPUT_ERRORS_TOTAL, NULL, out->errors);
  metrics_writer_put(&w, METRICS_ID_CONFIGURED_SERVICES, NULL, configured_services);
  metrics_writer_put(&w, METRICS_ID_ACTIVE_SERVICES, NULL, active_services);
  metrics_writer_put_inputs(&w, inputs, n_inputs);
  for (c = 0; c <= SRC_AAC_LATM; c++)
    metrics_writer_put(&w, METRICS_ID_RADIO_AUDIO_FRAMES_TOTAL, codec_name((source_codec_t)c), rm->frames_total[c]);
  metrics_writer_put(&w, METRICS_ID_RADIO_AUDIO_FRAMING_ERRORS_TOTAL, NULL, rm->framing_errors_total);
  metrics_writer_put(&w, METRICS_ID_RADIO_METADATA_UPDATES_TOTAL, NULL, rm->metadata_updates_total);
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
      metrics_writer_put(&w, METRICS_ID_CAS_EMM_DROPPED_TOTAL, label, vm.emm_dropped_total);
    }
  }
  metrics_exporter_send(mx, &w);
}

#define MPTS_POLL_MAX_MS 100
#define MPTS_MAX_FRAMES_PER_TICK 32 /* per input, per tick. caps one input's backlog from delaying the rest */

/* mpts.c is tool-agnostic (shared with dipitvhead). they adapt our concrete types to its void*-based ops vtables. */
static int mpts_program_get_sdt_info(void *ctx, psi_sdt_entry_t *out) {
  return tspacketizer_get_sdt_info((tspacketizer_t *)ctx, out);
}
static size_t mpts_program_build_eit(void *ctx, unsigned char *out, size_t cap) {
  return tspacketizer_build_eit((tspacketizer_t *)ctx, out, cap);
}
static int mpts_program_eit_pending(const void *ctx) {
  return tspacketizer_eit_pending((const tspacketizer_t *)ctx);
}
static const mpts_program_ops_t mpts_program_ops = {
    mpts_program_get_sdt_info, mpts_program_build_eit, mpts_program_eit_pending};

static size_t mpts_cas_build_cat(void *ctx, unsigned char *out, size_t cap) {
  return cas_build_cat((cas_t *)ctx, out, cap);
}
static int mpts_cas_ecm_due(void *ctx, size_t vendor_idx, double now_s, unsigned char *out, size_t cap, size_t *out_len) {
  return cas_vendor_ecm_due((cas_t *)ctx, vendor_idx, now_s, out, cap, out_len);
}
static int mpts_cas_next_emm(void *ctx, size_t vendor_idx, unsigned char *out, size_t cap, size_t *out_len) {
  return cas_vendor_next_emm((cas_t *)ctx, vendor_idx, out, cap, out_len);
}
static const mpts_cas_ops_t mpts_cas_ops = {mpts_cas_build_cat, mpts_cas_ecm_due, mpts_cas_next_emm};

static void radiohead_mpts_set_cas(mpts_t *mpts, cas_t *cas) {
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

static int radiohead_run_mpts(const config_t *cfg, metrics_exporter_t *mx) {
  mcast_t *mc;
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
  unsigned i, k, rr_start = 0;
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

  for (i = 0; i < n; i++) {
    meta_ctxs[i] = &metas[i];
    metas[i].rm = metrics_on ? &rm : NULL;
  }
  is = inputset_new(cfg, meta_cb, meta_ctxs, metrics_on ? input_stats : NULL);
  if (!is) {
    rc = 1;
    goto done;
  }
  for (i = 0; i < n; i++) {
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
    for (i = 0; i < n; i++)
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
    for (i = 0; i < n; i++) {
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

    now = mono_seconds();
    now_t = time(NULL);
    for (i = 0; i < n; i++)
      inputset_service(is, i, now_t);

    for (k = 0; k < n; k++) {
      unsigned frames_this_visit = 0;
      source_t *src;
      int connected_now;
      unsigned pfd_i;
      int ready = 0;

      i = (rr_start + k) % n;
      src = inputset_source(is, i);
      connected_now = src != NULL;

      if (connected_now && !was_connected[i]) {
        samples_total[i] = 0;
        last_synced_bytes[i] = 0;
      }
      was_connected[i] = connected_now;
      if (!src)
        continue;

      /* only read a slot poll() actually reported ready. source_next_frame() can block for several seconds
         (http_read()'s SO_RCVTIMEO) on a connected-but-currently-silent source, which would otherwise stall other inputs */
      for (pfd_i = 0; pfd_i < npfd; pfd_i++)
        if (pfd_slot[pfd_i] == i && (pfds[pfd_i].revents & (POLLIN | POLLERR | POLLHUP))) {
          ready = 1;
          break;
        }
      if (!ready)
        continue;

      while (frames_this_visit < MPTS_MAX_FRAMES_PER_TICK) {
        source_frame_t f;
        uint64_t pts;
        net_err_reason_t reason = NET_ERR_OTHER;
        int r = source_next_frame(src, &f, &reason);

        if (r == 0)
          break;
        if (r < 0) {
          input_metrics_note_read(metrics_on ? &input_stats[i] : NULL, -1, reason);
          if (metrics_on && reason == NET_ERR_FORMAT)
            rm.framing_errors_total++;
          if (metrics_on)
            input_stats[i].up = 0;
          inputset_mark_down(is, i, now_t);
          mpts_set_program(mpts, i, NULL);
          break;
        }
        frames_this_visit++;
        if (metrics_on) {
          input_stats[i].last_data_time = (double)time(NULL);
          rm.frames_total[f.codec]++;
        }

        if (!tsps[i]) {
          tspacketizer_cfg_t tc;
          tc.tsid = cfg->tsid;
          tc.onid = cfg->onid;
          tc.sid = inputset_sid(is, i);
          tc.stream_type = f.stream_type;
          tc.network_name = "";
          tc.service_name = inputset_service_name(is, i);
          tc.pmt_pid = inputset_pmt_pid(is, i);
          tc.audio_pid = inputset_audio_pid(is, i);
          tc.standalone = 0;
          tsps[i] = tspacketizer_new(&tc);
          if (!tsps[i]) {
            rc = 1;
            goto done;
          }
          if (cas)
            tspacketizer_set_cas(tsps[i], cas);
          log_line("input %u (%s): codec detected: %s, %u Hz", i, inputset_service_name(is, i), codec_name(f.codec), f.sample_rate);
        }
        mpts_set_program(mpts, i, tsps[i]);

        if (metas[i].dirty) {
          tspacketizer_set_metadata(tsps[i], metas[i].artist, metas[i].title);
          metas[i].dirty = 0;
          log_line("input %u (%s): now playing: %s%s%s", i, inputset_service_name(is, i), metas[i].artist, (metas[i].artist[0] && metas[i].title[0]) ? " - " : "", metas[i].title);
        }

        pts = samples_total[i] * 90000ULL / f.sample_rate;
        samples_total[i] += f.samples;
        out.cur_pts = pts;
        tspacketizer_feed(tsps[i], pts, now, f.data, f.len, packet_cb, &out);
        if (out.had_error) {
          rc = 1;
          goto done;
        }
      }
      if (metrics_on) {
        unsigned long long sb = source_bytes_total(src);
        if (sb > last_synced_bytes[i]) {
          input_stats[i].bytes_total += sb - last_synced_bytes[i];
          last_synced_bytes[i] = sb;
        }
      }
    }
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
    if (out.had_error) {
      rc = 1;
      goto done;
    }
    if (cfg->verbose && now - last_stat >= 1.0) {
      fprintf(stderr, "\r%.0fs, %llu TS packets\033[K", now - start, out.packets);
      fflush(stderr);
      last_stat = now;
    }
    {
      unsigned active = 0;
      for (i = 0; i < n; i++)
        if (tsps[i])
          active++;
      emit_metrics(mx, now, &out, n, active, input_stats, n, &rm, cas);
    }
  }

done:
  if (cas)
    cas_flush(cas, packet_cb, &out);
  flush_batch(&out);
  for (i = 0; i < n; i++)
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
  mcast_close(mc);

  if (cfg->verbose && log_stderr_is_tty())
    fputc('\n', stderr);
  if (rc == 0)
    log_line("stopped.");
  return rc;
}

int radiohead_run(const config_t *cfg, metrics_exporter_t *mx) {
  mcast_t *mc;
  out_ctx_t out;
  meta_state_t meta;
  tspacketizer_t *tsp = NULL;
  cas_t *cas = NULL;
  double start, last_stat = 0;
  int rc = 0;
  input_metrics_t im;
  radio_metrics_t rm;
  unsigned long long last_synced_bytes = 0;
  int metrics_on = metrics_exporter_enabled(mx);

  if (cfg->n_inputs > 1)
    return radiohead_run_mpts(cfg, mx);

  memset(&meta, 0, sizeof meta);
  memset(&out, 0, sizeof out);
  memset(&im, 0, sizeof im);
  memset(&rm, 0, sizeof rm);
  meta.rm = metrics_on ? &rm : NULL;
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

  if (cfg->cas_algo != CAS_ALGO_NONE || cfg->biss2_enabled || cfg->biss1_enabled || cfg->biss2_ca_enabled) {
    unsigned audio_pid = TSPACKETIZER_PID_AUDIO;
    cas = cas_start(cfg, &audio_pid, 1);
    if (!cas) {
      log_line("cas: failed to start");
      rc = 1;
      goto done;
    }
  }

  start = mono_seconds();
  while (!signal_stop_requested()) {
    net_err_reason_t reason = NET_ERR_OTHER;
    source_t *src = source_open(cfg->inputs[0].uri, cfg->insecure_tls, meta_cb, &meta, &reason);
    uint64_t samples_total = 0;

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
      last_synced_bytes = 0;
    }

    while (!signal_stop_requested()) {
      source_frame_t f;
      int r = source_next_frame(src, &f, &reason);
      uint64_t pts;
      double now;

      if (r == 0)
        continue;
      if (r < 0) {
        input_metrics_note_read(metrics_on ? &im : NULL, -1, reason);
        if (metrics_on && reason == NET_ERR_FORMAT)
          rm.framing_errors_total++;
        break;
      }
      if (metrics_on) {
        im.last_data_time = (double)time(NULL);
        rm.frames_total[f.codec]++;
      }

      if (!tsp) {
        tspacketizer_cfg_t tc;
        tc.tsid = cfg->tsid;
        tc.onid = cfg->onid;
        tc.sid = cfg->inputs[0].sid;
        tc.stream_type = f.stream_type;
        tc.network_name = cfg->nit_text;
        tc.service_name = cfg->inputs[0].sdt_text;
        tc.pmt_pid = 0;
        tc.audio_pid = 0;
        tc.standalone = 1;
        tsp = tspacketizer_new(&tc);
        if (!tsp) {
          rc = 1;
          goto done;
        }
        if (cas)
          tspacketizer_set_cas(tsp, cas);
        log_line("codec detected: %s, %u Hz", codec_name(f.codec), f.sample_rate);
      }
      if (meta.dirty) {
        tspacketizer_set_metadata(tsp, meta.artist, meta.title);
        meta.dirty = 0;
        log_line("now playing: %s%s%s", meta.artist, (meta.artist[0] && meta.title[0]) ? " - " : "", meta.title);
      }

      now = mono_seconds();
      pts = samples_total * 90000ULL / f.sample_rate;
      samples_total += f.samples;
      out.cur_pts = pts;
      if (cas) {
        cas_clock_tick(cas, pts);
        if (cas_failed(cas)) {
          log_line("cas: fatal, stopping");
          rc = 1;
          goto done;
        }
        if (signal_reload_requested())
          cas_reload_receivers(cas);
      }
      tspacketizer_feed(tsp, pts, now, f.data, f.len, packet_cb, &out);
      if (out.had_error) {
        rc = 1;
        goto done;
      }
      if (cfg->verbose && now - last_stat >= 1.0) {
        fprintf(stderr, "\r%.0fs, %llu TS packets\033[K", now - start, out.packets);
        fflush(stderr);
        last_stat = now;
      }
      if (metrics_on) {
        unsigned long long sb = source_bytes_total(src);
        if (sb > last_synced_bytes) {
          im.bytes_total += sb - last_synced_bytes;
          last_synced_bytes = sb;
        }
      }
      emit_metrics(mx, now, &out, 1, 1, &im, 1, &rm, cas);
    }
    if (metrics_on)
      im.up = 0;
    source_close(src);
    if (signal_stop_requested())
      break;
    if (cfg->error_retry_s <= 0) {
      rc = 1;
      break;
    }
    log_line("input error, retrying in %lds", cfg->error_retry_s);
    sleep_interruptible(cfg->error_retry_s);
  }

done:
  if (cas)
    cas_flush(cas, packet_cb, &out);
  flush_batch(&out);
  if (tsp)
    tspacketizer_free(tsp);
  if (cas)
    cas_stop(cas);
  if (out.rtph)
    rtpheader_free(out.rtph);
  mcast_close(mc);

  if (cfg->verbose && log_stderr_is_tty())
    fputc('\n', stderr);
  if (rc == 0)
    log_line("stopped.");
  return rc;
}
