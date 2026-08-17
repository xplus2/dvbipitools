/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "lib/ioutil.h"
#include "lib/log.h"
#include "lib/signal.h"

#include "../input/source.h"
#include "../mux/tspacketizer.h"
#include "priv.h"

void meta_cb(void *ctx, const char *artist, const char *title) {
  meta_state_t *m = ctx;
  bufcpy(m->artist, sizeof m->artist, artist);
  bufcpy(m->title, sizeof m->title, title);
  m->dirty = 1;
  if (m->rm)
    m->rm->metadata_updates_total++;
}

ristout_t *radiohead_rist_open(const config_t *cfg) {
  ristout_cfg_t rc;
  unsigned i;

  memset(&rc, 0, sizeof rc);
  for (i = 0; i < cfg->n_rist; i++)
    rc.peer_uri[i] = cfg->rist_uri[i];
  rc.npeers = (int)cfg->n_rist;
  rc.profile = cfg->rist_profile == RIST_PROF_MAIN ? RISTOUT_PROFILE_MAIN : RISTOUT_PROFILE_SIMPLE;
  rc.secret = cfg->rist_secret;
  rc.cname = cfg->rist_cname;
  rc.buffer_ms = cfg->rist_buffer_ms;
  rc.verbose = cfg->verbose;
  return ristout_open(&rc);
}

/* a failed output is never fatal; log only on the failure/recovery edge, keep retrying every batch */
static void note_send_result(int ok, int *had_error, unsigned long long *errors, const char *label) {
  if (!ok) {
    (*errors)++;
    if (!*had_error) {
      log_line("%s output: send failed, will keep retrying", label);
      *had_error = 1;
    }
  } else if (*had_error) {
    log_line("%s output: recovered", label);
    *had_error = 0;
  }
}

void flush_batch(out_ctx_t *o) {
  size_t n = (size_t)o->batch_count * 188;

  if (o->batch_count == 0)
    return;
  if (o->mc) {
    if (o->rtp) {
      rtpheader_build(o->rtph, (uint32_t)o->cur_pts, o->batch, 12);
      note_send_result(mcast_send(o->mc, o->batch, 12 + n) >= 0, &o->mc_had_error, &o->errors, "mcast");
    } else {
      note_send_result(mcast_send(o->mc, o->batch + 12, n) >= 0, &o->mc_had_error, &o->errors, "mcast");
    }
  }
  if (o->rist)
    note_send_result(ristout_write(o->rist, o->batch + 12, n) >= 0, &o->rist_had_error, &o->errors, "rist");
  o->batch_count = 0;
}

void packet_cb(void *ctx, const unsigned char *pkt188) {
  out_ctx_t *o = ctx;
  memcpy(o->batch + 12 + (size_t)o->batch_count * 188, pkt188, 188);
  o->batch_count++;
  o->packets++;
  if (o->batch_count == TS_PER_DGRAM)
    flush_batch(o);
}

const char *codec_name(source_codec_t c) {
  switch (c) {
    case SRC_MPEG_AUDIO:      return "mpeg-audio";
    case SRC_AAC_ADTS:        return "aac-adts";
    case SRC_AAC_LATM:        return "aac-latm";
  }
  return "?";
}

typedef struct {
  tspacketizer_t **tsp;
  cas_t *cas;
  const config_t *cfg;
  meta_state_t *meta;
  out_ctx_t *out;
  input_metrics_t *im;
  radio_metrics_t *rm;
  int metrics_on;
  metrics_exporter_t *mx;
  uint64_t *samples_total;
  double start;
  double *last_stat;
  unsigned long long *last_synced_bytes;
} single_tick_t;

/* processes one frame from src for the single-input path. 0: ok, keep looping.
   -1: r<0 (source error), caller should break its loop and reconnect.
   -2: fatal (tspacketizer_new() OOM or cas_failed()), caller must abort */
