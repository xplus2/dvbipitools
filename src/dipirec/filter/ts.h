/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_FILTER_TS_H
#define DIPIREC_FILTER_TS_H

#include "lib/demux/psi.h"

typedef struct ts_filter ts_filter_t;

/* --strip bits.
   NUL/NIT/EIT/CAT/RST  direct pid
   AIT/ECM              psi_classify()
   EMM                  psi_emm_pid()
   TDT/TOT              either bit drops all of pid 0x14
   INT                  table_id 0x4C, no fixed pid. */
#define STRIP_NUL 0x001u
#define STRIP_NIT 0x002u
#define STRIP_AIT 0x004u
#define STRIP_EIT 0x008u
#define STRIP_CAT 0x010u
#define STRIP_ECM 0x020u
#define STRIP_EMM 0x040u
#define STRIP_RST 0x080u
#define STRIP_TDT 0x100u
#define STRIP_TOT 0x200u
#define STRIP_INT 0x400u
#define STRIP_DEFAULT (STRIP_NUL | STRIP_NIT | STRIP_AIT | STRIP_EIT)

/* preferred_pmt_pid: 0 = auto (first candidate), else pin via psi_select_pmt_pid() */
ts_filter_t *ts_filter_new(int audio_all, unsigned audio_track, int strip_subs, unsigned preferred_pmt_pid, unsigned strip_mask);
void ts_filter_free(ts_filter_t *f);

/* filter one pkg: 1 = keep (out filled), 0 = strip */
int ts_filter_packet(ts_filter_t *f, const unsigned char *in, unsigned char *out);

/* stream model */
const psi_t *ts_filter_psi(const ts_filter_t *f);

/* 1 once -a track known missing */
int ts_filter_bad_track(const ts_filter_t *f);

#endif
