/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

int aacprobe_ics_info(br_t *b, aacprobe_ics_t *ics, unsigned sfi) {
  const aacprobe_swb_t *swb = &aacprobe_swb[sfi];
  br_u(b, 1);
  ics->window_seq = br_u(b, 2);
  br_u(b, 1);
  if (ics->window_seq == AACPROBE_EIGHT_SHORT) {
    unsigned grouping;
    ics->max_sfb = br_u(b, 4);
    grouping = br_u(b, 7);
    ics->n_groups = 1;
    ics->group_len[0] = 1;
    for (int i = 6; i >= 0; i--) {
      if ((grouping >> i) & 1) ics->group_len[ics->n_groups - 1]++;
      else ics->group_len[ics->n_groups++] = 1;
    }
    if (ics->max_sfb > swb->short_n) return -1;
  } else {
    ics->max_sfb = br_u(b, 6);
    ics->n_groups = 1;
    ics->group_len[0] = 1;
    if (br_u(b, 1)) return -1; /* predictor_data_present: not in AAC-LC */
    if (ics->max_sfb > swb->long_n) return -1;
  }
  return b->err ? -1 : 0;
}

static int section_data(br_t *b, aacprobe_ics_t *ics) {
  unsigned short_win = ics->window_seq == AACPROBE_EIGHT_SHORT;
  unsigned esc = short_win ? 7 : 31;
  for (unsigned g = 0; g < ics->n_groups; g++) {
    unsigned k = 0;
    unsigned i = 0;
    while (k < ics->max_sfb) {
      unsigned cb = br_u(b, 4);
      unsigned sect_len = 0;
      unsigned incr;
      do {
        incr = br_u(b, short_win ? 3 : 5);
        sect_len += incr;
      } while (incr == esc && !b->err);
      if (b->err || cb == 12 || !sect_len || k + sect_len > ics->max_sfb || i >= AACPROBE_MAX_SFB) return -1;
      ics->sect[g][i].start = (uint8_t)k;
      ics->sect[g][i].end = (uint8_t)(k + sect_len);
      ics->sect[g][i].cb = (uint8_t)cb;
      k += sect_len;
      i++;
    }
    ics->n_sect[g] = i;
  }
  return 0;
}

static int scale_factor_data(br_t *b, const aacprobe_ics_t *ics) {
  int noise_pcm = 1;
  for (unsigned g = 0; g < ics->n_groups; g++) {
    for (unsigned s = 0; s < ics->n_sect[g]; s++) {
      const aacprobe_sect_t *sect = &ics->sect[g][s];
      if (!sect->cb) continue;
      for (unsigned sfb = sect->start; sfb < sect->end; sfb++) {
        if (sect->cb == AACPROBE_NOISE_HCB && noise_pcm) {
          noise_pcm = 0;
          br_u(b, 9);
        } else if (aacprobe_huff(b, &aacprobe_book_sf) < 0) {
          return -1;
        }
      }
    }
  }
  return 0;
}

static void pulse_data(br_t *b) {
  unsigned n = br_u(b, 2);
  br_u(b, 6);
  for (unsigned i = 0; i <= n; i++) br_u(b, 9);
}

static void tns_data(br_t *b, const aacprobe_ics_t *ics) {
  unsigned short_win = ics->window_seq == AACPROBE_EIGHT_SHORT;
  unsigned windows = short_win ? 8 : 1;
  for (unsigned w = 0; w < windows && !b->err; w++) {
    unsigned n_filt = br_u(b, short_win ? 1 : 2);
    unsigned coef_res = n_filt ? br_u(b, 1) : 0;
    for (unsigned f = 0; f < n_filt; f++) {
      unsigned order;
      br_u(b, short_win ? 4 : 6);
      order = br_u(b, short_win ? 3 : 5);
      if (order) {
        unsigned coef_bits;
        br_u(b, 1);
        coef_bits = coef_res + 3 - br_u(b, 1);
        br_skip(b, (size_t)order * coef_bits);
      }
    }
  }
}

int aacprobe_ics(br_t *b, aacprobe_ics_t *ics, unsigned sfi, int common_window) {
  br_u(b, 8);
  if (!common_window && aacprobe_ics_info(b, ics, sfi)) return -1;
  if (section_data(b, ics) || scale_factor_data(b, ics)) return -1;
  if (br_u(b, 1)) {
    if (ics->window_seq == AACPROBE_EIGHT_SHORT) return -1;
    pulse_data(b);
  }
  if (br_u(b, 1)) tns_data(b, ics);
  if (br_u(b, 1)) return -1; /* gain_control_data: SSR only */
  if (b->err) return -1;
  return aacprobe_spectral(b, ics, sfi);
}
