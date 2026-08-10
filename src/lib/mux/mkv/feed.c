/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/ioutil.h"
#include "lib/log.h"
#include "priv.h"

/* Annex B NAL unit types */
#define H264_NAL_IDR 5
#define H264_NAL_SPS 7
#define H264_NAL_PPS 8
#define H264_NAL_AUD 9
#define H264_NAL_FILLER 12

#define HEVC_NAL_IRAP_FIRST 16 /* BLA_W_LP */
#define HEVC_NAL_IRAP_LAST 21  /* CRA_NUT */
#define HEVC_NAL_VPS 32
#define HEVC_NAL_SPS 33
#define HEVC_NAL_PPS 34
#define HEVC_NAL_AUD 35
#define HEVC_NAL_FILLER 38

static int64_t now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

/* finished subtitle -> one timed block */
static void on_cue(void *ctx, const ttx_cue_t *cue) {
  mkv_t *m = ctx;
  int64_t dur = cue->end_ms - cue->start_ms;
  size_t n = strlen(cue->text);
  int i;

  if (!n || dur <= 0)
    return;
  for (i = 0; i < m->ntrk; i++) {
    track_t *t = &m->trk[i];
    if (t->cls != PID_TELETEXT)
      continue;
    if (!m->started) {
      pend_add(m, t->num, cue->start_ms, (const unsigned char *)cue->text, n, 1, dur);
      return;
    }
    if (cue->end_ms <= m->t0) /* wholly before recording */
      return;
    if (cue->start_ms < m->t0) { /* --sub-lead pulled it in: clip, keep it */
      dur = cue->end_ms - m->t0;
      put_block(m, t->num, 0, (const unsigned char *)cue->text, n, 1, dur);
      return;
    }
    put_block(m, t->num, cue->start_ms - m->t0, (const unsigned char *)cue->text, n, 1, dur);
    return;
  }
}

/* psi_have_sdt() means "a section arrived", not "ours was in it": programs cycle
 * independently; check name itself. */
static int all_psi_named(const mkv_t *m) {
  int i;
  for (i = 0; i < m->npsi; i++)
    if (!psi_service_name(m->psi[i])[0])
      return 0;
  return 1;
}

/* start: all track headers present, video keyframe seen.
   brief SDT wait -> service name for Title/tags. multi-program: longer grace, per-program cycles skew */
void all_ready(mkv_t *m) {
  int i;
  int64_t grace_ms = (m->npsi > 1) ? 3000 : 2000;

  for (i = 0; i < m->ntrk; i++) {
    const track_t *t = &m->trk[i];
    if (!t->hdr_parsed)
      return;
    if (t->cls == PID_VIDEO && !t->got_key)
      return;
  }
  if (!m->ready_seen) {
    m->ready_seen = 1;
    m->ready_ms = now_ms();
  }
  if (!all_psi_named(m) && now_ms() - m->ready_ms < grace_ms)
    return;
  start(m);
}

static int rem_append(track_t *t, const unsigned char *d, size_t n) {
  if (t->remlen + n > t->remcap) {
    size_t nc = t->remcap ? t->remcap * 2 : 8192;
    unsigned char *np;
    while (nc < t->remlen + n)
      nc *= 2;
    np = realloc(t->rem, nc);
    if (!np)
      return -1;
    t->rem = np;
    t->remcap = nc;
  }
  memcpy(t->rem + t->remlen, d, n);
  t->remlen += n;
  return 0;
}

static int vbuf_add(track_t *t, const unsigned char *nal, size_t n) {
  size_t need = t->vbuflen + 4 + n;

  if (need > t->vbufcap) {
    size_t nc = t->vbufcap ? t->vbufcap * 2 : 65536;
    unsigned char *np;
    while (nc < need)
      nc *= 2;
    np = realloc(t->vbuf, nc);
    if (!np)
      return -1;
    t->vbuf = np;
    t->vbufcap = nc;
  }
  t->vbuf[t->vbuflen++] = (unsigned char)(n >> 24);
  t->vbuf[t->vbuflen++] = (unsigned char)(n >> 16);
  t->vbuf[t->vbuflen++] = (unsigned char)(n >> 8);
  t->vbuf[t->vbuflen++] = (unsigned char)n;
  memcpy(t->vbuf + t->vbuflen, nal, n);
  t->vbuflen += n;
  return 0;
}

static void ps_store(unsigned char *dst, size_t *dlen, const unsigned char *s, size_t n) {
  if (n && n <= MKV_PS_MAX) {
    memcpy(dst, s, n);
    *dlen = n;
  }
}

