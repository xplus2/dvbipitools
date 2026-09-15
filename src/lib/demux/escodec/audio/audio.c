/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include <string.h>

int next_frame(esc_track_t *t, const unsigned char *d, size_t len, esc_frame_t *f) {
  memset(f, 0, sizeof *f);
  f->layer = 2;
  switch (t->codec) {
    case CODEC_AC3:         return next_ac3(t, d, len, f);
    case CODEC_EAC3:        return next_eac3(t, d, len, f);
    case CODEC_MP2A:        return next_mpa(t, d, len, f);
    case CODEC_AAC:         return next_aac(t, d, len, f);
    case CODEC_AAC_LATM:    return next_latm(t, d, len, f);
    case CODEC_OPUS:        return next_opus(t, d, len, f);
    case CODEC_DTS:
    case CODEC_DTS_HD:
    case CODEC_DTS_HD_MA:   return next_dts(t, d, len, f);
    case CODEC_TRUEHD:      return next_truehd(t, d, len, f);
    case CODEC_AC4:         return next_ac4(t, d, len, f);
    default:                return -1;
  }
}
