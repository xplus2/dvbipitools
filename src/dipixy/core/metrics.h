/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_METRICS_H
#define DIPIXY_METRICS_H

#include <stddef.h>

#include "../args.h"
#include "lib/metrics/export.h"

/* stays disabled unless cfg->metrics_id is set, matching every other tool */
void dipixy_metrics_init(metrics_exporter_t *exp, const config_t *cfg);

void dipixy_metrics_close(metrics_exporter_t *exp);

/* call once/loop iteration. due-gated internally, noop if disabled */
void dipixy_metrics_push(metrics_exporter_t *exp);

/* bumps HTTP-layer counters, read by both UDS push and --metrics-http */
void dipixy_metrics_note_request(void);
void dipixy_metrics_note_http_error(void);

/* --metrics-http only. *out: thread-local buffer, valid until this thread's
   next call, caller must not free. Prometheus text exposition format. 0 ok, -1 OOM */
int dipixy_metrics_render_prometheus(char **out, size_t *out_len);

#endif
