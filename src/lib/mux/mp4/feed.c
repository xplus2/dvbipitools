/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>
#include <time.h>

#include "lib/demux/escodec/aubuild.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "priv.h"

static int64_t now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void video_emit(mp4_t *m, track_t *t, int64_t pts_ms, int64_t dts_ms, const unsigned char *d, size_t n, int key) {
  if (t->prev_dur_slot) {
    int64_t d_dur = dts_ms - t->prev_dts_ms; /* both sides: absolute dts, never t0-relative */
    uint32_t dur = d_dur > 0 ? (uint32_t)d_dur : 1;
    *t->prev_dur_slot = dur;
    t->last_dur = dur;
  }
  p4_route(m, t, dts_ms, (int32_t)(pts_ms - dts_ms), d, n, key, 0);
  t->prev_dts_ms = dts_ms;
}

static void on_cue(void *ctx, const ttx_cue_t *cue) {
  mp4_t *m = ctx;
  int64_t dur = cue->end_ms - cue->start_ms;
  unsigned char buf[2 + TTX_TEXT_MAX];
  size_t n = strlen(cue->text);
  if (!n || dur <= 0 || n > TTX_TEXT_MAX) return;
  buf[0] = (unsigned char)(n >> 8);
  buf[1] = (unsigned char)n;
  memcpy(buf + 2, cue->text, n);
  for (int i = 0; i < m->ntrk; i++) {
    track_t *t = &m->trk[i];
    if (t->cls != PID_TELETEXT) continue;
    p4_route(m, t, cue->start_ms, 0, buf, n + 2, 1, (uint32_t)dur);
    return;
  }
}

static int all_psi_named(const mp4_t *m) {
  for (int i = 0; i < m->npsi; i++) if (!psi_service_name(m->psi[i])[0]) return 0;
  return 1;
}

void p4_all_ready(mp4_t *m) {
  int64_t grace_ms = (m->npsi > 1) ? 3000 : 2000;

  for (int i = 0; i < m->ntrk; i++) {
    const track_t *t = &m->trk[i];
    if (!t->hdr_parsed) return;
    if (t->cls == PID_VIDEO && !t->got_key) return;
  }
  if (!m->ready_seen) {
    m->ready_seen = 1;
    m->ready_ms = now_ms();
  }
  if (!all_psi_named(m) && now_ms() - m->ready_ms < grace_ms) return;
  p4_start(m);
}

static void try_parse_h264_hdr(mp4_t *m, track_t *t) {
  if (h264_dims(t->es.sps, t->es.spslen, &t->width, &t->height) != 0)
    return;
  t->es.cpriv_len = build_avcc(&t->es, t->es.cpriv, sizeof t->es.cpriv);
  if (t->es.cpriv_len) {
    t->hdr_parsed = 1;
    p4_all_ready(m);
  }
}

static void try_parse_hevc_hdr(mp4_t *m, track_t *t) {
  if (hevc_info(t->es.sps, t->es.spslen, t->es.ptl, &t->es.chroma, &t->width, &t->height) != 0) return;
  t->es.cpriv_len = build_hvcc(&t->es, t->es.cpriv, sizeof t->es.cpriv);
  if (t->es.cpriv_len) {
    t->hdr_parsed = 1;
    p4_all_ready(m);
  }
}

static void try_parse_vvc_hdr(mp4_t *m, track_t *t) {
  if (vvc_dims(t->es.sps, t->es.spslen, &t->width, &t->height) != 0) return;
  t->es.cpriv_len = build_vvcc(&t->es, t->es.cpriv, sizeof t->es.cpriv);
  if (t->es.cpriv_len) {
    t->hdr_parsed = 1;
    p4_all_ready(m);
  }
}

static void handle_video(mp4_t *m, track_t *t, int has_pts, uint64_t pts, int has_dts, uint64_t dts, const unsigned char *d, size_t len) {
  size_t p, scl = 0;
  int key = 0;

  if (m->flushing)
    return;
  if (has_pts)
    t->pts_ms = pts_unwrap(&t->pts_uw, pts);
  t->ts_ms = has_dts ? pts_unwrap(&t->dts_uw, dts) : t->pts_ms;
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
    } else if (t->es.codec == CODEC_HEVC) {
      type = (d[ns] >> 1) & 0x3F;
      esc_handle_hevc_nal(&t->es, &t->vbuf, &t->vbuflen, &t->vbufcap, type, d + ns, n, &key);
    } else {
      type = n > 1 ? (d[ns + 1] >> 3) & 0x1F : 0;
      esc_handle_vvc_nal(&t->es, &t->vbuf, &t->vbuflen, &t->vbufcap, type, d + ns, n, &key);
    }
    p = q;
    scl = scl2;
  }

  if (!t->hdr_parsed) {
    if (t->es.codec == CODEC_H264 && t->es.spslen && t->es.ppslen)
      try_parse_h264_hdr(m, t);
    else if (t->es.codec == CODEC_HEVC && t->es.vpslen && t->es.spslen && t->es.ppslen)
      try_parse_hevc_hdr(m, t);
    else if (t->es.codec == CODEC_VVC && t->es.vpslen && t->es.spslen && t->es.ppslen)
      try_parse_vvc_hdr(m, t);
  }
  if (key)
    t->got_key = 1;
  if (t->hdr_parsed && t->got_key && t->vbuflen)
    video_emit(m, t, t->pts_ms, t->ts_ms, t->vbuf, t->vbuflen, key);
}

