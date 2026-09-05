/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"
#include "mp4push.h"

#include <string.h>

/* 1 ok (trk filled), 0 not enough decoded yet (no SPS/PPS, or malformed) */
static int build_video_track_cfg(const hls_seg_ctx_t *s, fmp4_track_cfg_t *trk, unsigned char *cpriv, size_t cpriv_cap) {
  unsigned w = 0, h = 0;
  size_t cpriv_len;

  if (s->video_codec == CODEC_H264) {
    if (!s->es.spslen || h264_dims(s->es.sps, s->es.spslen, &w, &h)) return 0; /* h264_dims: 0 ok, -1 malformed */
    cpriv_len = build_avcc(&s->es, cpriv, cpriv_cap);
  } else if (s->video_codec == CODEC_HEVC) {
    unsigned char ptl[12];
    unsigned chroma;
    if (!s->es.spslen || hevc_info(s->es.sps, s->es.spslen, ptl, &chroma, &w, &h)) return 0; /* hevc_info: 0 ok, -1 malformed */
    cpriv_len = build_hvcc(&s->es, cpriv, cpriv_cap);
  } else if (s->video_codec == CODEC_VVC) {
    if (!s->es.spslen || vvc_dims(s->es.sps, s->es.spslen, &w, &h)) return 0;
    cpriv_len = build_vvcc(&s->es, cpriv, cpriv_cap);
  } else {
    return 0; /* fmp4: H.264/HEVC/VVC only, no MPEG-2 sample entry */
  }
  memset(trk, 0, sizeof *trk);
  trk->codec = s->video_codec;
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
  trk->codec = s->audio_codec;
  trk->track_id = track_id;
  trk->timescale = s->audio_rate;
  trk->rate = s->audio_rate;
  trk->channels = s->audio_channels;
  if (s->audio_codec == CODEC_AAC || s->audio_codec == CODEC_AAC_LATM) {
    trk->cpriv = s->es_audio.cpriv;
    trk->cpriv_len = s->es_audio.cpriv_len;
  } else if (s->audio_codec == CODEC_AC3 || s->audio_codec == CODEC_EAC3) {
    trk->ac3_bsid = (unsigned char)s->audio_bsid;
    trk->ac3_bsmod = (unsigned char)s->audio_bsmod;
    trk->ac3_acmod = (unsigned char)s->audio_acmod;
    trk->ac3_lfeon = (unsigned char)s->audio_lfeon;
    trk->ac3_bitrate_code = s->audio_bitrate_code;
  }
}

/* creates fmux once video ready (and audio too, if present). called from both feed paths */
void try_create_fmux(hls_seg_ctx_t *s) {
  unsigned char vcpriv[FMP4_CPRIV_MAX];
  fmp4_track_cfg_t trk[2];
  int ntrk;
  unsigned char *out;
  size_t outlen;

  if (s->fmux) return;
  if (!build_video_track_cfg(s, &trk[0], vcpriv, sizeof vcpriv)) return;
  if (s->audio_present) {
    if (!s->audio_ready) return;
    build_audio_track_cfg(s, &trk[1], 2);
    s->fmp4_audio_track_idx = 1;
    ntrk = 2;
  } else {
    ntrk = 1;
  }
  s->fmux = fmp4_mux_new(trk, ntrk);
  if (!s->fmux) return;
  outlen = fmp4_init_segment(s->fmux, &out);
  if (outlen && hls_set_init_segment(s->cap_ctx, &s->filter, s->pmt_pid, SEG_CONTAINER_FMP4, s->video_codec, out, outlen) < 0)
    log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "hls: hls_set_init_segment failed, init segment lost");
}

/* open_now/cut_now apply once this au becomes pending, 1 call later. ts_ms: decode-order (dts, or pts if no dts). cts_ticks: (pts-dts) in track ticks, 0 wo dts.
   part_target > 0: each fragment = CMAF chunk, pushed via hls_push_part(); cut_now still finalizes the
   enclosing segment via hls_push_segment_ll(). part_target <= 0: unchanged, 1 fragment == 1 segment via hls_push_segment(). */
