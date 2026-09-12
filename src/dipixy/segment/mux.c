/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"
#include "mp4push.h"

#include <string.h>

/* 1 ok (trk filled), 0 not enough decoded yet (no SPS/PPS, or malformed) */
static int build_video_track_cfg(const hls_seg_ctx_t *s, fmp4_track_cfg_t *trk, unsigned char *cpriv, size_t cpriv_cap) {
  unsigned w = 0, h = 0;
  size_t cpriv_len;

  switch (s->demux.video_codec) {
    case CODEC_H264:
      if (!s->video.es.spslen || h264_dims(s->video.es.sps, s->video.es.spslen, &w, &h)) return 0; /* h264_dims: 0 ok, -1 malformed */
      cpriv_len = build_avcc(&s->video.es, cpriv, cpriv_cap);
      break;
    case CODEC_HEVC: {
      unsigned char ptl[12];
      unsigned chroma;
      if (!s->video.es.spslen || hevc_info(s->video.es.sps, s->video.es.spslen, ptl, &chroma, &w, &h)) return 0; /* hevc_info: 0 ok, -1 malformed */
      cpriv_len = build_hvcc(&s->video.es, cpriv, cpriv_cap);
      break;
    }
    case CODEC_VVC:
      if (!s->video.es.spslen || vvc_dims(s->video.es.sps, s->video.es.spslen, &w, &h)) return 0;
      cpriv_len = build_vvcc(&s->video.es, cpriv, cpriv_cap);
      break;
    default:
      return 0; /* fmp4: H.264/HEVC/VVC only, no MPEG-2 sample entry */
  }
  memset(trk, 0, sizeof *trk);
  trk->codec = s->demux.video_codec;
  trk->track_id = 1;
  trk->timescale = 90000;
  trk->width = w;
  trk->height = h;
  trk->cpriv = cpriv;
  trk->cpriv_len = cpriv_len;
  return 1;
}

static void build_audio_track_cfg(const hls_seg_ctx_t *s, fmp4_track_cfg_t *trk, unsigned track_id) {
  memset(trk, 0, sizeof *trk);
  trk->codec = s->demux.audio_codec;
  trk->track_id = track_id;
  trk->timescale = s->audio.audio_rate;
  trk->rate = s->audio.audio_rate;
  trk->channels = s->audio.audio_channels;
  switch (s->demux.audio_codec) {
    case CODEC_AAC:
    case CODEC_AAC_LATM:
      trk->cpriv = s->audio.es_audio.cpriv;
      trk->cpriv_len = s->audio.es_audio.cpriv_len;
      break;
    case CODEC_AC3:
    case CODEC_EAC3:
      trk->ac3_bsid = (unsigned char)s->audio.audio_bsid;
      trk->ac3_bsmod = (unsigned char)s->audio.audio_bsmod;
      trk->ac3_acmod = (unsigned char)s->audio.audio_acmod;
      trk->ac3_lfeon = (unsigned char)s->audio.audio_lfeon;
      trk->ac3_bitrate_code = s->audio.audio_bitrate_code;
      break;
    default:
      break;
  }
}

/* creates fmux once video ready (and audio too, if present). called from both feed paths */
void try_create_fmux(hls_seg_ctx_t *s) {
  unsigned char vcpriv[FMP4_CPRIV_MAX];
  fmp4_track_cfg_t trk[FMP4_MAX_TRACKS];
  int ntrk;
  unsigned char *out;
  size_t outlen;

  if (s->fmp4.fmux) return;
  if (!build_video_track_cfg(s, &trk[0], vcpriv, sizeof vcpriv)) return;
  ntrk = 1;
  s->fmp4.fmp4_audio_track_idx = -1;
  s->fmp4.fmp4_lcevc_track_idx = -1;
  if (s->audio.audio_present) {
    if (!s->audio.audio_ready) return;
    build_audio_track_cfg(s, &trk[ntrk], (unsigned)(ntrk + 1));
    s->fmp4.fmp4_audio_track_idx = ntrk;
    ntrk++;
  }
  if (s->demux.lcevc_pid_known) {
    memset(&trk[ntrk], 0, sizeof trk[ntrk]);
    trk[ntrk].codec = CODEC_LCEVC;
    trk[ntrk].track_id = (unsigned)(ntrk + 1);
    trk[ntrk].timescale = 90000;
    trk[ntrk].depends_on_track_id = trk[0].track_id;
    s->fmp4.fmp4_lcevc_track_idx = ntrk;
    ntrk++;
  }
  s->fmp4.fmux = fmp4_mux_new(trk, ntrk);
  if (!s->fmp4.fmux) return;
  outlen = fmp4_init_segment(s->fmp4.fmux, &out);
  if (outlen && (!s->store || hls_set_init_segment_at(s->store, s->demux.video_codec, out, outlen) < 0))
    log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "hls: hls_set_init_segment failed, init segment lost");
}

