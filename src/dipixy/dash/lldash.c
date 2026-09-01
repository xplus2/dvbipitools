/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lldash.h"
#include "dash_int.h"
#include "../reactor/internal.h"
#include "../version.h"
#include "../ws/ws_clients.h"

#include "lib/helper/ioutil.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DASHCHUNK_MAX_SUBS 256
#define DASHCHUNK_MAX_REACTOR_THREADS 32
#define DASHCHUNK_RING_BYTES (1u << 17) /* 128 KiB */

typedef struct {
  _Atomic int alive;
  capture_ctx_t *cap_ctx;
  pid_filter_t filter;
  unsigned pmt_pid;
  uint32_t want_seg;
  int proto; /* 1 h1, 2 h2, 3 h3 */
  int fd; /* h2 */
  void *h3c; /* h3: owning h3_conn_t* */
  int64_t h3_sid; /* h3: stream id */
  int ws_handle;
  int reactor_tid; /* h2/h3: owning reactor thread */
  _Atomic int finalized;
  uint8_t *ring; /* h2/h3 */
  _Atomic uint32_t wpos;
  _Atomic uint32_t rpos;
  _Atomic int ring_errored;
} dashchunk_sub_t;

static dashchunk_sub_t g_subs[DASHCHUNK_MAX_SUBS];
static pthread_mutex_t g_subs_mtx = PTHREAD_MUTEX_INITIALIZER;
static int g_reactor_efds[DASHCHUNK_MAX_REACTOR_THREADS];

static int sub_matches(const dashchunk_sub_t *s, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, uint32_t seq) {
  return s->cap_ctx == ctx && s->pmt_pid == pmt_pid && s->want_seg == seq && pid_filter_equal(&s->filter, filter);
}

static char *write_hex(char *dst, size_t v) {
  static const char digits[] = "0123456789abcdef";
  char tmp[16];
  int n = 0;
  do {
    tmp[n++] = digits[v & 0xf];
    v >>= 4;
  } while (v);
  for (int i = 0; i < n; i++)
    *dst++ = tmp[n - 1 - i];
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
  if (conn_send_buffered(c, hdr, (size_t)(p - hdr), NULL, 0) < 0) return;
  if (len) conn_send_buffered(c, data, len, "\r\n", 2);
  else conn_send_buffered(c, "\r\n", 2, NULL, 0);
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
  uint32_t wpos, rpos, idx;
  size_t first;
  if (!s->ring || !len) return;
  wpos = atomic_load_explicit(&s->wpos, memory_order_relaxed);
  rpos = atomic_load_explicit(&s->rpos, memory_order_acquire);
  if (len > DASHCHUNK_RING_BYTES - (wpos - rpos)) {
    atomic_store_explicit(&s->ring_errored, 1, memory_order_release);
    return;
  }
  idx = wpos & (DASHCHUNK_RING_BYTES - 1u);
  first = len;
  if (idx + first > DASHCHUNK_RING_BYTES) first = DASHCHUNK_RING_BYTES - idx;
  memcpy(s->ring + idx, data, first);
  if (first < len) memcpy(s->ring, data + first, len - first);
  atomic_store_explicit(&s->wpos, wpos + (uint32_t)len, memory_order_release);
  ws_clients_add_bytes(s->ws_handle, len);
}

static void on_part_pushed(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, hls_container_t container, uint32_t seq, const uint8_t *data, size_t len) {
  if (container != HLS_CONTAINER_FMP4) return;
  pthread_mutex_lock(&g_subs_mtx);
  for (int i = 0; i < DASHCHUNK_MAX_SUBS; i++) {
    dashchunk_sub_t *s = &g_subs[i];
    if (!atomic_load_explicit(&s->alive, memory_order_relaxed)) continue;
    if (!sub_matches(s, ctx, filter, pmt_pid, seq)) continue;
    if (s->proto == 1) {
      send_chunk(s->fd, s->ws_handle, data, len);
    } else if (s->proto == 2 || s->proto == 3) {
      ring_enqueue(s, data, len);
      wake_reactor(s->reactor_tid);
    }
  }
  pthread_mutex_unlock(&g_subs_mtx);
}

static void on_segment_done(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, hls_container_t container, uint32_t seq) {
  if (container != HLS_CONTAINER_FMP4) return;
  pthread_mutex_lock(&g_subs_mtx);
  for (int i = 0; i < DASHCHUNK_MAX_SUBS; i++) {
    dashchunk_sub_t *s = &g_subs[i];
    if (!atomic_load_explicit(&s->alive, memory_order_relaxed)) continue;
    if (!sub_matches(s, ctx, filter, pmt_pid, seq)) continue;
    if (s->proto == 1) {
      send_chunk(s->fd, s->ws_handle, NULL, 0); /* "0\r\n\r\n" terminator */
      atomic_store_explicit(&s->finalized, 1, memory_order_release);
    } else if (s->proto == 2 || s->proto == 3) {
      atomic_store_explicit(&s->finalized, 1, memory_order_release);
      wake_reactor(s->reactor_tid);
    }
  }
  pthread_mutex_unlock(&g_subs_mtx);
}

