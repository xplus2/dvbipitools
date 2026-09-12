/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_DIPIXY_SEGMENT_PIDLOCK_H
#define DVBIPITOOLS_DIPIXY_SEGMENT_PIDLOCK_H

#include "lib/demux/psi/psi.h"

#include "../ts/lcevcselect.h"
#include "../ts/pidfilter.h"

/* PAT, locked PMT pid, PCR pid, every ES pid, into allowed[] (cap entries max) */
void pidlock_snapshot(const psi_t *psi, unsigned *allowed, int *n_allowed, int cap);

int pidlock_allowed(const unsigned *allowed, int n_allowed, unsigned pid);

/* resolves lcevc pick, adds the rest to filter's excludes */
void pidlock_apply_lcevc(lcevc_select_t *lcevc, pid_filter_t *filter, const unsigned *pids, int count);

/* silent fallback to pkt: no section yet, or rewrite doesn't fit one packet. *cc_pmt advances only on success */
const unsigned char *pidlock_rewrite_pmt(const psi_t *tp, const pid_filter_t *filter, unsigned char *cc_pmt,
                                         const unsigned char *pkt, unsigned pid, unsigned char *rw, unsigned char *out188);

#endif
