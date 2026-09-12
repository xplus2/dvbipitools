/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_QSBR_H
#define DIPIXY_QSBR_H

#include <stdint.h>

#define QSBR_MAX_WORKERS 256

typedef struct qsbr_domain qsbr_domain_t;
qsbr_domain_t *qsbr_domain_create(int nworkers);
void qsbr_worker_quiescent(qsbr_domain_t *d, int tid);
int qsbr_worker_count(const qsbr_domain_t *d);
void qsbr_mark(const qsbr_domain_t *d, uint64_t *out);
int qsbr_mark_passed(const qsbr_domain_t *d, const uint64_t *mark);
int qsbr_mark_passed_excl(const qsbr_domain_t *d, const uint64_t *mark, int excl);
void qsbr_backoff(int spins);

#endif
