/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_PESSTAMP_H
#define DIPITVHEAD_PESSTAMP_H

#include <stdint.h>

#define PESSTAMP_NONE 0
#define PESSTAMP_FOUND 1
#define PESSTAMP_SPLIT (-1)
#define PESSTAMP_SCRAMBLED (-2)

typedef struct {
  int has_pts;
  int has_dts;
  int has_escr;
  uint64_t pts;
  uint64_t dts;
  uint64_t escr27;
} pes_stamp_t;

int pesstamp_read(const unsigned char pkt188[188], pes_stamp_t *st);
int pesstamp_shift(unsigned char pkt188[188], int64_t delta90k);

int afstamp_shift(unsigned char pkt188[188], int64_t delta90k);
int afstamp_clear_discontinuity(unsigned char pkt188[188]);

#endif
