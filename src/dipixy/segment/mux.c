/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include <string.h>

#define AC4_DEFER_MAX_FRAMES 4

/* 1 ok (trk filled), 0 not enough decoded yet (no SPS/PPS, or malformed) */
int build_video_track_cfg(const hls_seg_ctx_t *s, fmp4_track_cfg_t *trk, unsigned char *cpriv, size_t cpriv_cap) {
  unsigned w = 0, h = 0;
  size_t cpriv_len;

  if (!s->video.es.spslen) return 0;
  switch (s->demux.video_codec) {
    case CODEC_H264:
      if (h264_dims(s->video.es.sps, s->video.es.spslen, &w, &h)) return 0; /* h264_dims: 0 ok, -1 malformed */
      cpriv_len = build_avcc(&s->video.es, cpriv, cpriv_cap);
      break;
    case CODEC_HEVC: {
      unsigned char ptl[12];
      unsigned chroma;
      if (hevc_info(s->video.es.sps, s->video.es.spslen, ptl, &chroma, &w, &h)) return 0; /* hevc_info: 0 ok, -1 malformed */
      cpriv_len = build_hvcc(&s->video.es, cpriv, cpriv_cap);
      break;
    }
    case CODEC_VVC:
      if (vvc_dims(s->video.es.sps, s->video.es.spslen, &w, &h)) return 0;
      cpriv_len = build_vvcc(&s->video.es, cpriv, cpriv_cap);
      break;
    case CODEC_AV1: {
      av1_seq_hdr_t info;
      if (av1_seq_hdr_info(s->video.es.sps, s->video.es.spslen, &info, &w, &h)) return 0;
      cpriv_len = build_av1c(&info, s->video.es.sps, s->video.es.spslen, cpriv, cpriv_cap);
      break;
    }
    default:
      return 0;
  }
  memset(trk, 0, sizeof *trk);
  trk->codec = s->demux.video_codec;
  trk->track_id = 1;
  trk->timescale = HLS_TS_HZ;
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
  trk->timescale = s->audio.rate;
  trk->rate = s->audio.rate;
  trk->channels = s->audio.channels;
  switch (s->demux.audio_codec) {
    case CODEC_AAC:
    case CODEC_AAC_LATM:
    case CODEC_AC4:
      trk->cpriv = s->audio.es.cpriv;
      trk->cpriv_len = s->audio.es.cpriv_len;
      break;
    case CODEC_AC3:
    case CODEC_EAC3:
      trk->ac3_bsid = (unsigned char)s->audio.bsid;
      trk->ac3_bsmod = (unsigned char)s->audio.bsmod;
      trk->ac3_acmod = (unsigned char)s->audio.acmod;
      trk->ac3_lfeon = (unsigned char)s->audio.lfeon;
      trk->ac3_bitrate_code = s->audio.bitrate_code;
      break;
    case CODEC_TRUEHD:
      trk->truehd_format_info = s->audio.truehd_format_info;
      trk->truehd_peak_data_rate = s->audio.truehd_peak_data_rate;
      break;
    case CODEC_DTS:
    case CODEC_DTS_HD:
    case CODEC_DTS_HD_MA:
      trk->dts_has_core = s->audio.dts_has_core;
      break;
    default:
      break;
  }
}

void fmp4_mux_free_all(hls_seg_ctx_t *s) {
  fmp4_mux_free(s->fmp4.fmux);
  fmp4_mux_free(s->fmp4.fmux_v);
  fmp4_mux_free(s->fmp4.fmux_a);
  s->fmp4.fmux = NULL;
  s->fmp4.fmux_v = NULL;
  s->fmp4.fmux_a = NULL;
}

