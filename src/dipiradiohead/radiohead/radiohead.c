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

void flush_batch(out_ctx_t *o) {
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
