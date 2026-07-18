/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_FILTER_TS_H
#define DIPIREC_FILTER_TS_H

#include "lib/demux/psi.h"

typedef struct ts_filter ts_filter_t;

ts_filter_t *ts_filter_new(int audio_all, unsigned audio_track, int strip_subs);
void ts_filter_free(ts_filter_t *f);

/* filter one pkg: 1 = keep (out filled), 0 = strip */
int ts_filter_packet(ts_filter_t *f, const unsigned char *in, unsigned char *out);

/* stream model */
const psi_t *ts_filter_psi(const ts_filter_t *f);

/* 1 once -a track known missing */
int ts_filter_bad_track(const ts_filter_t *f);

#endif