/* sync twin stores to muxed timeline, drops their segments */
void fmp4_track_stores_sync(hls_seg_ctx_t *s) {
  if (!s->store) return;
  if (s->store_v && hls_store_sync_at(s->store_v, s->store) < 0)
    log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "YW700156", "hls: video track store sync failed, stale segments stay listed");
  if (s->store_a && hls_store_sync_at(s->store_a, s->store) < 0)
    log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "YW700157", "hls: audio track store sync failed, stale segments stay listed");
}

void fmp4_track_stores_anchor(hls_seg_ctx_t *s, uint64_t wall_ms) {
  if (s->store_v) hls_store_anchor_at(s->store_v, wall_ms);
  if (s->store_a) hls_store_anchor_at(s->store_a, wall_ms);
}

/* drop fmux + every in-flight fragment. buffers, mfhd sequence survive. next fmux's first sample sits at base_ms */
void fmp4_reset(hls_seg_ctx_t *s, uint64_t base_ms) {
  unsigned char *pend = s->fmp4.pend_data;
  size_t pend_cap = s->fmp4.pend_cap;
  uint32_t seq = s->fmp4.fmp4_seq;

  fmp4_mux_free_all(s);
  memset(&s->fmp4, 0, sizeof s->fmp4);
  s->fmp4.pend_data = pend;
  s->fmp4.pend_cap = pend_cap;
  s->fmp4.fmp4_seq = seq;
  s->fmp4.fmp4_audio_track_idx = -1;
  s->fmp4.fmp4_lcevc_track_idx = -1;
  s->fmp4.tl_base_ms = base_ms;
}

static hls_store_t *track_store(const hls_seg_ctx_t *s, seg_container_t container) {
  hls_store_t *t = hls_store_find(s->cap_ctx, &s->filter, s->pmt_pid, &s->lcevc, container);
  if (t) return t;
  hls_store_open(s->cap_ctx, &s->filter, s->pmt_pid, &s->lcevc, s->video.seg_target, s->video.max_segs, container);
  return hls_store_find(s->cap_ctx, &s->filter, s->pmt_pid, &s->lcevc, container);
}

static void track_init_segment(hls_seg_ctx_t *s, hls_store_t *store, fmp4_mux_t *m) {
  unsigned char *out;
  size_t outlen = fmp4_init_segment(m, &out);
  if (outlen && hls_set_init_segment_at(store, s->demux.video_codec, out, outlen) < 0)
    log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "YW700158", "hls: track init segment lost, DASH falls back to muxed");
}

static hls_chcfg_t audio_chcfg(const hls_seg_ctx_t *s, unsigned *value) {
  *value = s->audio.chcfg_value;
  switch (s->audio.chcfg_scheme) {
    case ESC_CHCFG_DOLBY_2011: return HLS_CHCFG_DOLBY_2011;
    case ESC_CHCFG_DOLBY_2015: return HLS_CHCFG_DOLBY_2015;
    case ESC_CHCFG_DTS_2012: return HLS_CHCFG_DTS_2012;
    case ESC_CHCFG_CICP: return HLS_CHCFG_CICP;
    default: break;
  }
  switch (s->demux.audio_codec) {
    case CODEC_AAC:
    case CODEC_AAC_LATM:
    case CODEC_OPUS:
    case CODEC_MP2A:
      *value = s->audio.channels;
      return s->audio.channels ? HLS_CHCFG_COUNT : HLS_CHCFG_NONE;
    default:
      return HLS_CHCFG_NONE;
  }
}

