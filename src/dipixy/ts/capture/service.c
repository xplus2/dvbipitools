/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include "lib/demux/rtp.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static void ring_write(capture_ctx_t *c, const unsigned char *data, size_t len) {
  size_t pos, first;
  uint64_t wt = atomic_load_explicit(&c->write_total, memory_order_relaxed);

  if (len > g_capture_ring_cap) {
    data += len - g_capture_ring_cap;
    len = g_capture_ring_cap;
  }
  pos = (size_t)(wt % g_capture_ring_cap);
  first = len < g_capture_ring_cap - pos ? len : g_capture_ring_cap - pos;
  memcpy(c->ring + pos, data, first);
  if (len > first) memcpy(c->ring, data + first, len - first);
  atomic_store_explicit(&c->write_total, wt + len, memory_order_release);
}

int capture_service(capture_ctx_t *ctx) {
  for (;;) {
    unsigned char buf[CAPTURE_RECV_BUF];
    ssize_t n;
    int unwrapped = 0;
    if (ctx->fcc) {
      unwrapped = 1;
      n = fcc_client_read(ctx->fcc, ctx->m, buf, sizeof buf);
      if (fcc_client_done(ctx->fcc)) {
        fcc_client_close(ctx->fcc);
        ctx->fcc = NULL;
      }
    } else if (ctx->ret) {
      unwrapped = 1;
      n = ret_client_read(ctx->ret, ctx->m, buf, sizeof buf);
    } else {
      n = mcast_recv(ctx->m, buf, sizeof buf, NULL);
    }
    if (n < 0)    return -1;
    if (n == 0)   return 0;
    if (unwrapped) {
      ring_write(ctx, buf, (size_t)n);
    } else if (ctx->rtp) {
      size_t off = rtp_payload_offset(buf, (size_t)n);
      if (off == 0) continue; /* not a valid RTP/TS datagram */
      ring_write(ctx, buf + off, (size_t)n - off);
    } else {
      ring_write(ctx, buf, (size_t)n);
    }
  }
}

capture_reader_t *capture_reader_open(capture_ctx_t *ctx) {
  capture_reader_t *r = calloc(1, sizeof *r);
  if (!r) return NULL;
  r->ctx = ctx;
  r->read_total = atomic_load_explicit(&ctx->write_total, memory_order_acquire);
  return r;
}

void capture_reader_close(capture_reader_t *r) { free(r); }

size_t capture_reader_read(capture_reader_t *r, unsigned char *buf, size_t cap) {
  const capture_ctx_t *c = r->ctx;
  uint64_t wt = atomic_load_explicit(&c->write_total, memory_order_acquire);
  uint64_t available;
  size_t n, pos, first;
  if (wt - r->read_total > g_capture_ring_cap) {
    uint64_t just_dropped = wt - r->read_total - g_capture_ring_cap;
    r->dropped += just_dropped;
    r->read_total = wt - g_capture_ring_cap;
    log_throttled(&r->drop_throttle, LOG_THROTTLE_WINDOW_S, "capture: reader fell behind, dropped %llu bytes", (unsigned long long)just_dropped);
  }
  available = wt - r->read_total;
  n = available < cap ? (size_t)available : cap;
  pos = (size_t)(r->read_total % g_capture_ring_cap);
  first = n < g_capture_ring_cap - pos ? n : g_capture_ring_cap - pos;
  memcpy(buf, c->ring + pos, first);
  if (n > first) memcpy(buf + first, c->ring, n - first);
  r->read_total += n;
  return n;
}

size_t capture_reader_dropped(const capture_reader_t *r) { return (size_t)r->dropped; }
