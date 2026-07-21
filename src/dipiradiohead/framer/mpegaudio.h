/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_FRAMER_MPEGAUDIO_H
#define DIPIRADIOHEAD_FRAMER_MPEGAUDIO_H

#include <stddef.h>

typedef struct {
  unsigned sample_rate;
  unsigned samples_per_frame;
  size_t frame_len; /* header included, whole frame */
} mpegaudio_info_t;

/* header sync (0xFF + top 3 bits set, layer bits != 0) at p[0..1] */
int mpegaudio_is_sync(const unsigned char *p, size_t avail);

/* 1 + fills info, 0 if avail < 4 (need more bytes), -1 if header is malformed */
int mpegaudio_probe(const unsigned char *p, size_t avail, mpegaudio_info_t *info);

#endif
