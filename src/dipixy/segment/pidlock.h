/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_DIPIXY_SEGMENT_PIDLOCK_H
#define DVBIPITOOLS_DIPIXY_SEGMENT_PIDLOCK_H

#include "lib/demux/psi/psi.h"
#include "lib/mux/pmt_filter.h"

#include "../ts/lcevcselect.h"
#include "../ts/pidfilter.h"

/* PAT, locked PMT pid, PCR pid, every ES pid, into allowed[] (cap entries max) */
void pidlock_snapshot(const psi_t *psi, unsigned *allowed, int *n_allowed, int cap);

int pidlock_allowed(const unsigned *allowed, int n_allowed, unsigned pid);

/* resolves lcevc pick, adds the rest to filter's excludes */
void pidlock_apply_lcevc(const lcevc_select_t *lcevc, pid_filter_t *filter, const unsigned *pids, int count);

/* PMT narrowed by filter, repacketized on cc_pmt. out: PMT_FILTER_PKT_MAX * 188 bytes, rw: PSI_SECTION_ASM_BUF_LEN.
   returns packets, *n_pkts of them. NULL: continuation packet of a replaced PMT, drop.
   pkt itself (*n_pkts 1): other pid, no filter, no section yet, or rewrite failed */
const unsigned char *pidlock_rewrite_pmt(const psi_t *tp, const pid_filter_t *filter, unsigned char *cc_pmt,
                                         const unsigned char *pkt, unsigned pid, unsigned char *rw, unsigned char *out, size_t *n_pkts);

/* PAT narrowed to the locked program (own tsid, version, cc_pat). NULL: continuation packet of a replaced PAT, drop.
   pkt itself: pid not 0, or no PAT/PMT seen yet */
const unsigned char *pidlock_rewrite_pat(const psi_t *tp, unsigned char *cc_pat, const unsigned char *pkt, unsigned pid, unsigned char *out188);

#endif
