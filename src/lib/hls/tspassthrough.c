/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "tspassthrough.h"

struct fwd_ctx {
  hls_live_t *h;
};

static int on_ts_packet(void *ctx, const unsigned char *pkt) {
  hls_live_emit(((struct fwd_ctx *)ctx)->h, pkt, 188);
  return 0;
}

void hls_ts_passthrough_feed(void *ctx, hls_live_t *h, const unsigned char *data, size_t len) {
  hls_ts_passthrough_t *p = ctx;
  struct fwd_ctx fc = {h};
  p->tspack.acclen = 0;
  tspack_feed(&p->tspack, data, len, on_ts_packet, &fc);
}
