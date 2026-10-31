/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include <string.h>

#define SYNC_EXT_SBR 0x2B7
#define SYNC_EXT_PS 0x548
#define AOT_LC 2
#define AOT_SBR 5
#define AOT_PS 29

static unsigned read_aot(br_t *b) {
  unsigned aot = br_u(b, 5);
  return aot == 31 ? 32 + br_u(b, 6) : aot;
}

static unsigned read_rate(br_t *b, unsigned *sfi) {
  *sfi = br_u(b, 4);
  if (*sfi == 15) return br_u(b, 24);
  return *sfi < 13 ? esc_aac_rates[*sfi] : 0;
}

static void read_sync_extension(br_t *b, asc_info_t *a) {
  size_t end = b->len * 8;
  if (end - b->bit < 16 || br_u(b, 11) != SYNC_EXT_SBR) return;
  if (read_aot(b) != AOT_SBR) return;
  a->sbr = (int)br_u(b, 1);
  if (a->sbr != 1) return;
  a->ext_rate = read_rate(b, &a->ext_sfi);
  if (end - b->bit >= 12 && br_u(b, 11) == SYNC_EXT_PS) a->ps = (int)br_u(b, 1);
}

int asc_parse(const unsigned char *d, size_t len, asc_info_t *a) {
  br_t b;
  b.d = d;
  b.len = len;
  b.bit = 0;
  b.err = 0;
  memset(a, 0, sizeof *a);
  a->sbr = -1;
  a->ps = -1;
  a->aot = read_aot(&b);
  a->rate = read_rate(&b, &a->sfi);
  a->channels = br_u(&b, 4);
  if (a->aot == AOT_SBR || a->aot == AOT_PS) {
    a->sbr = 1;
    if (a->aot == AOT_PS) a->ps = 1;
    a->ext_rate = read_rate(&b, &a->ext_sfi);
    a->aot = read_aot(&b);
    if (a->aot == 22) br_u(&b, 4);
  }
  if (b.err) return -1;
  if (a->aot != AOT_LC || !a->channels || br_u(&b, 1)) return 0; /* no GASpecificConfig tail to walk */
  if (br_u(&b, 1)) br_u(&b, 14);
  if (br_u(&b, 1)) return 0;
  if (!b.err && a->sbr == -1) read_sync_extension(&b, a);
  return 0;
}

static void put_bits(unsigned char *o, size_t *bit, unsigned v, unsigned n) {
  for (unsigned i = n; i--; (*bit)++)
    if ((v >> i) & 1) o[*bit / 8] |= (unsigned char)(0x80 >> (*bit % 8));
}

/* table 1.15 backward compatible form: LC, then SBR and PS sync extensions */
size_t asc_build_explicit(unsigned sfi, unsigned channels, unsigned ext_sfi, int ps, unsigned char *o, size_t cap) {
  size_t bit = 0;
  if (cap < ASC_EXPLICIT_MAX) return 0;
  memset(o, 0, ASC_EXPLICIT_MAX);
  put_bits(o, &bit, AOT_LC, 5);
  put_bits(o, &bit, sfi, 4);
  put_bits(o, &bit, channels, 4);
  put_bits(o, &bit, 0, 3); /* frameLengthFlag, dependsOnCoreCoder, extensionFlag */
  put_bits(o, &bit, SYNC_EXT_SBR, 11);
  put_bits(o, &bit, AOT_SBR, 5);
  put_bits(o, &bit, 1, 1);
  put_bits(o, &bit, ext_sfi, 4);
  put_bits(o, &bit, SYNC_EXT_PS, 11);
  put_bits(o, &bit, ps ? 1 : 0, 1);
  return (bit + 7) / 8;
}
