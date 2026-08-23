/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_SRT_SRTOUT_H
#define DVBIPITOOLS_LIB_NET_SRT_SRTOUT_H

#include <stddef.h>

#include "lib/metrics/export.h"

#include "srtcommon.h"

typedef struct {
  srtcommon_peer_t peers[SRTCOMMON_MAX_PEERS];
  int npeers;                  /* 1: plain connection. >1: requires group_mode != NONE */
  srtgroup_mode_t group_mode;  /* NONE: plain srt_connect()/srt_rendezvous() to peers[0] */
  int rendezvous;              /* srt_rendezvous() instead of connect(); group_mode must be NONE */
  const char *local_host;      /* NULL = wildcard. required when rendezvous set */
  unsigned local_port;         /* 0 = ephemeral; rendezvous wants a fixed port */
  srtcommon_opts_t opts;
  int verbose;                 /* gates libsrt's own NOTICE/DEBUG logging */
  metrics_exporter_t *mx;      /* NULL = no stats push */
  const char *tool_version;    /* required if mx set */
  unsigned safety_mult;        /* 0 = default 4; clamped to 32. pending-queue latency-window multiplier */
} srtout_cfg_t;

typedef struct srtout srtout_t;

typedef struct {
  int connected; /* 1 once link established, writable */
} srtout_status_t;

/* NULL: bad config, or initial resolve/socket-create failure. connect is
   async after that: writes queue before link comes up. no libsrt: always
   fails, logs why */
srtout_t *srtout_open(const srtout_cfg_t *cfg);

/* non-blocking: advances connect/reconnect state, flushes queued data once
   writable. call once per loop iteration regardless of other activity */
void srtout_service(srtout_t *r, srtout_status_t *out);

/* never blocks: chunks at SRT live payload size, queues what can't send yet
   (not connected, backpressure) up to a bitrate/latency-sized bound, drops
   oldest on overflow */
void srtout_write(srtout_t *r, const unsigned char *buf, size_t n);

void srtout_close(srtout_t *r);

#endif
