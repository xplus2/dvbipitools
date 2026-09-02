/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include "lib/demux/bitreader.h"

#include <string.h>

/* builds s->nal_scratch (AVCC AU bytes) as a side effect, used by fmp4 mode */
static int detect_keyframe(hls_seg_ctx_t *s, const unsigned char *d, size_t len) {
  size_t p, scl = 0;
  int key = 0;
  if (s->video_codec == CODEC_MPEG2V) {
    p = find_startcode(d, len, 0, &scl);
    while (p < len) {
      size_t ns = p + scl, scl2 = 0;
      size_t q = find_startcode(d, len, ns, &scl2);
      unsigned code = (ns < len) ? d[ns] : 0xFF;
      if (code == 0x00 && q >= ns + 3 && ((d[ns + 2] >> 3) & 0x07) == 1) /* picture_coding_type: 1 = I */
        key = 1;
      p = q;
      scl = scl2;
    }
    return key;
  }

  s->nal_scratch_len = 0;
  p = find_startcode(d, len, 0, &scl);
  while (p < len) {
    size_t ns = p + scl, scl2 = 0;
    size_t q = find_startcode(d, len, ns, &scl2);
    size_t n = q - ns;
    unsigned type;
    if (n) {
      if (s->video_codec == CODEC_H264) {
        type = d[ns] & 0x1F;
        esc_handle_h264_nal(&s->es, &s->nal_scratch, &s->nal_scratch_len, &s->nal_scratch_cap, type, d + ns, n, &key);
      } else if (s->video_codec == CODEC_HEVC) {
        type = (d[ns] >> 1) & 0x3F;
        esc_handle_hevc_nal(&s->es, &s->nal_scratch, &s->nal_scratch_len, &s->nal_scratch_cap, type, d + ns, n, &key);
      }
    }
    p = q;
    scl = scl2;
  }
  return key;
}

/* SEG_CONTAINER_TS, LL-HLS enabled only. will_trim: au also opens/cuts
   enclosing segment, s->buf about to shift to offset 0 */
static void ts_part_feed_au(hls_seg_ctx_t *s, int kf, int64_t ts_ms, size_t au_off, int will_trim) {
  double part_elapsed;
  int new_part_key;

  if (!s->part_open) {
    /* au_off is pre-trim: open_now's memmove runs right after, shifting buf
       to 0. will_trim mirrors close path below, avoids stale au_off post-trim. */
    s->part_start_off = will_trim ? 0 : au_off;
    s->part_start_ts_ms = ts_ms;
    s->part_key = kf;
    s->part_open = 1;
    s->have_last_au = 0;
    return;
  }

  part_elapsed = (ts_ms >= 0 && s->part_start_ts_ms >= 0) ? (double)(ts_ms - s->part_start_ts_ms) / 1000.0 : 0.0;
  if (!will_trim && part_elapsed < atomic_load_explicit(&s->part_target, memory_order_acquire)) {
    s->last_au_off = au_off;
    s->last_au_ts_ms = ts_ms;
    s->last_au_key = kf;
    s->have_last_au = 1;
    return;
  }

  /* frame duration rarely divides part_target evenly, naive close always overshoots */
  new_part_key = kf;
  if (!will_trim && s->have_last_au && s->last_au_off > s->part_start_off) {
    au_off = s->last_au_off;
    ts_ms = s->last_au_ts_ms;
    new_part_key = s->last_au_key;
    part_elapsed = (double)(ts_ms - s->part_start_ts_ms) / 1000.0;
  }

  if (au_off > s->part_start_off &&
      hls_push_part(s->cap_ctx, &s->filter, s->pmt_pid, SEG_CONTAINER_TS, s->buf + s->part_start_off, au_off - s->part_start_off, part_elapsed, s->part_key) < 0)
    log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "hls: hls_push_part failed, part lost");
  s->part_start_off = will_trim ? 0 : au_off;
  s->part_start_ts_ms = ts_ms;
  s->part_key = new_part_key;
  s->have_last_au = 0;
}

void handle_video_pes(hls_seg_ctx_t *s, int has_pts, uint64_t pts, int has_dts, uint64_t dts, const unsigned char *data, size_t len) {
  size_t cut_off;
  int64_t pts_ms, ts_ms;
  int32_t cts_ticks;
  double elapsed;
  int kf, open_now, cut_now;
  if (!s->have_pending_pes) return; /* stream start: no offset recorded yet for this PES */
  cut_off = s->pending_pes_off;

  kf = detect_keyframe(s, data, len);
  pts_ms = has_pts ? pts_unwrap(&s->ptswrap, pts) : -1;
  ts_ms = has_dts ? pts_unwrap(&s->dtswrap, dts) : pts_ms;
  cts_ticks = (has_dts && pts_ms >= 0) ? (int32_t)((pts_ms - ts_ms) * 90) : 0;

  open_now = kf && !s->seg_open;
  elapsed = (kf && s->seg_open && ts_ms >= 0 && s->first_ts_ms >= 0) ? (double)(ts_ms - s->first_ts_ms) / 1000.0 : 0.0;
  {
    double pt = atomic_load_explicit(&s->part_target, memory_order_acquire);
    cut_now = kf && s->seg_open && (elapsed >= s->seg_target || (s->container == SEG_CONTAINER_TS && pt <= 0.0 && s->boot_left > 0));
    if (s->container == SEG_CONTAINER_FMP4)
      fmp4_feed_au(s, kf, ts_ms, cts_ticks, open_now, cut_now, elapsed);
    else if (s->container == SEG_CONTAINER_TS && pt > 0.0 && (s->seg_open || open_now))
      ts_part_feed_au(s, kf, ts_ms, cut_off, open_now || cut_now);
  }
  if (!kf) return;
  if (open_now) {
    /* first keyframe ever: drop any pre-keyframe garbage, start segment 0 here */
    memmove(s->buf, s->buf + cut_off, s->len - cut_off);
    s->len -= cut_off;
    s->seg_open = 1;
    s->first_ts_ms = ts_ms;
    return;
  }
  if (!cut_now) return;
  if (s->container == SEG_CONTAINER_TS) {
    if (atomic_load_explicit(&s->part_target, memory_order_acquire) > 0.0) {
      if (hls_push_segment_ll(s->cap_ctx, &s->filter, s->pmt_pid, SEG_CONTAINER_TS, elapsed) < 0)
        log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "hls: hls_push_segment_ll failed, segment lost");
    } else {
      if (hls_push_segment(s->cap_ctx, &s->filter, s->pmt_pid, SEG_CONTAINER_TS, s->buf, cut_off, elapsed) < 0)
        log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "hls: hls_push_segment failed, segment lost");
      if (s->boot_left > 0)
        s->boot_left--;
    }
  }
  memmove(s->buf, s->buf + cut_off, s->len - cut_off);
  s->len -= cut_off;
  s->first_ts_ms = ts_ms;
}
