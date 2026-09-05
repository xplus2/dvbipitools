/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "mp4push.h"
#include "priv.h"
#include "../hls/hls.h"
#include "../version.h"
#include "../ws/ws_clients.h"

#include "lib/helper/log.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MP4PUSH_MAX_SUBS 4096
#define MP4PUSH_SUBS_HEADROOM 16
#define MP4PUSH_SUBS_FLOOR 32
#define MP4PUSH_MAX_REACTOR_THREADS 32
#define MP4PUSH_RING_BYTES (1u << 17)
#define MP4PUSH_SUB_FREE 0
#define MP4PUSH_SUB_ALIVE 1
#define MP4PUSH_SUB_CLOSING 2

typedef struct {
  _Atomic int alive;
  capture_ctx_t *cap_ctx;
  pid_filter_t filter;
  unsigned pmt_pid;
  int proto;
  int fd;
  void *h3c;
  int64_t h3_sid;
  int ws_handle;
  int reactor_tid;
  uint8_t *ring;
  _Atomic uint32_t wpos;
  _Atomic uint32_t rpos;
  _Atomic int ring_errored;
  _Atomic int seg_next;
  int tid_next;
} mp4push_sub_t;

static mp4push_sub_t *g_subs;
static int g_subs_n;
static int g_reactor_efds[MP4PUSH_MAX_REACTOR_THREADS];
static int g_tid_head[MP4PUSH_MAX_REACTOR_THREADS];

static void wake_reactor(int tid) {
  int efd;
  if (tid < 0 || tid >= MP4PUSH_MAX_REACTOR_THREADS) return;
  efd = g_reactor_efds[tid];
  if (efd >= 0) {
    uint64_t one = 1;
    ssize_t r = write(efd, &one, sizeof one);
    (void)r;
  }
}

static void ring_enqueue(mp4push_sub_t *s, const uint8_t *data, size_t len) {
  uint32_t wpos, rpos, idx;
  size_t first;
  if (!s->ring || !len) return;
  wpos = atomic_load_explicit(&s->wpos, memory_order_relaxed);
  rpos = atomic_load_explicit(&s->rpos, memory_order_acquire);
  if (len > MP4PUSH_RING_BYTES - (wpos - rpos)) {
    atomic_store_explicit(&s->ring_errored, 1, memory_order_release);
    return;
  }
  idx = wpos & (MP4PUSH_RING_BYTES - 1u);
  first = len;
  if (idx + first > MP4PUSH_RING_BYTES) first = MP4PUSH_RING_BYTES - idx;
  memcpy(s->ring + idx, data, first);
  if (first < len) memcpy(s->ring, data + first, len - first);
  atomic_store_explicit(&s->wpos, wpos + (uint32_t)len, memory_order_release);
  ws_clients_add_bytes(s->ws_handle, len);
}

static void link_tid_chain(int idx, int tid) {
  if (tid < 0 || tid >= MP4PUSH_MAX_REACTOR_THREADS) return;
  g_subs[idx].tid_next = g_tid_head[tid];
  g_tid_head[tid] = idx;
}

