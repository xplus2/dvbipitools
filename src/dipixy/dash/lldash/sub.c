/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dipixy/dash/lldash.h"
#include "dipixy/dash/dash_int.h"
#include "dipixy/altsvc.h"
#include "dipixy/reactor/internal.h"
#include "dipixy/version.h"
#include "dipixy/ws/ws_clients.h"
#include "lib/helper/byte_ring.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "dipixy/dash/lldash/lldash_int.h"

int sub_matches(const dashchunk_sub_t *s, const hls_store_t *store, uint32_t seq) {
  return s->cap_ctx == store->cap_ctx && s->pmt_pid == store->pmt_pid && s->want_seg == seq && pid_filter_equal(&s->filter, &store->filter) && lcevc_select_equal(&s->lcevc, &store->lcevc);
}

static char *write_hex(char *dst, size_t v) {
  static const char digits[] = "0123456789abcdef";
  char tmp[16];
  int n = 0;
  do {
    tmp[n++] = digits[v & 0xf];
    v >>= 4;
  } while (v);
  for (int i = 0; i < n; i++) *dst++ = tmp[n - 1 - i];
  return dst;
}

/* HTTP/1.1 chunk framing: "<hex size>\r\n<data>\r\n". cross-thread, matches ts_push's conn_send_buffered usage */
static void send_chunk(int fd, uint64_t gen, int ws_handle, const uint8_t *data, size_t len) {
  conn_t *c;
  char hdr[24];
  char *p = hdr;
  c = conn_for_fd_gen(fd, gen);
  if (!c) return;
  p = write_hex(p, len);
  *p++ = '\r';
  *p++ = '\n';
  if (len) conn_send_buffered3(c, hdr, (size_t)(p - hdr), data, len, "\r\n", 2);
  else conn_send_buffered(c, hdr, (size_t)(p - hdr), "\r\n", 2);
  ws_clients_add_bytes(ws_handle, len);
}

static void h1_wake(int fd, uint64_t gen) {
  conn_t *c = conn_for_fd_gen(fd, gen);
  if (c) conn_send_buffered(c, NULL, 0, NULL, 0); /* re-arm EPOLLOUT, flush rechecks finalized */
}

static void wake_reactor(int tid) {
  int efd;
  if (tid < 0 || tid >= DASHCHUNK_MAX_REACTOR_THREADS) return;
  efd = __atomic_load_n(&g_lldash_reactor_efds[tid], __ATOMIC_RELAXED);
  if (efd >= 0) {
    uint64_t one = 1;
    ssize_t r = write(efd, &one, sizeof one);
    (void)r;
  }
}

static void ring_enqueue(dashchunk_sub_t *s, const uint8_t *data, size_t len) {
  if (!byte_ring_write(&s->ring, data, len)) {
    atomic_store_explicit(&s->ring_errored, 1, memory_order_release);
    return;
  }
  ws_clients_add_bytes(s->ws_handle, len);
}

/* sub->lock held */
void sub_emit(dashchunk_sub_t *s, const uint8_t *data, size_t len) {
  if (s->proto == CONN_PROTO_H1) {
    send_chunk(s->fd, s->conn_gen, s->ws_handle, data, len);
  } else {
    ring_enqueue(s, data, len);
    wake_reactor(s->reactor_tid);
  }
}

/* sub->lock held. finalized only after terminator is queued */
void sub_end(dashchunk_sub_t *s) {
  if (s->ended) return;
  s->ended = 1;
  if (s->proto == CONN_PROTO_H1) {
    send_chunk(s->fd, s->conn_gen, s->ws_handle, NULL, 0);
    atomic_store_explicit(&s->finalized, 1, memory_order_release);
    h1_wake(s->fd, s->conn_gen);
  } else {
    atomic_store_explicit(&s->finalized, 1, memory_order_release);
    wake_reactor(s->reactor_tid);
  }
}

/* sub->lock held. kill stream without terminator */
void sub_abort(dashchunk_sub_t *s) {
  if (s->ended && s->proto == CONN_PROTO_H1) return; /* h2/h3 flag errored even once finished */
  s->ended = 1;
  if (s->proto == CONN_PROTO_H1) {
    conn_t *c = conn_for_fd_gen(s->fd, s->conn_gen);
    if (c) conn_request_close(c);
    atomic_store_explicit(&s->finalized, 1, memory_order_release);
    h1_wake(s->fd, s->conn_gen);
  } else {
    atomic_store_explicit(&s->ring_errored, 1, memory_order_release);
    atomic_store_explicit(&s->finalized, 1, memory_order_release);
    wake_reactor(s->reactor_tid);
  }
}

/* sub->lock held, consumer ready. replays parts missed since subscribe from snapshot */
static void sub_catch_up(dashchunk_sub_t *s) {
  const hls_snapshot_t *snap;
  const hls_parts_t *parts;
  const uint8_t *base;
  if (s->aborted) {
    sub_abort(s);
    return;
  }
  snap = atomic_load_explicit(&s->store->snap, memory_order_acquire);
  if (!snap) {
    sub_abort(s);
    return;
  }
  if (snap->live_msn == s->want_seg) {
    parts = &snap->live_parts;
    base = snap->live_data;
  } else {
    const hls_seg_t *seg = NULL;
    uint32_t off = s->want_seg - snap->oldest_seq;
    if (snap->count > 0 && s->want_seg >= snap->oldest_seq && off < (uint32_t)snap->count) seg = &snap->ring->segs[(snap->head + (int)off) % HLS_MAX_SEGS];
    if (!seg) {
      sub_abort(s);
      return;
    }
    parts = &seg->parts;
    base = seg->data;
  }
  for (int i = s->sent; i < parts->count; i++) sub_emit(s, base + parts->offset[i], parts->size[i]);
  if (parts->count > s->sent) s->sent = parts->count;
  if (snap->live_msn != s->want_seg) sub_end(s);
}

void sub_attach(int idx) {
  dashchunk_sub_t *s = &g_lldash_subs[idx];
  pthread_mutex_lock(&s->lock);
  s->attached = 1;
  sub_catch_up(s);
  pthread_mutex_unlock(&s->lock);
}
