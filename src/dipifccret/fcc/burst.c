/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>

#include "burst.h"

#define BURST_PKT_MAX (CHANNEL_MAX_PAYLOAD + 16) /* rtx header (14) + payload */

static double elapsed_seconds(const struct timespec *start) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)(now.tv_sec - start->tv_sec) + (double)(now.tv_nsec - start->tv_nsec) / 1e9;
}

burst_response_t burst_decide(const channel_t *c, const rtcp_rams_r_t *req) {
  if (!c)
    return BURST_REJECT; /* unresolved channel */

  if (req->has_min_buffer_fill && req->has_max_buffer_fill && req->min_buffer_fill_ms > req->max_buffer_fill_ms)
    return BURST_MALFORMED; /* min > max */

  if (!channel_has_rap(c))
    return BURST_REJECT; /* no RAP yet */

  if (req->has_max_bitrate && req->max_bitrate_bps <= atomic_load_explicit(&c->nominal_bps, memory_order_relaxed))
    return BURST_BITRATE_INSUFFICIENT; /* can't catch up at/below nominal */

  return BURST_ACCEPT;
}

burst_t *burst_new(const channel_t *c, double multiplier, double client_max_bps, unsigned char rtx_pt) {
  burst_t *b = calloc(1, sizeof *b);
  double target;

  if (!b)
    return NULL;
  b->rtx = rtx_ctx_new();
  if (!b->rtx) {
    free(b);
    return NULL;
  }
  target = atomic_load_explicit(&c->nominal_bps, memory_order_relaxed) * multiplier;
  if (client_max_bps > 0 && client_max_bps < target)
    target = client_max_bps;

  b->channel = c;
  b->rtx_pt = rtx_pt;
  b->target_bps = target;
  clock_gettime(CLOCK_MONOTONIC, &b->start_time);
  return b;
}

void burst_free(burst_t *b) {
  if (!b)
    return;
  rtx_ctx_free(b->rtx);
  free(b);
}

burst_tick_result_t burst_tick(burst_t *b, unsigned duration_cap_ms, burst_send_fn send_cb, void *user) {
  double elapsed_s, budget_bytes;
  size_t count;

  if (atomic_load_explicit(&b->done, memory_order_acquire))
    return BURST_TICK_DONE;

  elapsed_s = elapsed_seconds(&b->start_time);
  if (elapsed_s * 1000.0 >= (double)duration_cap_ms) {
    atomic_store_explicit(&b->done, 1, memory_order_release);
    return BURST_TICK_DONE;
  }

  budget_bytes = b->target_bps * elapsed_s / 8.0;
  count = channel_cache_count(b->channel);

  while (b->cursor < count && b->bytes_sent < budget_bytes) {
    rap_cache_entry_t e;
    unsigned char pkt[BURST_PKT_MAX];
    size_t n;
    uint32_t ssrc;

    if (atomic_load_explicit(&b->done, memory_order_acquire))
      return BURST_TICK_DONE; /* terminated mid-tick by another thread */

    if (!channel_cache_get(b->channel, b->cursor, &e))
      break; /* raced with reap/reclaim, stop rather than misread */

    ssrc = atomic_load_explicit(&b->channel->ssrc, memory_order_relaxed);
    n = rtx_build(b->rtx, ssrc, b->rtx_pt, e.timestamp, e.seq, e.payload, e.payload_len, pkt, sizeof pkt);
    if (n > 0 && send_cb)
      send_cb(pkt, n, user);

    b->bytes_sent += (double)e.payload_len;
    b->cursor++;
  }

  if (b->cursor >= count) {
    atomic_store_explicit(&b->done, 1, memory_order_release);
    return BURST_TICK_DONE;
  }
  return BURST_TICK_CONTINUE;
}

void burst_terminate(burst_t *b) {
  atomic_store_explicit(&b->done, 1, memory_order_release);
}

int burst_is_done(const burst_t *b) {
  return atomic_load_explicit(&b->done, memory_order_acquire);
}
