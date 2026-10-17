/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_HLS_TSPASSTHROUGH_H
#define DVBIPITOOLS_LIB_HLS_TSPASSTHROUGH_H

#include <stdint.h>

#include "lib/demux/tspack.h"
#include "live.h"

typedef struct {
  tspack_t tspack;
  int warned;
  unsigned char pat_cc;
  unsigned char pmt_cc;
  unsigned char es_cc;
  uint64_t base_pts;
  uint64_t frames; /* 1024-sample raw data blocks since base_pts */
  uint64_t last_pcr_pts;
  int have_pcr;
} hls_ts_passthrough_t;

void hls_ts_passthrough_feed(void *ctx, hls_live_t *h, const unsigned char *data, size_t len);

#endif
