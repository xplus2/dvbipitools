/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRIST_BRIDGE_H
#define DIPIRIST_BRIDGE_H

#include <librist/librist.h>

#include "lib/metrics/export.h"
#include "lib/net/ts/sink.h"
#include "lib/net/ts/source.h"

#include "cli/args.h"

/* exposed for unit testing, not CLI-facing */
enum rist_profile profile_of(rist_profile_sel_t p);

/* librist stats callback, arg is metrics_exporter_t. frees stats, always 0 */
int bridge_receiver_stats_cb(void *arg, const struct rist_stats *stats);

/* push snapshot w/o flow counters when no flow stats arrived lately. main loop, every iteration */
void bridge_receiver_idle_push(metrics_exporter_t *mx, double now_mono);

/* run until stop sig or error. 0 clean stop, 1 error */
int bridge_run(const config_t *cfg, metrics_exporter_t *mx);

#endif