void fmp4_feed_au(hls_seg_ctx_t *s, int kf, int64_t ts_ms, int32_t cts_ticks, int open_now, int cut_now, double elapsed) {
  double pt;
  int chunk_now;
  try_create_fmux(s);
  if (!s->fmux) return;
  pt = atomic_load_explicit(&s->part_target, memory_order_acquire);
  chunk_now = pt > 0.0 && s->fmp4_frag_open && ts_ms >= 0 && s->fmp4_frag_start_ts_ms >= 0 && (double)(ts_ms - s->fmp4_frag_start_ts_ms) / 1000.0 >= pt;
  if (s->fmp4_have_pend && ts_ms >= 0 && s->fmp4_pend_ts_ms >= 0 && (s->fmp4_frag_open || s->fmp4_pend_starts_frag)) {
    fmp4_sample_t samp;
    int64_t dur_ms = ts_ms - s->fmp4_pend_ts_ms;
    if (dur_ms < 0) dur_ms = 0;
    if (s->fmp4_pend_starts_frag) {
      if (s->fmp4_frag_open) {
        unsigned char *out;
        size_t outlen = fmp4_segment_end(s->fmux, &out);
        if (outlen) {
          mp4push_deliver(s, out, outlen);
          if (pt > 0.0) {
            double chunk_dur = (double)(s->fmp4_pend_ts_ms - s->fmp4_frag_start_ts_ms) / 1000.0;
            if (hls_push_part(s->cap_ctx, &s->filter, s->pmt_pid, SEG_CONTAINER_FMP4, out, outlen, chunk_dur, s->fmp4_frag_key) < 0)
              log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "hls: hls_push_part failed, fmp4 chunk lost");
            if (s->fmp4_pend_ends_seg && hls_push_segment_ll(s->cap_ctx, &s->filter, s->pmt_pid, SEG_CONTAINER_FMP4, s->fmp4_pend_elapsed) < 0)
              log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "hls: hls_push_segment_ll failed, fmp4 segment lost");
          } else if (hls_push_segment(s->cap_ctx, &s->filter, s->pmt_pid, SEG_CONTAINER_FMP4, out, outlen, s->fmp4_pend_elapsed) < 0) {
            log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "hls: hls_push_segment failed, fmp4 segment lost");
          }
        }
      } else {
        s->fmp4_anchor_ms = s->fmp4_pend_ts_ms;
      }
      fmp4_segment_begin(s->fmux, s->fmp4_seq++);
      s->fmp4_frag_open = 1;
      s->fmp4_frag_start_ts_ms = s->fmp4_pend_ts_ms;
      s->fmp4_frag_key = s->fmp4_pend_key;
    }
    memset(&samp, 0, sizeof samp);
    samp.track_idx = 0;
    samp.data = s->fmp4_pend_data;
    samp.size = s->fmp4_pend_len;
    samp.duration = (uint32_t)(dur_ms * 90);
    samp.cts_offset = s->fmp4_pend_cts;
    samp.keyframe = s->fmp4_pend_key;
    fmp4_segment_add_sample(s->fmux, &samp);
  }
  if (buf_reserve(&s->fmp4_pend_data, &s->fmp4_pend_cap, s->nal_scratch_len) < 0) {
    log_throttled(&s->oom_drop_throttle, LOG_THROTTLE_WINDOW_S, "hls: buf_reserve failed, fmp4 access unit dropped");
    return;
  }
  memcpy(s->fmp4_pend_data, s->nal_scratch, s->nal_scratch_len);
  s->fmp4_pend_len = s->nal_scratch_len;
  s->fmp4_pend_key = kf;
  s->fmp4_pend_ts_ms = ts_ms;
  s->fmp4_pend_cts = cts_ticks;
  s->fmp4_pend_starts_frag = open_now || cut_now || chunk_now;
  s->fmp4_pend_ends_seg = cut_now;
  s->fmp4_pend_elapsed = elapsed;
  s->fmp4_have_pend = 1;
}

/* fmp4 only, ts already carries audio raw. duration known upfront, no delayed-pending needed */
void fmp4_feed_audio_au(hls_seg_ctx_t *s, const esc_frame_t *f) {
  fmp4_sample_t samp;
  int32_t dur;
  if (!s->fmux || !s->fmp4_frag_open || s->fmp4_audio_track_idx < 0) return;
  if (!s->fmp4_audio_seeded) {
    if (!s->audio_pts_anchored)
      return; /* position vs video unknown yet: drop, retry next frame */
    {
      int64_t start_samples = s->audio_nominal_samples - (int64_t)f->samples;
      double est_ms = (double)s->audio_anchor_pts_ms + (double)(start_samples - s->audio_anchor_nominal_samples) * 1000.0 / (double)s->audio_rate;
      double delta_ms = est_ms - (double)s->fmp4_anchor_ms;
      if (delta_ms < 0.0) return; /* frame predates video's start: drop, retry next frame */
      fmp4_track_seed_dts(s->fmux, s->fmp4_audio_track_idx, (uint64_t)(delta_ms * (double)s->audio_rate / 1000.0 + 0.5));
    }
    s->fmp4_audio_seeded = 1;
    s->audio_pending_drift_samples = 0; /* stale: measured against dropped frames */
  }
  dur = (int32_t)f->samples;
  if (s->audio_pending_drift_samples) {
    int32_t corr = (int32_t)s->audio_pending_drift_samples;
    s->audio_pending_drift_samples = 0;
    if (dur + corr > 0) dur += corr;
  }
  memset(&samp, 0, sizeof samp);
  samp.track_idx = s->fmp4_audio_track_idx;
  samp.data = f->out;
  samp.size = f->outlen;
  samp.duration = (uint32_t)dur;
  samp.keyframe = 1;
  fmp4_segment_add_sample(s->fmux, &samp);
}