/* audio only: per-track muxers and stores. failure leaves muxed output only */
static void create_track_muxes(hls_seg_ctx_t *s, const fmp4_track_cfg_t *trk) {
  fmp4_track_cfg_t vtrk[2];
  hls_chcfg_t chcfg;
  unsigned chcfg_value;
  int nv = 1;

  s->store_v = track_store(s, SEG_CONTAINER_FMP4_VIDEO);
  s->store_a = track_store(s, SEG_CONTAINER_FMP4_AUDIO);
  if (!s->store_v || !s->store_a) return;
  fmp4_track_stores_sync(s);
  vtrk[0] = trk[0];
  if (s->fmp4.fmp4_lcevc_track_idx > 0) vtrk[nv++] = trk[s->fmp4.fmp4_lcevc_track_idx];
  s->fmp4.fmux_v = fmp4_mux_new(vtrk, nv);
  s->fmp4.fmux_a = fmp4_mux_new(&trk[s->fmp4.fmp4_audio_track_idx], 1);
  if (!s->fmp4.fmux_v || !s->fmp4.fmux_a) {
    fmp4_mux_free(s->fmp4.fmux_v);
    fmp4_mux_free(s->fmp4.fmux_a);
    s->fmp4.fmux_v = NULL;
    s->fmp4.fmux_a = NULL;
    return;
  }
  fmp4_track_seed_dts(s->fmp4.fmux_v, 0, s->fmp4.tl_base_ms * (HLS_TS_HZ / 1000));
  track_init_segment(s, s->store_v, s->fmp4.fmux_v);
  track_init_segment(s, s->store_a, s->fmp4.fmux_a);
  chcfg = audio_chcfg(s, &chcfg_value);
  if (hls_set_audio_params_at(s->store_a, s->audio.rate, chcfg, chcfg_value) < 0)
    log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "YW700160", "hls: audio parameters not recorded, DASH audio rate and channels missing");
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
  if (s->audio.present) {
    if (!s->audio.ready) return;
    build_audio_track_cfg(s, &trk[ntrk], (unsigned)(ntrk + 1));
    s->fmp4.fmp4_audio_track_idx = ntrk;
    ntrk++;
  }
  if (s->demux.lcevc_pid_known) {
    memset(&trk[ntrk], 0, sizeof trk[ntrk]);
    trk[ntrk].codec = CODEC_LCEVC;
    trk[ntrk].track_id = (unsigned)(ntrk + 1);
    trk[ntrk].timescale = HLS_TS_HZ;
    trk[ntrk].depends_on_track_id = trk[0].track_id;
    s->fmp4.fmp4_lcevc_track_idx = ntrk;
    ntrk++;
  }
  s->fmp4.fmux = fmp4_mux_new(trk, ntrk);
  if (!s->fmp4.fmux) return;
  fmp4_track_seed_dts(s->fmp4.fmux, 0, s->fmp4.tl_base_ms * (HLS_TS_HZ / 1000));
  if (s->audio.present && s->store) create_track_muxes(s, trk);
  outlen = fmp4_init_segment(s->fmp4.fmux, &out);
  if (outlen && (!s->store || hls_set_init_segment_at(s->store, s->demux.video_codec, out, outlen) < 0))
    log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "YW700144", "hls: hls_set_init_segment failed, init segment lost");
  if (outlen) mp4push_deliver(s, out, outlen); /* inline for live subscribers, late joiners read the store */
}

static void push_track_fragment(hls_seg_ctx_t *s, hls_store_t *store, fmp4_mux_t *m, double pt, double chunk_dur, int key) {
  unsigned char *out;
  size_t outlen;
  int failed;
  if (!m || !store) return;
  outlen = fmp4_segment_end(m, &out);
  if (!outlen) return;
  if (pt > 0.0) failed = hls_push_part_at(store, out, outlen, chunk_dur, key) < 0 || (s->fmp4.pend_ends_seg && hls_push_segment_ll_at(store, s->fmp4.pend_elapsed) < 0);
  else failed = hls_push_segment_at(store, out, outlen, s->fmp4.pend_elapsed) < 0;
  if (failed) log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "YW700159", "hls: track store push failed, DASH track data lost");
}