static void fmp4_close_fragment(hls_seg_ctx_t *s, double pt) {
  unsigned char *out;
  size_t outlen = fmp4_segment_end(s->fmp4.fmux, &out);
  if (!outlen) return;
  mp4push_deliver(s, out, outlen);
  if (pt > 0.0) {
    double chunk_dur = (double)(s->fmp4.fmp4_pend_ts_ms - s->fmp4.fmp4_frag_start_ts_ms) / 1000.0;
    if (!s->store || hls_push_part_at(s->store, out, outlen, chunk_dur, s->fmp4.fmp4_frag_key) < 0)
      log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "hls: hls_push_part failed, fmp4 chunk lost");
    if (s->fmp4.fmp4_pend_ends_seg && (!s->store || hls_push_segment_ll_at(s->store, s->fmp4.fmp4_pend_elapsed) < 0))
      log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "hls: hls_push_segment_ll failed, fmp4 segment lost");
  } else if (!s->store || hls_push_segment_at(s->store, out, outlen, s->fmp4.fmp4_pend_elapsed) < 0) {
    log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "hls: hls_push_segment failed, fmp4 segment lost");
  }
}

static void fmp4_open_fragment(hls_seg_ctx_t *s) {
  fmp4_segment_begin(s->fmp4.fmux, s->fmp4.fmp4_seq++);
  s->fmp4.fmp4_frag_open = 1;
  s->fmp4.fmp4_frag_start_ts_ms = s->fmp4.fmp4_pend_ts_ms;
  s->fmp4.fmp4_frag_key = s->fmp4.fmp4_pend_key;
}

/* open_now/cut_now apply once au is pending, 1 call later. ts_ms: decode-order (dts, or pts if no dts). cts_ticks: (pts-dts) in track ticks, 0 wo dts */
void fmp4_feed_au(hls_seg_ctx_t *s, int kf, int64_t ts_ms, int32_t cts_ticks, int open_now, int cut_now, double elapsed) {
  double pt;
  int chunk_now;
  try_create_fmux(s);
  if (!s->fmp4.fmux) return;
  pt = atomic_load_explicit(&s->part.part_target, memory_order_acquire);
  chunk_now = pt > 0.0 && s->fmp4.fmp4_frag_open && ts_ms >= 0 && s->fmp4.fmp4_frag_start_ts_ms >= 0 && (double)(ts_ms - s->fmp4.fmp4_frag_start_ts_ms) / 1000.0 >= pt;
  if (s->fmp4.fmp4_have_pend && ts_ms >= 0 && s->fmp4.fmp4_pend_ts_ms >= 0 && (s->fmp4.fmp4_frag_open || s->fmp4.fmp4_pend_starts_frag)) {
    fmp4_sample_t samp;
    int64_t dur_ms = ts_ms - s->fmp4.fmp4_pend_ts_ms;
    if (dur_ms < 0) dur_ms = 0;
    if (s->fmp4.fmp4_pend_starts_frag) {
      if (s->fmp4.fmp4_frag_open)
        fmp4_close_fragment(s, pt);
      else
        s->fmp4.fmp4_anchor_ms = s->fmp4.fmp4_pend_ts_ms;

      fmp4_open_fragment(s);
    }
    memset(&samp, 0, sizeof samp);
    samp.track_idx = 0;
    samp.data = s->fmp4.fmp4_pend_data;
    samp.size = s->fmp4.fmp4_pend_len;
    samp.duration = (uint32_t)(dur_ms * 90);
    samp.cts_offset = s->fmp4.fmp4_pend_cts;
    samp.keyframe = s->fmp4.fmp4_pend_key;
    fmp4_segment_add_sample(s->fmp4.fmux, &samp);
  }
  if (buf_reserve(&s->fmp4.fmp4_pend_data, &s->fmp4.fmp4_pend_cap, s->video.nal_scratch_len) < 0) {
    log_throttled(&s->oom_drop_throttle, LOG_THROTTLE_WINDOW_S, "hls: buf_reserve failed, fmp4 access unit dropped"); return;
  }
  memcpy(s->fmp4.fmp4_pend_data, s->video.nal_scratch, s->video.nal_scratch_len);
  s->fmp4.fmp4_pend_len = s->video.nal_scratch_len;
  s->fmp4.fmp4_pend_key = kf;
  s->fmp4.fmp4_pend_ts_ms = ts_ms;
  s->fmp4.fmp4_pend_cts = cts_ticks;
  s->fmp4.fmp4_pend_starts_frag = open_now || cut_now || chunk_now;
  s->fmp4.fmp4_pend_ends_seg = cut_now;
  s->fmp4.fmp4_pend_elapsed = elapsed;
  s->fmp4.fmp4_have_pend = 1;
}

