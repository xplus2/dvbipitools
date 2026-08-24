/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_SRT_SRTSRC_H
#define DVBIPITOOLS_LIB_NET_SRT_SRTSRC_H

#include <stddef.h>

#include "lib/metrics/export.h"

/* tssource.c-facing wrapper on srtin_t, single peer, no bonding/rendezvous
   (use dipisrt). thread-wraps blocking srtin_open() into ristin_t's shape. */
typedef struct {
  const char *host;
  unsigned port;
  int listen;               /* 1 = @, bind/listen/accept. 0 = call out (connect) */
  const char *passphrase;   /* NULL/"" = no encryption */
  int pbkeylen;             /* 0 = default (16), else 16/24/32 */
  const char *streamid;     /* NULL/"" = none */
  const char *packetfilter; /* NULL/"" = none, raw SRTO_PACKETFILTER string */
  unsigned latency_ms;       /* 0 = library default */
  int verbose;               /* gates libsrt's own NOTICE/DEBUG logging */
  metrics_exporter_t *mx;    /* NULL = no stats push */
  const char *tool_version;  /* required if mx set */
} srtsrc_cfg_t;

typedef struct srtsrc srtsrc_t;

/* never blocks: thread runs srtin_open()'s blocking connect/accept, feeds a pipe.
   connect failure surfaces as EOF on srtsrc_fd(). NULL only on local setup failure. no libsrt: fail */
srtsrc_t *srtsrc_open(const srtsrc_cfg_t *cfg);

int srtsrc_fd(const srtsrc_t *r); /* pipe read end, blocking, poll-safe */

void srtsrc_close(srtsrc_t *r);

#endif
