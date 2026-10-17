/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include "lib/demux/bitreader.h"
#include "lib/demux/tspack.h"
#include "lib/sys/ioutil.h"

#include <string.h>

/* buffers survive. seg_target, max_segs stay */
void video_reset(hls_seg_ctx_t *s) {
  unsigned char *nal = s->video.nal_scratch;
  size_t nal_cap = s->video.nal_scratch_cap;
  unsigned char *av1 = s->video.av1_rb;
  size_t av1_cap = s->video.av1_rbcap;
  double seg_target = s->video.seg_target;
  int max_segs = s->video.max_segs;
  unsigned char *pend = s->lcevc_track.pend_data;
  size_t pend_cap = s->lcevc_track.pend_cap;

  memset(&s->video, 0, sizeof s->video);
  s->video.nal_scratch = nal;
  s->video.nal_scratch_cap = nal_cap;
  s->video.av1_rb = av1;
  s->video.av1_rbcap = av1_cap;
  s->video.seg_target = seg_target;
  s->video.max_segs = max_segs;
  s->video.boot_left = max_segs;
  s->video.first_ts = -1;

  memset(&s->lcevc_track, 0, sizeof s->lcevc_track);
  s->lcevc_track.pend_data = pend;
  s->lcevc_track.pend_cap = pend_cap;

  s->part.part_open = 0;
  s->part.have_last_au = 0;
  s->have_pending_pes = 0;
  s->pending_pes_off = 0;
  s->pending_pes_wall_ms = 0;
  s->len = 0;
}

/* builds s->video.nal_scratch (AVCC AU bytes) as a side effect, used by fmp4 mode */
int detect_keyframe(hls_seg_ctx_t *s, const unsigned char *d, size_t len) {
  size_t p;
  size_t scl = 0;
  int key = 0;
  if (s->demux.video_codec == CODEC_MPEG2V) {
    p = find_startcode(d, len, 0, &scl);
    while (p < len) {
      size_t ns = p + scl;
      size_t scl2 = 0;
      size_t q = find_startcode(d, len, ns, &scl2);
      unsigned code = (ns < len) ? d[ns] : 0xFF;
      if (code == 0x00 && q >= ns + 3 && ((d[ns + 2] >> 3) & 0x07) == 1) key = 1; /* picture_coding_type: 1 = I */
      p = q;
      scl = scl2;
    }
    return key;
  }

  s->video.nal_scratch_len = 0;
  if (s->demux.video_codec == CODEC_AV1) {
    esc_split_obus(&s->video.es, &s->video.nal_scratch, &s->video.nal_scratch_len, &s->video.nal_scratch_cap, d, len, &key, &s->video.av1_rb, &s->video.av1_rbcap);
  } else if (s->demux.video_codec == CODEC_H264 || s->demux.video_codec == CODEC_HEVC || s->demux.video_codec == CODEC_VVC) {
    esc_split_nals(&s->video.es, &s->video.nal_scratch, &s->video.nal_scratch_len, &s->video.nal_scratch_cap, d, len, &key, NULL);
  }
  return key;
}

