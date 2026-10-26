/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISDS_ANNOUNCE_H
#define DIPISDS_ANNOUNCE_H

#include "lib/metrics/export.h"

#include "cli/args.h"
#include "input.h"

int announce_run(const config_t *cfg, metrics_exporter_t *mx);

typedef struct {
  unsigned char *data; /* NULL = not announced */
  size_t len;
  unsigned version;
} sds_doc_t;

typedef struct {
  input_t in;
  unsigned version; /* raw .xml input */
  sds_doc_t broadcast;
  sds_doc_t sp;
  sds_doc_t package; /* --packages */
  sds_doc_t cell;    /* --cells */
  sds_doc_t rmsfus;  /* --rms-name/--fus-name */
  sds_doc_t bcg;     /* --bcg-name */
} sds_state_t;

/* loads cfg->input_path via input_load; INPUT_SERVICES also builds docs. 0 ok, -1 error on stderr */
int state_load(const config_t *cfg, sds_state_t *st);
int state_reload(const config_t *cfg, sds_state_t *st);
void state_free(sds_state_t *st);

#endif
