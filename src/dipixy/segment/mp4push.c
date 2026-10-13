/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "mp4push.h"
#include "priv.h"
#include "../altsvc.h"
#include "../hls/hls.h"
#include "../version.h"
#include "../ws/ws_clients.h"

#include "lib/helper/byte_ring.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MP4PUSH_MAX_SUBS 4096
#define MP4PUSH_SUBS_HEADROOM 16
#define MP4PUSH_SUBS_FLOOR 32
#define MP4PUSH_MAX_REACTOR_THREADS 32
#define MP4PUSH_RING_BYTES (1u << 19)
#define MP4PUSH_SUB_FREE 0
#define MP4PUSH_SUB_ALIVE 1
#define MP4PUSH_SUB_CLOSING 2

typedef struct {
  _Atomic int alive;
  capture_ctx_t *cap_ctx;
  pid_filter_t filter;
  unsigned pmt_pid;
  lcevc_select_t lcevc;
  conn_proto_t proto;
  int fd;
  uint64_t conn_gen; /* H1: conn ident, fd may be reused after teardown */
  void *h2c;
  void *h2_slot;
  void *h3c;
  int64_t h3_sid;
  int ws_handle;
  _Atomic int reactor_tid;
  byte_ring_t ring;
  _Atomic int ring_errored;
  _Atomic int seg_next;
  int tid_next;
} mp4push_sub_t;

static mp4push_sub_t *g_subs;
static int g_subs_n;
static int g_reactor_efds[MP4PUSH_MAX_REACTOR_THREADS];
static int g_tid_head[MP4PUSH_MAX_REACTOR_THREADS];
static int *g_free_slots;
static int g_free_slots_n;
static pthread_mutex_t g_free_lock = PTHREAD_MUTEX_INITIALIZER;

static void free_slot(int idx) {
  atomic_store_explicit(&g_subs[idx].alive, MP4PUSH_SUB_FREE, memory_order_release);
  pthread_mutex_lock(&g_free_lock);
  g_free_slots[g_free_slots_n++] = idx;
  pthread_mutex_unlock(&g_free_lock);
}

static void wake_reactor(int tid) {
  int efd;
  if (tid < 0 || tid >= MP4PUSH_MAX_REACTOR_THREADS) return;
  efd = __atomic_load_n(&g_reactor_efds[tid], __ATOMIC_RELAXED);
  if (efd >= 0) {
    uint64_t one = 1;
    ssize_t r = write(efd, &one, sizeof one);
    (void)r;
  }
}

static void ring_enqueue(mp4push_sub_t *s, const uint8_t *data, size_t len) {
  if (!byte_ring_write(&s->ring, data, len)) {
    atomic_store_explicit(&s->ring_errored, 1, memory_order_release);
    return;
  }
  ws_clients_add_bytes(s->ws_handle, len);
}

static void link_tid_chain(int idx, int tid) {
  if (tid < 0 || tid >= MP4PUSH_MAX_REACTOR_THREADS) return;
  g_subs[idx].tid_next = g_tid_head[tid];
  g_tid_head[tid] = idx;
}

static void unlink_tid_chain(int idx) {
  const mp4push_sub_t *s = &g_subs[idx];
  int tid = atomic_load_explicit(&s->reactor_tid, memory_order_relaxed);
  int cur;
  if (tid < 0 || tid >= MP4PUSH_MAX_REACTOR_THREADS) return;
  if (g_tid_head[tid] == idx) {
    g_tid_head[tid] = s->tid_next;
    return;
  }
  cur = g_tid_head[tid];
  while (cur != -1) {
    mp4push_sub_t *cs = &g_subs[cur];
    if (cs->tid_next == idx) {
      cs->tid_next = s->tid_next;
      return;
    }
    cur = cs->tid_next;
  }
}

static void seg_link(hls_seg_ctx_t *s, int idx) {
  int old_head = atomic_load_explicit(&s->mp4push_sub_head, memory_order_relaxed);
  atomic_store_explicit(&g_subs[idx].seg_next, old_head, memory_order_relaxed);
  atomic_store_explicit(&s->mp4push_sub_head, idx, memory_order_release);
}