/* SEG_CONTAINER_TS, LL-HLS enabled only. will_trim: au also opens/cuts enclosing segment, s->buf about to shift to offset 0 */
static void ts_part_feed_au(hls_seg_ctx_t *s, int kf, int64_t ts, size_t au_off, int will_trim) {
  double part_elapsed;
  int new_part_key;

  if (!s->part.part_open) {
    /* au_off is pre-trim: open_now's memmove runs right after, shifting buf to 0. will_trim mirrors close path below, no stale au_off post-trim. */
    s->part.part_start_off = will_trim ? 0 : au_off;
    s->part.part_start_ts = ts;
    s->part.part_key = kf;
    s->part.part_open = 1;
    s->part.have_last_au = 0;
    return;
  }

  part_elapsed = (ts >= 0 && s->part.part_start_ts >= 0) ? (double)(ts - s->part.part_start_ts) / HLS_TS_HZ : 0.0;
  if (!will_trim && (part_elapsed < atomic_load_explicit(&s->part.part_target, memory_order_acquire) || s->part.n_parts >= HLS_MAX_PARTS - 1)) {
    s->part.last_au_off = au_off;
    s->part.last_au_ts = ts;
    s->part.last_au_key = kf;
    s->part.have_last_au = 1;
    return;
  }

  /* frame duration rarely divides part_target evenly, naive close always overshoots */
  new_part_key = kf;
  if (!will_trim && s->part.have_last_au && s->part.last_au_off > s->part.part_start_off) {
    au_off = s->part.last_au_off;
    ts = s->part.last_au_ts;
    new_part_key = s->part.last_au_key;
    part_elapsed = (double)(ts - s->part.part_start_ts) / HLS_TS_HZ;
  }

  if (au_off > s->part.part_start_off && (!s->store || hls_push_part_at(s->store, s->buf + s->part.part_start_off, au_off - s->part.part_start_off, part_elapsed, s->part.part_key) < 0))
    log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "YW700148", "hls: hls_push_part failed, part lost");
  s->part.n_parts = will_trim ? 0 : s->part.n_parts + 1;
  s->part.part_start_off = will_trim ? 0 : au_off;
  s->part.part_start_ts = ts;
  s->part.part_key = new_part_key;
  s->part.have_last_au = 0;
}

void hls_seg_stamp_cc(unsigned char *cc, unsigned char *pkts, size_t n_pkts, unsigned pid) {
  for (size_t i = 0; i < n_pkts; i++) {
    unsigned char *p = pkts + i * 188;
    if (tspack_pid(p) != pid || !(p[3] & 0x10)) continue;
    *cc = (*cc + 1) & 0x0F;
    p[3] = (p[3] & 0xF0) | *cc;
  }
}

static unsigned char psi_next_cc(const hls_seg_ctx_t *s, unsigned pid, unsigned char last) {
  for (size_t off = 0; off + 188 <= s->len; off += 188)
    if (tspack_pid(s->buf + off) == pid && (s->buf[off + 3] & 0x10)) return s->buf[off + 3] & 0x0F;
  return (last + 1) & 0x0F;
}

static void psi_restamp(hls_seg_ctx_t *s, unsigned pmt_pid, unsigned char pat_next, unsigned char pmt_next) {
  s->buf_cc_pat = (pat_next - 1) & 0x0F;
  hls_seg_stamp_cc(&s->buf_cc_pat, s->buf, s->len / 188, 0);
  if (!pmt_pid) return;
  s->buf_cc_pmt = (pmt_next - 1) & 0x0F;
  hls_seg_stamp_cc(&s->buf_cc_pmt, s->buf, s->len / 188, pmt_pid);
}

/* fmp4 doesn't keep raw ts */
static void buf_trim(hls_seg_ctx_t *s, size_t cut_off) {
  size_t n;
  unsigned pmt_pid;
  unsigned char pat_next;
  unsigned char pmt_next;
  if (s->container != SEG_CONTAINER_TS) return;
  memmove(s->buf, s->buf + cut_off, s->len - cut_off);
  s->len -= cut_off;
  n = (s->psi_pat_ok ? 188u : 0u) + (size_t)s->psi_pmt_n * 188;
  if (!n || (s->len >= 188 && !(s->buf[1] & 0x1F) && !s->buf[2]) || buf_reserve(&s->buf, &s->cap, s->len + n) < 0) return;
  pmt_pid = s->psi_pmt_n ? tspack_pid(s->psi_pmt) : 0;
  pat_next = psi_next_cc(s, 0, s->buf_cc_pat);
  pmt_next = pmt_pid ? psi_next_cc(s, pmt_pid, s->buf_cc_pmt) : 0;
  memmove(s->buf + n, s->buf, s->len);
  if (s->psi_pat_ok) memcpy(s->buf, s->psi_pat, 188);
  memcpy(s->buf + (s->psi_pat_ok ? 188 : 0), s->psi_pmt, (size_t)s->psi_pmt_n * 188);
  s->len += n;
  psi_restamp(s, pmt_pid, pat_next, pmt_next);
}

