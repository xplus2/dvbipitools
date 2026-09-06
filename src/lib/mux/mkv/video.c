/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>

#include "lib/helper/ioutil.h"

#include "priv.h"

void codec_id_for(codec_t codec, const esc_frame_t *f, char *out, size_t out_len) {
  /* might be incomplete, reflects what's available for testing ...
     "some ipi-providers are always trying to ice skate uphill" */
  switch (codec) {
    case CODEC_AC3:           bufcpy(out, out_len, "A_AC3"); return;
    case CODEC_EAC3:          bufcpy(out, out_len, "A_EAC3"); return;
    case CODEC_AAC:
    case CODEC_AAC_LATM:      bufcpy(out, out_len, "A_AAC"); return;
    case CODEC_H264:          bufcpy(out, out_len, "V_MPEG4/ISO/AVC"); return;
    case CODEC_HEVC:          bufcpy(out, out_len, "V_MPEGH/ISO/HEVC"); return;
    case CODEC_MPEG2V:        bufcpy(out, out_len, "V_MPEG2"); return;
    case CODEC_MP2A:          snprintf(out, out_len, "A_MPEG/L%d", f ? f->layer : 2); return;
    default:                  bufcpy(out, out_len, "S_UNKNOWN"); return;
  }
}
