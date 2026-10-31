/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_HLS_SEGMENT_PRIV_H
#define DIPIXY_HLS_SEGMENT_PRIV_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#include "lib/demux/aacprobe/aacprobe.h"
#include "lib/demux/escodec/aubuild.h"
#include "lib/demux/pes.h"
#include "lib/demux/psi/psi.h"
#include "lib/helper/log.h"
#include "lib/mux/fmp4/fmp4.h"
#include "lib/mux/pmt_filter.h"

#include "../ts/lcevcselect.h"

#include "segment.h"

/* 90 kHz ticks: pts/dts domain, fmp4 video timescale */
#define HLS_TS_HZ 90000

/* pts/dts step beyond this is a splice, not a frame: dropped from timeline */
#define HLS_JUMP_MAX_TICKS HLS_TS_HZ
#define HLS_SPLICE_TICKS (PTS_DISC_MS * (HLS_TS_HZ / 1000))

/* pid locking: which pid is video/audio, once psi_ready(). demux.c owns this */
typedef struct {
  psi_t *psi;
  pes_t *pes;
  int video_pid_known;
  unsigned video_pid;
  codec_t video_codec;
  int audio_pid_known;
  unsigned audio_pid;
  codec_t audio_codec;
  int lcevc_pid_known; /* fmp4 only: one specific standalone-ES lcevc pid resolved */
  unsigned lcevc_pid;

  /* locked program's own pids only, built once video_pid_known: PAT, its PMT, PCR, PES. MPTS -> SPTS */
  unsigned allowed_pids[PSI_MAX_ES + 3];
  int n_allowed;

  unsigned long pmt_seen; /* psi_pmt_parsed() at last lock check */
} seg_demux_t;

/* video ES decode + classic (non-fmp4) segmentation cadence. video.c owns this */
typedef struct {
  esc_track_t es;              /* SPS/PPS scratch, also feeds cpriv for fmp4's avcC/hvcC */
  unsigned char *nal_scratch;  /* esc_handle_*_nal's AU buffer, discarded each AU */
  size_t nal_scratch_len;
  size_t nal_scratch_cap;
  unsigned char *av1_rb;
  size_t av1_rbcap;
  pts_unwrap_t ptswrap;
  pts_unwrap_t dtswrap;

  int seg_open;          /* 1 once first keyframe seen */
  int64_t last_ts;
  int have_last_ts;
  int64_t first_ts;   /* decode-order ticks (dts, or pts if no dts) at segment start, -1 if unknown */
  double seg_target;
  int max_segs;
  int boot_left; /* classic TS cold-open cut countdown */
  hls_video_info_t info; /* fmp4 only, valid once fmux exists */
  int measure_rate;      /* 1: frame rate pending, derive from sample count */
  unsigned seg_samples;  /* samples in current segment */
} seg_video_t;

/* fmp4 only, ts already carries audio raw. picked at video pid lock-on. audio.c owns this */
typedef struct {
  int present;   /* 1: locked program has a supported audio ES */
  esc_track_t es; /* AAC ASC / LATM state, next_frame()'s scratch */
  unsigned char *rem;
  size_t remlen;
  size_t remcap;
  int ready; /* 1 once first audio frame parsed: rate/channels/etc below are valid */
  unsigned rate;
  unsigned channels;
  unsigned out_rate; /* with SBR, else 0 */
  unsigned out_channels; /* with SBR, else 0 */
  aacprobe_t aac_probe;
  int aac_probing;
  int aac_settled;
  asc_info_t aac_asc;
  unsigned char aac_cfg[ASC_EXPLICIT_MAX]; /* explicit SBR/PS config, 0 len: source config stands */
  size_t aac_cfg_len;
  unsigned bsid; /* AC3/EAC3 only */
  unsigned bsmod;
  unsigned acmod;
  unsigned lfeon;
  unsigned bitrate_code; /* AC3: frmsizecod >> 1. EAC3: estimated data_rate kbps */
  unsigned truehd_format_info; /* TrueHD only */
  unsigned truehd_peak_data_rate; /* TrueHD only */
  int dts_has_core; /* DTS/DTS-HD/DTS-HD-MA only */
  unsigned ac4_bitstream_version;
  unsigned ac4_presentation_version;
  unsigned ac4_mdcompat;
  int ac4_last_iframe;
  int64_t ac4_frame_count;

  /* nominal audio duration drifts unbounded off encoder clock, pes pts corrects. nominal kept in exact native samples,
     never ms: ms accumulation truncates frames */
  pts_unwrap_t ptswrap;
  int pts_anchored;
  int64_t anchor_pts;
  int64_t anchor_nominal_samples;
  int64_t nominal_samples;
  int64_t pending_drift_samples;
  int fmp4_audio_seeded;
  esc_chcfg_t chcfg_scheme; /* from first frame, DASH channel configuration */
  unsigned chcfg_value;
} seg_audio_t;