void handle_video_pes(hls_seg_ctx_t *s, int has_pts, uint64_t pts, int has_dts, uint64_t dts, const unsigned char *data, size_t len) {
  size_t cut_off;
  int64_t pts_t;
  int64_t ts;
  int32_t cts_ticks;
  double elapsed;
  double pt;
  int kf;
  int open_now;
  int cut_now;
  if (!s->have_pending_pes) return; /* stream start: no offset recorded yet for this PES */
  cut_off = s->pending_pes_off;
  kf = detect_keyframe(s, data, len);
  pts_t = has_pts ? pts_unwrap_ticks(&s->video.ptswrap, pts) : -1;
  ts = has_dts ? pts_unwrap_ticks(&s->video.dtswrap, dts) : pts_t;
  cts_ticks = (has_dts && pts_t >= 0) ? (int32_t)(pts_t - ts) : 0;
  if (ts >= 0) {
    int64_t step = ts - s->video.last_ts;
    if (s->video.have_last_ts && (step > HLS_SPLICE_TICKS || step < -HLS_SPLICE_TICKS)) {
      s->splice_pending = 1;
      return;
    }
    s->video.last_ts = ts;
    s->video.have_last_ts = 1;
  }
  open_now = kf && !s->video.seg_open;
  elapsed = (kf && s->video.seg_open && ts >= 0 && s->video.first_ts >= 0) ? (double)(ts - s->video.first_ts) / HLS_TS_HZ : 0.0;
  pt = atomic_load_explicit(&s->part.part_target, memory_order_acquire);
  cut_now = kf && s->video.seg_open && (elapsed >= s->video.seg_target || (s->container == SEG_CONTAINER_TS && pt <= 0.0 && s->video.boot_left > 0));
  if (s->container == SEG_CONTAINER_FMP4)
    fmp4_feed_au(s, kf, ts, cts_ticks, open_now, cut_now, elapsed);
  else if (s->container == SEG_CONTAINER_TS && pt > 0.0 && (s->video.seg_open || open_now))
    ts_part_feed_au(s, kf, ts, cut_off, open_now || cut_now);
  if (!kf) return;
  if (open_now) {
    /* first keyframe ever: drop any pre-keyframe garbage, start segment 0 here */
    buf_trim(s, cut_off);
    s->video.seg_open = 1;
    s->video.first_ts = ts;
    if (s->store && s->pending_pes_wall_ms && hls_store_anchor_at(s->store, s->pending_pes_wall_ms) < 0)
      log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "YW700155", "hls: store anchor failed, availabilityStartTime stays at store open");
    if (s->pending_pes_wall_ms) fmp4_track_stores_anchor(s, s->pending_pes_wall_ms);
    return;
  }
  if (!cut_now) return;
  if (s->container == SEG_CONTAINER_TS) {
    if (pt > 0.0) {
      if (!s->store || hls_push_segment_ll_at(s->store, elapsed) < 0)
        log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "YW700149", "hls: hls_push_segment_ll failed, segment lost");
    } else {
      if (!s->store || hls_push_segment_at(s->store, s->buf, cut_off, elapsed) < 0)
        log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "YW700150", "hls: hls_push_segment failed, segment lost");
      if (s->video.boot_left > 0) s->video.boot_left--;
    }
  }
  buf_trim(s, cut_off);
  s->video.first_ts = ts;
}

void handle_lcevc_pes(hls_seg_ctx_t *s, int has_pts, uint64_t pts, const unsigned char *data, size_t len) {
  int64_t ts = has_pts ? pts_unwrap_ticks(&s->lcevc_track.ptswrap, pts) : -1;
  if (ts < 0) return;
  fmp4_feed_lcevc_au(s, ts, data, len);
}
