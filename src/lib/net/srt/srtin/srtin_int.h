/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef LIB_NET_SRT_SRTIN_SRTIN_INT_H
#define LIB_NET_SRT_SRTIN_SRTIN_INT_H

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

#define SRT_RCVTIMEO_MS 200
#define SRT_CONNECT_SLICE_MS 100
#define SRT_CONNECT_TIMEOUT_DEFAULT_MS 3000
#define SRT_REVIVE_INTERVAL_S 1.0 /* min gap between bonded member re-add checks */

struct srtin {
  SRTSOCKET sock;
  SRTSOCKET listeners[SRTCOMMON_MAX_PEERS]; /* kept alive alongside sock, see open_single_listener() */
  int n_listeners;
  atomic_int *busy; /* listener side: 1 while sock live. read by libsrt thread, heap: outlives r use */
  srtgroup_mode_t group_mode;
  srtin_cfg_t cfg; /* caller reconnect re-dials from it */
  metrics_exporter_t *mx;
  const char *tool_version;
  double next_revive_at; /* mono_seconds() gate for bonded caller member re-add */
  srtcommon_resolver_t *revive_res; /* in-flight member re-add lookups */
};

int stop_wanted(const srtin_cfg_t *cfg);
SRTSOCKET open_single_caller(const srtin_cfg_t *cfg, int *transient);
int accept_poll(SRTSOCKET lsn, SRTSOCKET *out, atomic_int *busy);
SRTSOCKET open_single_listener(const srtin_cfg_t *cfg, SRTSOCKET *lsn_out, atomic_int *busy);
SRTSOCKET open_group_caller(const srtin_cfg_t *cfg, int *transient);
SRTSOCKET open_srt_input(const srtin_cfg_t *cfg, SRTSOCKET *listeners_out, int *n_listeners_out, atomic_int *busy, int *transient);
void idle_wait(void);

#endif