/* fmp4 lcevc track, 1 pes = 1 au. video.c owns */
typedef struct {
  pts_unwrap_t ptswrap;
  unsigned char *pend_data;
  size_t pend_len;
  size_t pend_cap;
  int64_t pend_ts;
  int64_t last_dur; /* last in-range sample duration */
  int have_pend;
  int seeded; /* seed_dts() done, relative to fmp4_anchor */
} seg_lcevc_t;

/* fmp4 fragment/segment mux state. mux.c owns this */
typedef struct {
  fmp4_mux_t *fmux; /* SEG_CONTAINER_FMP4 only, created once SPS/PPS known */
  fmp4_mux_t *fmux_v; /* video (+lcevc) tracks of fmux. DASH, audio streams only */
  fmp4_mux_t *fmux_a; /* audio track of fmux. DASH, audio streams only */
  int fmp4_frag_open;
  int64_t fmp4_frag_start_ts; /* current open fragment's first sample ts, drives chunk_now */
  int fmp4_frag_key;             /* current open fragment's first sample keyframe flag */
  uint32_t fmp4_seq;
  int64_t fmp4_anchor;
  int fmp4_audio_track_idx; /* -1 until fmux created with an audio track */
  int fmp4_lcevc_track_idx; /* -1 until fmux created with an lcevc track */

  /* fmp4 duration: next au's dts minus this one's. delayed one au past pending_pes_off below */
  unsigned char *pend_data;
  size_t pend_len;
  size_t pend_cap;
  int pend_key;
  int64_t pend_ts;
  int64_t last_dur; /* last in-range sample duration */
  int32_t pend_cts;    /* (pts - dts) in track ticks, 0 if no dts */
  int pend_starts_frag;
  int pend_ends_seg;   /* set alongside starts_frag: this boundary also closes the enclosing segment */
  double pend_elapsed; /* set alongside starts_frag, used if this pend later closes a segment */
  int fmp4_have_pend;

  uint64_t tl_base_ms; /* timeline position of fmux's first sample: tfdt continues across a relock */

  int fmp4_ac4_defer;
  int64_t fmp4_ac4_defer_start_count;
  int fmp4_ac4_defer_ends_seg;
  double fmp4_ac4_defer_elapsed;
} seg_fmp4_t;

/* LL-HLS/LL-DASH part boundary tracking, both container kinds */
typedef struct {
  /* 0 = LL off. TS: part closes on keyframe or part_target secs, whichever first.
     FMP4: chunk closes on part_target secs, sample-aligned, keyframe independent.
     atomic: hls_seg_touch() write vs pump's unlocked read */
  _Atomic double part_target;
  size_t part_start_off;
  int64_t part_start_ts;
  int part_key;
  int part_open;
  int n_parts; /* last slot reserved for segment-ending part */

  /* fallback close point: avoids exceeding part_target */
  size_t last_au_off;
  int64_t last_au_ts;
  int last_au_key;
  int have_last_au;
} seg_part_t;

