/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>

#include "priv.h"

const char *codec_id_for(codec_t codec, const esc_frame_t *f) {
  static char buf[24];
  /* might be incomplete, reflects what's available for testing ...
     "some ipi-providers are always trying to ice skate uphill" */
  switch (codec) {
    case CODEC_AC3:           return "A_AC3";
    case CODEC_EAC3:          return "A_EAC3";
    case CODEC_AAC:
    case CODEC_AAC_LATM:      return "A_AAC";
    case CODEC_H264:          return "V_MPEG4/ISO/AVC";
    case CODEC_HEVC:          return "V_MPEGH/ISO/HEVC";
    case CODEC_MPEG2V:        return "V_MPEG2";
    case CODEC_MP2A:          snprintf(buf, sizeof buf, "A_MPEG/L%d", f ? f->layer : 2);
                              return buf;
    default:                  return "S_UNKNOWN";
  }
}