void dash_lldash_init(void) {
  for (int i = 0; i < DASHCHUNK_MAX_REACTOR_THREADS; i++) g_reactor_efds[i] = -1;
  hls_set_part_pushed_cb(on_part_pushed);
  hls_set_segment_done_cb(on_segment_done);
}

int dash_lldash_subscribe(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename, int proto) {
  hls_store_t *s;
  uint64_t want_t_ms;
  uint32_t want_seg;
  int idx = -1;

  if (!parse_dash_seg_filename(filename, &want_t_ms)) return -1;
  s = find_store_locked(ctx, filter, pmt_pid, HLS_CONTAINER_FMP4);
  if (!s || s->part_target <= 0.0 || s->cum_ms != want_t_ms) {
    if (s) pthread_mutex_unlock(store_lock(s));
    return -1;
  }
  want_seg = s->live_msn;
  pthread_mutex_unlock(store_lock(s));

  pthread_mutex_lock(&g_subs_mtx);
  for (int i = 0; i < DASHCHUNK_MAX_SUBS; i++) {
    int expected = 0;
    if (atomic_compare_exchange_strong_explicit(&g_subs[i].alive, &expected, 1, memory_order_acquire, memory_order_relaxed)) {
      idx = i;
      break;
    }
  }
  if (idx < 0) {
    pthread_mutex_unlock(&g_subs_mtx);
    return -1;
  }
  g_subs[idx].cap_ctx = ctx;
  g_subs[idx].filter = *filter;
  g_subs[idx].pmt_pid = pmt_pid;
  g_subs[idx].want_seg = want_seg;
  g_subs[idx].proto = proto;
  g_subs[idx].fd = -1;
  g_subs[idx].h3c = NULL;
  g_subs[idx].h3_sid = -1;
  g_subs[idx].ws_handle = -1;
  g_subs[idx].reactor_tid = -1;
  atomic_store_explicit(&g_subs[idx].finalized, 0, memory_order_relaxed);
  atomic_store_explicit(&g_subs[idx].ring_errored, 0, memory_order_relaxed);
  if (proto == 2 || proto == 3) {
    g_subs[idx].ring = malloc(DASHCHUNK_RING_BYTES);
    atomic_store_explicit(&g_subs[idx].wpos, 0, memory_order_relaxed);
    atomic_store_explicit(&g_subs[idx].rpos, 0, memory_order_relaxed);
    if (!g_subs[idx].ring) {
      atomic_store_explicit(&g_subs[idx].alive, 0, memory_order_release);
      pthread_mutex_unlock(&g_subs_mtx);
      return -1;
    }
  } else {
    g_subs[idx].ring = NULL;
  }
  pthread_mutex_unlock(&g_subs_mtx);
  return idx;
}

void dash_lldash_h2_bind(int slot, int fd, int reactor_tid, int ws_handle) {
  if (slot < 0 || slot >= DASHCHUNK_MAX_SUBS) return;
  g_subs[slot].fd = fd;
  g_subs[slot].reactor_tid = reactor_tid;
  g_subs[slot].ws_handle = ws_handle;
}

int dash_lldash_sub_fd(int slot) {
  if (slot < 0 || slot >= DASHCHUNK_MAX_SUBS) return -1;
  return g_subs[slot].fd;
}

void dash_lldash_h3_bind(int slot, void *h3c, int64_t h3_sid, int reactor_tid, int ws_handle) {
  if (slot < 0 || slot >= DASHCHUNK_MAX_SUBS) return;
  g_subs[slot].h3c = h3c;
  g_subs[slot].h3_sid = h3_sid;
  g_subs[slot].reactor_tid = reactor_tid;
  g_subs[slot].ws_handle = ws_handle;
}

void *dash_lldash_sub_h3c(int slot) {
  if (slot < 0 || slot >= DASHCHUNK_MAX_SUBS) return NULL;
  return g_subs[slot].h3c;
}

int64_t dash_lldash_sub_h3_sid(int slot) {
  if (slot < 0 || slot >= DASHCHUNK_MAX_SUBS) return -1;
  return g_subs[slot].h3_sid;
}

