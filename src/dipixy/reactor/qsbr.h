/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_QSBR_H
#define DIPIXY_QSBR_H

#include <stdint.h>

void qsbr_init(int nworkers);
void qsbr_worker_quiescent(int tid);
int qsbr_worker_count(void);
void qsbr_mark(uint64_t *out);
int qsbr_mark_passed(const uint64_t *mark);

#endif
