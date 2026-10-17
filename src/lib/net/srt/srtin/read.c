/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <unistd.h>
#include <srt/srt.h>

#include "lib/helper/log.h"
#include "lib/sys/signal.h"
#include "lib/net/srt/srtcommon.h"
#include "lib/net/srt/srtin.h"
#include "lib/net/srt/srtin/srtin_int.h"

/* no stats without live socket: header and extras still go out */
static void push_stats(srtin_t *r) {
  SRT_TRACEBSTATS st;
  int have;

  if (!r->mx || !metrics_exporter_due(r->mx, mono_seconds())) return;
  have = r->sock != SRT_INVALID_SOCK && srt_bstats(r->sock, &st, 0) == 0;
  if (!have) {
    metrics_push_entries(r->mx, r->tool_version, NULL, 0);
    return;
  }
  metrics_entry_t e[] = {
  {METRICS_ID_SRT_RECEIVER_RECEIVED_TOTAL, NULL, (uint64_t)st.pktRecvTotal},
  {METRICS_ID_SRT_RECEIVER_LOST_TOTAL, NULL, (uint64_t)st.pktRcvLossTotal},
  {METRICS_ID_SRT_RECEIVER_DROPPED_TOTAL, NULL, (uint64_t)st.pktRcvDropTotal},
  {METRICS_ID_SRT_RECEIVER_RTT_MILLISECONDS, NULL, (uint64_t)st.msRTT},
  {METRICS_ID_SRT_RECEIVER_BUFFER_MILLISECONDS, NULL, (uint64_t)st.msRcvTsbPdDelay},
  };
  metrics_push_entries(r->mx, r->tool_version, e, sizeof e / sizeof e[0]);
}

void idle_wait(void) {
  int wfd = signal_wake_fd();
  if (wfd >= 0) {
    struct pollfd pfd = {wfd, POLLIN, 0};
    poll(&pfd, 1, SRT_RCVTIMEO_MS);
  } else {
    struct timespec ts = {0, SRT_RCVTIMEO_MS * 1000000L};
    nanosleep(&ts, NULL);
  }
}

/* 1: r->sock live again */
static int reconnect_once(srtin_t *r) {
  if (r->group_mode != SRTGROUP_NONE && r->n_listeners > 0) {
    r->sock = srt_accept_bond(r->listeners, r->n_listeners, SRT_RCVTIMEO_MS);
    if (r->sock != SRT_INVALID_SOCK) return 1;
    if (srt_getlasterror(NULL) != SRT_ETIMEOUT) log_code("LW600141", "srt: reconnect attempt failed: %s", srt_getlasterror_str());
    return 0;
  }
  if (r->group_mode != SRTGROUP_NONE) {
    r->sock = open_group_caller(&r->cfg, NULL);
    if (r->sock == SRT_INVALID_SOCK) idle_wait();
    return r->sock != SRT_INVALID_SOCK;
  }
  if (r->n_listeners > 0) {
    if (accept_poll(r->listeners[0], &r->sock, r->busy) < 0) idle_wait();
    return r->sock != SRT_INVALID_SOCK;
  }
  r->sock = open_single_caller(&r->cfg, NULL);
  if (r->sock == SRT_INVALID_SOCK) idle_wait();
  return r->sock != SRT_INVALID_SOCK;
}

static int read_once(srtin_t *r, unsigned char *buf, size_t cap, int *reconnected_out) {
  int n;

  *reconnected_out = 0;

  /* mid-reconnect: one attempt per call, returns like a timeout. caller's loop drives retry cadence, !internal */
  if (r->sock == SRT_INVALID_SOCK) {
    if (stop_wanted(&r->cfg)) return 0;
    if (!reconnect_once(r)) return 0;
    *reconnected_out = 1;
    return 0;
  }

  if (r->group_mode != SRTGROUP_NONE && r->n_listeners == 0) {
    /* libsrt never re-dials a broken member of a live group */
    double now = mono_seconds();
    if (r->revive_res || now >= r->next_revive_at) {
      if (!r->revive_res) r->next_revive_at = now + SRT_REVIVE_INTERVAL_S;
      srtcommon_group_revive(r->sock, r->cfg.peers, r->cfg.npeers, &r->revive_res);
    }
  }

  n = srt_recvmsg2(r->sock, (char *)buf, (int)cap, NULL);
  if (n == SRT_ERROR) {
    int err = srt_getlasterror(NULL);
    if (err == SRT_ETIMEOUT) return 0;
    if (err == SRT_EASYNCRCV) {
      /* group with every member link down right now, not a dead group: retry */
      idle_wait();
      return 0;
    }
    if (r->group_mode != SRTGROUP_NONE) {
      /* group error: state could be corrupted, reconnect regardless of cause.
         next call retries, one attempt each, see above */
      log_code("LW600142", "srt: read failed on group (%s), reconnecting", srt_getlasterror_str());
    } else {
      /* peer gone: listener goes back to accept, caller re-dials */
      log_code("LW600143", "srt: link lost (%s), reconnecting", srt_getlasterror_str());
    }
    srt_close(r->sock);
    r->sock = SRT_INVALID_SOCK;
    atomic_store(r->busy, 0);
    return 0;
  }
  return n;
}

int srtin_read(srtin_t *r, unsigned char *buf, size_t cap, int *reconnected_out) {
  int n = read_once(r, buf, cap, reconnected_out);
  push_stats(r);
  return n;
}

void srtin_stats(const srtin_t *r, srtin_stats_t *out) {
  SRT_TRACEBSTATS st;

  memset(out, 0, sizeof *out);
  if (r->sock == SRT_INVALID_SOCK || srt_bstats(r->sock, &st, 0) != 0) return;
  out->connected = 1;
  out->packets = (uint64_t)st.pktRecvTotal;
  out->lost = (uint64_t)st.pktRcvLossTotal;
  out->dropped = (uint64_t)st.pktRcvDropTotal;
  out->rtt_ms = st.msRTT;
  out->mbps = st.mbpsRecvRate;
}

void srtin_close(srtin_t *r) {
  if (!r) return;
  if (r->sock != SRT_INVALID_SOCK) srt_close(r->sock);
  for (int i = 0; i < r->n_listeners; i++) srt_close(r->listeners[i]);
  srtcommon_resolver_free(r->revive_res);
  srtcommon_cleanup();
  free(r->busy);
  free(r);
}
