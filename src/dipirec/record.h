/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_RECORD_H
#define DIPIREC_RECORD_H

#include <stddef.h>

#include "args.h"
#include "lib/metrics/export.h"

/* run recording; 0 on success */
int record_run(const config_t *cfg, metrics_exporter_t *mx);

/* 1 if a stop signal or -t duration_s has elapsed since start (mono_seconds()) */
int stop_now(const config_t *cfg, double start);

/* secs as "H:MM:SS" or "M:SS", clamped to a bounded length (cap 99:59:59) */
void fmt_dur(double secs, char *buf, size_t n);

#endif
