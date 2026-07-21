/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "mpegaudio.h"

static const unsigned rate_mpeg1[3] = {44100, 48000, 32000};
static const unsigned rate_mpeg2[3] = {22050, 24000, 16000};
static const unsigned rate_mpeg25[3] = {11025, 12000, 8000};

static const unsigned br_v1l1[16] = {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0};
static const unsigned br_v1l2[16] = {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0};
static const unsigned br_v1l3[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
static const unsigned br_v2l1[16] = {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0};
static const unsigned br_v2l23[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};

int mpegaudio_is_sync(const unsigned char *p, size_t avail) {
  if (avail < 2)
    return 0;
  return p[0] == 0xFF && (p[1] & 0xE0) == 0xE0;
}

int mpegaudio_probe(const unsigned char *p, size_t avail, mpegaudio_info_t *info) {
  unsigned version, layer, br_idx, sr_idx, padding, bitrate, sample_rate, samples;
  const unsigned *sr_table, *br_table;
  size_t frame_len;

  if (avail < 4)
    return 0;
  if (!mpegaudio_is_sync(p, avail))
    return -1;

  version = (p[1] >> 3) & 0x03; /* 0=v2.5 1=rsv 2=v2 3=v1 */
  layer = (p[1] >> 1) & 0x03;   /* 0=rsv 1=L3 2=L2 3=L1 */
  br_idx = (p[2] >> 4) & 0x0F;
  sr_idx = (p[2] >> 2) & 0x03;
  padding = (p[2] >> 1) & 0x01;

  if (version == 1 || layer == 0 || sr_idx == 3 || br_idx == 0 || br_idx == 15)
    return -1;

  if (version == 3)
    sr_table = rate_mpeg1;
  else if (version == 2)
    sr_table = rate_mpeg2;
  else
    sr_table = rate_mpeg25;
  sample_rate = sr_table[sr_idx];

  if (version == 3 && layer == 3)
    br_table = br_v1l1;
  else if (version == 3 && layer == 2)
    br_table = br_v1l2;
  else if (version == 3 && layer == 1)
    br_table = br_v1l3;
  else if (layer == 3)
    br_table = br_v2l1;
  else
    br_table = br_v2l23;
  bitrate = br_table[br_idx] * 1000;
  if (bitrate == 0)
    return -1;

  if (layer == 3) { /* Layer I */
    frame_len = (12 * bitrate / sample_rate + padding) * 4;
    samples = 384;
  } else if (layer == 2) { /* Layer II */
    frame_len = 144 * bitrate / sample_rate + padding;
    samples = 1152;
  } else if (version == 3) { /* Layer III, MPEG1 */
    frame_len = 144 * bitrate / sample_rate + padding;
    samples = 1152;
  } else { /* Layer III, MPEG2/2.5 */
    frame_len = 72 * bitrate / sample_rate + padding;
    samples = 576;
  }

  info->sample_rate = sample_rate;
  info->samples_per_frame = samples;
  info->frame_len = frame_len;
  return 1;
}