/* track stores first: ready muxed store implies listed DASH tracks */
static void fmp4_close_fragment(hls_seg_ctx_t *s, double pt) {
  unsigned char *out;
  size_t outlen;
  double chunk_dur = (double)(s->fmp4.pend_ts - s->fmp4.fmp4_frag_start_ts) / HLS_TS_HZ;
  push_track_fragment(s, s->store_v, s->fmp4.fmux_v, pt, chunk_dur, s->fmp4.fmp4_frag_key);
  push_track_fragment(s, s->store_a, s->fmp4.fmux_a, pt, chunk_dur, 1);
  outlen = fmp4_segment_end(s->fmp4.fmux, &out);
  if (!outlen) return;
  mp4push_deliver(s, out, outlen);
  if (pt > 0.0) {
    s->part.n_parts = s->fmp4.pend_ends_seg ? 0 : s->part.n_parts + 1;
    if (!s->store || hls_push_part_at(s->store, out, outlen, chunk_dur, s->fmp4.fmp4_frag_key) < 0)
      log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "YW700145", "hls: hls_push_part failed, fmp4 chunk lost");
    if (s->fmp4.pend_ends_seg && (!s->store || hls_push_segment_ll_at(s->store, s->fmp4.pend_elapsed) < 0))
      log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "YW700146", "hls: hls_push_segment_ll failed, fmp4 segment lost");
  } else if (!s->store || hls_push_segment_at(s->store, out, outlen, s->fmp4.pend_elapsed) < 0) {
    log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "YW700147", "hls: hls_push_segment failed, fmp4 segment lost");
  }
}

static void fmp4_open_fragment(hls_seg_ctx_t *s) {
  if (s->fmp4.fmux_v) fmp4_segment_begin(s->fmp4.fmux_v, s->fmp4.fmp4_seq);
  if (s->fmp4.fmux_a) fmp4_segment_begin(s->fmp4.fmux_a, s->fmp4.fmp4_seq);
  fmp4_segment_begin(s->fmp4.fmux, s->fmp4.fmp4_seq++);
  s->fmp4.fmp4_frag_open = 1;
  s->fmp4.fmp4_frag_start_ts = s->fmp4.pend_ts;
  s->fmp4.fmp4_frag_key = s->fmp4.pend_key;
}

static void ac4_defer_apply(hls_seg_ctx_t *s, int *wants_cut, int *cut_now, double *elapsed) {
  if (s->fmp4.fmp4_ac4_defer) {
    if (s->audio.ac4_last_iframe || s->audio.ac4_frame_count - s->fmp4.fmp4_ac4_defer_start_count >= AC4_DEFER_MAX_FRAMES) {
      s->fmp4.fmp4_ac4_defer = 0;
      *wants_cut = 1;
      *cut_now = s->fmp4.fmp4_ac4_defer_ends_seg;
      *elapsed = s->fmp4.fmp4_ac4_defer_elapsed;
    } else {
      *wants_cut = 0;
    }
  } else if (*wants_cut && s->demux.audio_codec == CODEC_AC4 && s->audio.present && s->audio.ready && !s->audio.ac4_last_iframe) {
    s->fmp4.fmp4_ac4_defer = 1;
    s->fmp4.fmp4_ac4_defer_start_count = s->audio.ac4_frame_count;
    s->fmp4.fmp4_ac4_defer_ends_seg = *cut_now;
    s->fmp4.fmp4_ac4_defer_elapsed = *elapsed;
    *wants_cut = 0;
  }
}

/* out-of-range step: reuse last good dur, jump leaves timeline */
static int64_t clamp_dur(int64_t dur, int64_t *last) {
  if (dur < 0) return 0;
  if (dur > HLS_JUMP_MAX_TICKS) return *last ? *last : HLS_JUMP_MAX_TICKS;
  *last = dur;
  return dur;
}

