/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIBCG_ANNOUNCE_H
#define DIPIBCG_ANNOUNCE_H

#include "lib/metrics/export.h"

#include "args.h"

int announce_run(const config_t *cfg, metrics_exporter_t *mx);

#endif