typedef struct hls_seg_ctx {
  int splice_pending;
  capture_ctx_t *cap_ctx;
  _Atomic(struct hls_seg_ctx *) chain_next;
  hls_store_t *store;
  hls_store_t *store_v; /* SEG_CONTAINER_FMP4_VIDEO/AUDIO twins of store, NULL until fmux_v/fmux_a exist */
  hls_store_t *store_a;
  pid_filter_t filter; /* lookup key, as requested */
  pid_filter_t drop;   /* filter plus lcevc layers not selected: what feeds drop */
  unsigned pmt_pid; /* 0 = auto */
  unsigned char cc_pmt; /* SEG_CONTAINER_TS: rewritten PMT packets' own cc */
  unsigned char cc_pat; /* SEG_CONTAINER_TS: rewritten PAT packets' own cc */
  unsigned char buf_cc_pmt; /* SEG_CONTAINER_TS: cc of last PMT packet in buf, segments continue it */
  unsigned char buf_cc_pat; /* SEG_CONTAINER_TS: cc of last PAT packet in buf */
  unsigned char psi_pat[188];
  int psi_pat_ok;
  unsigned char psi_pmt[PMT_FILTER_PKT_MAX * 188];
  unsigned psi_pmt_n;
  lcevc_select_t lcevc;
  _Atomic int64_t last_request_ms;
  _Atomic int refcount;

  seg_demux_t demux;
  seg_video_t video;
  seg_audio_t audio;
  seg_lcevc_t lcevc_track;
  seg_fmp4_t fmp4;
  seg_part_t part;

  /* TS only: accumulates every incoming packet (all PIDs) since last cut */
  unsigned char *buf;
  size_t len;
  size_t cap;
  size_t buf_max; /* len limit without keyframe, TS only */

  /* offset in buf marking start of PES currently being reassembled by pes.c.
     pes.c's callback fires for PES only once its successor's PUSI arrives, one AU late -> "pending" */
  size_t pending_pes_off;
  uint64_t pending_pes_wall_ms; /* unix ms, PUSI arrival of PES */
  int have_pending_pes;
  seg_container_t container;

  log_throttle_t seg_push_fail_throttle;
  log_throttle_t oom_drop_throttle;
  log_throttle_t audio_parse_throttle;

  _Atomic int mp4push_sub_head; /* -1 empty, else segment/mp4push.c chain */
} hls_seg_ctx_t;

/* segment.c */
int buf_reserve(unsigned char **buf, size_t *cap, size_t need);
void hls_seg_registry_lock(void);
void hls_seg_registry_unlock(void);
hls_seg_ctx_t *hls_seg_find_locked(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container);
int seg_try_pin(hls_seg_ctx_t *s);
void seg_unpin(hls_seg_ctx_t *s);
hls_seg_ctx_t *find_locked(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container);
int touch_pinned(hls_seg_ctx_t *s, double part_target, int *llhls_newly_on);
void unlink_from_ctx_chain(hls_seg_ctx_t *victim);

/* mp4push.c */
void mp4push_deliver(const hls_seg_ctx_t *s, const unsigned char *data, size_t len);

/* mux.c */
void fmp4_reset(hls_seg_ctx_t *s, uint64_t base_ms);
void try_create_fmux(hls_seg_ctx_t *s);
void fmp4_feed_au(hls_seg_ctx_t *s, int kf, int64_t ts, int32_t cts_ticks, int open_now, int cut_now, double elapsed);
void fmp4_feed_audio_au(hls_seg_ctx_t *s, const esc_frame_t *f);
void fmp4_feed_lcevc_au(hls_seg_ctx_t *s, int64_t ts, const unsigned char *data, size_t len);
void fmp4_track_stores_sync(hls_seg_ctx_t *s);
void fmp4_track_stores_anchor(hls_seg_ctx_t *s, uint64_t wall_ms);
void fmp4_mux_free_all(hls_seg_ctx_t *s);
int build_video_track_cfg(hls_seg_ctx_t *s, fmp4_track_cfg_t *trk, unsigned char *cpriv, size_t cpriv_cap);

/* video.c */
void hls_seg_stamp_cc(unsigned char *cc, unsigned char *pkts, size_t n_pkts, unsigned pid);
void video_reset(hls_seg_ctx_t *s);
void handle_video_pes(hls_seg_ctx_t *s, int has_pts, uint64_t pts, int has_dts, uint64_t dts, const unsigned char *data, size_t len);
void handle_lcevc_pes(hls_seg_ctx_t *s, int has_pts, uint64_t pts, const unsigned char *data, size_t len);
int detect_keyframe(hls_seg_ctx_t *s, const unsigned char *d, size_t len);
void fmp4_note_leading(hls_seg_ctx_t *s);

/* audio.c */
void audio_reset(hls_seg_ctx_t *s);
void handle_audio_pes(hls_seg_ctx_t *s, int has_pts, uint64_t pts, const unsigned char *data, size_t len);

/* demux.c */
void hls_seg_on_pes(void *ctx, unsigned pid, int has_pts, uint64_t pts, int has_dts, uint64_t dts, const unsigned char *data, size_t len);

#endif