/* open_now/cut_now apply once au is pending, 1 call later. ts: 90 kHz decode-order (dts, or pts if no dts). cts_ticks: (pts-dts) in track ticks, 0 wo dts */
void fmp4_feed_au(hls_seg_ctx_t *s, int kf, int64_t ts, int32_t cts_ticks, int open_now, int cut_now, double elapsed) {
  double pt;
  int chunk_now;
  int wants_cut;
  try_create_fmux(s);
  if (!s->fmp4.fmux) return;
  pt = atomic_load_explicit(&s->part.part_target, memory_order_acquire);
  chunk_now = pt > 0.0 && s->part.n_parts + (s->fmp4.fmp4_have_pend && s->fmp4.pend_starts_frag && s->fmp4.fmp4_frag_open) < HLS_MAX_PARTS - 1 && s->fmp4.fmp4_frag_open && ts >= 0 && s->fmp4.fmp4_frag_start_ts >= 0 && (double)(ts - s->fmp4.fmp4_frag_start_ts) / HLS_TS_HZ >= pt;
  wants_cut = open_now || cut_now || chunk_now;
  ac4_defer_apply(s, &wants_cut, &cut_now, &elapsed);
  if (s->fmp4.fmp4_have_pend && ts >= 0 && s->fmp4.pend_ts >= 0 && (s->fmp4.fmp4_frag_open || s->fmp4.pend_starts_frag)) {
    fmp4_sample_t samp;
    int64_t dur = clamp_dur(ts - s->fmp4.pend_ts, &s->fmp4.last_dur);
    if (s->fmp4.pend_starts_frag) {
      if (s->fmp4.fmp4_frag_open) fmp4_close_fragment(s, pt);
      else s->fmp4.fmp4_anchor = s->fmp4.pend_ts;
      fmp4_open_fragment(s);
    }
    memset(&samp, 0, sizeof samp);
    samp.track_idx = 0;
    samp.data = s->fmp4.pend_data;
    samp.size = s->fmp4.pend_len;
    samp.duration = (uint32_t)dur;
    samp.cts_offset = s->fmp4.pend_cts;
    samp.keyframe = s->fmp4.pend_key;
    fmp4_segment_add_sample(s->fmp4.fmux, &samp);
    if (s->fmp4.fmux_v) fmp4_segment_add_sample(s->fmp4.fmux_v, &samp);
  }
  if (buf_reserve(&s->fmp4.pend_data, &s->fmp4.pend_cap, s->video.nal_scratch_len) < 0) {
    log_throttled(&s->oom_drop_throttle, LOG_THROTTLE_WINDOW_S, "YW500216", "hls: buf_reserve failed, fmp4 access unit dropped");
    return;
  }
  memcpy(s->fmp4.pend_data, s->video.nal_scratch, s->video.nal_scratch_len);
  s->fmp4.pend_len = s->video.nal_scratch_len;
  s->fmp4.pend_key = kf;
  s->fmp4.pend_ts = ts;
  s->fmp4.pend_cts = cts_ticks;
  s->fmp4.pend_starts_frag = wants_cut;
  s->fmp4.pend_ends_seg = cut_now;
  s->fmp4.pend_elapsed = elapsed;
  s->fmp4.fmp4_have_pend = 1;
}