static void unlink_tid_chain(int idx) {
  const mp4push_sub_t *s = &g_subs[idx];
  int tid = s->reactor_tid;
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

void mp4push_deliver(hls_seg_ctx_t *s, const unsigned char *data, size_t len) {
  int i = atomic_load_explicit(&s->mp4push_sub_head, memory_order_acquire);
  while (i != -1) {
    mp4push_sub_t *sub = &g_subs[i];
    int next = atomic_load_explicit(&sub->seg_next, memory_order_relaxed);
    if (atomic_load_explicit(&sub->alive, memory_order_acquire) == MP4PUSH_SUB_ALIVE) {
      if (sub->proto == 1) {
        conn_t *c = conn_for_fd(sub->fd);
        if (c) {
          conn_send_buffered(c, data, len, NULL, 0);
          ws_clients_add_bytes(sub->ws_handle, len);
        }
      } else {
        ring_enqueue(sub, data, len);
        wake_reactor(sub->reactor_tid);
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
    log_line(TOOL_NAME ": out of memory sizing mp4push subscriber table (%d entries)", n);
    return;
  }
  g_subs_n = n;
  for (int i = 0; i < MP4PUSH_MAX_REACTOR_THREADS; i++) {
    g_reactor_efds[i] = -1;
    g_tid_head[i] = -1;
  }
}

int mp4push_subscribe(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, int proto) {
  hls_seg_ctx_t *s;
  int idx = -1;

  for (int i = 0; i < g_subs_n; i++) {
    int expected = MP4PUSH_SUB_FREE;
    if (atomic_compare_exchange_strong_explicit(&g_subs[i].alive, &expected, MP4PUSH_SUB_ALIVE, memory_order_acquire, memory_order_relaxed)) {
      idx = i;
      break;
    }
  }
  if (idx < 0) return -1;

  g_subs[idx].cap_ctx = ctx;
  g_subs[idx].filter = *filter;
  g_subs[idx].pmt_pid = pmt_pid;
  g_subs[idx].proto = proto;
  g_subs[idx].fd = -1;
  g_subs[idx].h3c = NULL;
  g_subs[idx].h3_sid = -1;
  g_subs[idx].ws_handle = -1;
  g_subs[idx].reactor_tid = -1;
  g_subs[idx].tid_next = -1;
  atomic_store_explicit(&g_subs[idx].ring_errored, 0, memory_order_relaxed);
  if (proto == 2 || proto == 3) {
    hls_resp_t resp;
    if (!g_subs[idx].ring) g_subs[idx].ring = malloc(MP4PUSH_RING_BYTES);
    atomic_store_explicit(&g_subs[idx].wpos, 0, memory_order_relaxed);
    atomic_store_explicit(&g_subs[idx].rpos, 0, memory_order_relaxed);
    if (!g_subs[idx].ring) {
      atomic_store_explicit(&g_subs[idx].alive, MP4PUSH_SUB_FREE, memory_order_release);
      return -1;
    }
    if (hls_render(ctx, filter, pmt_pid, SEG_CONTAINER_FMP4, "init.mp4", 0, NULL, &resp) && resp.status == 200) {
      ring_enqueue(&g_subs[idx], resp.body, resp.body_len);
      hls_resp_body_release(resp.body, resp.zc);
    }
  }

  hls_seg_registry_lock();
  s = hls_seg_find_locked(ctx, filter, pmt_pid, SEG_CONTAINER_FMP4);
  if (s) seg_link(s, idx);
  hls_seg_registry_unlock();
  if (!s) {
    atomic_store_explicit(&g_subs[idx].alive, MP4PUSH_SUB_FREE, memory_order_release);
    return -1;
  }
  return idx;
}

int mp4push_try_attach(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, int ws_handle) {
  static const char hdr[] = "HTTP/1.1 200 OK\r\nServer: " TOOL_NAME "/" TOOL_VERSION "\r\nContent-Type: video/mp4\r\nConnection: close\r\n\r\n";
  hls_resp_t resp;
  int idx;
  conn_queue(c, hdr, strlen(hdr));
  if (hls_render(ctx, filter, pmt_pid, SEG_CONTAINER_FMP4, "init.mp4", 0, NULL, &resp) && resp.status == 200) {
    conn_queue(c, resp.body, resp.body_len);
    hls_resp_body_release(resp.body, resp.zc);
  }
  idx = mp4push_subscribe(ctx, filter, pmt_pid, 1);
  if (idx < 0) return 0;
  g_subs[idx].fd = c->fd;
  g_subs[idx].ws_handle = ws_handle;
  c->slot = idx;
  c->become_mp4push = 1;
  return 1;
}

void mp4push_h2_bind(int slot, int fd, int reactor_tid, int ws_handle) {
  if (slot < 0 || slot >= g_subs_n) return;
  g_subs[slot].fd = fd;
  g_subs[slot].reactor_tid = reactor_tid;
  g_subs[slot].ws_handle = ws_handle;
  link_tid_chain(slot, reactor_tid);
}

int mp4push_sub_fd(int slot) {
  if (slot < 0 || slot >= g_subs_n) return -1;
  return g_subs[slot].fd;
}

void mp4push_h3_bind(int slot, void *h3c, int64_t h3_sid, int reactor_tid, int ws_handle) {
  if (slot < 0 || slot >= g_subs_n) return;
  g_subs[slot].h3c = h3c;
  g_subs[slot].h3_sid = h3_sid;
  g_subs[slot].reactor_tid = reactor_tid;
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
  mp4push_sub_t *s;
  uint32_t wpos, rpos, idx, avail, n;
  if (slot < 0 || slot >= g_subs_n) return 0;
  s = &g_subs[slot];
  if (!s->ring) return 0;
  wpos = atomic_load_explicit(&s->wpos, memory_order_acquire);
  rpos = atomic_load_explicit(&s->rpos, memory_order_relaxed);
  if (wpos == rpos) return 0;
  idx = rpos & (MP4PUSH_RING_BYTES - 1u);
  avail = wpos - rpos;
  n = MP4PUSH_RING_BYTES - idx;
  if (n > avail) n = avail;
  if (n > maxlen) n = (uint32_t)maxlen;
  memcpy(buf, s->ring + idx, n);
  atomic_store_explicit(&s->rpos, rpos + n, memory_order_release);
  return n;
}

const uint8_t *mp4push_ring_peek(int slot, size_t *len) {
  mp4push_sub_t *s;
  uint32_t wpos, rpos, idx, avail, contig;
  *len = 0;
  if (slot < 0 || slot >= g_subs_n) return NULL;
  s = &g_subs[slot];
  if (!s->ring) return NULL;
  wpos = atomic_load_explicit(&s->wpos, memory_order_acquire);
  rpos = atomic_load_explicit(&s->rpos, memory_order_relaxed);
  if (wpos == rpos) return NULL;
  idx = rpos & (MP4PUSH_RING_BYTES - 1u);
  avail = wpos - rpos;
  contig = MP4PUSH_RING_BYTES - idx;
  if (contig > avail) contig = avail;
  *len = contig;
  return s->ring + idx;
}

void mp4push_ring_advance(int slot, size_t n) {
  if (slot < 0 || slot >= g_subs_n) return;
  atomic_fetch_add_explicit(&g_subs[slot].rpos, (uint32_t)n, memory_order_release);
}

int mp4push_ring_pending(int slot) {
  mp4push_sub_t *s;
  if (slot < 0 || slot >= g_subs_n) return 0;
  s = &g_subs[slot];
  return atomic_load_explicit(&s->wpos, memory_order_acquire) != atomic_load_explicit(&s->rpos, memory_order_relaxed);
}

int mp4push_ring_errored(int slot) {
  if (slot < 0 || slot >= g_subs_n) return 0;
  return atomic_load_explicit(&g_subs[slot].ring_errored, memory_order_acquire);
}

void mp4push_register_reactor_efd(int tid, int efd) {
  if (tid < 0 || tid >= MP4PUSH_MAX_REACTOR_THREADS) return;
  g_reactor_efds[tid] = efd;
}

void mp4push_flush_ready(int tid) {
  int i;
  if (tid < 0 || tid >= MP4PUSH_MAX_REACTOR_THREADS) return;
  i = g_tid_head[tid];
  while (i != -1) {
    mp4push_sub_t *s = &g_subs[i];
    int next = s->tid_next;
    if (atomic_load_explicit(&s->alive, memory_order_relaxed) == MP4PUSH_SUB_ALIVE &&
        (mp4push_ring_pending(i) || atomic_load_explicit(&s->ring_errored, memory_order_acquire))) {
#ifdef HAVE_HTTP2
      if (s->proto == 2) h2_mp4push_wake(i);
#endif
#ifdef HAVE_HTTP3
      if (s->proto == 3) h3_mp4push_wake(i);
#endif
    }
    i = next;
  }
}

void mp4push_sub_close(int slot) {
  mp4push_sub_t *s;
  int expected;
  hls_seg_ctx_t *seg;
  if (slot < 0 || slot >= g_subs_n) return;
  s = &g_subs[slot];
  expected = MP4PUSH_SUB_ALIVE;
  if (!atomic_compare_exchange_strong_explicit(&s->alive, &expected, MP4PUSH_SUB_CLOSING, memory_order_acquire, memory_order_relaxed))
    return;
  capture_wait_pumps_quiescent();
  hls_seg_registry_lock();
  seg = hls_seg_find_locked(s->cap_ctx, &s->filter, s->pmt_pid, SEG_CONTAINER_FMP4);
  if (seg) seg_unlink(seg, slot);
  hls_seg_registry_unlock();
  if (s->proto == 2 || s->proto == 3) unlink_tid_chain(slot);
  atomic_store_explicit(&s->alive, MP4PUSH_SUB_FREE, memory_order_release);
}
