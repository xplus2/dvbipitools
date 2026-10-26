/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <srt/srt.h>

#include "lib/net/srt/srtcommon.h"
#include "lib/net/srt/srtout.h"
#include "lib/net/srt/srtout/srtout_int.h"

/* loop may exit before a just-started connect finishes: flush before teardown,
   else pending data drops silently */
static void flush_before_close(srtout_t *r) {
  int waited_ms = 0;
  while (r->pending_count > 0 && waited_ms < SRTOUT_CLOSE_DRAIN_MAX_MS) {
    struct timespec ts = {0, 20L * 1000000L};
    service_step(r);
    if (r->pending_count == 0) break;
    nanosleep(&ts, NULL);
    waited_ms += 20;
  }
}

/* sndbuffer empty != delivered to peer app. measured: single small msg, ~1s real wait needed.
   floor: max(MIN_WAIT_MS, peer latency), capped at DRAIN_MAX_MS */
static void drain_before_close(SRTSOCKET sock) {
  int waited_ms = 0;
  size_t blocks;
  size_t bytes;
  int peer_latency = 0;
  int optlen = sizeof peer_latency;
  int floor_ms = SRTOUT_CLOSE_MIN_WAIT_MS;

  while (srt_getsndbuffer(sock, &blocks, &bytes) == 0 && blocks > 0 && waited_ms < SRTOUT_CLOSE_DRAIN_MAX_MS) {
    struct timespec ts = {0, 20L * 1000000L};
    nanosleep(&ts, NULL);
    waited_ms += 20;
  }
  if (srt_getsockopt(sock, 0, SRTO_PEERLATENCY, &peer_latency, &optlen) == 0 && peer_latency > floor_ms) floor_ms = peer_latency;
  if (floor_ms > SRTOUT_CLOSE_DRAIN_MAX_MS) floor_ms = SRTOUT_CLOSE_DRAIN_MAX_MS;
  if (floor_ms > waited_ms) {
    struct timespec ts;
    int remain_ms = floor_ms - waited_ms;
    ts.tv_sec = remain_ms / 1000;
    ts.tv_nsec = (long)(remain_ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
  }
}

static void close_impl(srtout_t *r, int graceful) {
  if (!r) return;
  if (graceful) {
    flush_carry(r);
    flush_before_close(r);
  }
  if (r->sock != SRT_INVALID_SOCK) {
    if (graceful) drain_before_close(r->sock);
    srt_close(r->sock);
  }
  srt_epoll_release(r->eid);
  srtcommon_resolver_free(r->revive_res);
  srtcommon_resolver_free(r->reconnect_res);
  srtcommon_cleanup();
  free(r->pending);
  free(r->pending_len);
  free(r);
}

void srtout_close(srtout_t *r) {
  close_impl(r, 1);
}

void srtout_close_now(srtout_t *r) {
  close_impl(r, 0);
}
