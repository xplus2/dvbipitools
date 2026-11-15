/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_REACTOR_WAKE_H
#define DIPIXY_REACTOR_WAKE_H

#include "conn.h"

#include <stdint.h>

typedef enum { WAKE_TSPUSH, WAKE_DASHCHUNK, WAKE_MP4PUSH, WAKE_WAITERS } wake_kind_t;

/* -1 efd: unregister */
void reactor_wake_register(int tid, int efd);
/* after all pump + reactor threads joined */
void reactor_wake_close_all(void);

/* any thread. eventfd written once per pending-mask fill */
void reactor_wake(int tid, wake_kind_t kind);
/* per-thread: wakes between begin + end coalesce into an eventfd write per tid */
void reactor_wake_batch_begin(void);
void reactor_wake_batch_end(void);

/* tid's own thread, on eventfd readable: bitmask of (1u << wake_kind_t) */
unsigned reactor_wake_take(int tid);

void reactor_wait_note(int64_t deadline_ms);
/* 0 on first park: recheck after publishing waiting flag */
int reactor_wait_timeout_ms(int tid, int cap_ms);
void reactor_wake_waiters(void);

#endif
