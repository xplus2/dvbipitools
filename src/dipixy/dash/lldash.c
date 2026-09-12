/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lldash.h"
#include "dash_int.h"
#include "../reactor/internal.h"
#include "../version.h"
#include "../ws/ws_clients.h"

#include "lib/helper/byte_ring.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DASHCHUNK_MAX_SUBS 4096
#define DASHCHUNK_SUBS_HEADROOM 16
#define DASHCHUNK_SUBS_FLOOR 32
#define DASHCHUNK_MAX_REACTOR_THREADS 32
#define DASHCHUNK_RING_BYTES (1u << 17)

#define DASHCHUNK_SUB_FREE 0
#define DASHCHUNK_SUB_ALIVE 1
#define DASHCHUNK_SUB_CLOSING 2

typedef struct {
  _Atomic int alive;
  hls_store_t *store;
  capture_ctx_t *cap_ctx;
  pid_filter_t filter;
  unsigned pmt_pid;
  lcevc_select_t lcevc;
  uint32_t want_seg;
  dash_proto_t proto;
  int fd;
  void *h2c;
  void *h2_slot;
  void *h3c; /* h3: owning h3_conn_t* */
  int64_t h3_sid; /* h3: stream id */
  int ws_handle;
  int reactor_tid; /* h2/h3: owning reactor thread */
  _Atomic int finalized;
  byte_ring_t ring; /* h2/h3 */
  _Atomic int ring_errored;
  _Atomic int store_next;
  int tid_next;
} dashchunk_sub_t;

static dashchunk_sub_t *g_subs;
static int g_subs_n;
static int g_reactor_efds[DASHCHUNK_MAX_REACTOR_THREADS];
static int g_tid_head[DASHCHUNK_MAX_REACTOR_THREADS];
static int *g_free_slots;
static int g_free_slots_n;
static pthread_mutex_t g_free_lock = PTHREAD_MUTEX_INITIALIZER;

static void free_slot(int idx) {
  atomic_store_explicit(&g_subs[idx].alive, DASHCHUNK_SUB_FREE, memory_order_release);
  pthread_mutex_lock(&g_free_lock);
  g_free_slots[g_free_slots_n++] = idx;
  pthread_mutex_unlock(&g_free_lock);
}

