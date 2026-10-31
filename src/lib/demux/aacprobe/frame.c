/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include <string.h>

#define NO_SBR_FRAMES 8
#define MONO_SBR_FRAMES 4
#define MAX_FRAMES 32

int aacprobe_huff(br_t *b, const aacprobe_book_t *book) {
  uint32_t code = 0;
  unsigned have = 0;
  for (unsigned i = 0; i < book->n; i++) {
    const aacprobe_vlc_t *v = &book->vlc[i];
    while (have < v->len) {
      code = (code << 1) | br_u(b, 1);
      have++;
    }
    if (b->err) return -1;
    if (code == v->code) return v->sym;
  }
  return -1;
}

static void byte_align(br_t *b) {
  b->bit = (b->bit + 7) & ~(size_t)7;
}

/* ID_SCE, ID_LFE */
static int single(br_t *b, const aacprobe_t *p, aacprobe_ics_t *ics) {
  br_u(b, 4);
  return aacprobe_ics(b, ics, p->sfi, 0);
}

static int pair(br_t *b, const aacprobe_t *p, aacprobe_ics_t *ics) {
  unsigned common;
  br_u(b, 4);
  common = br_u(b, 1);
  if (common) {
    unsigned ms;
    if (aacprobe_ics_info(b, ics, p->sfi)) return -1;
    ms = br_u(b, 2);
    if (ms == 1) br_skip(b, (size_t)ics->n_groups * ics->max_sfb);
  }
  if (aacprobe_ics(b, ics, p->sfi, (int)common)) return -1;
  return aacprobe_ics(b, ics, p->sfi, (int)common);
}

static int coupling(br_t *b, const aacprobe_t *p, aacprobe_ics_t *ics) {
  unsigned ind_sw;
  unsigned num_coupled;
  unsigned lists = 0;
  br_u(b, 4);
  ind_sw = br_u(b, 1);
  num_coupled = br_u(b, 3);
  for (unsigned c = 0; c <= num_coupled; c++) {
    lists++;
    if (br_u(b, 1)) {
      unsigned tag_and_lr;
      br_u(b, 4);
      tag_and_lr = br_u(b, 2);
      if (tag_and_lr == 3) lists++;
    } else {
      br_u(b, 4);
    }
  }
  br_u(b, 4); /* cc_domain, gain_element_sign, gain_element_scale */
  if (aacprobe_ics(b, ics, p->sfi, 0)) return -1;
  for (unsigned c = 1; c < lists; c++) {
    unsigned cge = ind_sw ? 1 : br_u(b, 1);
    if (cge) {
      if (aacprobe_huff(b, &aacprobe_book_sf) < 0) return -1;
      continue;
    }
    for (unsigned g = 0; g < ics->n_groups; g++) {
      for (unsigned s = 0; s < ics->n_sect[g]; s++) {
        const aacprobe_sect_t *sect = &ics->sect[g][s];
        if (!sect->cb) continue;
        for (unsigned sfb = sect->start; sfb < sect->end; sfb++)
          if (aacprobe_huff(b, &aacprobe_book_sf) < 0) return -1;
      }
    }
  }
  return b->err ? -1 : 0;
}

static void data_stream(br_t *b) {
  unsigned align;
  unsigned cnt;
  br_u(b, 4);
  align = br_u(b, 1);
  cnt = br_u(b, 8);
  if (cnt == 255) cnt += br_u(b, 8);
  if (align) byte_align(b);
  br_skip(b, (size_t)cnt * 8);
}

static void program_config(br_t *b) {
  unsigned front, side, back, lfe, assoc, cc;
  br_u(b, 10);
  front = br_u(b, 4);
  side = br_u(b, 4);
  back = br_u(b, 4);
  lfe = br_u(b, 2);
  assoc = br_u(b, 3);
  cc = br_u(b, 4);
  if (br_u(b, 1)) br_u(b, 4);
  if (br_u(b, 1)) br_u(b, 4);
  if (br_u(b, 1)) br_u(b, 3);
  br_skip(b, ((size_t)front + side + back) * 5 + ((size_t)lfe + assoc) * 4 + (size_t)cc * 5);
  byte_align(b);
  br_skip(b, (size_t)br_u(b, 8) * 8);
}

int aacprobe_frame(aacprobe_t *p, br_t *b) {
  aacprobe_ics_t ics;
  int prev = AACPROBE_ID_END;
  int has_channel = 0;
  for (;;) {
    unsigned id = br_u(b, 3);
    int rc = 0;
    if (b->err) return -1;
    switch (id) {
      case AACPROBE_ID_SCE:
      case AACPROBE_ID_LFE: rc = single(b, p, &ics); break;
      case AACPROBE_ID_CPE: rc = pair(b, p, &ics); break;
      case AACPROBE_ID_CCE: rc = coupling(b, p, &ics); break;
      case AACPROBE_ID_DSE: data_stream(b); break;
      case AACPROBE_ID_PCE: program_config(b); break;
      case AACPROBE_ID_FIL: rc = aacprobe_fill(b, p, prev); break;
      default: return has_channel ? 0 : -1;
    }
    if (rc || b->err) return -1;
    if (id == AACPROBE_ID_SCE || id == AACPROBE_ID_CPE) prev = (int)id;
    if (id == AACPROBE_ID_SCE || id == AACPROBE_ID_CPE || id == AACPROBE_ID_LFE) has_channel = 1;
  }
}

void aacprobe_init(aacprobe_t *p, unsigned sfi, unsigned sbr_rate) {
  memset(p, 0, sizeof *p);
  p->sfi = sfi;
  p->sbr_rate = sbr_rate;
  if (sfi > 12) p->done = 1;
}

int aacprobe_feed(aacprobe_t *p, const unsigned char *frame, size_t len) {
  br_t b;
  if (p->done) return 1;
  b.d = frame;
  b.len = len;
  b.bit = 0;
  b.err = 0;
  p->frames++;
  if (aacprobe_frame(p, &b) == 0) p->parsed++;
  if (p->ps_seen) {
    p->result = AACPROBE_SBR_PS;
  } else if (p->sbr_seen && !p->sbr_mono) {
    p->result = AACPROBE_SBR; /* PS needs a mono core */
  } else if (p->sbr_parsed >= MONO_SBR_FRAMES) {
    p->result = AACPROBE_SBR;
  } else if (!p->sbr_seen && p->parsed >= NO_SBR_FRAMES) {
    p->result = AACPROBE_LC;
  }
  p->done = p->result != AACPROBE_UNDECIDED || p->frames >= MAX_FRAMES;
  return p->done;
}