/* fmp4 only, standalone-ES lcevc track. one PES = one au, duration from next au's pts */
void fmp4_feed_lcevc_au(hls_seg_ctx_t *s, int64_t ts, const unsigned char *data, size_t len) {
  if (!s->fmp4.fmux || !s->fmp4.fmp4_frag_open || s->fmp4.fmp4_lcevc_track_idx < 0) return;
  if (!s->lcevc_track.seeded) {
    int64_t delta = ts - s->fmp4.fmp4_anchor;
    if (delta < 0) return; /* au predates video's start: drop, retry next au */
    fmp4_track_seed_dts(s->fmp4.fmux, s->fmp4.fmp4_lcevc_track_idx, (uint64_t)delta + s->fmp4.tl_base_ms * (HLS_TS_HZ / 1000));
    if (s->fmp4.fmux_v) fmp4_track_seed_dts(s->fmp4.fmux_v, 1, (uint64_t)delta + s->fmp4.tl_base_ms * (HLS_TS_HZ / 1000));
    s->lcevc_track.seeded = 1;
  }
  if (s->lcevc_track.have_pend) {
    fmp4_sample_t samp;
    int64_t dur = clamp_dur(ts - s->lcevc_track.pend_ts, &s->lcevc_track.last_dur);
    memset(&samp, 0, sizeof samp);
    samp.track_idx = s->fmp4.fmp4_lcevc_track_idx;
    samp.data = s->lcevc_track.pend_data;
    samp.size = s->lcevc_track.pend_len;
    samp.duration = (uint32_t)dur;
    samp.keyframe = 1;
    fmp4_segment_add_sample(s->fmp4.fmux, &samp);
    if (s->fmp4.fmux_v) {
      samp.track_idx = 1;
      fmp4_segment_add_sample(s->fmp4.fmux_v, &samp);
    }
  }
  if (buf_reserve(&s->lcevc_track.pend_data, &s->lcevc_track.pend_cap, len) < 0) {
    log_throttled(&s->oom_drop_throttle, LOG_THROTTLE_WINDOW_S, "YW500217", "hls: buf_reserve failed, lcevc au dropped");
    return;
  }
  memcpy(s->lcevc_track.pend_data, data, len);
  s->lcevc_track.pend_len = len;
  s->lcevc_track.pend_ts = ts;
  s->lcevc_track.have_pend = 1;
}

/* fmp4 only, ts already carries audio raw. duration known upfront, no delayed-pending needed */
void fmp4_feed_audio_au(hls_seg_ctx_t *s, const esc_frame_t *f) {
  fmp4_sample_t samp;
  int32_t dur;
  int64_t start_samples;
  double est;
  double delta;
  if (!s->fmp4.fmux || !s->fmp4.fmp4_frag_open || s->fmp4.fmp4_audio_track_idx < 0) return;
  if (!s->audio.fmp4_audio_seeded) {
    if (!s->audio.pts_anchored) return; /* position vs video unknown yet: drop, retry next frame */

    start_samples = s->audio.nominal_samples - (int64_t)f->samples;
    est = (double)s->audio.anchor_pts + (double)(start_samples - s->audio.anchor_nominal_samples) * HLS_TS_HZ / (double)s->audio.rate;
    delta = est - (double)s->fmp4.fmp4_anchor;
    if (delta < 0.0) return; /* frame predates video's start: drop, retry next frame */
    uint64_t seed = (uint64_t)((delta + (double)s->fmp4.tl_base_ms * (HLS_TS_HZ / 1000)) * (double)s->audio.rate / HLS_TS_HZ + 0.5);
    fmp4_track_seed_dts(s->fmp4.fmux, s->fmp4.fmp4_audio_track_idx, seed);
    if (s->fmp4.fmux_a) fmp4_track_seed_dts(s->fmp4.fmux_a, 0, seed);
    s->audio.fmp4_audio_seeded = 1;
    s->audio.pending_drift_samples = 0; /* stale: measured against dropped frames */
  }
  dur = (int32_t)f->samples;
  if (s->audio.pending_drift_samples) {
    int32_t corr = (int32_t)s->audio.pending_drift_samples;
    s->audio.pending_drift_samples = 0;
    if (dur + corr > 0) dur += corr;
  }
  memset(&samp, 0, sizeof samp);
  samp.track_idx = s->fmp4.fmp4_audio_track_idx;
  samp.data = f->out;
  samp.size = f->outlen;
  samp.duration = (uint32_t)dur;
  samp.keyframe = s->demux.audio_codec == CODEC_AC4 ? f->ac4_iframe : 1;
  fmp4_segment_add_sample(s->fmp4.fmux, &samp);
  if (s->fmp4.fmux_a) {
    samp.track_idx = 0;
    fmp4_segment_add_sample(s->fmp4.fmux_a, &samp);
  }
}
