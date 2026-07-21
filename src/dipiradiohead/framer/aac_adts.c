/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "aac_adts.h"

int aac_adts_is_sync(const unsigned char *p, size_t avail) {
  if (avail < 2)
    return 0;
  return p[0] == 0xFF && (p[1] & 0xF6) == 0xF0;
}

int aac_adts_probe(const unsigned char *p, size_t avail, aac_adts_info_t *info) {
  static const unsigned rates[13] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000,  7350};
  unsigned sr_idx, frame_length, raw_blocks;

  if (avail < 7)
    return 0;
  if (!aac_adts_is_sync(p, avail))
    return -1;

  sr_idx = (p[2] >> 2) & 0x0F;
  if (sr_idx >= 13)
    return -1;
  raw_blocks = p[6] & 0x03; /* only single-AAC-frame ADTS packets are supported */
  if (raw_blocks != 0)
    return -1;

  frame_length = ((unsigned)(p[3] & 0x03) << 11) | ((unsigned)p[4] << 3) | ((unsigned)(p[5] >> 5) & 0x07);
  if (frame_length < 7)
    return -1;

  info->sample_rate = rates[sr_idx];
  info->samples_per_frame = 1024;
  info->frame_len = frame_length;
  return 1;
}
