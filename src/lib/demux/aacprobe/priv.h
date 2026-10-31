/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_AACPROBE_PRIV_H
#define DVBIPITOOLS_LIB_DEMUX_AACPROBE_PRIV_H

#include <stdint.h>

#include "aacprobe.h"
#include "lib/demux/bitreader.h"

#define AACPROBE_MAX_SFB 51
#define AACPROBE_MAX_GROUPS 8
#define AACPROBE_SBR_BOOKS 10

#define AACPROBE_ID_SCE 0
#define AACPROBE_ID_CPE 1
#define AACPROBE_ID_CCE 2
#define AACPROBE_ID_LFE 3
#define AACPROBE_ID_DSE 4
#define AACPROBE_ID_PCE 5
#define AACPROBE_ID_FIL 6
#define AACPROBE_ID_END 7

#define AACPROBE_EIGHT_SHORT 2
#define AACPROBE_NOISE_HCB 13
#define AACPROBE_INTENSITY_HCB2 14
#define AACPROBE_INTENSITY_HCB 15

typedef enum {
  SBR_BOOK_T_ENV_15, SBR_BOOK_F_ENV_15, SBR_BOOK_T_BAL_15, SBR_BOOK_F_BAL_15, SBR_BOOK_T_ENV_30,
  SBR_BOOK_F_ENV_30, SBR_BOOK_T_BAL_30, SBR_BOOK_F_BAL_30, SBR_BOOK_T_NOISE, SBR_BOOK_T_NOISE_BAL
} aacprobe_sbr_book_t;

typedef struct {
  uint32_t code;
  uint16_t sym;
  uint8_t len;
} aacprobe_vlc_t;

typedef struct {
  const aacprobe_vlc_t *vlc;
  unsigned n;
} aacprobe_book_t;

typedef struct {
  const uint16_t *long_off;
  unsigned long_n;
  const uint16_t *short_off;
  unsigned short_n;
} aacprobe_swb_t;

typedef struct {
  uint8_t start;
  uint8_t end;
  uint8_t cb;
} aacprobe_sect_t;

/* one ics_info() plus its section_data() */
typedef struct {
  unsigned window_seq;
  unsigned max_sfb;
  unsigned n_groups;
  unsigned group_len[AACPROBE_MAX_GROUPS];
  unsigned n_sect[AACPROBE_MAX_GROUPS];
  aacprobe_sect_t sect[AACPROBE_MAX_GROUPS][AACPROBE_MAX_SFB];
} aacprobe_ics_t;

extern const aacprobe_book_t aacprobe_book_sf;
extern const aacprobe_book_t aacprobe_book_spectral[11];
extern const aacprobe_swb_t aacprobe_swb[13];
extern const aacprobe_book_t aacprobe_book_sbr[AACPROBE_SBR_BOOKS];

/* symbol, -1 on bad code or short data */
int aacprobe_huff(br_t *b, const aacprobe_book_t *book);

/* 0 ok, -1 on bad or unsupported data, also for all below */
int aacprobe_frame(aacprobe_t *p, br_t *b);
int aacprobe_ics_info(br_t *b, aacprobe_ics_t *ics, unsigned sfi);
int aacprobe_ics(br_t *b, aacprobe_ics_t *ics, unsigned sfi, int common_window);
int aacprobe_spectral(br_t *b, const aacprobe_ics_t *ics, unsigned sfi);
int aacprobe_fill(br_t *b, aacprobe_t *p, int prev_id);
int aacprobe_sbr_header(br_t *b, aacprobe_sbr_t *sbr, unsigned sbr_rate);
int aacprobe_sbr_bands(aacprobe_sbr_t *sbr, unsigned sbr_rate, unsigned start, unsigned stop, unsigned xover, unsigned scale, unsigned alter, unsigned noise);
/* mono only. *ps set on EXTENSION_ID_PS */
int aacprobe_sbr_data(br_t *b, const aacprobe_sbr_t *sbr, int *ps);

#endif
