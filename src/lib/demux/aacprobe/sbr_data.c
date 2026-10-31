/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#define MAX_ENV 5
#define FIXFIX 0
#define FIXVAR 1
#define VARFIX 2
#define EXTENSION_ID_PS 2

typedef struct {
  unsigned num_env;
  unsigned num_noise;
  unsigned amp_res;
  unsigned freq_res[MAX_ENV];
  unsigned df_env[MAX_ENV];
  unsigned df_noise[2];
} sbr_ch_t;

static unsigned ptr_bits(unsigned num_env) {
  unsigned bits = 0;
  while ((1u << bits) < num_env + 1) bits++;
  return bits;
}

static int read_freq_res(br_t *b, sbr_ch_t *c) {
  if (c->num_env > MAX_ENV) return -1;
  for (unsigned e = 0; e < c->num_env; e++) c->freq_res[e] = br_u(b, 1);
  return 0;
}

static int sbr_grid(br_t *b, sbr_ch_t *c) {
  unsigned cls = br_u(b, 2);
  if (cls == FIXFIX) {
    c->num_env = 1u << br_u(b, 2);
    if (c->num_env == 1) c->amp_res = 0;
    if (c->num_env > MAX_ENV) return -1;
    c->freq_res[0] = br_u(b, 1);
    for (unsigned e = 1; e < c->num_env; e++) c->freq_res[e] = c->freq_res[0];
  } else if (cls == FIXVAR || cls == VARFIX) {
    br_u(b, 2);
    c->num_env = br_u(b, 2) + 1;
    br_skip(b, (size_t)(c->num_env - 1) * 2);
    br_u(b, (int)ptr_bits(c->num_env));
    if (read_freq_res(b, c)) return -1;
  } else {
    unsigned rel0;
    unsigned rel1;
    br_u(b, 4);
    rel0 = br_u(b, 2);
    rel1 = br_u(b, 2);
    c->num_env = rel0 + rel1 + 1;
    br_skip(b, (size_t)(rel0 + rel1) * 2);
    br_u(b, (int)ptr_bits(c->num_env));
    if (read_freq_res(b, c)) return -1;
  }
  c->num_noise = c->num_env > 1 ? 2 : 1;
  return b->err ? -1 : 0;
}

/* first code of df-coded data is literal */
static int coded_values(br_t *b, unsigned df, unsigned start_bits, unsigned n, aacprobe_sbr_book_t t_book, aacprobe_sbr_book_t f_book) {
  const aacprobe_book_t *book = &aacprobe_book_sbr[df ? t_book : f_book];
  unsigned i = 0;
  if (!df) {
    br_u(b, (int)start_bits);
    i = 1;
  }
  for (; i < n; i++)
    if (aacprobe_huff(b, book) < 0) return -1;
  return b->err ? -1 : 0;
}

static int sbr_envelope(br_t *b, const sbr_ch_t *c, const aacprobe_sbr_t *sbr) {
  aacprobe_sbr_book_t t_book = c->amp_res ? SBR_BOOK_T_ENV_30 : SBR_BOOK_T_ENV_15;
  aacprobe_sbr_book_t f_book = c->amp_res ? SBR_BOOK_F_ENV_30 : SBR_BOOK_F_ENV_15;
  for (unsigned e = 0; e < c->num_env; e++) {
    unsigned bands = c->freq_res[e] ? sbr->n_high : sbr->n_low;
    if (coded_values(b, c->df_env[e], c->amp_res ? 6 : 7, bands, t_book, f_book)) return -1;
  }
  return 0;
}

static int sbr_noise(br_t *b, const sbr_ch_t *c, const aacprobe_sbr_t *sbr) {
  for (unsigned n = 0; n < c->num_noise; n++)
    if (coded_values(b, c->df_noise[n], 5, sbr->n_q, SBR_BOOK_T_NOISE, SBR_BOOK_F_ENV_30)) return -1;
  return 0;
}

int aacprobe_sbr_data(br_t *b, const aacprobe_sbr_t *sbr, int *ps) {
  sbr_ch_t c = {0};
  c.amp_res = sbr->amp_res;
  if (br_u(b, 1)) br_u(b, 4);
  if (sbr_grid(b, &c)) return -1;
  for (unsigned e = 0; e < c.num_env; e++) c.df_env[e] = br_u(b, 1);
  for (unsigned n = 0; n < c.num_noise; n++) c.df_noise[n] = br_u(b, 1);
  br_skip(b, (size_t)sbr->n_q * 2);
  if (sbr_envelope(b, &c, sbr) || sbr_noise(b, &c, sbr)) return -1;
  if (br_u(b, 1)) br_skip(b, sbr->n_high);
  if (br_u(b, 1)) {
    unsigned cnt = br_u(b, 4);
    if (cnt == 15) cnt += br_u(b, 8);
    if (cnt * 8 > 7 && br_u(b, 2) == EXTENSION_ID_PS) *ps = 1;
  }
  return b->err ? -1 : 0;
}
