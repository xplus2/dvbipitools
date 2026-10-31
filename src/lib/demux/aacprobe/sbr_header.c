/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include <math.h>
#include <stdlib.h>

#define MAX_BANDS 64

static const signed char start_offset[6][16] = {
  {-8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7},
  {-5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 9, 11, 13},
  {-5, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 9, 11, 13, 16},
  {-6, -4, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 9, 11, 13, 16},
  {-4, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 9, 11, 13, 16, 20},
  {-2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 9, 11, 13, 16, 20, 24},
};

static int nint(double x) {
  return (int)floor(x + 0.5);
}

static int cmp_int(const void *a, const void *b) {
  return *(const int *)a - *(const int *)b;
}

static const signed char *offset_row(unsigned fs) {
  switch (fs) {
    case 16000: return start_offset[0];
    case 22050: return start_offset[1];
    case 24000: return start_offset[2];
    case 32000: return start_offset[3];
    default: break;
  }
  if (fs >= 44100 && fs <= 64000) return start_offset[4];
  return fs > 64000 ? start_offset[5] : NULL;
}

static int stop_band(unsigned fs, int k0, unsigned stop) {
  int base = fs < 32000 ? 6000 : (fs < 64000 ? 8000 : 10000);
  int stop_min = nint((double)base * 128.0 / fs);
  int dk[13];
  int k2 = stop_min;
  if (stop == 14) k2 = 2 * k0;
  else if (stop == 15) k2 = 3 * k0;
  else {
    double ratio = 64.0 / stop_min;
    for (int p = 0; p < 13; p++) dk[p] = nint(stop_min * pow(ratio, (p + 1) / 13.0)) - nint(stop_min * pow(ratio, p / 13.0));
    qsort(dk, 13, sizeof dk[0], cmp_int);
    for (unsigned i = 0; i < stop; i++) k2 += dk[i];
  }
  return k2 < 64 ? k2 : 64;
}

/* figure 4.39. number of bands, -1 on bad range */
static int master_linear(int k0, int k2, unsigned alter, int *f) {
  int dk = alter ? 2 : 1;
  int n = alter ? 2 * nint((k2 - k0) / 4.0) : 2 * ((k2 - k0) / 2);
  int vdk[MAX_BANDS];
  int diff;
  int k;
  int incr;
  if (n <= 0 || n > MAX_BANDS) return -1;
  for (k = 0; k < n; k++) vdk[k] = dk;
  diff = k2 - (k0 + n * dk);
  incr = diff < 0 ? 1 : -1;
  k = diff < 0 ? 0 : n - 1;
  while (diff) {
    if (k < 0 || k >= n) return -1;
    vdk[k] -= incr;
    k += incr;
    diff += incr;
  }
  f[0] = k0;
  for (k = 1; k <= n; k++) f[k] = f[k - 1] + vdk[k - 1];
  return n;
}

static void region_dk(int lo, int hi, int n, int *dk) {
  double ratio = (double)hi / lo;
  for (int k = 0; k < n; k++) dk[k] = nint(lo * pow(ratio, (double)(k + 1) / n)) - nint(lo * pow(ratio, (double)k / n));
}

/* figure 4.40. number of bands, -1 on bad range */
static int master_warped(int k0, int k2, unsigned scale, unsigned alter, int *f) {
  static const int band_count[3] = {12, 10, 8};
  int bands = band_count[scale - 1];
  int two_regions = (double)k2 / k0 > 2.2449;
  int k1 = two_regions ? 2 * k0 : k2;
  int n0 = 2 * nint(bands * log((double)k1 / k0) / (2.0 * M_LN2));
  int n1 = 0;
  int d0[MAX_BANDS];
  int d1[MAX_BANDS];
  if (n0 <= 0 || n0 > MAX_BANDS) return -1;
  region_dk(k0, k1, n0, d0);
  qsort(d0, (size_t)n0, sizeof d0[0], cmp_int);
  f[0] = k0;
  for (int k = 1; k <= n0; k++) f[k] = f[k - 1] + d0[k - 1];
  if (!two_regions) return n0;
  n1 = 2 * nint(bands * log((double)k2 / k1) / (2.0 * M_LN2 * (alter ? 1.3 : 1.0)));
  if (n1 <= 0 || n0 + n1 > MAX_BANDS) return -1;
  region_dk(k1, k2, n1, d1);
  qsort(d1, (size_t)n1, sizeof d1[0], cmp_int);
  if (d1[0] < d0[n0 - 1]) {
    int change = d0[n0 - 1] - d1[0];
    int limit = (d1[n1 - 1] - d1[0]) / 2;
    if (change > limit) change = limit;
    d1[0] += change;
    d1[n1 - 1] -= change;
    qsort(d1, (size_t)n1, sizeof d1[0], cmp_int);
  }
  for (int k = 1; k <= n1; k++) f[n0 + k] = f[n0 + k - 1] + d1[k - 1];
  return n0 + n1;
}

int aacprobe_sbr_bands(aacprobe_sbr_t *sbr, unsigned fs, unsigned start, unsigned stop, unsigned xover, unsigned scale, unsigned alter, unsigned noise) {
  const signed char *offset = offset_row(fs);
  int base = fs < 32000 ? 3000 : (fs < 64000 ? 4000 : 5000);
  int f[MAX_BANDS + 1];
  int k0;
  int k2;
  int n_master;
  int n_high;
  int nq = 1;
  if (!offset || start > 15 || stop > 15) return -1;
  k0 = nint((double)base * 128.0 / fs) + offset[start];
  k2 = stop_band(fs, k0, stop);
  if (k0 <= 0 || k2 <= k0) return -1;
  n_master = scale ? master_warped(k0, k2, scale, alter, f) : master_linear(k0, k2, alter, f);
  if (n_master <= 0 || (int)xover >= n_master) return -1;
  n_high = n_master - (int)xover;
  if (noise) nq = nint(noise * log((double)k2 / f[xover]) / M_LN2);
  if (nq < 1) nq = 1;
  if (nq > 5) return -1;
  sbr->n_high = (unsigned)n_high;
  sbr->n_low = (unsigned)(n_high / 2 + (n_high - 2 * (n_high / 2)));
  sbr->n_q = (unsigned)nq;
  return 0;
}

int aacprobe_sbr_header(br_t *b, aacprobe_sbr_t *sbr, unsigned sbr_rate) {
  unsigned amp = br_u(b, 1);
  unsigned start = br_u(b, 4);
  unsigned stop = br_u(b, 4);
  unsigned xover = br_u(b, 3);
  unsigned extra1;
  unsigned extra2;
  unsigned scale = 2;
  unsigned alter = 1;
  unsigned noise = 2;
  br_u(b, 2);
  extra1 = br_u(b, 1);
  extra2 = br_u(b, 1);
  if (extra1) {
    scale = br_u(b, 2);
    alter = br_u(b, 1);
    noise = br_u(b, 2);
  }
  if (extra2) br_u(b, 6);
  if (b->err || aacprobe_sbr_bands(sbr, sbr_rate, start, stop, xover, scale, alter, noise)) return -1;
  sbr->amp_res = amp;
  sbr->have_header = 1;
  return 0;
}
