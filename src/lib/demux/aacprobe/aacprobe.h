/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_AACPROBE_H
#define DVBIPITOOLS_LIB_DEMUX_AACPROBE_H

#include <stddef.h>

typedef enum { AACPROBE_UNDECIDED, AACPROBE_LC, AACPROBE_SBR, AACPROBE_SBR_PS } aacprobe_result_t;

/* SBR header state, parsed once per stream (no in-flight for now) */
typedef struct {
  int have_header;
  unsigned amp_res;
  unsigned n_low;
  unsigned n_high;
  unsigned n_q;
} aacprobe_sbr_t;

typedef struct {
  unsigned sfi;
  unsigned sbr_rate; /* FsSBR */
  unsigned frames;
  unsigned parsed; /* frames walked to ID_END */
  unsigned sbr_parsed; /* mono frames with full sbr_data() */
  int sbr_seen;
  int sbr_mono; /* last SBR payload follows SCE */
  int ps_seen;
  int done;
  aacprobe_result_t result;
  aacprobe_sbr_t sbr;
} aacprobe_t;

/* 1024 sample AAC-LC core, ADTS or LATM. sfi: core samplingFrequencyIndex, sbr_rate: FsSBR */
void aacprobe_init(aacprobe_t *p, unsigned sfi, unsigned sbr_rate);

/* raw_data_block() bytes. 1 once p->result is final */
int aacprobe_feed(aacprobe_t *p, const unsigned char *frame, size_t len);

#endif
