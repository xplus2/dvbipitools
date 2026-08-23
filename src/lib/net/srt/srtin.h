/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_SRT_SRTIN_H
#define DVBIPITOOLS_LIB_NET_SRT_SRTIN_H

#include <stddef.h>

#include "lib/metrics/export.h"

#include "srtcommon.h"

typedef struct {
  srtcommon_peer_t peers[SRTCOMMON_MAX_PEERS]; /* listen: local bind addrs. else: remote peers to call */
  int npeers;                   /* 1: plain connection. >1: requires group_mode != NONE */
  srtgroup_mode_t group_mode;   /* NONE: plain srt_connect()/srt_accept() on peers[0] */
  int listen;                   /* 1: bind/listen/accept. 0: call out (connect/rendezvous) */
  int rendezvous;               /* srt_rendezvous() instead of connect(); listen/group_mode must be NONE */
  const char *local_host;       /* NULL = wildcard. required when rendezvous set */
  unsigned local_port;          /* 0 = ephemeral; rendezvous wants a fixed port */
  srtcommon_opts_t opts;
  int verbose;                  /* gates libsrt's own NOTICE/DEBUG logging */
  metrics_exporter_t *mx;       /* NULL = no stats push */
  const char *tool_version;     /* required if mx set */
} srtin_cfg_t;

typedef struct srtin srtin_t;

/* blocks until connected/accepted or a fatal setup error. NULL on failure.
   built without libsrt: always fails, logs why */
srtin_t *srtin_open(const srtin_cfg_t *cfg);

/* blocks up to a short internal timeout. n>0: data. 0: nothing this call (timeout,
   transient async-recv, or a group reconnect happened/was attempted, see
   reconnected_out). -1: fatal, unrecoverable read error */
int srtin_read(srtin_t *r, unsigned char *buf, size_t cap, int *reconnected_out);

void srtin_close(srtin_t *r);

#endif
