/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_HLS_SEGMENT_PRIV_H
#define DIPIXY_HLS_SEGMENT_PRIV_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#include "lib/demux/escodec/aubuild.h"
#include "lib/demux/pes.h"
#include "lib/demux/psi/psi.h"
#include "lib/helper/log.h"
#include "lib/mux/fmp4/fmp4.h"

#include "segment.h"

typedef struct hls_seg_ctx {
  capture_ctx_t *cap_ctx;
  _Atomic(struct hls_seg_ctx *) chain_next;
  pid_filter_t filter;
  unsigned pmt_pid; /* 0 = auto */
  unsigned char cc_pmt; /* HLS_CONTAINER_TS: rewritten PMT packets' own cc */
  _Atomic int64_t last_request_ms;
  _Atomic int refcount;

  psi_t *psi;
  pes_t *pes;
  int video_pid_known;
  unsigned video_pid;
  codec_t video_codec;

  /* locked program's own pids only, built once video_pid_known: PAT, its PMT, PCR, PES. MPTS -> SPTS */
  unsigned allowed_pids[PSI_MAX_ES + 3];
  int n_allowed;

  /* fmp4 only, ts already carries audio raw. picked at video pid lock-on */
  int audio_present;   /* 1: locked program has a supported audio ES */
  int audio_pid_known;
  unsigned audio_pid;
  codec_t audio_codec;
  esc_track_t es_audio; /* AAC ASC / LATM state, next_frame()'s scratch */
  unsigned char *audio_rem;
  size_t audio_remlen, audio_remcap;
  int audio_ready; /* 1 once first audio frame parsed: rate/channels/etc below are valid */
  unsigned audio_rate, audio_channels;
  unsigned audio_bsid, audio_bsmod, audio_acmod, audio_lfeon; /* AC3/EAC3 only */
  unsigned audio_bitrate_code; /* AC3: frmsizecod. EAC3: estimated data_rate kbps */
  int fmp4_audio_track_idx; /* -1 until fmux created with an audio track */

  /* nominal audio duration drifts unbounded off encoder clock, pes pts corrects. nominal kept in exact native samples,
     never ms: ms accumulation truncates frames */
  pts_unwrap_t audio_ptswrap;
  int audio_pts_anchored;
  int64_t audio_anchor_pts_ms;
  int64_t audio_anchor_nominal_samples;
  int64_t audio_nominal_samples;
  int64_t audio_pending_drift_samples;

  /* accumulates every incoming packet (all PIDs) since last cut */
  unsigned char *buf;
  size_t len, cap;

  /* offset in buf marking start of PES currently being reassembled by pes.c.
     pes.c's callback fires for PES only once its successor's PUSI arrives, one AU late -> "pending" */
  size_t pending_pes_off;
  int have_pending_pes;

  int seg_open;          /* 1 once first keyframe seen */
  int64_t first_ts_ms;   /* decode-order ms (dts, or pts if no dts) at segment start, -1 if unknown */
  pts_unwrap_t ptswrap, dtswrap;
  double seg_target;
  int max_segs;
  int boot_left; /* classic TS cold-open cut countdown */

  esc_track_t es;              /* SPS/PPS scratch, also feeds cpriv for fmp4's avcC/hvcC */
  unsigned char *nal_scratch;  /* esc_handle_*_nal's AU buffer, discarded each AU */
  size_t nal_scratch_len, nal_scratch_cap;

  hls_container_t container;
  fmp4_mux_t *fmux; /* HLS_CONTAINER_FMP4 only, created once SPS/PPS known */
  int fmp4_frag_open;
  int64_t fmp4_frag_start_ts_ms; /* current open fragment's first sample ts, drives chunk_now */
  int fmp4_frag_key;             /* current open fragment's first sample keyframe flag */
  uint32_t fmp4_seq;
  int64_t fmp4_anchor_ms;
  int fmp4_audio_seeded;

  /* fmp4 duration: next au's dts minus this one's. delayed one au past pending_pes_off above */
  unsigned char *fmp4_pend_data;
  size_t fmp4_pend_len, fmp4_pend_cap;
  int fmp4_pend_key;
  int64_t fmp4_pend_ts_ms;
  int32_t fmp4_pend_cts;    /* (pts - dts) in track ticks, 0 if no dts */
  int fmp4_pend_starts_frag;
  int fmp4_pend_ends_seg;   /* set alongside starts_frag: this boundary also closes the enclosing segment */
  double fmp4_pend_elapsed; /* set alongside starts_frag, used if this pend later closes a segment */
  int fmp4_have_pend;

  /* 0 = LL off. TS: part closes on keyframe or part_target secs, whichever first.
     FMP4: chunk closes on part_target secs, sample-aligned, independent of keyframe.
     atomic: hls_seg_touch() write vs pump's unlocked read */
  _Atomic double part_target;
  size_t part_start_off;
  int64_t part_start_ts_ms;
  int part_key;
  int part_open;

  /* fallback close point: avoids exceeding part_target */
  size_t last_au_off;
  int64_t last_au_ts_ms;
  int last_au_key;
  int have_last_au;

  log_throttle_t seg_push_fail_throttle;
  log_throttle_t oom_drop_throttle;
  log_throttle_t audio_parse_throttle;
} hls_seg_ctx_t;

/* segment.c */
int buf_reserve(unsigned char **buf, size_t *cap, size_t need);

/* mux.c */
void try_create_fmux(hls_seg_ctx_t *s);
void fmp4_feed_au(hls_seg_ctx_t *s, int kf, int64_t ts_ms, int32_t cts_ticks, int open_now, int cut_now, double elapsed);
void fmp4_feed_audio_au(hls_seg_ctx_t *s, const esc_frame_t *f);

/* video.c */
void handle_video_pes(hls_seg_ctx_t *s, int has_pts, uint64_t pts, int has_dts, uint64_t dts, const unsigned char *data, size_t len);

/* audio.c */
void handle_audio_pes(hls_seg_ctx_t *s, int has_pts, uint64_t pts, const unsigned char *data, size_t len);

/* demux.c */
void hls_seg_on_pes(void *ctx, unsigned pid, int has_pts, uint64_t pts, int has_dts, uint64_t dts, const unsigned char *data, size_t len);

#endif
