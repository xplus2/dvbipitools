/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIDESCRAMBLE_OUTPUTS_H
#define DIPIDESCRAMBLE_OUTPUTS_H

#include "cli/args.h"
#include "pipeline.h"

/* opens every -o target: plain files into lc->outfd[], rtmp(s) targets into lc->rtmp[]. 0 ok, -1 fail */
int dscr_open_outputs(const config_t *cfg, loop_ctx_t *lc, int *mkv_fd);

void dscr_close_outputs(loop_ctx_t *lc, int mkv_fd);

#endif
