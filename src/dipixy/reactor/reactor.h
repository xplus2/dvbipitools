/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIXY_REACTOR_H
#define DIPIXY_REACTOR_H

#include "../args.h"
#include "../ts/channels/channels.h"
#include "lib/metrics/export.h"

/* -1/-2/-3 = that many x ncpu (clamped >= 1). positive = itself */
static inline int reactor_resolve_workers(int workers_spec, int ncpu) {
  if (ncpu < 1) ncpu = 1;
  if (workers_spec < 0) return ncpu * -workers_spec;
  return workers_spec;
}

/* blocks until stop requested. mx never NULL, maybe disabled.
   0 ok, -1 setup failure. on_listening (may be NULL) fires at listening */
int reactor_run(const config_t *cfg, const channels_t *channels, metrics_exporter_t *mx, void (*on_listening)(const config_t *cfg));

/* metrics accessors, thread-safe, cheap enough to poll on every push/render */
long reactor_connections_total(void);   /* every accept ever, monotonic */
long reactor_connections_active(void);  /* live count */
unsigned long long reactor_bytes_served_total(void); /* wire bytes queued to clients, headers+body */

/* resolved worker thread count, set once at reactor_run() startup. 0 before then */
int reactor_worker_count(void);

#endif