static void handle_audio(mp4_t *m, track_t *t, int has_pts, uint64_t pts, const unsigned char *data, size_t len) {
  size_t pos = 0;
  if (has_pts && t->remlen == 0) t->ts_ms = pts_unwrap(&t->pts_uw, pts);
  if (t->remlen > MP4_REM_MAX) t->remlen = 0;
  if (esc_rem_append(&t->rem, &t->remlen, &t->remcap, data, len)) return;

  while (pos < t->remlen) {
    esc_frame_t f;
    int r = next_frame(&t->es, t->rem + pos, t->remlen - pos, &f);
    if (r > 0) break;
    if (r < 0) {
      pos++;
      continue;
    }
    if (!t->hdr_parsed) {
      if (f.rate) t->es.rate = f.rate;
      if (f.ch) t->es.channels = f.ch;
      if (t->es.codec == CODEC_AC3 || t->es.codec == CODEC_EAC3) {
        t->ac3_bsid = (unsigned char)f.bsid;
        t->ac3_bsmod = (unsigned char)f.bsmod;
        t->ac3_acmod = (unsigned char)f.acmod;
        t->ac3_lfeon = (unsigned char)f.lfeon;
        t->ac3_bitrate_code = f.bitrate_code;
      }
      t->hdr_parsed = 1;
      p4_all_ready(m);
    }
    if (f.outlen)
      p4_route(m, t, t->ts_ms, 0, f.out, f.outlen, 1, t->es.rate ? (uint32_t)(f.samples * 1000 / t->es.rate) : 0);
    if (t->es.rate && f.samples)
      t->ts_ms += (int64_t)f.samples * 1000 / (int64_t)t->es.rate;
    pos += f.consumed;
  }
  if (pos) {
    memmove(t->rem, t->rem + pos, t->remlen - pos);
    t->remlen -= pos;
  }
}

void p4_on_pes(void *ctx, unsigned pid, int has_pts, uint64_t pts, int has_dts, uint64_t dts, const unsigned char *data, size_t len) {
  mp4_t *m = ctx;
  track_t *t = p4_find_track(m, pid);
  if (!t || m->err) return;
  if (t->cls == PID_TELETEXT)
    ttx_pes(t->ttx, has_pts, pts, data, len);
  else if (t->cls == PID_VIDEO)
    handle_video(m, t, has_pts, pts, has_dts, dts, data, len);
  else
    handle_audio(m, t, has_pts, pts, data, len);
}

static int video_supported(codec_t c) {
  return c == CODEC_H264 || c == CODEC_HEVC || c == CODEC_VVC;
}

static int audio_supported(codec_t c) {
  return c == CODEC_AC3 || c == CODEC_EAC3 || c == CODEC_MP2A || c == CODEC_AAC || c == CODEC_AAC_LATM;
}

static void add_track(mp4_t *m, const psi_es_t *es, int psi_idx) {
  track_t *t = &m->trk[m->ntrk];
  memset(t, 0, sizeof *t);
  t->pid = es->pid;
  t->track_id = (unsigned)(m->ntrk + 1);
  t->es.codec = es->codec;
  t->cls = es->cls;
  t->psi_idx = psi_idx;
  memcpy(t->lang, es->lang, sizeof t->lang);
  if (pes_track(m->pes, t->pid))
    log_line("mp4: pes tracker full, pid 0x%04x will not be reassembled", t->pid);
  m->ntrk++;
}

void p4_setup(mp4_t *m) {
  const psi_es_t *es;
  int c, k;
  if (m->video_ok) {
    es = psi_es(m->psi[0], &c);
    for (k = 0; k < c && m->ntrk < MP4_MAX_TRACKS; k++) {
      if (es[k].cls != PID_VIDEO) continue;
      if (!video_supported(es[k].codec)) {
        log_line("mp4=no_vc(%s)", codec_name(es[k].codec));
        continue;
      }
      add_track(m, &es[k], 0);
    }
  }
  for (int p = 0; p < m->npsi; p++) {
    es = psi_es(m->psi[p], &c);
    for (k = 0; k < c && m->ntrk < MP4_MAX_TRACKS; k++) {
      if (es[k].cls != PID_AUDIO) continue;
      if (!m->opts->audio_all && es[k].audio_index != (int)m->opts->audio_track)
        continue;
      if (!audio_supported(es[k].codec)) {
        log_line("mp4=no_ac(%s)", codec_name(es[k].codec));
        continue;
      }
      add_track(m, &es[k], p);
    }
  }
  if (m->opts->subs_srt && m->npsi == 1) {
    es = psi_es(m->psi[0], &c);
    for (k = 0; k < c && m->ntrk < MP4_MAX_TRACKS; k++) {
      track_t *t;
      if (es[k].cls != PID_TELETEXT || !es[k].ttx_page)
        continue;
      add_track(m, &es[k], 0);
      t = &m->trk[m->ntrk - 1];
      t->ttx = ttx_new(es[k].ttx_page, es[k].ttx_lang, m->opts->sub_lead_ms, on_cue, m);
      if (!t->ttx) {
        m->ntrk--;
        continue;
      }
      memcpy(t->lang, es[k].ttx_lang, sizeof t->lang);
      t->hdr_parsed = 1;
      log_line("mp4=txtsub(%u=%s)", es[k].ttx_page, es[k].ttx_lang);
      break;
    }
  }
  if (!m->ntrk)
    log_line("mp4=no_mux");
  m->setup = 1;
}
