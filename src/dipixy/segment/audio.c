/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"
#include <string.h>

#define HLS_AUDIO_REM_MAX 65536
#define AAC_SBR_CORE_MAX 24000 /* dual-rate SBR up to 48 kHz output */

/* buffers survive */
void audio_reset(hls_seg_ctx_t *s) {
  unsigned char *rem = s->audio.rem;
  size_t remcap = s->audio.remcap;
  memset(&s->audio, 0, sizeof s->audio);
  s->audio.rem = rem;
  s->audio.remcap = remcap;
}

static int sfi_of_rate(unsigned rate) {
  for (int i = 0; i < 13; i++)
    if (esc_aac_rates[i] == rate) return i;
  return -1;
}

static void aac_settle(hls_seg_ctx_t *s, aacprobe_result_t result) {
  const asc_info_t *a = &s->audio.aac_asc;
  int ext_sfi;
  s->audio.aac_settled = 1;
  s->audio.aac_probing = 0;
  if (result != AACPROBE_SBR && result != AACPROBE_SBR_PS) return;
  ext_sfi = a->ext_rate ? sfi_of_rate(a->ext_rate) : sfi_of_rate(a->rate * 2);
  if (ext_sfi < 0) return;
  s->audio.aac_cfg_len = asc_build_explicit(a->sfi, a->channels, (unsigned)ext_sfi, result == AACPROBE_SBR_PS, s->audio.aac_cfg, sizeof s->audio.aac_cfg);
  s->audio.out_rate = esc_aac_rates[ext_sfi];
  s->audio.out_channels = result == AACPROBE_SBR_PS ? 2 : a->channels;
}

/* 1st declared cfg, then probe. 1 while frames needed */
static int aac_signalling_pending(hls_seg_ctx_t *s, const esc_frame_t *f) {
  asc_info_t *a = &s->audio.aac_asc;
  if ((s->demux.audio_codec != CODEC_AAC && s->demux.audio_codec != CODEC_AAC_LATM) || s->audio.aac_settled) return 0;
  if (!s->audio.aac_probing) {
    if (asc_parse(s->audio.es.cpriv, s->audio.es.cpriv_len, a) || a->aot != 2 || a->sfi > 12) {
      s->audio.aac_settled = 1;
      return 0;
    }
    if (a->ps == 1 || a->sbr == 0 || (a->sbr == -1 && a->rate > AAC_SBR_CORE_MAX)) {
      aac_settle(s, a->ps == 1 ? AACPROBE_SBR_PS : AACPROBE_LC);
      return 0;
    }
    aacprobe_init(&s->audio.aac_probe, a->sfi, a->ext_rate ? a->ext_rate : a->rate * 2);
    s->audio.aac_probing = 1;
  }
  if (!aacprobe_feed(&s->audio.aac_probe, f->out, f->outlen)) return 1;
  aac_settle(s, s->audio.aac_probe.result == AACPROBE_UNDECIDED && a->sbr == 1 ? AACPROBE_SBR : s->audio.aac_probe.result);
  return 0;
}

void handle_audio_pes(hls_seg_ctx_t *s, int has_pts, uint64_t pts, const unsigned char *data, size_t len) {
  size_t pos = 0;

  if (s->container != SEG_CONTAINER_FMP4) return;
  if (has_pts && s->audio.ready) {
    int64_t pts_t = pts_unwrap_ticks(&s->audio.ptswrap, pts);
    if (!s->audio.pts_anchored) {
      s->audio.anchor_pts = pts_t;
      s->audio.anchor_nominal_samples = s->audio.nominal_samples;
      s->audio.pts_anchored = 1;
    } else {
      int64_t expected_samples = (pts_t - s->audio.anchor_pts) * (int64_t)s->audio.rate / HLS_TS_HZ;
      int64_t actual_samples = s->audio.nominal_samples - s->audio.anchor_nominal_samples;
      int64_t drift = expected_samples - actual_samples;
      int64_t mag = drift < 0 ? -drift : drift;
      s->audio.pending_drift_samples = mag * HLS_TS_HZ / (int64_t)s->audio.rate > HLS_JUMP_MAX_TICKS ? 0 : drift;
      s->audio.anchor_pts = pts_t;
      s->audio.anchor_nominal_samples = s->audio.nominal_samples;
    }
  }
  if (s->audio.remlen > HLS_AUDIO_REM_MAX) {
    log_throttled(&s->audio_parse_throttle, LOG_THROTTLE_WINDOW_S, "YW400202", "hls: remlen exceeded HLS_AUDIO_REM_MAX, discarding pending bytes");
    s->audio.remlen = 0;
  }
  if (esc_rem_append(&s->audio.rem, &s->audio.remlen, &s->audio.remcap, data, len)) return;

  while (pos < s->audio.remlen) {
    esc_frame_t f;
    int r = next_frame(&s->audio.es, s->audio.rem + pos, s->audio.remlen - pos, &f);
    if (r > 0) break;
    if (r < 0) {
      log_throttled(&s->audio_parse_throttle, LOG_THROTTLE_WINDOW_S, "YW400203", "hls: audio ES parse failed, resyncing byte by byte");
      pos++;
      continue;
    }
    if (!s->audio.ready) {
      if (aac_signalling_pending(s, &f)) {
        pos += f.consumed;
        continue;
      }
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
      s->audio.chcfg_scheme = f.chcfg_scheme;
      s->audio.chcfg_value = f.chcfg_value;
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
