/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#define FIRST_PAIR_HCB 5
#define ESC_HCB 11
#define ESC_FLAG 16
#define MAX_ESC_ONES 8

/* one hcod_esc: N ones, zero, N+4 bits */
static int skip_escape(br_t *b) {
  unsigned ones = 0;
  while (br_u(b, 1)) {
    if (++ones > MAX_ESC_ONES || b->err) return -1;
  }
  br_skip(b, ones + 4);
  return 0;
}

/* table 4.151: radix = LAV + 1, signed books need no sign bits */
static const struct {
  unsigned radix;
  int is_signed;
} book_info[11] = {
  {3, 1}, {3, 1}, {3, 0},
  {3, 0}, {9, 1}, {9, 1},
  {8, 0}, {8, 0}, {13, 0},
  {13, 0}, {17, 0}
};

static int skip_quad(br_t *b, unsigned cb) {
  int sym = aacprobe_huff(b, &aacprobe_book_spectral[cb - 1]);
  unsigned v;
  if (sym < 0) return -1;
  if (book_info[cb - 1].is_signed) return 0;
  v = (unsigned)sym;
  for (int i = 0; i < 4; i++) {
    if (v % 3) br_u(b, 1);
    v /= 3;
  }
  return 0;
}

static int skip_pair(br_t *b, unsigned cb) {
  int sym = aacprobe_huff(b, &aacprobe_book_spectral[cb - 1]);
  unsigned radix = book_info[cb - 1].radix;
  unsigned y;
  unsigned z;
  if (sym < 0) return -1;
  if (book_info[cb - 1].is_signed) return 0;
  y = (unsigned)sym / radix;
  z = (unsigned)sym % radix;
  if (y) br_u(b, 1);
  if (z) br_u(b, 1);
  if (cb == ESC_HCB) {
    if (y == ESC_FLAG && skip_escape(b)) return -1;
    if (z == ESC_FLAG && skip_escape(b)) return -1;
  }
  return 0;
}

static unsigned section_coefs(const aacprobe_ics_t *ics, const aacprobe_sect_t *sect, unsigned sfi, unsigned group) {
  const aacprobe_swb_t *swb = &aacprobe_swb[sfi];
  if (ics->window_seq == AACPROBE_EIGHT_SHORT)
    return (unsigned)(swb->short_off[sect->end] - swb->short_off[sect->start]) * ics->group_len[group];
  return (unsigned)(swb->long_off[sect->end] - swb->long_off[sect->start]);
}

int aacprobe_spectral(br_t *b, const aacprobe_ics_t *ics, unsigned sfi) {
  for (unsigned g = 0; g < ics->n_groups; g++) {
    for (unsigned s = 0; s < ics->n_sect[g]; s++) {
      const aacprobe_sect_t *sect = &ics->sect[g][s];
      unsigned coefs;
      if (!sect->cb || sect->cb >= AACPROBE_NOISE_HCB) continue;
      coefs = section_coefs(ics, sect, sfi, g);
      for (unsigned k = 0; k < coefs; k += sect->cb < FIRST_PAIR_HCB ? 4 : 2) {
        int rc = sect->cb < FIRST_PAIR_HCB ? skip_quad(b, sect->cb) : skip_pair(b, sect->cb);
        if (rc || b->err) return -1;
      }
    }
  }
  return 0;
}
