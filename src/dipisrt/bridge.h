/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISRT_BRIDGE_H
#define DIPISRT_BRIDGE_H

#include "lib/metrics/export.h"
#include "lib/net/srt/srtin.h"
#include "lib/net/srt/srtout.h"
#include "lib/net/ts/sink.h"
#include "lib/net/ts/source.h"

#include <stdint.h>

#include "cli/args.h"

/* runs until stop sig or error. 0 clean stop, 1 error */
int bridge_run(const config_t *cfg, metrics_exporter_t *mx);

typedef struct {
  uint32_t hash;
  int len;
} dedup_entry_t;

/* cand: bit per history start offset (oldest first) still consistent with replay */
typedef struct {
  uint32_t cand;
  int pos;
} dedup_replay_t;

void dedup_replay_begin(dedup_replay_t *r, int hist_n);
/* 1: chunk is replay of history, skip. 0: new data, replay over */
int dedup_replay_match(dedup_replay_t *r, const dedup_entry_t *hist, int hist_n, int next, uint32_t hash, int len);
void dedup_record(dedup_entry_t *hist, int hist_n, int *next, uint32_t hash, int len);

#endif