static int sub_matches(const dashchunk_sub_t *s, const hls_store_t *store, uint32_t seq) {
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
static void send_chunk(int fd, int ws_handle, const uint8_t *data, size_t len) {
  conn_t *c;
  char hdr[24];
  char *p = hdr;
  c = conn_for_fd(fd);
  if (!c) return;
  p = write_hex(p, len);
  *p++ = '\r'; *p++ = '\n';
  if (len) conn_send_buffered3(c, hdr, (size_t)(p - hdr), data, len, "\r\n", 2);
  else conn_send_buffered(c, hdr, (size_t)(p - hdr), "\r\n", 2);
  ws_clients_add_bytes(ws_handle, len);
}

static void wake_reactor(int tid) {
  int efd;
  if (tid < 0 || tid >= DASHCHUNK_MAX_REACTOR_THREADS) return;
  efd = g_reactor_efds[tid];
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

static void link_store_chain(hls_store_t *store, int idx) {
  int old_head = atomic_load_explicit(&store->lldash_sub_head, memory_order_relaxed);
  atomic_store_explicit(&g_subs[idx].store_next, old_head, memory_order_relaxed);
  while (!atomic_compare_exchange_weak_explicit(&store->lldash_sub_head, &old_head, idx, memory_order_release, memory_order_relaxed))
    atomic_store_explicit(&g_subs[idx].store_next, old_head, memory_order_relaxed);
}

static void unlink_store_chain(hls_store_t *store, int idx) {
  const dashchunk_sub_t *s = &g_subs[idx];
  int next = atomic_load_explicit(&s->store_next, memory_order_relaxed);
  int cur = atomic_load_explicit(&store->lldash_sub_head, memory_order_relaxed);
  if (cur == idx) {
    atomic_store_explicit(&store->lldash_sub_head, next, memory_order_relaxed);
    return;
  }
  while (cur != -1) {
    dashchunk_sub_t *cs = &g_subs[cur];
    int cs_next = atomic_load_explicit(&cs->store_next, memory_order_relaxed);
    if (cs_next == idx) {
      atomic_store_explicit(&cs->store_next, next, memory_order_relaxed);
      return;
    }
    cur = cs_next;
  }
}

static void link_tid_chain(int idx, int tid) {
  if (tid < 0 || tid >= DASHCHUNK_MAX_REACTOR_THREADS) return;
  g_subs[idx].tid_next = g_tid_head[tid];
  g_tid_head[tid] = idx;
}

static void unlink_tid_chain(int idx) {
  const dashchunk_sub_t *s = &g_subs[idx];
  int tid = s->reactor_tid;
  int cur;
  if (tid < 0 || tid >= DASHCHUNK_MAX_REACTOR_THREADS) return;
  if (g_tid_head[tid] == idx) {
    g_tid_head[tid] = s->tid_next;
    return;
  }
  cur = g_tid_head[tid];
  while (cur != -1) {
    dashchunk_sub_t *cs = &g_subs[cur];
    if (cs->tid_next == idx) {
      cs->tid_next = s->tid_next;
      return;
    }
    cur = cs->tid_next;
  }
}

static void on_part_pushed(const hls_store_t *store, uint32_t seq, const uint8_t *data, size_t len) {
  int i;
  if (store->container != SEG_CONTAINER_FMP4) return;
  i = atomic_load_explicit(&store->lldash_sub_head, memory_order_acquire);
  while (i != -1) {
    dashchunk_sub_t *s = &g_subs[i];
    int next = atomic_load_explicit(&s->store_next, memory_order_relaxed);
    if (atomic_load_explicit(&s->alive, memory_order_acquire) == DASHCHUNK_SUB_ALIVE && sub_matches(s, store, seq)) {
      if (s->proto == DASH_PROTO_H1) {
        send_chunk(s->fd, s->ws_handle, data, len);
      } else if (s->proto == DASH_PROTO_H2 || s->proto == DASH_PROTO_H3) {
        ring_enqueue(s, data, len);
        wake_reactor(s->reactor_tid);
      }
    }
    i = next;
  }
}

static void on_segment_done(const hls_store_t *store, uint32_t seq) {
  int i;
  if (store->container != SEG_CONTAINER_FMP4) return;
  i = atomic_load_explicit(&store->lldash_sub_head, memory_order_acquire);
  while (i != -1) {
    dashchunk_sub_t *s = &g_subs[i];
    int next = atomic_load_explicit(&s->store_next, memory_order_relaxed);
    if (atomic_load_explicit(&s->alive, memory_order_acquire) == DASHCHUNK_SUB_ALIVE && sub_matches(s, store, seq)) {
      if (s->proto == DASH_PROTO_H1) {
        send_chunk(s->fd, s->ws_handle, NULL, 0);
        atomic_store_explicit(&s->finalized, 1, memory_order_release);
      } else if (s->proto == DASH_PROTO_H2 || s->proto == DASH_PROTO_H3) {
        atomic_store_explicit(&s->finalized, 1, memory_order_release);
        wake_reactor(s->reactor_tid);
      }
    }
    i = next;
  }
}

static void on_store_closing(const hls_store_t *store) {
  int i = atomic_load_explicit(&store->lldash_sub_head, memory_order_acquire);
  while (i != -1) {
    dashchunk_sub_t *s = &g_subs[i];
    int next = atomic_load_explicit(&s->store_next, memory_order_relaxed);
    if (atomic_load_explicit(&s->alive, memory_order_acquire) == DASHCHUNK_SUB_ALIVE) {
      if (s->proto == DASH_PROTO_H1) {
        send_chunk(s->fd, s->ws_handle, NULL, 0);
        atomic_store_explicit(&s->finalized, 1, memory_order_release);
      } else if (s->proto == DASH_PROTO_H2 || s->proto == DASH_PROTO_H3) {
        atomic_store_explicit(&s->ring_errored, 1, memory_order_release);
        atomic_store_explicit(&s->finalized, 1, memory_order_release);
        wake_reactor(s->reactor_tid);
      }
    }
    i = next;
  }
}

void dash_lldash_init(int max_clients) {
  int n = (max_clients > 0 ? max_clients : 0) + DASHCHUNK_SUBS_HEADROOM;
  if (n < DASHCHUNK_SUBS_FLOOR) n = DASHCHUNK_SUBS_FLOOR;
  if (n > DASHCHUNK_MAX_SUBS) n = DASHCHUNK_MAX_SUBS;
  g_subs = calloc((size_t)n, sizeof *g_subs);
  if (!g_subs) {
    log_line(TOOL_NAME ": out of memory sizing dash_lldash subscriber table (%d entries)", n);
    return;
  }
  g_subs_n = n;
  for (int i = 0; i < DASHCHUNK_MAX_REACTOR_THREADS; i++) {
    g_reactor_efds[i] = -1;
    g_tid_head[i] = -1;
  }
  g_free_slots = malloc(sizeof(int) * (size_t)n);
  if (g_free_slots) {
    for (int i = 0; i < n; i++) g_free_slots[i] = i;
    g_free_slots_n = n;
  }
  hls_set_part_pushed_cb(on_part_pushed);
  hls_set_segment_done_cb(on_segment_done);
  hls_set_store_closing_cb(on_store_closing);
}

int dash_lldash_subscribe(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename, dash_proto_t proto) {
  hls_store_t *s;
  const hls_snapshot_t *snap;
  uint64_t want_t_ms;
  uint32_t want_seg;
  int idx = -1;

  if (!parse_dash_seg_filename(filename, &want_t_ms)) return -1;
  s = hls_store_find(ctx, filter, pmt_pid, lcevc, SEG_CONTAINER_FMP4);
  snap = s ? atomic_load_explicit(&s->snap, memory_order_acquire) : NULL;
  if (!snap || snap->part_target <= 0.0 || snap->cum_ms != want_t_ms) return -1;
  want_seg = snap->live_msn;
  pthread_mutex_lock(&g_free_lock);
  if (g_free_slots_n > 0) idx = g_free_slots[--g_free_slots_n];
  pthread_mutex_unlock(&g_free_lock);
  if (idx < 0) return -1;
  atomic_store_explicit(&g_subs[idx].alive, DASHCHUNK_SUB_ALIVE, memory_order_release);

  g_subs[idx].store = s;
  g_subs[idx].cap_ctx = ctx;
  g_subs[idx].filter = *filter;
  g_subs[idx].pmt_pid = pmt_pid;
  g_subs[idx].lcevc = *lcevc;
  g_subs[idx].want_seg = want_seg;
  g_subs[idx].proto = proto;
  g_subs[idx].fd = -1;
  g_subs[idx].h2c = NULL;
  g_subs[idx].h2_slot = NULL;
  g_subs[idx].h3c = NULL;
  g_subs[idx].h3_sid = -1;
  g_subs[idx].ws_handle = -1;
  g_subs[idx].reactor_tid = -1;
  g_subs[idx].tid_next = -1;
  atomic_store_explicit(&g_subs[idx].finalized, 0, memory_order_relaxed);
  atomic_store_explicit(&g_subs[idx].ring_errored, 0, memory_order_relaxed);
  if (proto == DASH_PROTO_H2 || proto == DASH_PROTO_H3) {
    byte_ring_reset(&g_subs[idx].ring, DASHCHUNK_RING_BYTES);
    if (!g_subs[idx].ring.buf) {
      free_slot(idx);
      return -1;
    }
  }
  link_store_chain(s, idx);
  return idx;
}

void dash_lldash_h2_bind(int slot, void *h2c, void *h2_slot, int reactor_tid, int ws_handle) {
  if (slot < 0 || slot >= g_subs_n) return;
  g_subs[slot].h2c = h2c;
  g_subs[slot].h2_slot = h2_slot;
  g_subs[slot].reactor_tid = reactor_tid;
  g_subs[slot].ws_handle = ws_handle;
  link_tid_chain(slot, reactor_tid);
}

void *dash_lldash_sub_h2c(int slot) {
  if (slot < 0 || slot >= g_subs_n) return NULL;
  return g_subs[slot].h2c;
}

void *dash_lldash_sub_h2_slot(int slot) {
  if (slot < 0 || slot >= g_subs_n) return NULL;
  return g_subs[slot].h2_slot;
}

void dash_lldash_h3_bind(int slot, void *h3c, int64_t h3_sid, int reactor_tid, int ws_handle) {
  if (slot < 0 || slot >= g_subs_n) return;
  g_subs[slot].h3c = h3c;
  g_subs[slot].h3_sid = h3_sid;
  g_subs[slot].reactor_tid = reactor_tid;
  g_subs[slot].ws_handle = ws_handle;
  link_tid_chain(slot, reactor_tid);
}

void *dash_lldash_sub_h3c(int slot) {
  if (slot < 0 || slot >= g_subs_n) return NULL;
  return g_subs[slot].h3c;
}

int64_t dash_lldash_sub_h3_sid(int slot) {
  if (slot < 0 || slot >= g_subs_n) return -1;
  return g_subs[slot].h3_sid;
}

size_t dash_lldash_ring_read(int slot, uint8_t *buf, size_t maxlen) {
  if (slot < 0 || slot >= g_subs_n) return 0;
  return byte_ring_read(&g_subs[slot].ring, buf, maxlen);
}

int dash_lldash_ring_pending(int slot) {
  dashchunk_sub_t *s;
  if (slot < 0 || slot >= g_subs_n) return 0;
  s = &g_subs[slot];
  return atomic_load_explicit(&s->ring.wpos, memory_order_acquire) != atomic_load_explicit(&s->ring.rpos, memory_order_relaxed);
}

int dash_lldash_ring_errored(int slot) {
  if (slot < 0 || slot >= g_subs_n) return 0;
  return atomic_load_explicit(&g_subs[slot].ring_errored, memory_order_acquire);
}

const uint8_t *dash_lldash_ring_peek(int slot, size_t *len) {
  if (slot < 0 || slot >= g_subs_n) {
    *len = 0;
    return NULL;
  }
  return byte_ring_peek(&g_subs[slot].ring, len);
}

void dash_lldash_ring_advance(int slot, size_t n) {
  if (slot < 0 || slot >= g_subs_n) return;
  byte_ring_advance(&g_subs[slot].ring, n);
}

void dash_lldash_register_reactor_efd(int tid, int efd) {
  if (tid < 0 || tid >= DASHCHUNK_MAX_REACTOR_THREADS) return;
  g_reactor_efds[tid] = efd;
}

void dash_lldash_flush_ready(int tid) {
  int i;
  if (tid < 0 || tid >= DASHCHUNK_MAX_REACTOR_THREADS) return;
  i = g_tid_head[tid];
  while (i != -1) {
    dashchunk_sub_t *s = &g_subs[i];
    int next = s->tid_next;
    if (atomic_load_explicit(&s->alive, memory_order_relaxed) == DASHCHUNK_SUB_ALIVE &&
        (dash_lldash_ring_pending(i) || atomic_load_explicit(&s->finalized, memory_order_acquire) ||
         atomic_load_explicit(&s->ring_errored, memory_order_acquire))) {
#ifdef HAVE_HTTP2
      if (s->proto == DASH_PROTO_H2) h2_dashchunk_wake(i);
#endif
#ifdef HAVE_HTTP3
      if (s->proto == DASH_PROTO_H3) h3_dashchunk_wake(i);
#endif
    }
    i = next;
  }
}

int dash_lldash_try_attach(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, const char *filename, int keep_alive, const char *origin_hdr, int ws_handle) {
  char cors_hdr[192];
  char hdr[384];
  strbuf_t b;
  int idx = dash_lldash_subscribe(ctx, filter, pmt_pid, lcevc, filename, 1);
  if (idx < 0) return 0;
  g_subs[idx].fd = c->fd;
  g_subs[idx].ws_handle = ws_handle;
  cors_prepare(origin_hdr, cors_hdr, sizeof cors_hdr);
  hls_sb_init(&b, hdr, sizeof hdr);
  hls_sb_add(&b, "HTTP/1.1 200 OK\r\nServer: " TOOL_NAME "/" TOOL_VERSION "\r\nContent-Type: video/mp4\r\nTransfer-Encoding: chunked\r\nCache-Control: no-cache, no-store, must-revalidate\r\n");
  hls_sb_add(&b, cors_hdr);
  hls_sb_add(&b, "Connection: ");
  hls_sb_add(&b, keep_alive ? "keep-alive" : "close");
  hls_sb_add(&b, "\r\n\r\n");
  conn_queue(c, hdr, b.len);
  c->slot = idx;
  c->become_dashchunk = 1;
  c->keep_alive = keep_alive ? 1 : 0;
  return 1;
}

int dash_lldash_sub_finalized(int slot) {
  if (slot < 0 || slot >= g_subs_n) return 1;
  return atomic_load_explicit(&g_subs[slot].finalized, memory_order_acquire);
}

void dash_lldash_sub_close(int slot) {
  dashchunk_sub_t *s;
  int expected;
  if (slot < 0 || slot >= g_subs_n) return;
  s = &g_subs[slot];
  expected = DASHCHUNK_SUB_ALIVE;
  if (!atomic_compare_exchange_strong_explicit(&s->alive, &expected, DASHCHUNK_SUB_CLOSING, memory_order_acquire, memory_order_relaxed)) return;
  capture_wait_pumps_quiescent();
  unlink_store_chain(s->store, slot);
  if (s->proto == DASH_PROTO_H2 || s->proto == DASH_PROTO_H3) unlink_tid_chain(slot);
  free_slot(slot);
}