/* fmp4 only, standalone-ES lcevc track. one PES = one au, duration from next au's pts */
void fmp4_feed_lcevc_au(hls_seg_ctx_t *s, int64_t ts_ms, const unsigned char *data, size_t len) {
  if (!s->fmp4.fmux || !s->fmp4.fmp4_frag_open || s->fmp4.fmp4_lcevc_track_idx < 0) return;
  if (!s->lcevc_track.seeded) {
    int64_t delta_ms = ts_ms - s->fmp4.fmp4_anchor_ms;
    if (delta_ms < 0) return; /* au predates video's start: drop, retry next au */
    fmp4_track_seed_dts(s->fmp4.fmux, s->fmp4.fmp4_lcevc_track_idx, (uint64_t)(delta_ms * 90));
    s->lcevc_track.seeded = 1;
  }
  if (s->lcevc_track.have_pend) {
    fmp4_sample_t samp;
    int64_t dur_ms = ts_ms - s->lcevc_track.pend_ts_ms;
    if (dur_ms < 0) dur_ms = 0;
    memset(&samp, 0, sizeof samp);
    samp.track_idx = s->fmp4.fmp4_lcevc_track_idx;
    samp.data = s->lcevc_track.pend_data;
    samp.size = s->lcevc_track.pend_len;
    samp.duration = (uint32_t)(dur_ms * 90);
    samp.keyframe = 1;
    fmp4_segment_add_sample(s->fmp4.fmux, &samp);
  }
  if (buf_reserve(&s->lcevc_track.pend_data, &s->lcevc_track.pend_cap, len) < 0) {
    log_throttled(&s->oom_drop_throttle, LOG_THROTTLE_WINDOW_S, "hls: buf_reserve failed, lcevc au dropped");
    return;
  }
  memcpy(s->lcevc_track.pend_data, data, len);
  s->lcevc_track.pend_len = len;
  s->lcevc_track.pend_ts_ms = ts_ms;
  s->lcevc_track.have_pend = 1;
}

/* fmp4 only, ts already carries audio raw. duration known upfront, no delayed-pending needed */
void fmp4_feed_audio_au(hls_seg_ctx_t *s, const esc_frame_t *f) {
  fmp4_sample_t samp;
  int32_t dur;
  if (!s->fmp4.fmux || !s->fmp4.fmp4_frag_open || s->fmp4.fmp4_audio_track_idx < 0) return;
  if (!s->audio.fmp4_audio_seeded) {
    if (!s->audio.audio_pts_anchored) return; /* position vs video unknown yet: drop, retry next frame */

    {
      int64_t start_samples = s->audio.audio_nominal_samples - (int64_t)f->samples;
      double est_ms = (double)s->audio.audio_anchor_pts_ms + (double)(start_samples - s->audio.audio_anchor_nominal_samples) * 1000.0 / (double)s->audio.audio_rate;
      double delta_ms = est_ms - (double)s->fmp4.fmp4_anchor_ms;
      if (delta_ms < 0.0) return; /* frame predates video's start: drop, retry next frame */
      fmp4_track_seed_dts(s->fmp4.fmux, s->fmp4.fmp4_audio_track_idx, (uint64_t)(delta_ms * (double)s->audio.audio_rate / 1000.0 + 0.5));
    }
    s->audio.fmp4_audio_seeded = 1;
    s->audio.audio_pending_drift_samples = 0; /* stale: measured against dropped frames */
  }
  dur = (int32_t)f->samples;
  if (s->audio.audio_pending_drift_samples) {
    int32_t corr = (int32_t)s->audio.audio_pending_drift_samples;
    s->audio.audio_pending_drift_samples = 0;
    if (dur + corr > 0) dur += corr;
  }
  memset(&samp, 0, sizeof samp);
  samp.track_idx = s->fmp4.fmp4_audio_track_idx;
  samp.data = f->out;
  samp.size = f->outlen;
  samp.duration = (uint32_t)dur;
  samp.keyframe = 1;
  fmp4_segment_add_sample(s->fmp4.fmux, &samp);
}
