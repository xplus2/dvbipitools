/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISRT_BRIDGE_H
#define DIPISRT_BRIDGE_H

#include "lib/metrics/export.h"
#include "lib/net/srt/srtin.h"
#include "lib/net/srt/srtout.h"
#include "lib/net/tssink.h"
#include "lib/net/tssource.h"

#include <stdint.h>

#include "args.h"

/* runs until stop sig or error. 0 clean stop, 1 error */
int bridge_run(const config_t *cfg, metrics_exporter_t *mx);

typedef struct {
  uint32_t hash;
  int len;
} dedup_entry_t;

int dedup_is_duplicate(const dedup_entry_t *hist, int hist_n, uint32_t hash, int len);
void dedup_record(dedup_entry_t *hist, int hist_n, int *next, uint32_t hash, int len);

#endif
