/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRADIOHEAD_INPUT_INPUTSET_H
#define DIPIRADIOHEAD_INPUT_INPUTSET_H

#include <time.h>

#include "lib/metrics/export.h"
#include "lib/net/retryset.h"

#include "../args.h"
#include "source.h"

/* sentinel retry_deadline meaning "don't retry this slot" - reproduces the single-input
   -e-not-given contract (fail once, don't reconnect) for whichever slot hits it */
#define INPUTSET_NEVER RETRYSET_NEVER

typedef struct inputset inputset_t;

/* slot per cfg->inputs[i], retried independently. cfg must outlive inputset (borrowed uri/service_name).
   ctxs[i] (or ctxs itself) may be NULL: per-input source_meta_cb ctx, cb shared.
   retry: error_retry_s > 0 -> that interval, always. == 0 && n_inputs > 1 -> 5s default (logged). == 0 && n_inputs == 1 -> never
   input_stats[i] (or input_stats itself): nullable, one input_metrics_t per slot, caller-owned - must outlive inputset, survives reconnects. */
inputset_t *inputset_new(const config_t *cfg, source_meta_cb cb, void *const *ctxs, input_metrics_t *input_stats);
void inputset_free(inputset_t *is);

unsigned inputset_count(const inputset_t *is);
unsigned inputset_sid(const inputset_t *is, unsigned idx);
const char *inputset_service_name(const inputset_t *is, unsigned idx);
unsigned inputset_pmt_pid(const inputset_t *is, unsigned idx);
unsigned inputset_audio_pid(const inputset_t *is, unsigned idx);

/* NULL if slot idx isn't currently connected (down or still (re)connecting) */
source_t *inputset_source(const inputset_t *is, unsigned idx);

/* what to poll() for right now; fd is -1 if there's nothing to wait on (slot is down,
   waiting for its retry deadline - see inputset_next_deadline()) */
int inputset_poll_fd(const inputset_t *is, unsigned idx);
short inputset_poll_events(const inputset_t *is, unsigned idx);

/* soonest retry deadline across down slots, for the caller's poll() timeout. INPUTSET_NEVER if none due. */
time_t inputset_next_deadline(const inputset_t *is);

/* advances slot idx: steps the async open if connecting, starts one if down and due. call
   after poll() readiness or on a timeout tick (harmless no-op otherwise). */
void inputset_service(inputset_t *is, unsigned idx, time_t now);

/* call when source_next_frame() hard-errors (-1) on a connected slot: closes it, schedules retry per policy. */
void inputset_mark_down(inputset_t *is, unsigned idx, time_t now);

#endif
