/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "../helper/log.h"

#include "retryset.h"

#define RETRYSET_DEFAULT_RETRY_S 5

typedef enum { RETRYSET_DOWN, RETRYSET_CONNECTING, RETRYSET_CONNECTED } slot_state_t;

typedef struct {
  void *slot_ctx;
  const char *label; /* borrowed, may be NULL */

  slot_state_t state;
  void *opening; /* during RETRYSET_CONNECTING */
  void *result;  /* during RETRYSET_CONNECTED */
  time_t retry_deadline; /* meaningful during RETRYSET_DOWN */
} retryset_slot_t;

struct retryset {
  retryset_slot_t slots[RETRYSET_MAX_SLOTS];
  unsigned count;
  const retryset_ops_t *ops;
  long retry_interval_s; /* 0 = never retry, see RETRYSET_NEVER */
};

retryset_t *retryset_new(unsigned n, void *const *slot_ctxs, const char *const *labels,
                          const retryset_ops_t *ops, long error_retry_s) {
  retryset_t *rs;

  if (n > RETRYSET_MAX_SLOTS)
    return NULL;
  rs = calloc(1, sizeof *rs);
  if (!rs)
    return NULL;
  rs->count = n;
  rs->ops = ops;
  rs->retry_interval_s = error_retry_s;
  if (rs->retry_interval_s <= 0 && n > 1) {
    rs->retry_interval_s = RETRYSET_DEFAULT_RETRY_S;
    log_line("multi-input: no -e given, defaulting to %lds per-input retry", rs->retry_interval_s);
  }

  for (unsigned i = 0; i < n; i++) {
    retryset_slot_t *sl = &rs->slots[i];
    sl->slot_ctx = slot_ctxs[i];
    sl->label = labels ? labels[i] : NULL;
    sl->state = RETRYSET_DOWN;
    sl->retry_deadline = 0; /* due immediately */
  }
  return rs;
}

void retryset_free(retryset_t *rs) {
  if (!rs)
    return;
  for (unsigned i = 0; i < rs->count; i++) {
    retryset_slot_t *sl = &rs->slots[i];
    if (sl->opening)
      rs->ops->open_free(sl->opening);
    if (sl->result)
      rs->ops->result_close(sl->result);
  }
  free(rs);
}

unsigned retryset_count(const retryset_t *rs) { return rs->count; }

void *retryset_result(const retryset_t *rs, unsigned idx) {
  const retryset_slot_t *sl = &rs->slots[idx];
  return sl->state == RETRYSET_CONNECTED ? sl->result : NULL;
}

int retryset_poll_fd(const retryset_t *rs, unsigned idx) {
  const retryset_slot_t *sl = &rs->slots[idx];
  if (sl->state == RETRYSET_CONNECTING)
    return rs->ops->open_poll_fd(sl->opening);
  if (sl->state == RETRYSET_CONNECTED)
    return rs->ops->result_fd(sl->result);
  return -1;
}

short retryset_poll_events(const retryset_t *rs, unsigned idx) {
  const retryset_slot_t *sl = &rs->slots[idx];
  if (sl->state == RETRYSET_CONNECTING)
    return rs->ops->open_poll_events(sl->opening);
  if (sl->state == RETRYSET_CONNECTED)
    return POLLIN;
  return 0;
}

time_t retryset_next_deadline(const retryset_t *rs) {
  time_t best = RETRYSET_NEVER;

  for (unsigned i = 0; i < rs->count; i++) {
    const retryset_slot_t *sl = &rs->slots[i];
    if (sl->state != RETRYSET_DOWN || sl->retry_deadline == RETRYSET_NEVER)
      continue;
    if (best == RETRYSET_NEVER || sl->retry_deadline < best)
      best = sl->retry_deadline;
  }
  return best;
}

static time_t next_retry_deadline(const retryset_t *rs, time_t now) {
  return rs->retry_interval_s > 0 ? now + rs->retry_interval_s : RETRYSET_NEVER;
}

static void log_slot(unsigned idx, const char *label, const char *fmt, ...) {
  char msg[128];
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(msg, sizeof msg, fmt, ap);
  va_end(ap);
  if (label)
    log_line("input %u (%s): %s", idx, label, msg);
  else
    log_line("input %u: %s", idx, msg);
}

void retryset_service(retryset_t *rs, unsigned idx, time_t now) {
  retryset_slot_t *sl = &rs->slots[idx];

  if (sl->state == RETRYSET_CONNECTING) {
    retryset_open_state_t st = rs->ops->open_step(sl->opening);
    if (st == RETRYSET_OPEN_PENDING)
      return;
    if (st == RETRYSET_OPEN_DONE) {
      sl->result = rs->ops->open_take(sl->opening);
      sl->opening = NULL;
      sl->state = RETRYSET_CONNECTED;
      log_slot(idx, sl->label, "connected");
      return;
    }
    rs->ops->open_free(sl->opening);
    sl->opening = NULL;
    sl->state = RETRYSET_DOWN;
    sl->retry_deadline = next_retry_deadline(rs, now);
    if (sl->retry_deadline != RETRYSET_NEVER)
      log_slot(idx, sl->label, "connect failed, retrying in %lds", rs->retry_interval_s);
    else
      log_slot(idx, sl->label, "connect failed, not retrying");
    return;
  }

  if (sl->state == RETRYSET_CONNECTED)
    return; /* caller drives its own reads directly, see retryset_mark_down() on error */

  if (sl->retry_deadline == RETRYSET_NEVER || now < sl->retry_deadline)
    return;
  sl->opening = rs->ops->open_start(sl->slot_ctx);
  if (!sl->opening) {
    sl->retry_deadline = next_retry_deadline(rs, now);
    return;
  }
  sl->state = RETRYSET_CONNECTING;
}

void retryset_mark_down(retryset_t *rs, unsigned idx, time_t now) {
  retryset_slot_t *sl = &rs->slots[idx];

  if (sl->state != RETRYSET_CONNECTED)
    return;
  rs->ops->result_close(sl->result);
  sl->result = NULL;
  sl->state = RETRYSET_DOWN;
  sl->retry_deadline = next_retry_deadline(rs, now);
  if (sl->retry_deadline != RETRYSET_NEVER)
    log_slot(idx, sl->label, "read error, retrying in %lds", rs->retry_interval_s);
  else
    log_slot(idx, sl->label, "read error, not retrying");
}