static void seg_unlink(hls_seg_ctx_t *s, int idx) {
  int next = atomic_load_explicit(&g_subs[idx].seg_next, memory_order_relaxed);
  int cur = atomic_load_explicit(&s->mp4push_sub_head, memory_order_relaxed);
  if (cur == idx) {
    atomic_store_explicit(&s->mp4push_sub_head, next, memory_order_relaxed);
    return;
  }
  while (cur != -1) {
    mp4push_sub_t *cs = &g_subs[cur];
    int cs_next = atomic_load_explicit(&cs->seg_next, memory_order_relaxed);
    if (cs_next == idx) {
      atomic_store_explicit(&cs->seg_next, next, memory_order_relaxed);
      return;
    }
    cur = cs_next;
  }
}

void mp4push_deliver(const hls_seg_ctx_t *s, const unsigned char *data, size_t len) {
  int i = atomic_load_explicit(&s->mp4push_sub_head, memory_order_acquire);
  while (i != -1) {
    mp4push_sub_t *sub = &g_subs[i];
    int next = atomic_load_explicit(&sub->seg_next, memory_order_relaxed);
    if (atomic_load_explicit(&sub->alive, memory_order_acquire) == MP4PUSH_SUB_ALIVE) {
      if (sub->proto == CONN_PROTO_H1) {
        conn_t *c = conn_for_fd_gen(sub->fd, sub->conn_gen);
        if (c) {
          conn_send_buffered(c, data, len, NULL, 0);
          ws_clients_add_bytes(sub->ws_handle, len);
        }
      } else {
        ring_enqueue(sub, data, len);
        wake_reactor(atomic_load_explicit(&sub->reactor_tid, memory_order_acquire));
      }
    }
    i = next;
  }
}

void mp4push_init(int max_clients) {
  int n = (max_clients > 0 ? max_clients : 0) + MP4PUSH_SUBS_HEADROOM;
  if (n < MP4PUSH_SUBS_FLOOR) n = MP4PUSH_SUBS_FLOOR;
  if (n > MP4PUSH_MAX_SUBS) n = MP4PUSH_MAX_SUBS;
  g_subs = calloc((size_t)n, sizeof *g_subs);
  if (!g_subs) {
    log_code("YS500215", TOOL_NAME ": out of memory sizing mp4push subscriber table (%d entries)", n);
    return;
  }
  g_subs_n = n;
  for (int i = 0; i < MP4PUSH_MAX_REACTOR_THREADS; i++) {
    g_reactor_efds[i] = -1;
    g_tid_head[i] = -1;
  }
  g_free_slots = malloc(sizeof(int) * (size_t)n);
  if (g_free_slots) {
    for (int i = 0; i < n; i++) g_free_slots[i] = i;
    g_free_slots_n = n;
  }
}

static int subscribe_slot(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, conn_proto_t proto, int fd, uint64_t conn_gen, int ws_handle) {
  hls_seg_ctx_t *s;
  int idx = -1;
  pthread_mutex_lock(&g_free_lock);
  if (g_free_slots_n > 0) idx = g_free_slots[--g_free_slots_n];
  pthread_mutex_unlock(&g_free_lock);
  if (idx < 0) return -1;

  g_subs[idx].cap_ctx = ctx;
  g_subs[idx].filter = *filter;
  g_subs[idx].pmt_pid = pmt_pid;
  g_subs[idx].lcevc = *lcevc;
  g_subs[idx].proto = proto;
  g_subs[idx].fd = fd;
  g_subs[idx].conn_gen = conn_gen;
  g_subs[idx].h2c = NULL;
  g_subs[idx].h2_slot = NULL;
  g_subs[idx].h3c = NULL;
  g_subs[idx].h3_sid = -1;
  g_subs[idx].ws_handle = ws_handle;
  atomic_store_explicit(&g_subs[idx].reactor_tid, -1, memory_order_relaxed);
  g_subs[idx].tid_next = -1;
  atomic_store_explicit(&g_subs[idx].ring_errored, 0, memory_order_relaxed);
  if (proto == CONN_PROTO_H2 || proto == CONN_PROTO_H3) {
    hls_resp_t resp;
    byte_ring_reset(&g_subs[idx].ring, MP4PUSH_RING_BYTES);
    if (!g_subs[idx].ring.buf) {
      free_slot(idx);
      return -1;
    }
    if (hls_render(ctx, filter, pmt_pid, lcevc, SEG_CONTAINER_FMP4, "init.mp4", 0, NULL, &resp) && resp.status == 200) {
      ring_enqueue(&g_subs[idx], resp.body, resp.body_len);
      hls_resp_body_release(resp.body, resp.owner, resp.zc);
    }
  }

  atomic_store_explicit(&g_subs[idx].alive, MP4PUSH_SUB_ALIVE, memory_order_release);
  hls_seg_registry_lock();
  s = hls_seg_find_locked(ctx, filter, pmt_pid, lcevc, SEG_CONTAINER_FMP4);
  if (s) seg_link(s, idx);
  hls_seg_registry_unlock();
  if (!s) {
    free_slot(idx);
    return -1;
  }
  return idx;
}

