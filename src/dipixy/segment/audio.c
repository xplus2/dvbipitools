/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"
#include <string.h>

#define HLS_AUDIO_REM_MAX 65536

void handle_audio_pes(hls_seg_ctx_t *s, int has_pts, uint64_t pts, const unsigned char *data, size_t len) {
  size_t pos = 0;

  if (s->container != SEG_CONTAINER_FMP4) return;
  if (has_pts && s->audio.ready) {
    int64_t pts_ms = pts_unwrap(&s->audio.ptswrap, pts);
    if (!s->audio.pts_anchored) {
      s->audio.anchor_pts_ms = pts_ms;
      s->audio.anchor_nominal_samples = s->audio.nominal_samples;
      s->audio.pts_anchored = 1;
    } else {
      int64_t expected_samples = (pts_ms - s->audio.anchor_pts_ms) * (int64_t)s->audio.rate / 1000;
      int64_t actual_samples = s->audio.nominal_samples - s->audio.anchor_nominal_samples;
      s->audio.pending_drift_samples = expected_samples - actual_samples;
      s->audio.anchor_pts_ms = pts_ms;
      s->audio.anchor_nominal_samples = s->audio.nominal_samples;
    }
  }
  if (s->audio.remlen > HLS_AUDIO_REM_MAX) {
    log_throttled(&s->audio_parse_throttle, LOG_THROTTLE_WINDOW_S, "hls: remlen exceeded HLS_AUDIO_REM_MAX, discarding pending bytes");
    s->audio.remlen = 0;
  }
  if (esc_rem_append(&s->audio.rem, &s->audio.remlen, &s->audio.remcap, data, len)) return;

  while (pos < s->audio.remlen) {
    esc_frame_t f;
    int r = next_frame(&s->audio.es, s->audio.rem + pos, s->audio.remlen - pos, &f);
    if (r > 0) break;
    if (r < 0) {
      log_throttled(&s->audio_parse_throttle, LOG_THROTTLE_WINDOW_S, "hls: audio ES parse failed, resyncing byte by byte");
      pos++;
      continue;
    }
    if (!s->audio.ready) {
      s->audio.rate = f.rate;
      s->audio.channels = f.ch;
      s->audio.bsid = f.bsid;
      s->audio.bsmod = f.bsmod;
      s->audio.acmod = f.acmod;
      s->audio.lfeon = f.lfeon;
      s->audio.truehd_format_info = f.truehd_format_info;
      s->audio.truehd_peak_data_rate = f.truehd_peak_data_rate;
      s->audio.dts_has_core = f.dts_has_core;
      s->audio.ac4_bitstream_version = f.ac4_bitstream_version;
      s->audio.ac4_presentation_version = f.ac4_presentation_version;
      s->audio.ac4_mdcompat = f.ac4_mdcompat;
      if (s->demux.audio_codec == CODEC_EAC3) {
        unsigned samples = f.samples ? f.samples : 1;
        s->audio.bitrate_code = (unsigned)((uint64_t)f.consumed * 8 * f.rate / samples / 1000);
      } else {
        s->audio.bitrate_code = f.bitrate_code;
      }
      s->audio.ready = 1;
      try_create_fmux(s);
    }
    if (f.samples) s->audio.nominal_samples += (int64_t)f.samples;
    if (s->demux.audio_codec == CODEC_AC4) {
      s->audio.ac4_last_iframe = f.ac4_iframe;
      s->audio.ac4_frame_count++;
    }
    if (f.outlen) fmp4_feed_audio_au(s, &f);
    pos += f.consumed;
  }
  if (pos) {
    memmove(s->audio.rem, s->audio.rem + pos, s->audio.remlen - pos);
    s->audio.remlen -= pos;
  }
}
