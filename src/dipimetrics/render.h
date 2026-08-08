/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIMETRICS_RENDER_H
#define DIPIMETRICS_RENDER_H

#include <stddef.h>

#include "store.h"

/* renders the current store as OpenMetrics text. mallocs *out (caller frees
   via free()), sets *out_len (excludes the NUL terminator). */
void render_openmetrics(const store_t *st, double now_mono, char **out, size_t *out_len);

#endif
