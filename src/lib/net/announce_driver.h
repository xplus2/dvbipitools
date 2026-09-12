/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_ANNOUNCE_DRIVER_H
#define DVBIPITOOLS_LIB_NET_ANNOUNCE_DRIVER_H

#include "multicast.h"

typedef struct {
  void *ctx;
  void (*on_ready)(void *ctx, mcast_t *m);
  void (*reload)(void *ctx);
  int (*run_cycle)(void *ctx, mcast_t *m, unsigned cycle);
  void (*cleanup)(void *ctx);
  int family;
  const char *mcast_group;
  unsigned mcast_port;
  const char *iface;
  int dscp;
  long interval_s;
} announce_driver_t;

int announce_driver_run(const announce_driver_t *d);

#endif
