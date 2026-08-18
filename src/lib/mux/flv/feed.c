/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/demux/escodec/aubuild.h"
#include "lib/log.h"
#include "priv.h"

/* live push: no pend[]-style buffer/rewrite like mkv. pre-ready frames dropped, no rewind for late joiners. */
void flv_all_ready(flv_t *f) {
  if (f->started)
    return;
  if (f->have_v && (!f->vtrk.hdr_parsed || !f->vtrk.got_key))
    return;
  if (f->have_a && !f->atrk.hdr_parsed)
    return;
  if (!f->have_v && !f->have_a)
    return;
  f->t0 = f->have_v ? f->vtrk.ts_ms : f->atrk.ts_ms;
  f->started = 1;
  flv_emit_metadata(f);
}

static void try_parse_h264_hdr(flv_t *f, flv_track_t *t) {
  unsigned w, h;
  if (h264_dims(t->es.sps, t->es.spslen, &w, &h) != 0)
    return;
  t->es.cpriv_len = build_avcc(&t->es, t->es.cpriv, sizeof t->es.cpriv);
  if (t->es.cpriv_len) {
    t->hdr_parsed = 1;
    flv_all_ready(f);
  }
}

static void try_parse_hevc_hdr(flv_t *f, flv_track_t *t) {
  unsigned w, h;
  if (hevc_info(t->es.sps, t->es.spslen, t->es.ptl, &t->es.chroma, &w, &h) != 0)
    return;
  t->es.cpriv_len = build_hvcc(&t->es, t->es.cpriv, sizeof t->es.cpriv);
  if (t->es.cpriv_len) {
    t->hdr_parsed = 1;
    flv_all_ready(f);
  }
}

/* one video PES = one Annex-B access unit */
static void handle_video(flv_t *f, flv_track_t *t, int has_pts, uint64_t pts, const unsigned char *d, size_t len) {
  size_t p, scl = 0;
  int key = 0;

  if (f->flushing)
    return;
  if (has_pts)
    t->ts_ms = pts_unwrap(&t->pts, pts);
  t->vbuflen = 0;

  p = find_startcode(d, len, 0, &scl);
  while (p < len) {
    size_t ns = p + scl, scl2 = 0;
    size_t q = find_startcode(d, len, ns, &scl2);
    size_t n = q - ns;
    unsigned type;

    if (!n) {
      p = q;
      scl = scl2;
      continue;
    }
    if (t->es.codec == CODEC_H264) {
      type = d[ns] & 0x1F;
      esc_handle_h264_nal(&t->es, &t->vbuf, &t->vbuflen, &t->vbufcap, type, d + ns, n, &key);
    } else {
      type = (d[ns] >> 1) & 0x3F;
      esc_handle_hevc_nal(&t->es, &t->vbuf, &t->vbuflen, &t->vbufcap, type, d + ns, n, &key);
    }
    p = q;
    scl = scl2;
  }

  if (!t->hdr_parsed) {
    if (t->es.codec == CODEC_H264 && t->es.spslen && t->es.ppslen)
      try_parse_h264_hdr(f, t);
    else if (t->es.codec == CODEC_HEVC && t->es.vpslen && t->es.spslen && t->es.ppslen)
      try_parse_hevc_hdr(f, t);
  }
  if (key)
    t->got_key = 1;
  if (f->started && t->hdr_parsed && t->got_key && t->vbuflen)
    flv_emit_video(f, t, t->vbuf, t->vbuflen, key);
}

static void handle_audio(flv_t *f, flv_track_t *t, int has_pts, uint64_t pts, const unsigned char *data, size_t len) {
  size_t pos = 0;

  if (has_pts && t->remlen == 0) /* anchor on frame boundary */
    t->ts_ms = pts_unwrap(&t->pts, pts);
  if (t->remlen > FLV_REM_MAX)
    t->remlen = 0;
  if (esc_rem_append(&t->rem, &t->remlen, &t->remcap, data, len))
    return;

  while (pos < t->remlen) {
    esc_frame_t fr;
    int r = next_frame(&t->es, t->rem + pos, t->remlen - pos, &fr);
    if (r > 0)
      break;
    if (r < 0) {
      pos++;
      continue;
    }
    if (!t->hdr_parsed) {
      if (fr.rate)
        t->es.rate = fr.rate;
      if (fr.ch)
        t->es.channels = fr.ch;
      t->hdr_parsed = 1;
      flv_all_ready(f);
    }
    if (fr.outlen && f->started)
      flv_emit_audio(f, t, fr.out, fr.outlen);
    if (t->es.rate && fr.samples)
      t->ts_ms += (int64_t)fr.samples * 1000 / (int64_t)t->es.rate;
    pos += fr.consumed;
  }
  if (pos) {
    memmove(t->rem, t->rem + pos, t->remlen - pos);
    t->remlen -= pos;
  }
}

void flv_on_pes(void *ctx, unsigned pid, int has_pts, uint64_t pts, const unsigned char *data, size_t len) {
  flv_t *f = ctx;
  flv_track_t *t = NULL;

  if (f->err)
    return;
  if (f->have_v && pid == f->vtrk.pid)
    t = &f->vtrk;
  else if (f->have_a && pid == f->atrk.pid)
    t = &f->atrk;
  if (!t)
    return;
  if (t->cls == PID_VIDEO)
    handle_video(f, t, has_pts, pts, data, len);
  else
    handle_audio(f, t, has_pts, pts, data, len);
}

/* RTMP model: 1 H.264/HEVC video + 1 AC-3/E-AC-3/AAC audio max. MPEG-2 video,
   MP2 audio: no FLV slot, no Enhanced FourCC, skipped+logged. */
static int video_supported(codec_t c) { return c == CODEC_H264 || c == CODEC_HEVC; }
static int audio_supported(codec_t c) { return c == CODEC_AC3 || c == CODEC_EAC3 || c == CODEC_AAC || c == CODEC_AAC_LATM; }

void flv_setup(flv_t *f) {
  const psi_es_t *es;
  int c, k;

  es = psi_es(f->psi, &c);
  for (k = 0; k < c && !f->have_v; k++) {
    if (es[k].cls != PID_VIDEO)
      continue;
    if (!video_supported(es[k].codec)) {
      log_line("flv=no_vc(%s)", codec_name(es[k].codec));
      continue;
    }
    memset(&f->vtrk, 0, sizeof f->vtrk);
    f->vtrk.pid = es[k].pid;
    f->vtrk.cls = PID_VIDEO;
    f->vtrk.es.codec = es[k].codec;
    if (pes_track(f->pes, f->vtrk.pid))
      log_line("flv: pes tracker full, pid 0x%04x will not be reassembled", f->vtrk.pid);
    f->have_v = 1;
  }
  for (k = 0; k < c && !f->have_a; k++) {
    if (es[k].cls != PID_AUDIO)
      continue;
    if (f->opts->audio_track && es[k].audio_index != (int)f->opts->audio_track)
      continue;
    if (!audio_supported(es[k].codec)) {
      log_line("flv=no_ac(%s)", codec_name(es[k].codec));
      continue;
    }
    memset(&f->atrk, 0, sizeof f->atrk);
    f->atrk.pid = es[k].pid;
    f->atrk.cls = PID_AUDIO;
    f->atrk.es.codec = es[k].codec;
    if (pes_track(f->pes, f->atrk.pid))
      log_line("flv: pes tracker full, pid 0x%04x will not be reassembled", f->atrk.pid);
    f->have_a = 1;
  }
  if (!f->have_v && !f->have_a)
    log_line("flv=no_mux");
  f->setup = 1;
}
