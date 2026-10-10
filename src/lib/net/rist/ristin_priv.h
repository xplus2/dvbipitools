/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_NET_RIST_RISTIN_PRIV_H
#define DVBIPITOOLS_LIB_NET_RIST_RISTIN_PRIV_H

#include <librist/librist.h>

#include "lib/helper/pipereader.h"
#include "lib/metrics/export.h"

struct ristin {
  struct rist_ctx *ctx;
  pipereader_t io;
  metrics_exporter_t *mx;
  const char *tool_version;
  char peer_uri[512];
  char secret[128];
  char cname[128];
  int key_size;
  unsigned buffer_ms;
  int simple;
  int verbose;
};

/* librist stats callback, owns stats */
int receiver_stats_cb(void *arg, const struct rist_stats *stats);

#endif