int mp4push_subscribe(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, conn_proto_t proto) {
  return subscribe_slot(ctx, filter, pmt_pid, lcevc, proto, -1, 0, -1);
}

int mp4push_try_attach(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, int ws_handle) {
  char hdr[192];
  sbuf_t b;
  hls_resp_t resp;
  int idx;
  sbuf_init(&b, hdr, sizeof hdr);
  sbuf_add(&b, "HTTP/1.1 200 OK\r\nServer: " TOOL_NAME "/" TOOL_VERSION "\r\nContent-Type: video/mp4\r\nConnection: close\r\n");
  sbuf_add(&b, altsvc_h1_line(c->ssl != NULL));
  sbuf_add(&b, "\r\n");
  conn_queue(c, hdr, b.len);
  if (hls_render(ctx, filter, pmt_pid, lcevc, SEG_CONTAINER_FMP4, "init.mp4", 0, NULL, &resp) && resp.status == 200) {
    conn_queue(c, resp.body, resp.body_len);
    hls_resp_body_release(resp.body, resp.owner, resp.zc);
  }
  idx = subscribe_slot(ctx, filter, pmt_pid, lcevc, CONN_PROTO_H1, c->fd, atomic_load_explicit(&c->gen, memory_order_relaxed), ws_handle);
  if (idx < 0) return 0;
  c->slot = idx;
  c->next_state = CONN_NEXT_MP4PUSH;
  return 1;
}

void mp4push_h2_bind(int slot, void *h2c, void *h2_slot, int reactor_tid, int ws_handle) {
  if (slot < 0 || slot >= g_subs_n) return;
  g_subs[slot].h2c = h2c;
  g_subs[slot].h2_slot = h2_slot;
  atomic_store_explicit(&g_subs[slot].reactor_tid, reactor_tid, memory_order_release);
  g_subs[slot].ws_handle = ws_handle;
  link_tid_chain(slot, reactor_tid);
}

void *mp4push_sub_h2c(int slot) {
  if (slot < 0 || slot >= g_subs_n) return NULL;
  return g_subs[slot].h2c;
}

void *mp4push_sub_h2_slot(int slot) {
  if (slot < 0 || slot >= g_subs_n) return NULL;
  return g_subs[slot].h2_slot;
}

void mp4push_h3_bind(int slot, void *h3c, int64_t h3_sid, int reactor_tid, int ws_handle) {
  if (slot < 0 || slot >= g_subs_n) return;
  g_subs[slot].h3c = h3c;
  g_subs[slot].h3_sid = h3_sid;
  atomic_store_explicit(&g_subs[slot].reactor_tid, reactor_tid, memory_order_release);
  g_subs[slot].ws_handle = ws_handle;
  link_tid_chain(slot, reactor_tid);
}

void *mp4push_sub_h3c(int slot) {
  if (slot < 0 || slot >= g_subs_n) return NULL;
  return g_subs[slot].h3c;
}

int64_t mp4push_sub_h3_sid(int slot) {
  if (slot < 0 || slot >= g_subs_n) return -1;
  return g_subs[slot].h3_sid;
}

