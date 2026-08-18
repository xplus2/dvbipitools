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

#define BURST_RTP_CLOCK_HZ 90000.0 /* MP2T-over-RTP fixed rate, RFC 2250 */

/* ms spanned by cached RAP-anchored run. 0 if fewer than 2 entries. */
static unsigned cache_depth_ms(const channel_t *c) {
  rap_cache_meta_t first, last;
  size_t count = channel_cache_count(c);
  uint32_t diff;

  if (count < 2 || !channel_cache_peek_meta(c, 0, &first) || !channel_cache_peek_meta(c, count - 1, &last))
    return 0;
  diff = last.timestamp - first.timestamp; /* wraps safely, span << 2^32 ticks */
  return (unsigned)((double)diff * 1000.0 / BURST_RTP_CLOCK_HZ);
}

burst_response_t burst_decide(const channel_t *c, const rtcp_rams_r_t *req, unsigned max_buffer_fill_bound_ms) {
  if (!c)
    return req->ignore_media_ssrc ? BURST_REJECT : BURST_SSRC_NOT_FOUND;

  if (max_buffer_fill_bound_ms > 0 && req->has_min_buffer_fill && req->min_buffer_fill_ms > max_buffer_fill_bound_ms)
    return BURST_MIN_BUFFER_INVALID; /* RFC 6285 Sec 10 DoS bound */

  if (req->has_max_buffer_fill && req->max_buffer_fill_ms == 0)
    return BURST_MAX_BUFFER_INVALID; /* zero buffer, nothing fits */

  if (req->has_min_buffer_fill && req->has_max_buffer_fill && req->min_buffer_fill_ms > req->max_buffer_fill_ms)
    return BURST_MALFORMED; /* min > max */

  if (!channel_has_rap(c))
    return BURST_NO_RAP;

  if (req->has_min_buffer_fill && cache_depth_ms(c) < req->min_buffer_fill_ms)
    return BURST_NO_VALID_START; /* cache depth < requested min */

  if (req->has_max_bitrate && req->max_bitrate_bps <= atomic_load_explicit(&c->nominal_bps, memory_order_relaxed))
    return BURST_BITRATE_INSUFFICIENT; /* can't catch up at/below nominal */

  return BURST_ACCEPT;
}

burst_t *burst_new(const channel_t *c, double multiplier, double client_max_bps, unsigned char rtx_pt) {
  burst_t *b = calloc(1, sizeof *b);
  double target;

  if (!b)
    return NULL;
  target = atomic_load_explicit(&c->nominal_bps, memory_order_relaxed) * multiplier;
  if (client_max_bps > 0 && client_max_bps < target)
    target = client_max_bps;

  b->channel = c;
  b->generation = atomic_load_explicit(&c->generation, memory_order_relaxed);
  atomic_init(&b->refs, 1u);
  b->rtx_pt = rtx_pt;
  atomic_store_explicit(&b->target_bps, target, memory_order_relaxed);
  clock_gettime(CLOCK_MONOTONIC, &b->start_time);
  return b;
}

void burst_free(burst_t *b) {
  free(b);
}

void burst_acquire(burst_t *b) {
  atomic_fetch_add_explicit(&b->refs, 1u, memory_order_acquire);
}

void burst_release(burst_t *b) {
  if (atomic_fetch_sub_explicit(&b->refs, 1u, memory_order_acq_rel) == 1u)
    burst_free(b);
}

burst_tick_result_t burst_tick(burst_t *b, unsigned duration_cap_ms, burst_send_fn send_cb, void *user) {
  double elapsed_s, budget_bytes;
  size_t count;

  if (atomic_load_explicit(&b->done, memory_order_acquire))
    return BURST_TICK_DONE;

  if (atomic_load_explicit(&b->channel->generation, memory_order_acquire) != b->generation) {
    atomic_store_explicit(&b->done, 1, memory_order_release);
    return BURST_TICK_DONE; /* slot recycled under us: channel identity no longer ours */
  }

  elapsed_s = elapsed_seconds(&b->start_time);
  if (elapsed_s * 1000.0 >= (double)duration_cap_ms) {
    atomic_store_explicit(&b->done, 1, memory_order_release);
    return BURST_TICK_DONE;
  }

  budget_bytes = atomic_load_explicit(&b->target_bps, memory_order_relaxed) * elapsed_s / 8.0;
  count = channel_cache_count(b->channel);

  while (b->cursor < count && b->bytes_sent < budget_bytes) {
    rap_cache_entry_t e;
    unsigned char pkt[BURST_PKT_MAX];
    size_t n;
    uint32_t ssrc;

    if (atomic_load_explicit(&b->done, memory_order_acquire))
      return BURST_TICK_DONE; /* terminated mid-tick by another thread */

    if (atomic_load_explicit(&b->channel->generation, memory_order_acquire) != b->generation) {
      atomic_store_explicit(&b->done, 1, memory_order_release);
      return BURST_TICK_DONE; /* slot recycled mid-tick */
    }

    if (!channel_cache_get(b->channel, b->cursor, &e))
      break; /* raced with reap/reclaim, stop rather than misread */

    if (atomic_load_explicit(&b->has_stop_seq, memory_order_acquire)) {
      uint16_t stop_seq = atomic_load_explicit(&b->stop_seq, memory_order_relaxed);
      if ((int16_t)(e.seq - stop_seq) >= 0) { /* at or past client's first multicast packet: don't resend it */
        atomic_store_explicit(&b->done, 1, memory_order_release);
        return BURST_TICK_DONE;
      }
    }

    ssrc = atomic_load_explicit(&b->channel->ssrc, memory_order_relaxed);
    n = rtx_build(&b->rtx_seq, ssrc, b->rtx_pt, e.timestamp, e.seq, e.payload, e.payload_len, pkt, sizeof pkt);
    if (n > 0 && send_cb)
      send_cb(pkt, n, e.dscp, user);

    b->bytes_sent += (double)e.payload_len;
    b->cursor++;
  }

  if (b->cursor >= count) {
    atomic_store_explicit(&b->done, 1, memory_order_release);
    return BURST_TICK_DONE;
  }
  return BURST_TICK_CONTINUE;
}

void burst_terminate(burst_t *b, int has_stop_seq, uint32_t stop_seqnum) {
  if (!has_stop_seq) {
    atomic_store_explicit(&b->done, 1, memory_order_release);
    return;
  }
  atomic_store_explicit(&b->stop_seq, (uint16_t)stop_seqnum, memory_order_relaxed);
  atomic_store_explicit(&b->has_stop_seq, 1, memory_order_release);
}

int burst_is_done(const burst_t *b) {
  return atomic_load_explicit(&b->done, memory_order_acquire);
}
