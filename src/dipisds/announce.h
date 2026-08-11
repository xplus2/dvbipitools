/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISDS_ANNOUNCE_H
#define DIPISDS_ANNOUNCE_H

#include "lib/metrics/export.h"

#include "args.h"
#include "input.h"

int announce_run(const config_t *cfg, metrics_exporter_t *mx);

typedef struct {
  input_t in;
  unsigned char *broadcast_doc;
  unsigned char *sp_doc;
  size_t broadcast_len;
  size_t sp_len;
} sds_state_t;

/* loads cfg->input_path via input_load; INPUT_SERVICES also builds broadcast_doc/sp_doc. 0 ok, -1 error on stderr */
int state_load(const config_t *cfg, sds_state_t *st);
void state_free(sds_state_t *st);

#endif
