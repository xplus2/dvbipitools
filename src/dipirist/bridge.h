/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIRIST_BRIDGE_H
#define DIPIRIST_BRIDGE_H

#include "lib/metrics/export.h"

#include "args.h"

/* runs until stop signal or hard error. 0 clean stop, 1 error */
int bridge_run(const config_t *cfg, metrics_exporter_t *mx);

#endif