static size_t find_startcode(const unsigned char *d, size_t len, size_t from, size_t *sclen) {
  size_t i;

  for (i = from; i + 3 <= len; i++) {
    if (d[i] || d[i + 1])
      continue;
    if (d[i + 2] == 1) {
      *sclen = 3;
      return i;
    }
    if (i + 4 <= len && d[i + 2] == 0 && d[i + 3] == 1) {
      *sclen = 4;
      return i;
    }
  }
  return len;
}

/* one video PES = one Annex-B access unit */
static void handle_video(mkv_t *m, track_t *t, int has_pts, uint64_t pts, const unsigned char *d, size_t len) {
  size_t p, scl = 0;
  int key = 0;

  /* EOS flush: cut-off picture */
  if (m->flushing)
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
    if (t->codec == CODEC_H264) {
      type = d[ns] & 0x1F;
      switch (type) {
        case H264_NAL_SPS:
          ps_store(t->sps, &t->spslen, d + ns, n);
          break;
        case H264_NAL_PPS:
          ps_store(t->pps, &t->ppslen, d + ns, n);
          break;
        case H264_NAL_AUD:
        case H264_NAL_FILLER:
          break;
        default:
          if (type == H264_NAL_IDR)
            key = 1;
          vbuf_add(t, d + ns, n);
      }
    } else {
      type = (d[ns] >> 1) & 0x3F;
      switch (type) {
        case HEVC_NAL_VPS:
          ps_store(t->vps, &t->vpslen, d + ns, n);
          break;
        case HEVC_NAL_SPS:
          ps_store(t->sps, &t->spslen, d + ns, n);
          break;
        case HEVC_NAL_PPS:
          ps_store(t->pps, &t->ppslen, d + ns, n);
          break;
        case HEVC_NAL_AUD:
        case HEVC_NAL_FILLER:
          break;
        default:
          if (type >= HEVC_NAL_IRAP_FIRST && type <= HEVC_NAL_IRAP_LAST)
            key = 1;
          vbuf_add(t, d + ns, n);
      }
    }
    p = q;
    scl = scl2;
  }

  if (!t->hdr_parsed) {
    if (t->codec == CODEC_H264 && t->spslen && t->ppslen) {
      if (h264_dims(t->sps, t->spslen, &t->width, &t->height) == 0) {
        t->cpriv_len = build_avcc(t, t->cpriv, sizeof t->cpriv);
        if (t->cpriv_len) {
          bufcpy(t->codecid, sizeof t->codecid, codec_id_for(t, NULL));
          t->hdr_parsed = 1;
          all_ready(m);
        }
      }
    } else if (t->codec == CODEC_HEVC && t->vpslen && t->spslen && t->ppslen) {
      if (hevc_info(t->sps, t->spslen, t->ptl, &t->chroma, &t->width,&t->height) == 0) {
        t->cpriv_len = build_hvcc(t, t->cpriv, sizeof t->cpriv);
        if (t->cpriv_len) {
          bufcpy(t->codecid, sizeof t->codecid, codec_id_for(t, NULL));
          t->hdr_parsed = 1;
          all_ready(m);
        }
      }
    }
  }
  /* undecodable before first keyframe */
  if (key)
    t->got_key = 1;
  if (t->hdr_parsed && t->got_key && t->vbuflen)
    emit(m, t, t->vbuf, t->vbuflen, key);
}

/* V_MPEG2: raw ES, start codes intact, no length-prefix/CodecPrivate.
   one PES = one picture, same as handle_video */
static void handle_mpeg2(mkv_t *m, track_t *t, int has_pts, uint64_t pts, const unsigned char *d, size_t len) {
  size_t p, scl = 0;
  int key = 0;

  if (m->flushing)
    return;
  if (has_pts)
    t->ts_ms = pts_unwrap(&t->pts, pts);

  p = find_startcode(d, len, 0, &scl);
  while (p < len) {
    size_t ns = p + scl, scl2 = 0;
    size_t q = find_startcode(d, len, ns, &scl2);
    unsigned code = (ns < len) ? d[ns] : 0xFF;

    if (code == 0xB3 && !t->hdr_parsed && q >= ns + 4) {
      unsigned w = ((unsigned)d[ns + 1] << 4) | (d[ns + 2] >> 4);
      unsigned h = ((unsigned)(d[ns + 2] & 0x0F) << 8) | d[ns + 3];
      if (w && h) {
        t->width = w;
        t->height = h;
        bufcpy(t->codecid, sizeof t->codecid, codec_id_for(t, NULL));
        t->hdr_parsed = 1;
        all_ready(m);
      }
    } else if (code == 0x00 && q >= ns + 3) {
      if (((d[ns + 2] >> 3) & 0x07) == 1) /* picture_coding_type: 1 = I */
        key = 1;
    }
    p = q;
    scl = scl2;
  }

  if (key)
    t->got_key = 1;
  if (t->hdr_parsed && t->got_key)
    emit(m, t, d, len, key);
}