static int process_single_frame(single_tick_t *tk, source_t *src) {
  source_frame_t f;
  net_err_reason_t reason = NET_ERR_OTHER;
  int r = source_next_frame(src, &f, &reason);
  uint64_t pts;
  double now;

  if (r == 0)
    return 0;
  if (r < 0) {
    input_metrics_note_read(tk->metrics_on ? tk->im : NULL, -1, reason);
    if (tk->metrics_on && reason == NET_ERR_FORMAT)
      tk->rm->framing_errors_total++;
    return -1;
  }
  if (tk->metrics_on) {
    tk->im->last_data_time = (double)time(NULL);
    tk->rm->frames_total[f.codec]++;
  }

  if (!*tk->tsp) {
    tspacketizer_cfg_t tc;
    tc.tsid = tk->cfg->tsid;
    tc.onid = tk->cfg->onid;
    tc.sid = tk->cfg->inputs[0].sid;
    tc.stream_type = f.stream_type;
    tc.network_name = tk->cfg->nit_text;
    tc.service_name = tk->cfg->inputs[0].sdt_text;
    tc.pmt_pid = 0;
    tc.audio_pid = 0;
    tc.standalone = 1;
    *tk->tsp = tspacketizer_new(&tc);
    if (!*tk->tsp)
      return -2;
    if (tk->cas)
      tspacketizer_set_cas(*tk->tsp, tk->cas);
    log_line("codec detected: %s, %u Hz", codec_name(f.codec), f.sample_rate);
  }
  if (tk->meta->dirty) {
    tspacketizer_set_metadata(*tk->tsp, tk->meta->artist, tk->meta->title);
    tk->meta->dirty = 0;
    log_line("now playing: %s%s%s", tk->meta->artist, (tk->meta->artist[0] && tk->meta->title[0]) ? " - " : "", tk->meta->title);
  }

  now = mono_seconds();
  pts = *tk->samples_total * 90000ULL / f.sample_rate;
  *tk->samples_total += f.samples;
  tk->out->cur_pts = pts;
  if (tk->cas) {
    cas_clock_tick(tk->cas, pts);
    if (cas_failed(tk->cas)) {
      log_line("cas: fatal, stopping");
      return -2;
    }
    if (signal_reload_requested())
      cas_reload_receivers(tk->cas);
  }
  tspacketizer_feed(*tk->tsp, pts, now, f.data, f.len, packet_cb, tk->out);
  if (tk->cfg->verbose && now - *tk->last_stat >= 1.0) {
    fprintf(stderr, "\r%.0fs, %llu TS packets\033[K", now - tk->start, tk->out->packets);
    fflush(stderr);
    *tk->last_stat = now;
  }
  if (tk->metrics_on) {
    unsigned long long sb = source_bytes_total(src);
    if (sb > *tk->last_synced_bytes) {
      tk->im->bytes_total += sb - *tk->last_synced_bytes;
      *tk->last_synced_bytes = sb;
    }
  }
  emit_metrics(tk->mx, now, tk->out, 1, 1, tk->im, 1, tk->rm, tk->cas);
  return 0;
}

int radiohead_run(const config_t *cfg, metrics_exporter_t *mx) {
  mcast_t *mc = NULL;
  out_ctx_t out;
  meta_state_t meta;
  tspacketizer_t *tsp = NULL;
  cas_t *cas = NULL;
  double start, last_stat = 0;
  int rc = 0;
  input_metrics_t im;
  radio_metrics_t rm;
  unsigned long long last_synced_bytes = 0;
  uint64_t samples_total = 0;
  int metrics_on = metrics_exporter_enabled(mx);
  single_tick_t tk;

  if (cfg->n_inputs > 1)
    return radiohead_run_mpts(cfg, mx);

  memset(&meta, 0, sizeof meta);
  memset(&out, 0, sizeof out);
  memset(&im, 0, sizeof im);
  memset(&rm, 0, sizeof rm);
  meta.rm = metrics_on ? &rm : NULL;
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
  tk.tsp = &tsp;
  tk.cas = cas;
  tk.cfg = cfg;
  tk.meta = &meta;
  tk.out = &out;
  tk.im = &im;
  tk.rm = &rm;
  tk.metrics_on = metrics_on;
  tk.mx = mx;
  tk.samples_total = &samples_total;
  tk.start = start;
  tk.last_stat = &last_stat;
  tk.last_synced_bytes = &last_synced_bytes;

  while (!signal_stop_requested()) {
    net_err_reason_t reason = NET_ERR_OTHER;
    source_t *src = source_open(cfg->inputs[0].uri, cfg->insecure_tls, meta_cb, &meta, &reason);
    samples_total = 0;

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
      int step = process_single_frame(&tk, src);
      if (step == -2) {
        rc = 1;
        goto done;
      }
      if (step == -1)
        break;
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
  if (out.rist)
    ristout_close(out.rist);
  if (mc)
    mcast_close(mc);

  if (cfg->verbose && log_stderr_is_tty())
    fputc('\n', stderr);
  if (rc == 0)
    log_line("stopped.");
  return rc;
}
