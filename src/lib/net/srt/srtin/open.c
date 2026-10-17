/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <unistd.h>
#include <srt/srt.h>

#include "lib/helper/log.h"
#include "lib/sys/signal.h"
#include "lib/net/srt/srtcommon.h"
#include "lib/net/srt/srtin.h"
#include "lib/net/srt/srtin/srtin_int.h"

srtin_t *srtin_open(const srtin_cfg_t *cfg) {
  srtin_t *r;
  SRTSOCKET s;
  SRTSOCKET listeners[SRTCOMMON_MAX_PEERS];
  int n_listeners;
  atomic_int *busy;

  if (cfg->npeers <= 0 || cfg->npeers > SRTCOMMON_MAX_PEERS) {
    log_code("LE000322", "srt: invalid peer count %d", cfg->npeers);
    return NULL;
  }
  if (cfg->group_mode == SRTGROUP_NONE && cfg->npeers != 1) {
    log_code("LE000323", "srt: plain connection needs exactly one peer, got %d", cfg->npeers);
    return NULL;
  }

  if (srtcommon_startup() != 0) {
    log_code("LE500166", "srt: startup failed: %s", srt_getlasterror_str());
    return NULL;
  }
  srtcommon_open_logging(cfg->verbose);

  busy = calloc(1, sizeof *busy);
  if (!busy) {
    srtcommon_cleanup();
    return NULL;
  }

  for (;;) {
    int transient;
    s = open_srt_input(cfg, listeners, &n_listeners, busy, &transient);
    if (s != SRT_INVALID_SOCK || !cfg->retry_open || !transient || stop_wanted(cfg)) break;
    idle_wait();
  }
  if (s == SRT_INVALID_SOCK) {
    free(busy);
    srtcommon_cleanup();
    return NULL;
  }

  r = calloc(1, sizeof *r);
  if (!r) {
    srt_close(s);
    for (int i = 0; i < n_listeners; i++)
      srt_close(listeners[i]);
    free(busy);
    srtcommon_cleanup();
    return NULL;
  }
  r->sock = s;
  r->busy = busy;
  r->n_listeners = n_listeners;
  memcpy(r->listeners, listeners, (size_t)n_listeners * sizeof listeners[0]);
  r->group_mode = cfg->group_mode;
  r->cfg = *cfg;
  r->mx = cfg->mx;
  r->tool_version = cfg->tool_version;
  return r;
}
