/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_FRAMER_AAC_LATM_H
#define DIPIRADIOHEAD_FRAMER_AAC_LATM_H

#include <stddef.h>

typedef struct aac_latm aac_latm_t;

typedef struct {
  unsigned sample_rate;
  unsigned samples_per_frame; /* 1024 */
  size_t frame_len;           /* whole LOAS frame, header included */
} aac_latm_info_t;

aac_latm_t *aac_latm_new(void);
void aac_latm_free(aac_latm_t *c);

/* LOAS sync 0x2B7 (0x56, top 3 bits of next byte set) at p[0..1] */
int aac_latm_is_sync(const unsigned char *p, size_t avail);

/* 1 + fills info, 0 if avail insufficient (need more), -1 if malformed */
int aac_latm_probe(aac_latm_t *c, const unsigned char *p, size_t avail, aac_latm_info_t *info);

#endif
