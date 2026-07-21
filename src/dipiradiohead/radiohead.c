/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lib/log.h"
#include "lib/net/multicast.h"
#include "lib/signal.h"

#include "input/source.h"
#include "mux/rtpheader.h"
#include "mux/tspacketizer.h"
#include "radiohead.h"

#define TS_PER_DGRAM 7

typedef struct {
  mcast_t *mc;
  int rtp;
  rtpheader_t *rtph;
  uint64_t cur_pts;
  unsigned char batch[TS_PER_DGRAM * 188];
  int batch_count;
  int had_error;
  unsigned long long packets;
} out_ctx_t;

typedef struct {
  char artist[256], title[256];
  int dirty;
} meta_state_t;

static void meta_cb(void *ctx, const char *artist, const char *title) {
  meta_state_t *m = ctx;
  snprintf(m->artist, sizeof m->artist, "%s", artist);
  snprintf(m->title, sizeof m->title, "%s", title);
  m->dirty = 1;
}

static void flush_batch(out_ctx_t *o) {
  unsigned char dgram[12 + TS_PER_DGRAM * 188];
  size_t off = 0;

  if (o->batch_count == 0)
    return;
  if (o->rtp)
    off = rtpheader_build(o->rtph, (uint32_t)o->cur_pts, dgram, sizeof dgram);
  memcpy(dgram + off, o->batch, (size_t)o->batch_count * 188);
  if (mcast_send(o->mc, dgram, off + (size_t)o->batch_count * 188) < 0)
    o->had_error = 1;
  o->batch_count = 0;
}

static void packet_cb(void *ctx, const unsigned char *pkt188) {
  out_ctx_t *o = ctx;
  memcpy(o->batch + (size_t)o->batch_count * 188, pkt188, 188);
  o->batch_count++;
  o->packets++;
  if (o->batch_count == TS_PER_DGRAM)
    flush_batch(o);
}

static double mono(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static void sleep_interruptible(long seconds) {
  time_t deadline = time(NULL) + seconds;
  while (!signal_stop_requested() && time(NULL) < deadline)
    usleep(200000);
}

static const char *codec_name(source_codec_t c) {
  switch (c) {
    case SRC_MPEG_AUDIO:      return "mpeg-audio";
    case SRC_AAC_ADTS:        return "aac-adts";
    case SRC_AAC_LATM:        return "aac-latm";
  }
  return "?";
}

int radiohead_run(const config_t *cfg) {
  mcast_t *mc;
  out_ctx_t out;
  meta_state_t meta;
  tspacketizer_t *tsp = NULL;
  double start, last_stat = 0;
  int rc = 0;
  memset(&meta, 0, sizeof meta);
  memset(&out, 0, sizeof out);
  mc = mcast_open_send(cfg->family, cfg->mcast_group, cfg->mcast_port, cfg->iface, 0);
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

  start = mono();
  while (!signal_stop_requested()) {
    source_t *src = source_open(cfg->input_uri, cfg->insecure_tls, meta_cb, &meta);
    uint64_t samples_total = 0;

    if (!src) {
      if (cfg->error_retry_s <= 0) {
        rc = 1;
        break;
      }
      log_line("input error, retrying in %lds", cfg->error_retry_s);
      sleep_interruptible(cfg->error_retry_s);
      continue;
    }

    while (!signal_stop_requested()) {
      source_frame_t f;
      int r = source_next_frame(src, &f);
      uint64_t pts;

      if (r == 0)
        continue;
      if (r < 0)
        break;

      if (!tsp) {
        tspacketizer_cfg_t tc;
        tc.tsid = cfg->tsid;
        tc.onid = cfg->onid;
        tc.sid = cfg->sid;
        tc.stream_type = f.stream_type;
        tc.network_name = cfg->nit_text;
        tc.service_name = cfg->sdt_text;
        tsp = tspacketizer_new(&tc);
        if (!tsp) {
          rc = 1;
          goto done;
        }
        log_line("codec detected: %s, %u Hz", codec_name(f.codec), f.sample_rate);
      }
      if (meta.dirty) {
        tspacketizer_set_metadata(tsp, meta.artist, meta.title);
        meta.dirty = 0;
        log_line("now playing: %s%s%s", meta.artist, (meta.artist[0] && meta.title[0]) ? " - " : "", meta.title);
      }

      pts = samples_total * 90000ULL / f.sample_rate;
      samples_total += f.samples;
      out.cur_pts = pts;
      tspacketizer_feed(tsp, pts, f.data, f.len, packet_cb, &out);
      if (out.had_error) {
        rc = 1;
        goto done;
      }
      if (cfg->verbose && mono() - last_stat >= 1.0) {
        fprintf(stderr, "\r%.0fs, %llu TS packets\033[K", mono() - start, out.packets);
        fflush(stderr);
        last_stat = mono();
      }
    }
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
  flush_batch(&out);
  if (tsp)
    tspacketizer_free(tsp);
  if (out.rtph)
    rtpheader_free(out.rtph);
  mcast_close(mc);

  if (cfg->verbose && log_stderr_is_tty())
    fputc('\n', stderr);
  if (rc == 0)
    log_line("stopped.");
  return rc;
}
