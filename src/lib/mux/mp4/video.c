/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

const char *p4_entry_fourcc_for(codec_t codec) {
  switch (codec) {
    case CODEC_HEVC:  return "hvc1";
    case CODEC_VVC:   return "vvc1";
    case CODEC_AC3:   return "ac-3";
    case CODEC_EAC3:  return "ec-3";
    case CODEC_H264:  return "avc1";
    default:          return "mp4a";
  }
}
