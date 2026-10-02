/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_INPUT_FRAMEQUEUE_H
#define DIPIRADIOHEAD_INPUT_FRAMEQUEUE_H

#include <stddef.h>

#include "source.h"

typedef struct framequeue framequeue_t;

/* NULL on OOM */
framequeue_t *framequeue_new(void);
void framequeue_free(framequeue_t *q);

/* copies f and f->data. -1 on OOM */
int framequeue_push(framequeue_t *q, const source_frame_t *f);

/* oldest frame. 1 + fills *out, 0 empty. out->data valid until next pop or free */
int framequeue_pop(framequeue_t *q, source_frame_t *out);

size_t framequeue_count(const framequeue_t *q);

/* audio duration queued */
unsigned framequeue_ms(const framequeue_t *q);

#endif
