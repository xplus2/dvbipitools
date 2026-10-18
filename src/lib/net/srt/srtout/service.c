/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <srt/srt.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/sys/signal.h"
#include "lib/net/srt/srtcommon.h"
#include "lib/net/srt/srtout.h"
#include "lib/net/srt/srtout/srtout_int.h"

static uint64_t queue_ms(const srtout_t *r) {
  if (r->bitrate_ema_bps <= 0) return 0;
  return (uint64_t)((double)r->pending_bytes * 1000.0 / r->bitrate_ema_bps);
}

/* stats push covers whole connection/group, not per bonded member */
static void push_stats(srtout_t *r) {
  SRT_TRACEBSTATS st;
  int have;
  size_t n = 0;
  if (!r->mx || !metrics_exporter_due(r->mx, mono_seconds())) return;
  memset(&st, 0, sizeof st);
  have = r->sock != SRT_INVALID_SOCK && srt_bstats(r->sock, &st, 0) == 0;
  metrics_entry_t e[] = {
{METRICS_ID_SRT_SENDER_SENT_TOTAL, r->peer_label, (uint64_t)st.pktSentTotal},
{METRICS_ID_SRT_SENDER_RETRANSMITTED_TOTAL, r->peer_label, (uint64_t)st.pktRetransTotal},
{METRICS_ID_SRT_SENDER_RTT_MILLISECONDS, r->peer_label, (uint64_t)st.msRTT},
{METRICS_ID_SRT_SENDER_LOST_TOTAL, r->peer_label, (uint64_t)st.pktSndLossTotal},
{METRICS_ID_SRT_SENDER_DROPPED_TOTAL, r->peer_label, (uint64_t)st.pktSndDropTotal},
  };
  metrics_entry_t q[] = {
{METRICS_ID_SRT_SENDER_QUEUE_CHUNKS, r->peer_label, (uint64_t)r->pending_count},
{METRICS_ID_SRT_SENDER_QUEUE_CAPACITY_CHUNKS, r->peer_label, (uint64_t)r->pending_cap},
{METRICS_ID_SRT_SENDER_QUEUE_HIGH_WATERMARK_CHUNKS, r->peer_label, (uint64_t)r->pending_hwm},
{METRICS_ID_SRT_SENDER_QUEUE_DROPPED_CHUNKS_TOTAL, r->peer_label, r->queue_dropped},
{METRICS_ID_SRT_SENDER_QUEUE_MILLISECONDS, r->peer_label, queue_ms(r)},
  };
  metrics_entry_t all[sizeof e / sizeof e[0] + sizeof q / sizeof q[0]];
  if (have) {
    n = sizeof e / sizeof e[0];
    memcpy(all, e, sizeof e);
  }
  if (r->queue_metrics != SRT_QUEUE_METRICS_OFF) {
    size_t nq = r->queue_metrics == SRT_QUEUE_METRICS_FULL ? 4 : 2;
    memcpy(all + n, q, nq * sizeof q[0]);
    n += nq;
    if (r->bitrate_ema_bps > 0) all[n++] = q[4];
  }
  metrics_push_entries(r->mx, r->tool_version, all, n);
}

/* libsrt never redials a broken member of a live group */
static void revive_members(srtout_t *r) {
  double now = mono_seconds();

  if (r->cfg.group_mode == SRTGROUP_NONE || !r->connected)
    return;
  if (!r->revive_res) {
    if (now < r->next_revive_at) return;
    r->next_revive_at = now + SRTOUT_REVIVE_INTERVAL_S;
  }
  srtcommon_group_revive(r->sock, r->cfg.peers, r->cfg.npeers, &r->revive_res);
}

static int send_chunk(srtout_t *r, const unsigned char *p, size_t len);

void flush_carry(srtout_t *r) {
  if (r->carry_len > 0) send_chunk(r, r->carry, (size_t)r->carry_len);
  r->carry_len = 0;
}

