/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"
#include <string.h>

#define HLS_AUDIO_REM_MAX 65536

void handle_audio_pes(hls_seg_ctx_t *s, int has_pts, uint64_t pts, const unsigned char *data, size_t len) {
  size_t pos = 0;

  if (s->container != SEG_CONTAINER_FMP4) return;
  if (has_pts && s->audio.audio_ready) {
    int64_t pts_ms = pts_unwrap(&s->audio.audio_ptswrap, pts);
    if (!s->audio.audio_pts_anchored) {
      s->audio.audio_anchor_pts_ms = pts_ms;
      s->audio.audio_anchor_nominal_samples = s->audio.audio_nominal_samples;
      s->audio.audio_pts_anchored = 1;
    } else {
      int64_t expected_samples = (pts_ms - s->audio.audio_anchor_pts_ms) * (int64_t)s->audio.audio_rate / 1000;
      int64_t actual_samples = s->audio.audio_nominal_samples - s->audio.audio_anchor_nominal_samples;
      s->audio.audio_pending_drift_samples = expected_samples - actual_samples;
      s->audio.audio_anchor_pts_ms = pts_ms;
      s->audio.audio_anchor_nominal_samples = s->audio.audio_nominal_samples;
    }
  }
  if (s->audio.audio_remlen > HLS_AUDIO_REM_MAX) {
    log_throttled(&s->audio_parse_throttle, LOG_THROTTLE_WINDOW_S, "hls: audio_remlen exceeded HLS_AUDIO_REM_MAX, discarding pending bytes");
    s->audio.audio_remlen = 0;
  }
  if (esc_rem_append(&s->audio.audio_rem, &s->audio.audio_remlen, &s->audio.audio_remcap, data, len)) return;

  while (pos < s->audio.audio_remlen) {
    esc_frame_t f;
    int r = next_frame(&s->audio.es_audio, s->audio.audio_rem + pos, s->audio.audio_remlen - pos, &f);
    if (r > 0) break;
    if (r < 0) {
      log_throttled(&s->audio_parse_throttle, LOG_THROTTLE_WINDOW_S, "hls: audio ES parse failed, resyncing byte by byte");
      pos++;
      continue;
    }
    if (!s->audio.audio_ready) {
      s->audio.audio_rate = f.rate;
      s->audio.audio_channels = f.ch;
      s->audio.audio_bsid = f.bsid;
      s->audio.audio_bsmod = f.bsmod;
      s->audio.audio_acmod = f.acmod;
      s->audio.audio_lfeon = f.lfeon;
      if (s->demux.audio_codec == CODEC_EAC3) {
        unsigned samples = f.samples ? f.samples : 1;
        s->audio.audio_bitrate_code = (unsigned)((uint64_t)f.consumed * 8 * f.rate / samples / 1000);
      } else {
        s->audio.audio_bitrate_code = f.bitrate_code;
      }
      s->audio.audio_ready = 1;
      try_create_fmux(s);
    }
    if (f.samples) s->audio.audio_nominal_samples += (int64_t)f.samples;
    if (f.outlen) fmp4_feed_audio_au(s, &f);
    pos += f.consumed;
  }
  if (pos) {
    memmove(s->audio.audio_rem, s->audio.audio_rem + pos, s->audio.audio_remlen - pos);
    s->audio.audio_remlen -= pos;
  }
}
