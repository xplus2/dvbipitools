/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_RETRYSET_H
#define DVBIPITOOLS_LIB_NET_RETRYSET_H

#include <time.h>

#define RETRYSET_MAX_SLOTS 32

/* retry_deadline sentinel: never retry this slot (single-input, no -e given) */
#define RETRYSET_NEVER ((time_t)-1)

typedef enum { RETRYSET_OPEN_PENDING, RETRYSET_OPEN_DONE, RETRYSET_OPEN_ERROR } retryset_open_state_t;

/* generic poll_fd/poll_events/step/take/free shape (matches http_async_t, source_open_async_t,
   tssrc_open_async_t). opaque handles, caller adapts its concrete type via one-liners */
typedef struct {
  void *(*open_start)(void *slot_ctx);  /* NULL = immediate failure */
  int (*open_poll_fd)(const void *opening);
  short (*open_poll_events)(const void *opening);
  retryset_open_state_t (*open_step)(void *opening);
  void *(*open_take)(void *opening);    /* DONE only, consumes opening */
  void (*open_free)(void *opening);     /* safe any state, incl. NULL */
  int (*result_fd)(const void *result);
  void (*result_close)(void *result);
} retryset_ops_t;

typedef struct retryset retryset_t;

/* slot_ctxs[i]/labels[i] borrowed (labels/entries may be NULL, log only). NULL if n too big or alloc failure.
   retry: error_retry_s > 0 -> that interval. == 0 && n > 1 -> 5s default. 0 && n == 1 -> never. */
retryset_t *retryset_new(unsigned n, void *const *slot_ctxs, const char *const *labels,
                          const retryset_ops_t *ops, long error_retry_s);
void retryset_free(retryset_t *rs);

unsigned retryset_count(const retryset_t *rs);

/* NULL if slot idx not connected (down or connecting) */
void *retryset_result(const retryset_t *rs, unsigned idx);

/* poll() target; fd -1 if nothing to wait on (down, waiting for retry deadline) */
int retryset_poll_fd(const retryset_t *rs, unsigned idx);
short retryset_poll_events(const retryset_t *rs, unsigned idx);

/* soonest retry deadline across down slots, RETRYSET_NEVER if none due */
time_t retryset_next_deadline(const retryset_t *rs);

/* steps open if connecting, starts one if down and due. call after poll() or on a tick */
void retryset_service(retryset_t *rs, unsigned idx, time_t now);

/* call on a hard read error for a connected slot: closes result, reschedules retry */
void retryset_mark_down(retryset_t *rs, unsigned idx, time_t now);

#endif