void service_step(srtout_t *r) {
  if (r->carry_len > 0 && mono_seconds() - r->carry_since >= SRTOUT_CARRY_MAX_S) flush_carry(r);
  resize_pending_if_needed(r);
  if (r->sock == SRT_INVALID_SOCK) {
    if ((r->reconnect_res || mono_seconds() >= r->next_reconnect_at) && start_connect(r, 0) < 0)
      r->next_reconnect_at = mono_seconds() + SRTOUT_RECONNECT_BACKOFF_S;
  } else {
    SRT_EPOLL_EVENT ev;
    if (srt_epoll_uwait(r->eid, &ev, 1, 0) > 0 && ev.fd == r->sock) {
      if (ev.events & SRT_EPOLL_OUT) {
        r->connected = 1;
        srtout_flush_pending(r);
      }
      if (ev.events & SRT_EPOLL_ERR) {
        /* ERR can be stale from connect race. confirm broken before teardown */
        SRT_SOCKSTATUS st = srt_getsockstate(r->sock);
        if (st == SRTS_BROKEN || st == SRTS_CLOSED || st == SRTS_NONEXIST)
          teardown_for_reconnect(r, "link failed");
      }
    }
    if (r->sock != SRT_INVALID_SOCK)
      revive_members(r);
  }
}

void srtout_service(srtout_t *r, srtout_status_t *out) {
  service_step(r);
  push_stats(r);
  out->connected = r->connected;
}

/* 0 ok, -1 if send failed or a chunk was dropped */
static int send_chunk(srtout_t *r, const unsigned char *p, size_t len) {
  int rc = 0;

  if (r->pending_count == 0 && r->connected) {
    int sent = srt_sendmsg2(r->sock, (const char *)p, (int)len, NULL);
    if (sent != SRT_ERROR) return 0;
    if (srt_getlasterror(NULL) != SRT_EASYNCSND) {
      teardown_for_reconnect(r, "write failed");
      rc = -1;
    }
  }
  if (enqueue_pending(r, p, (int)len)) rc = -1;
  return rc;
}

int srtout_write(srtout_t *r, const unsigned char *buf, size_t n) {
  int rc = 0;
  size_t off = 0;

  r->bytes_since_check += n;
  if (r->carry_len > 0) {
    size_t take = (size_t)(SRTOUT_PLSIZE - r->carry_len);
    if (take > n) take = n;
    memcpy(r->carry + r->carry_len, buf, take);
    r->carry_len += (int)take;
    off = take;
    if (r->carry_len < SRTOUT_PLSIZE) return 0;
    rc = send_chunk(r, r->carry, SRTOUT_PLSIZE);
    r->carry_len = 0;
  }
  for (; n - off >= (size_t)SRTOUT_PLSIZE; off += (size_t)SRTOUT_PLSIZE)
    if (send_chunk(r, buf + off, SRTOUT_PLSIZE)) rc = -1;
  if (off < n) {
    memcpy(r->carry, buf + off, n - off);
    r->carry_since = mono_seconds();
    r->carry_len = (int)(n - off);
  }
  return rc;
}

void srtout_queue_stats(const srtout_t *r, srtout_queue_stats_t *out) {
  out->peer_label = r->peer_label;
  out->chunks = r->pending_count;
  out->capacity = r->pending_cap;
  out->high_watermark = r->pending_hwm;
  out->dropped = r->queue_dropped;
  out->ms = queue_ms(r);
  out->ms_known = r->bitrate_ema_bps > 0;
}

void srtout_link_stats(const srtout_t *r, srtout_link_stats_t *out) {
  SRT_TRACEBSTATS st;

  memset(out, 0, sizeof *out);
  if (r->sock == SRT_INVALID_SOCK || srt_bstats(r->sock, &st, 0) != 0) return;
  out->connected = 1;
  out->packets = (uint64_t)st.pktSentTotal;
  out->retransmitted = (uint64_t)st.pktRetransTotal;
  out->lost = (uint64_t)st.pktSndLossTotal;
  out->dropped = (uint64_t)st.pktSndDropTotal;
  out->rtt_ms = st.msRTT;
  out->mbps = st.mbpsSendRate;
}