size_t dash_lldash_ring_read(int slot, uint8_t *buf, size_t maxlen) {
  dashchunk_sub_t *s;
  uint32_t wpos, rpos, idx, avail, n;
  if (slot < 0 || slot >= DASHCHUNK_MAX_SUBS) return 0;
  s = &g_subs[slot];
  if (!s->ring) return 0;
  wpos = atomic_load_explicit(&s->wpos, memory_order_acquire);
  rpos = atomic_load_explicit(&s->rpos, memory_order_relaxed);
  if (wpos == rpos) return 0;
  idx = rpos & (DASHCHUNK_RING_BYTES - 1u);
  avail = wpos - rpos;
  n = DASHCHUNK_RING_BYTES - idx;
  if (n > avail) n = avail;
  if (n > maxlen) n = (uint32_t)maxlen;
  memcpy(buf, s->ring + idx, n);
  atomic_store_explicit(&s->rpos, rpos + n, memory_order_release);
  return n;
}

int dash_lldash_ring_pending(int slot) {
  dashchunk_sub_t *s;
  if (slot < 0 || slot >= DASHCHUNK_MAX_SUBS) return 0;
  s = &g_subs[slot];
  return atomic_load_explicit(&s->wpos, memory_order_acquire) != atomic_load_explicit(&s->rpos, memory_order_relaxed);
}

int dash_lldash_ring_errored(int slot) {
  if (slot < 0 || slot >= DASHCHUNK_MAX_SUBS) return 0;
  return atomic_load_explicit(&g_subs[slot].ring_errored, memory_order_acquire);
}

const uint8_t *dash_lldash_ring_peek(int slot, size_t *len) {
  dashchunk_sub_t *s;
  uint32_t wpos, rpos, idx, avail, contig;
  *len = 0;
  if (slot < 0 || slot >= DASHCHUNK_MAX_SUBS) return NULL;
  s = &g_subs[slot];
  if (!s->ring) return NULL;
  wpos = atomic_load_explicit(&s->wpos, memory_order_acquire);
  rpos = atomic_load_explicit(&s->rpos, memory_order_relaxed);
  if (wpos == rpos) return NULL;
  idx = rpos & (DASHCHUNK_RING_BYTES - 1u);
  avail = wpos - rpos;
  contig = DASHCHUNK_RING_BYTES - idx;
  if (contig > avail) contig = avail;
  *len = contig;
  return s->ring + idx;
}

void dash_lldash_ring_advance(int slot, size_t n) {
  if (slot < 0 || slot >= DASHCHUNK_MAX_SUBS) return;
  atomic_fetch_add_explicit(&g_subs[slot].rpos, (uint32_t)n, memory_order_release);
}

void dash_lldash_register_reactor_efd(int tid, int efd) {
  if (tid < 0 || tid >= DASHCHUNK_MAX_REACTOR_THREADS) return;
  g_reactor_efds[tid] = efd;
}

void dash_lldash_flush_ready(int tid) {
  pthread_mutex_lock(&g_subs_mtx);
  for (int i = 0; i < DASHCHUNK_MAX_SUBS; i++) {
    dashchunk_sub_t *s = &g_subs[i];
    if (!atomic_load_explicit(&s->alive, memory_order_relaxed)) continue;
    if ((s->proto != 2 && s->proto != 3) || s->reactor_tid != tid) continue;
    if (!dash_lldash_ring_pending(i) && !atomic_load_explicit(&s->finalized, memory_order_acquire) &&
        !atomic_load_explicit(&s->ring_errored, memory_order_acquire))
      continue;
#ifdef HAVE_HTTP2
    if (s->proto == 2) h2_dashchunk_wake(i);
#endif
#ifdef HAVE_HTTP3
    if (s->proto == 3) h3_dashchunk_wake(i);
#endif
  }
  pthread_mutex_unlock(&g_subs_mtx);
}

int dash_lldash_try_attach(conn_t *c, capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const char *filename, int keep_alive, const char *origin_hdr, int ws_handle) {
  char cors_hdr[192];
  char hdr[384];
  strbuf_t b;
  int idx = dash_lldash_subscribe(ctx, filter, pmt_pid, filename, 1);
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
  if (slot < 0 || slot >= DASHCHUNK_MAX_SUBS) return 1;
  return atomic_load_explicit(&g_subs[slot].finalized, memory_order_acquire);
}

void dash_lldash_sub_close(int slot) {
  uint8_t *ring;
  if (slot < 0 || slot >= DASHCHUNK_MAX_SUBS) return;
  pthread_mutex_lock(&g_subs_mtx);
  ring = g_subs[slot].ring;
  g_subs[slot].ring = NULL;
  atomic_store_explicit(&g_subs[slot].alive, 0, memory_order_release);
  pthread_mutex_unlock(&g_subs_mtx);
  free(ring);
}