size_t mp4push_ring_read(int slot, uint8_t *buf, size_t maxlen) {
  if (slot < 0 || slot >= g_subs_n) return 0;
  return byte_ring_read(&g_subs[slot].ring, buf, maxlen);
}

const uint8_t *mp4push_ring_peek(int slot, size_t *len) {
  if (slot < 0 || slot >= g_subs_n) {
    *len = 0;
    return NULL;
  }
  return byte_ring_peek(&g_subs[slot].ring, len);
}

void mp4push_ring_hold(int slot, size_t n) {
  if (slot < 0 || slot >= g_subs_n) return;
  byte_ring_hold(&g_subs[slot].ring, n);
}

void mp4push_ring_ack(int slot, size_t n) {
  if (slot < 0 || slot >= g_subs_n) return;
  byte_ring_ack(&g_subs[slot].ring, n);
}

int mp4push_ring_pending(int slot) {
  const mp4push_sub_t *s;
  if (slot < 0 || slot >= g_subs_n) return 0;
  s = &g_subs[slot];
  return byte_ring_unsent(&s->ring) != 0;
}

int mp4push_ring_errored(int slot) {
  if (slot < 0 || slot >= g_subs_n) return 0;
  return atomic_load_explicit(&g_subs[slot].ring_errored, memory_order_acquire);
}

void mp4push_register_reactor_efd(int tid, int efd) {
  if (tid < 0 || tid >= MP4PUSH_MAX_REACTOR_THREADS) return;
  __atomic_store_n(&g_reactor_efds[tid], efd, __ATOMIC_RELAXED);
}

void mp4push_close_reactor_efds(void) {
  for (int i = 0; i < MP4PUSH_MAX_REACTOR_THREADS; i++) {
    int efd = __atomic_exchange_n(&g_reactor_efds[i], -1, __ATOMIC_RELAXED);
    if (efd >= 0) close(efd);
  }
}

void mp4push_flush_ready(int tid) {
  int i;
  if (tid < 0 || tid >= MP4PUSH_MAX_REACTOR_THREADS) return;
  i = g_tid_head[tid];
  while (i != -1) {
    const mp4push_sub_t *s = &g_subs[i];
    int next = s->tid_next;
    if (atomic_load_explicit(&s->alive, memory_order_relaxed) == MP4PUSH_SUB_ALIVE &&
        (mp4push_ring_pending(i) || atomic_load_explicit(&s->ring_errored, memory_order_acquire))) {
#ifdef HAVE_HTTP2
      if (s->proto == CONN_PROTO_H2) h2_mp4push_wake(i);
#endif
#ifdef HAVE_HTTP3
      if (s->proto == CONN_PROTO_H3) h3_mp4push_wake(i);
#endif
    }
    i = next;
  }
}

static void mp4push_finish_sub_close(void *arg) {
  int slot = (int)(intptr_t)arg;
  const mp4push_sub_t *s = &g_subs[slot];
  if (s->proto == CONN_PROTO_H2 || s->proto == CONN_PROTO_H3) unlink_tid_chain(slot);
  free_slot(slot);
}

void mp4push_sub_close(int slot) {
  mp4push_sub_t *s;
  hls_seg_ctx_t *seg;
  int expected;
  if (slot < 0 || slot >= g_subs_n) return;
  s = &g_subs[slot];
  expected = MP4PUSH_SUB_ALIVE;
  if (!atomic_compare_exchange_strong_explicit(&s->alive, &expected, MP4PUSH_SUB_CLOSING, memory_order_acquire, memory_order_relaxed)) return;
  hls_seg_registry_lock();
  seg = hls_seg_find_locked(s->cap_ctx, &s->filter, s->pmt_pid, &s->lcevc, SEG_CONTAINER_FMP4);
  if (seg) seg_unlink(seg, slot);
  hls_seg_registry_unlock();
  if (!capture_defer_after_quiescent(mp4push_finish_sub_close, (void *)(intptr_t)slot)) {
    capture_wait_pumps_quiescent();
    mp4push_finish_sub_close((void *)(intptr_t)slot);
  }
}
