/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <srt/srt.h>

#include "lib/helper/log.h"
#include "lib/sys/signal.h"
#include "lib/net/srt/srtcommon.h"
#include "lib/net/srt/srtout.h"
#include "lib/net/srt/srtout/srtout_int.h"

static unsigned char *pending_slot(srtout_t *r, int idx) { return r->pending + (size_t)idx * SRTOUT_PLSIZE; }

void note_enqueue_off(srtout_t *r) { (void)r; }

void note_enqueue_hwm(srtout_t *r) {
  if (r->pending_count > r->pending_hwm) r->pending_hwm = r->pending_count;
}

/* 1 if oldest chunk was dropped */
int enqueue_pending(srtout_t *r, const unsigned char *buf, int len) {
  int tail;
  int dropped = 0;

  if (r->pending_count == r->pending_cap) {
    r->pending_bytes -= (uint64_t)r->pending_len[r->pending_head];
    r->pending_head = (r->pending_head + 1) % r->pending_cap;
    r->pending_count--;
    r->queue_dropped++;
    dropped = 1;
    if (!r->drop_logged) {
      log_code("LW100131", "srt output: send queue full, dropping oldest queued chunk");
      r->drop_logged = 1;
    }
  } else {
    r->drop_logged = 0;
  }
  tail = (r->pending_head + r->pending_count) % r->pending_cap;
  memcpy(pending_slot(r, tail), buf, (size_t)len);
  r->pending_len[tail] = len;
  r->pending_count++;
  r->pending_bytes += (uint64_t)len;
  r->note_enqueue(r);
  return dropped;
}

/* sends what it can without blocking. stops at first still-full result.
   real send error tears down, schedules reconnect */
void srtout_flush_pending(srtout_t *r) {
  while (r->pending_count > 0 && r->sock != SRT_INVALID_SOCK) {
    int head = r->pending_head;
    int sent = srt_sendmsg2(r->sock, (const char *)pending_slot(r, head), r->pending_len[head], NULL);

    if (sent == SRT_ERROR) {
      if (srt_getlasterror(NULL) == SRT_EASYNCSND)
        return;
      teardown_for_reconnect(r, "write failed on queued data");
      return;
    }
    r->pending_bytes -= (uint64_t)r->pending_len[head];
    r->pending_head = (head + 1) % r->pending_cap;
    r->pending_count--;
  }
}

/* MemAvailable: safe estimate, skips reclaimable cache. -1: /proc/meminfo unreadable */
static long read_ram_bytes(void) {
  FILE *f = fopen("/proc/meminfo", "r");
  unsigned long kb = 0;
  char line[128];

  if (!f)
    return -1;
  while (fgets(line, sizeof line, f)) {
    if (sscanf(line, "MemAvailable: %lu kB", &kb) == 1)
      break;
  }
  fclose(f);
  return kb ? (long)kb * 1024 : -1;
}

/* only feeds a soft ceiling, cached SRTOUT_RAM_CHECK_INTERVAL_S */
static long available_ram_bytes(srtout_t *r, double now) {
  if (r->ram_checked_at == 0.0 || now - r->ram_checked_at >= SRTOUT_RAM_CHECK_INTERVAL_S) {
    r->ram_cached_bytes = read_ram_bytes();
    r->ram_checked_at = now;
  }
  return r->ram_cached_bytes;
}

static int pending_target_chunks(srtout_t *r, double now) {
  double bps = r->bitrate_ema_bps > 0 ? r->bitrate_ema_bps : (double)SRTOUT_INITIAL_BITRATE_BPS;
  unsigned latency_ms = r->cfg.opts.latency_ms ? r->cfg.opts.latency_ms : SRTOUT_DEFAULT_LATENCY_MS;
  double floor_bytes = (double)SRTOUT_PENDING_FLOOR_CHUNKS * SRTOUT_PLSIZE;
  double target_bytes = bps * (latency_ms / 1000.0) * r->safety_mult;
  long ram = available_ram_bytes(r, now);
  long ceiling_bytes = ram > 0 ? ram / SRTOUT_RAM_FRACTION_DIVISOR : -1;

  if (target_bytes < floor_bytes)
    target_bytes = floor_bytes;
  if (ceiling_bytes > 0 && target_bytes > (double)ceiling_bytes)
    target_bytes = (double)ceiling_bytes;
  if (target_bytes < floor_bytes)
    target_bytes = floor_bytes; /* RAM tighter than floor: keep floor anyway */
  return (int)(target_bytes / SRTOUT_PLSIZE);
}

/* retargets capacity, never below what's queued: low instant bitrate
   (finite source draining) must not discard backlog still catching up on link.
   malloc failure: keeps current buffer, retries next tick */
static void resize_pending(srtout_t *r, int new_cap) {
  unsigned char *new_pending;
  int *new_len;

  if (new_cap < r->pending_count)
    new_cap = r->pending_count;
  if (new_cap == r->pending_cap)
    return;
  new_pending = malloc((size_t)new_cap * SRTOUT_PLSIZE);
  new_len = malloc((size_t)new_cap * sizeof *new_len);
  if (!new_pending || !new_len) {
    log_code("LE500167", "srt output: pending buffer resize to %d chunks failed, keeping %d", new_cap, r->pending_cap);
    free(new_pending);
    free(new_len);
    return;
  }
  for (int i = 0; i < r->pending_count; i++) {
    int src = (r->pending_head + i) % r->pending_cap;
    memcpy(new_pending + (size_t)i * SRTOUT_PLSIZE, pending_slot(r, src), (size_t)r->pending_len[src]);
    new_len[i] = r->pending_len[src];
  }
  free(r->pending);
  free(r->pending_len);
  r->pending = new_pending;
  r->pending_len = new_len;
  r->pending_cap = new_cap;
  r->pending_head = 0;
}

/* re-estimates input bitrate (EMA), retargets pending buffer to
   safety_mult latency-windows at that rate, bounded by RAM headroom.
   hysteresis: small target drift does not realloc */
void resize_pending_if_needed(srtout_t *r) {
  double now = mono_seconds();
  int target;

  if (r->last_check_at > 0 && now - r->last_check_at < SRTOUT_RESIZE_INTERVAL_S)
    return;
  if (r->last_check_at > 0) {
    double inst = (double)r->bytes_since_check / (now - r->last_check_at);
    r->bitrate_ema_bps = r->bitrate_ema_bps > 0 ? r->bitrate_ema_bps + SRTOUT_BITRATE_EMA_ALPHA * (inst - r->bitrate_ema_bps)
                                                : inst;
  }
  r->bytes_since_check = 0;
  r->last_check_at = now;
  target = pending_target_chunks(r, now);
  if (target > r->pending_cap + r->pending_cap * SRTOUT_GROW_PCT / 100 ||
      target < r->pending_cap * SRTOUT_SHRINK_PCT / 100)
    resize_pending(r, target);
}
