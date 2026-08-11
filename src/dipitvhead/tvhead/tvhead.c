/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

int tvhead_run(const config_t *cfg, metrics_exporter_t *mx) {
  if (cfg->n_inputs > 1)
    return tvhead_run_mpts(cfg, mx);
  return tvhead_run_single(cfg, mx);
}
