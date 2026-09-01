/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_TS_PUSH_INT_H
#define DIPIXY_TS_PUSH_INT_H

#include "ts_push.h"

/* ts_push.c */
extern int g_tid_head[TS_PUSH_MAX_REACTOR_THREADS];
void ts_push_wake_reactor(int tid);
void ts_push_ring_enqueue(ts_sub_t *s, const uint8_t *data, size_t len);

/* ts_push_feed.c */
void ts_push_rawaudio_emit(void *vctx, const unsigned char *data, size_t len);
void ts_push_drop_sub(const ts_sub_t *s, int idx);

#endif
