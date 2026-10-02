/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIDESCRAMBLE_PMT_SELECT_H
#define DIPIDESCRAMBLE_PMT_SELECT_H

#include "lib/demux/psi/psi.h"
#include "lib/net/ts/source.h"

#include "cli/args.h"

/* mpts discovery + -p decision. 0: proceed (pmt_pid/all_pids/n_all_pids filled in). 1: abort, message already printed. */
int dscr_resolve_pmt_selection(const config_t *cfg, tssrc_t *src, unsigned *pmt_pid, unsigned *all_pids, int *n_all_pids);

#endif
