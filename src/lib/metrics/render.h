/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIMETRICS_RENDER_H
#define DIPIMETRICS_RENDER_H

#include <stddef.h>

#include "lib/sys/ioutil.h"
#include "export.h"
#include "store.h"

/* mallocs *out (caller frees via free()), sets *out_len (excludes NUL terminator). */
void render_openmetrics(const store_t *st, double now_mono, char **out, size_t *out_len);

/* appends metric families only, no snapshot age, self metrics or EOF.
   instance_labels 0 drops component/headend_id. openmetrics 1 declares counter
   and info families without their _total/_info sample suffix */
void render_series(dstrbuf_t *sb, const store_t *st, int instance_labels, int openmetrics);

/* renders what fill puts as one body without instance labels or EOF. mallocs *out
   (free()), *out_len excludes NUL. 0 ok, -1 OOM or fill overflow */
int render_local(metrics_component_t component, metrics_extra_fn fill, void *ctx, char **out, size_t *out_len);

#endif
