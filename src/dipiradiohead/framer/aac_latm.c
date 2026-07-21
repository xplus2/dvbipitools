/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>

#include "lib/demux/bitreader.h"

#include "aac_latm.h"

struct aac_latm {
  int have_config;
  unsigned sample_rate;
};

aac_latm_t *aac_latm_new(void) { return calloc(1, sizeof(struct aac_latm)); }

void aac_latm_free(aac_latm_t *c) { free(c); }

int aac_latm_is_sync(const unsigned char *p, size_t avail) {
  if (avail < 2)
    return 0;
  return p[0] == 0x56 && (p[1] & 0xE0) == 0xE0;
}

static unsigned get_audio_object_type(br_t *b) {
  unsigned t = br_u(b, 5);
  if (t == 31)
    t = 32 + br_u(b, 6);
  return t;
}

/* no SBR extension parse: implicit SBR streams report the core (half) rate here */
static int audio_specific_config(br_t *b, unsigned *sample_rate) {
  unsigned sr_idx;
  static const unsigned rates[13] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000,  7350};

  get_audio_object_type(b);
  sr_idx = br_u(b, 4);
  if (sr_idx == 0x0F)
    *sample_rate = br_u(b, 24);
  else if (sr_idx < 13)
    *sample_rate = rates[sr_idx];
  else
    return -1;
  br_u(b, 4); /* channelConfiguration, unused */
  return b->err ? -1 : 0;
}

/* StreamMuxConfig, audioMuxVersion 0/1 non-"A" path, single program/layer only */
static int stream_mux_config(br_t *b, unsigned *sample_rate) {
  unsigned version, num_program, num_layer;
  version = br_u(b, 1);
  if (version == 1)
    return -1; /* audioMuxVersionA / LATM v1 extensions not supported */
  br_u(b, 1);   /* allStreamsSameTimeFraming */
  br_u(b, 6);   /* numSubFrames */
  num_program = br_u(b, 4);
  num_layer = br_u(b, 3);
  if (b->err || num_program != 0 || num_layer != 0)
    return -1; /* only a single program/layer is supported */
  return audio_specific_config(b, sample_rate);
}

int aac_latm_probe(aac_latm_t *c, const unsigned char *p, size_t avail, aac_latm_info_t *info) {
  unsigned frame_payload_len;
  size_t total;
  br_t b;

  if (avail < 3)
    return 0;
  if (!aac_latm_is_sync(p, avail))
    return -1;

  frame_payload_len = ((unsigned)(p[1] & 0x1F) << 8) | p[2];
  total = 3 + frame_payload_len;
  if (avail < total)
    return 0;

  b.d = p + 3;
  b.len = frame_payload_len;
  b.bit = 0;
  b.err = 0;
  if (!br_u(&b, 1)) { /* !useSameStreamMux: fresh StreamMuxConfig follows */
    if (stream_mux_config(&b, &c->sample_rate) != 0)
      return -1;
    c->have_config = 1;
  }
  if (!c->have_config)
    return -1;

  info->sample_rate = c->sample_rate;
  info->samples_per_frame = 1024;
  info->frame_len = total;
  return 1;
}