static void handle_audio(mkv_t *m, track_t *t, int has_pts, uint64_t pts, const unsigned char *data, size_t len) {
  size_t pos = 0;

  if (has_pts && t->remlen == 0) /* anchor on frame boundary */
    t->ts_ms = pts_unwrap(&t->pts, pts);
  if (t->remlen > MKV_REM_MAX)
    t->remlen = 0;
  if (rem_append(t, data, len))
    return;

  while (pos < t->remlen) {
    frame_t f;
    int r = next_frame(t, t->rem + pos, t->remlen - pos, &f);
    if (r > 0)
      break;
    if (r < 0) {
      pos++;
      continue;
    }
    if (!t->hdr_parsed) {
      if (f.rate)
        t->rate = f.rate;
      if (f.ch)
        t->channels = f.ch;
      bufcpy(t->codecid, sizeof t->codecid, codec_id_for(t, &f));
      t->hdr_parsed = 1;
      all_ready(m);
    }
    if (f.outlen)
      emit(m, t, f.out, f.outlen, 1);
    if (t->rate && f.samples)
      t->ts_ms += (int64_t)f.samples * 1000 / (int64_t)t->rate;
    pos += f.consumed;
  }
  if (pos) {
    memmove(t->rem, t->rem + pos, t->remlen - pos);
    t->remlen -= pos;
  }
}

void on_pes(void *ctx, unsigned pid, int has_pts, uint64_t pts, const unsigned char *data, size_t len) {
  mkv_t *m = ctx;
  track_t *t = find_track(m, pid);

  if (!t || m->err)
    return;
  if (t->cls == PID_TELETEXT)
    ttx_pes(t->ttx, has_pts, pts, data, len);
  else if (t->cls == PID_VIDEO && t->codec == CODEC_MPEG2V)
    handle_mpeg2(m, t, has_pts, pts, data, len);
  else if (t->cls == PID_VIDEO)
    handle_video(m, t, has_pts, pts, data, len);
  else
    handle_audio(m, t, has_pts, pts, data, len);
}

static int audio_supported(codec_t c) {
  return c == CODEC_AC3 || c == CODEC_EAC3 || c == CODEC_MP2A || c == CODEC_AAC || c == CODEC_AAC_LATM;
}

static void add_track(mkv_t *m, const psi_es_t *es, int psi_idx) {
  track_t *t = &m->trk[m->ntrk];

  memset(t, 0, sizeof *t);
  t->pid = es->pid;
  t->num = m->ntrk + 1;
  t->codec = es->codec;
  t->cls = es->cls;
  t->psi_idx = psi_idx;
  memcpy(t->lang, es->lang, sizeof t->lang);
  if (pes_track(m->pes, t->pid))
    log_line("mkv: pes tracker full, pid 0x%04x will not be reassembled", t->pid);
  m->ntrk++;
}

void setup(mkv_t *m) {
  const psi_es_t *es;
  int c, k, p;

  if (m->video_ok) {
    es = psi_es(m->psi[0], &c);
    for (k = 0; k < c && m->ntrk < MKV_MAX_TRACKS; k++) {
      if (es[k].cls != PID_VIDEO)
        continue;
      if (es[k].codec != CODEC_H264 && es[k].codec != CODEC_HEVC &&
          es[k].codec != CODEC_MPEG2V) {
        log_line("mkv=no_vc(%s)", codec_name(es[k].codec));
        continue;
      }
      add_track(m, &es[k], 0);
    }
  }
  for (p = 0; p < m->npsi; p++) {
    es = psi_es(m->psi[p], &c);
    for (k = 0; k < c && m->ntrk < MKV_MAX_TRACKS; k++) {
      if (es[k].cls != PID_AUDIO)
        continue;
      if (!m->opts->audio_all && es[k].audio_index != (int)m->opts->audio_track)
        continue;
      if (!audio_supported(es[k].codec)) {
        log_line("mkv=no_ac(%s)", codec_name(es[k].codec));
        continue;
      }
      add_track(m, &es[k], p);
    }
  }
  if (m->opts->subs_srt && m->npsi == 1) {
    es = psi_es(m->psi[0], &c);
    for (k = 0; k < c && m->ntrk < MKV_MAX_TRACKS; k++) {
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
      bufcpy(t->codecid, sizeof t->codecid, "S_TEXT/UTF8");
      t->hdr_parsed = 1; /* no setup data */
      log_line("mkv=txtsub(%u=%s)", es[k].ttx_page, es[k].ttx_lang);
      break;
    }
  }
  if (!m->ntrk)
    log_line("mkv=no_mux");
  m->setup = 1;
}
