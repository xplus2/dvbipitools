/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_FRAMER_AAC_ADTS_H
#define DIPIRADIOHEAD_FRAMER_AAC_ADTS_H

#include <stddef.h>

typedef struct {
  unsigned sample_rate;
  unsigned samples_per_frame; /* 1024 */
  size_t frame_len;           /* header (7 or 9 B) included, whole frame */
} aac_adts_info_t;

/* sync 0xFFF + layer bits 00 at p[0..1] */
int aac_adts_is_sync(const unsigned char *p, size_t avail);

/* 1 + fills info, 0 if avail < 7 (need more), -1 if header malformed */
int aac_adts_probe(const unsigned char *p, size_t avail, aac_adts_info_t *info);

#endif
