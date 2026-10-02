/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_RELEASEQ_H
#define DIPITVHEAD_RELEASEQ_H

#include <stddef.h>
#include <stdint.h>

typedef struct releaseq releaseq_t;

releaseq_t *releaseq_new(size_t max_packets);
void releaseq_free(releaseq_t *q);
size_t releaseq_len(const releaseq_t *q);
int releaseq_push(releaseq_t *q, const unsigned char pkt188[188], int has_tag, uint64_t tag90);
const unsigned char *releaseq_head(const releaseq_t *q, int *has_tag, uint64_t *tag90);
void releaseq_pop(releaseq_t *q);

#endif
