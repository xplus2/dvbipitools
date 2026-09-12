/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "segstore_int.h"
#include "reactor/internal.h"
#include "reactor/qsbr.h"
#include "version.h"
#include "lib/helper/log.h"

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ring holds 1 ref, each zc send adds 1 more, free (or pool-return) on last drop */
typedef struct {
  _Atomic int refcnt;
  int pool_class;
} seg_buf_hdr_t;

/* size-class freelist, reused across segment/part rotation */
#define SEG_POOL_CLASSES 15 /* up to 4096<<14 = 64 MiB */

typedef struct {
  _Atomic(seg_buf_hdr_t *) *slots; /* hls_set_seg_pool_cap()'d array, n slots, each NULL or a free buf */
  _Atomic int64_t last_used_ms;
} seg_pool_class_t;

static seg_pool_class_t seg_pool[SEG_POOL_CLASSES];
static int g_seg_pool_cap = 8;

#define SEG_POOL_IDLE_MS 30000

/* call once at startup, before any HLS traffic */
void hls_set_seg_pool_cap(int n) {
  if (n < 1) n = 1;
  g_seg_pool_cap = n;
  for (int i = 0; i < SEG_POOL_CLASSES; i++) seg_pool[i].slots = calloc((size_t)n, sizeof *seg_pool[i].slots);
}

static int seg_pool_class_for(size_t size) {
  size_t cap = 4096;
  int cls = 0;
  while (cap < size && cls < SEG_POOL_CLASSES - 1) {
    cap <<= 1;
    cls++;
  }
  return cls;
}

static size_t seg_pool_class_cap(int cls) { return (size_t)4096 << cls; }

uint8_t *seg_buf_alloc(size_t size) {
  int cls = seg_pool_class_for(size);
  seg_pool_class_t *c = &seg_pool[cls];
  seg_buf_hdr_t *h = NULL;

  if (c->slots) for (int i = 0; i < g_seg_pool_cap; i++) {
    seg_buf_hdr_t *expected = atomic_load_explicit(&c->slots[i], memory_order_relaxed);
    if (!expected) continue;
    if (atomic_compare_exchange_strong_explicit(&c->slots[i], &expected, NULL, memory_order_acquire, memory_order_relaxed)) {
      h = expected;
      break;
    }
  }
  if (h)
    atomic_store_explicit(&c->last_used_ms, now_ms(), memory_order_relaxed);
  else {
    h = malloc(sizeof(*h) + seg_pool_class_cap(cls));
    if (!h) return NULL;
    h->pool_class = cls;
  }
  atomic_init(&h->refcnt, 1);
  return (uint8_t *)(h + 1);
}

/* NULL-safe like free(): a zero-size push (hls_push_segment_ll with live_len==0) stores NULL */
void seg_buf_ref(uint8_t *data) {
  if (!data) return;
  seg_buf_hdr_t *h = (seg_buf_hdr_t *)data - 1;
  atomic_fetch_add_explicit(&h->refcnt, 1, memory_order_relaxed);
}

void seg_buf_unref(uint8_t *data) {
  seg_buf_hdr_t *h;
  seg_pool_class_t *c;
  if (!data) return;
  h = (seg_buf_hdr_t *)data - 1;
  if (atomic_fetch_sub_explicit(&h->refcnt, 1, memory_order_acq_rel) != 1) return;
  c = &seg_pool[h->pool_class];
  if (c->slots) for (int i = 0; i < g_seg_pool_cap; i++) {
    seg_buf_hdr_t *expected = NULL;
    if (atomic_compare_exchange_strong_explicit(&c->slots[i], &expected, h, memory_order_release, memory_order_relaxed)) {
      atomic_store_explicit(&c->last_used_ms, now_ms(), memory_order_relaxed);
      return;
    }
  }
  free(h);
}

/* keyed on last touch, not client count */
void hls_seg_pool_trim_idle(void) {
  int64_t now = now_ms();

  for (int i = 0; i < SEG_POOL_CLASSES; i++) {
    seg_pool_class_t *c = &seg_pool[i];
    int64_t last = atomic_load_explicit(&c->last_used_ms, memory_order_relaxed);
    if (!last || now - last < (int64_t)SEG_POOL_IDLE_MS) continue;
    for (int j = 0; j < g_seg_pool_cap; j++) {
      seg_buf_hdr_t *h = atomic_exchange_explicit(&c->slots[j], NULL, memory_order_acquire);
      free(h);
    }
  }
}

void seg_buf_release_cb(void *arg) { seg_buf_unref((uint8_t *)arg); }

static hls_store_t *g_stores;
static int g_stores_n;
static pthread_mutex_t *g_store_locks; /* writer-only now, see segstore_int.h */

enum { STORE_FREE = 0, STORE_OPENING = 1, STORE_OPEN = 2, STORE_CLOSING = 3 };
static _Atomic int *g_slot_state; /* kept out of hls_store_t: open() sets fields, no memset */

static hls_store_closing_cb g_store_closing_cb;
static hls_init_codecs_fn g_init_codecs_cb;

static qsbr_domain_t *g_segstore_qsbr;

void hls_store_set_qsbr(qsbr_domain_t *d) { g_segstore_qsbr = d; }

void hls_store_init(int max_channels) {
  int n = max_channels > 0 ? max_channels : 1;
  if (n > HLS_MAX_STORES) n = HLS_MAX_STORES;
  g_stores = calloc((size_t)n, sizeof *g_stores);
  g_store_locks = calloc((size_t)n, sizeof *g_store_locks);
  g_slot_state = calloc((size_t)n, sizeof *g_slot_state);
  if (!g_stores || !g_store_locks || !g_slot_state) {
    log_line(TOOL_NAME ": out of memory sizing store table (%d entries)", n);
    free(g_stores);
    free(g_store_locks);
    free(g_slot_state);
    g_stores = NULL;
    g_store_locks = NULL;
    g_slot_state = NULL;
    return;
  }
  g_stores_n = n;
}

pthread_mutex_t *store_lock(const hls_store_t *s) { return &g_store_locks[s - g_stores]; }
static _Atomic int *slot_state(const hls_store_t *s) { return &g_slot_state[s - g_stores]; }

hls_store_t *hls_store_find(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container) {
  for (int i = 0; i < g_stores_n; i++) {
    if (atomic_load_explicit(&g_slot_state[i], memory_order_acquire) != STORE_OPEN) continue;
    if (g_stores[i].cap_ctx == ctx && g_stores[i].pmt_pid == pmt_pid && g_stores[i].container == container &&
        pid_filter_equal(&g_stores[i].filter, filter) && lcevc_select_equal(&g_stores[i].lcevc, lcevc))
      return &g_stores[i];
  }
  return NULL;
}

static hls_snapshot_t *snap_clone(const hls_snapshot_t *base) {
  hls_snapshot_t *ns = malloc(sizeof *ns);
  if (!ns) return NULL;
  if (!base) {
    memset(ns, 0, sizeof *ns);
    return ns;
  }
  *ns = *base;
  for (int i = 0; i < ns->count; i++) seg_buf_ref(ns->segs[(ns->head + i) % HLS_MAX_SEGS].data);
  seg_buf_ref(ns->live_data);
  seg_buf_ref(ns->init_data);
  atomic_store_explicit(&ns->cache_plain, NULL, memory_order_relaxed);
  atomic_store_explicit(&ns->cache_ll, NULL, memory_order_relaxed);
  atomic_store_explicit(&ns->cache_lcevc[0], NULL, memory_order_relaxed);
  atomic_store_explicit(&ns->cache_lcevc[1], NULL, memory_order_relaxed);
  atomic_store_explicit(&ns->cache_mpd, NULL, memory_order_relaxed);
  atomic_store_explicit(&ns->cache_mpd_ll, NULL, memory_order_relaxed);
  return ns;
}

static void cached_text_free(cached_text_t *t) {
  if (!t) return;
  free(t->text);
  free(t);
}

static void snap_free(hls_snapshot_t *ns) {
  if (!ns) return;
  for (int i = 0; i < ns->count; i++) seg_buf_unref(ns->segs[(ns->head + i) % HLS_MAX_SEGS].data);
  seg_buf_unref(ns->live_data);
  seg_buf_unref(ns->init_data);
  cached_text_free(atomic_load_explicit(&ns->cache_plain, memory_order_relaxed));
  cached_text_free(atomic_load_explicit(&ns->cache_ll, memory_order_relaxed));
  cached_text_free(atomic_load_explicit(&ns->cache_lcevc[0], memory_order_relaxed));
  cached_text_free(atomic_load_explicit(&ns->cache_lcevc[1], memory_order_relaxed));
  cached_text_free(atomic_load_explicit(&ns->cache_mpd, memory_order_relaxed));
  cached_text_free(atomic_load_explicit(&ns->cache_mpd_ll, memory_order_relaxed));
  free(ns);
}

const cached_text_t *snapshot_cache_text(_Atomic(cached_text_t *) *slot, text_fmt_fn fmt, void *ctx, size_t buf_cap) {
  cached_text_t *cur = atomic_load_explicit(slot, memory_order_acquire);
  cached_text_t *nc;
  char *buf;
  cached_text_t *expected;
  if (cur) return cur;
  nc = malloc(sizeof *nc);
  if (!nc) return NULL;
  buf = malloc(buf_cap);
  if (!buf) {
    free(nc);
    return NULL;
  }
  nc->len = fmt(ctx, buf, buf_cap);
  nc->text = buf;
  expected = NULL;
  if (atomic_compare_exchange_strong_explicit(slot, &expected, nc, memory_order_release, memory_order_acquire)) return nc;
  free(buf);
  free(nc);
  return expected;
}

/* pump-thread only: worker self-block risks deadlock */
static void snap_retire(hls_store_t *s, hls_snapshot_t *old) {
  if (!old) return;
  for (;;) {
    for (int i = 0; i < s->retiring_n; ) {
      if (qsbr_mark_passed(g_segstore_qsbr, s->retiring_mark[i])) {
        snap_free(s->retiring[i]);
        s->retiring[i] = s->retiring[--s->retiring_n];
        memcpy(s->retiring_mark[i], s->retiring_mark[s->retiring_n], sizeof s->retiring_mark[i]);
      } else i++;
    }
    if (s->retiring_n < HLS_SNAP_RETIRE_DEPTH) break;
    pthread_mutex_unlock(store_lock(s));
    { struct timespec ts = {0, 1000000}; nanosleep(&ts, NULL); }
    pthread_mutex_lock(store_lock(s));
  }

  qsbr_mark(g_segstore_qsbr, s->retiring_mark[s->retiring_n]);
  s->retiring[s->retiring_n] = old;
  s->retiring_n++;
}

typedef struct slot_retire_node {
  int idx;
  hls_snapshot_t *snaps[HLS_SNAP_RETIRE_DEPTH + 1];
  int nsnaps;
  uint64_t *mark;
  _Atomic(struct slot_retire_node *) next;
} slot_retire_node_t;

static _Atomic(slot_retire_node_t *) g_slot_retire_head;

static void slot_retire_push(slot_retire_node_t *node) {
  slot_retire_node_t *old_head = atomic_load_explicit(&g_slot_retire_head, memory_order_relaxed);
  for (;;) {
    atomic_store_explicit(&node->next, old_head, memory_order_relaxed);
    if (atomic_compare_exchange_weak_explicit(&g_slot_retire_head, &old_head, node, memory_order_release, memory_order_relaxed)) break;
  }
}

void hls_store_slot_reclaim_sweep(void) {
  slot_retire_node_t *chain = atomic_exchange_explicit(&g_slot_retire_head, NULL, memory_order_acquire);
  slot_retire_node_t *keep_head = NULL;
  slot_retire_node_t *keep_tail = NULL;
  while (chain) {
    slot_retire_node_t *next = atomic_load_explicit(&chain->next, memory_order_relaxed);
    if (!chain->mark || qsbr_mark_passed(g_segstore_qsbr, chain->mark)) {
      for (int i = 0; i < chain->nsnaps; i++) snap_free(chain->snaps[i]);
      if (chain->idx >= 0) atomic_store_explicit(&g_slot_state[chain->idx], STORE_FREE, memory_order_release);
      free(chain->mark);
      free(chain);
    } else {
      atomic_store_explicit(&chain->next, keep_head, memory_order_relaxed);
      keep_head = chain;
      if (!keep_tail) keep_tail = chain;
    }
    chain = next;
  }
  if (keep_head) {
    slot_retire_node_t *old_head = atomic_load_explicit(&g_slot_retire_head, memory_order_relaxed);
    for (;;) {
      atomic_store_explicit(&keep_tail->next, old_head, memory_order_relaxed);
      if (atomic_compare_exchange_weak_explicit(&g_slot_retire_head, &old_head, keep_head, memory_order_release, memory_order_relaxed)) break;
    }
  }
}

/* non-blocking, safe for a reactor worker thread to call */
static void snap_retire_async(hls_snapshot_t *snap) {
  slot_retire_node_t *node;
  int nw;
  if (!snap) return;
  node = malloc(sizeof *node);
  if (!node) {
    snap_free(snap);
    return;
  }
  node->idx = -1;
  node->nsnaps = 1;
  node->snaps[0] = snap;
  nw = qsbr_worker_count(g_segstore_qsbr);
  nw = nw > 0 ? nw : 1;
  node->mark = calloc((size_t)nw, sizeof *node->mark);
  if (node->mark) qsbr_mark(g_segstore_qsbr, node->mark);
  slot_retire_push(node);
}

/* caller holds store_lock(s), non-blocking */
static void snap_drain_all_async(hls_store_t *s) {
  hls_snapshot_t *cur = atomic_exchange_explicit(&s->snap, NULL, memory_order_acq_rel);
  snap_retire_async(cur);
  for (int i = 0; i < s->retiring_n; i++) snap_retire_async(s->retiring[i]);
  s->retiring_n = 0;
}

/* RFC8216 4.3.3.1: TARGETDURATION must not change. td_hw immune to ring eviction shrink. */
int hls_target_duration(const hls_snapshot_t *snap) {
  int td = (int)ceil(snap->td_hw);
  return td > 0 ? td : 1;
}

void hls_store_open(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, double seg_target, int max_segs, seg_container_t container) {
  hls_store_t *s;
  int expected;
  if (max_segs < 2) max_segs = 2;
  if (max_segs > HLS_MAX_SEGS) max_segs = HLS_MAX_SEGS;

  for (;;) {
    s = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
    if (s) {
      expected = STORE_OPEN;
      if (atomic_compare_exchange_strong_explicit(slot_state(s), &expected, STORE_OPENING, memory_order_acq_rel, memory_order_relaxed)) break;
      continue;
    }
    for (int i = 0; i < g_stores_n; i++) {
      expected = STORE_FREE;
      if (atomic_compare_exchange_strong_explicit(&g_slot_state[i], &expected, STORE_OPENING, memory_order_acq_rel, memory_order_relaxed)) {
        s = &g_stores[i];
        break;
      }
    }
    break;
  }
  if (!s) return;
  pthread_mutex_lock(store_lock(s));
  snap_drain_all_async(s);
  s->cap_ctx = ctx;
  s->filter = *filter;
  s->pmt_pid = pmt_pid;
  s->lcevc = *lcevc;
  s->seg_target = seg_target;
  s->max_segs = max_segs;
  s->container = container;
  s->opened_at = time(NULL);
  atomic_init(&s->lldash_sub_head, -1);
  atomic_store_explicit(slot_state(s), STORE_OPEN, memory_order_release);
  pthread_mutex_unlock(store_lock(s));
}

void hls_store_close(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container) {
  hls_store_t *s = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
  int expected = STORE_OPEN;
  slot_retire_node_t *node;
  int nw;
  if (!s) return;
  if (!atomic_compare_exchange_strong_explicit(slot_state(s), &expected, STORE_CLOSING, memory_order_acq_rel, memory_order_relaxed)) return;
  node = malloc(sizeof *node);
  if (!node) {
    log_line(TOOL_NAME ": hls: close retire alloc failed, freeing slot without a QSBR wait");
    atomic_store_explicit(slot_state(s), STORE_FREE, memory_order_release);
    return;
  }
  node->idx = (int)(s - g_stores);
  node->nsnaps = 0;

  pthread_mutex_lock(store_lock(s));
  {
    hls_snapshot_t *cur = atomic_exchange_explicit(&s->snap, NULL, memory_order_acq_rel);
    if (cur) node->snaps[node->nsnaps++] = cur;
    for (int i = 0; i < s->retiring_n; i++) node->snaps[node->nsnaps++] = s->retiring[i];
    s->retiring_n = 0;
  }
  pthread_mutex_unlock(store_lock(s));

  nw = qsbr_worker_count(g_segstore_qsbr);
  nw = nw > 0 ? nw : 1;
  node->mark = calloc((size_t)nw, sizeof *node->mark);
  if (node->mark) qsbr_mark(g_segstore_qsbr, node->mark);
  slot_retire_push(node);

  if (g_store_closing_cb) g_store_closing_cb(s);
}

int hls_set_init_segment_at(hls_store_t *s, codec_t video_codec, const uint8_t *data, size_t size) {
  hls_snapshot_t *old;
  hls_snapshot_t *ns;
  if (size > HLS_INIT_SEG_MAX) {
    log_line("hls_set_init_segment: %zu exceeds HLS_INIT_SEG_MAX %d", size, HLS_INIT_SEG_MAX);
    return -1;
  }
  pthread_mutex_lock(store_lock(s));
  old = atomic_load_explicit(&s->snap, memory_order_acquire);
  ns = snap_clone(old);
  if (!ns) {
    pthread_mutex_unlock(store_lock(s));
    return -1;
  }
  {
    uint8_t *nd = seg_buf_alloc(size);
    if (!nd) {
      snap_free(ns);
      pthread_mutex_unlock(store_lock(s));
      return -1;
    }
    memcpy(nd, data, size);
    seg_buf_unref(ns->init_data);
    ns->init_data = nd;
  }
  ns->init_size = size;
  ns->init_gen++;
  ns->video_codec = video_codec;
  if (g_init_codecs_cb)
    g_init_codecs_cb(ns->init_data, ns->init_size, video_codec, ns->vcodec_str, sizeof ns->vcodec_str, ns->acodec_str, sizeof ns->acodec_str);
  else {
    ns->vcodec_str[0] = '\0';
    ns->acodec_str[0] = '\0';
  }
  atomic_store_explicit(&s->snap, ns, memory_order_release);
  snap_retire(s, old);
  pthread_mutex_unlock(store_lock(s));
  return 0;
}

int hls_set_init_segment(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container, codec_t video_codec, const uint8_t *data, size_t size) {
  hls_store_t *s = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
  if (!s) return -1;
  return hls_set_init_segment_at(s, video_codec, data, size);
}

int hls_set_lcevc_pids(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container, const unsigned *lcevc_pid, int lcevc_pid_count) {
  hls_store_t *s;
  hls_snapshot_t *old;
  hls_snapshot_t *ns;
  if (lcevc_pid_count > PSI_LCEVC_MAX_LINKS) lcevc_pid_count = PSI_LCEVC_MAX_LINKS;
  s = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
  if (!s) return -1;
  pthread_mutex_lock(store_lock(s));
  old = atomic_load_explicit(&s->snap, memory_order_acquire);
  ns = snap_clone(old);
  if (!ns) {
    pthread_mutex_unlock(store_lock(s));
    return -1;
  }
  for (int i = 0; i < lcevc_pid_count; i++) ns->lcevc_pid[i] = lcevc_pid[i];
  ns->lcevc_pid_count = lcevc_pid_count;
  atomic_store_explicit(&s->snap, ns, memory_order_release);
  snap_retire(s, old);
  pthread_mutex_unlock(store_lock(s));
  return 0;
}

int hls_push_segment_at(hls_store_t *s, const uint8_t *data, size_t size, double duration) {
  hls_snapshot_t *old;
  hls_snapshot_t *ns;
  uint8_t *copy;
  int idx;

  copy = seg_buf_alloc(size);
  if (!copy) return -1;
  memcpy(copy, data, size);

  pthread_mutex_lock(store_lock(s));
  old = atomic_load_explicit(&s->snap, memory_order_acquire);
  ns = snap_clone(old);
  if (!ns) {
    pthread_mutex_unlock(store_lock(s));
    seg_buf_unref(copy);
    return -1;
  }
  if (ns->count >= s->max_segs) {
    seg_buf_unref(ns->segs[ns->head].data); /* not carried forward, cancel clone's ref */
    ns->segs[ns->head].data = NULL;
    ns->head = (ns->head + 1) % HLS_MAX_SEGS;
    ns->count--;
    ns->oldest_seq++;
  }
  idx = (ns->head + ns->count) % HLS_MAX_SEGS;
  ns->segs[idx].data = copy;
  ns->segs[idx].size = size;
  ns->segs[idx].duration = duration;
  ns->segs[idx].seq = ns->next_seq;
  ns->segs[idx].start_ms = ns->cum_ms;
  ns->segs[idx].parts.count = 0;
  ns->next_seq++;
  ns->cum_ms += (uint64_t)(duration * 1000.0 + 0.5);
  ns->count++;
  if (duration > ns->td_hw) ns->td_hw = duration;
  atomic_store_explicit(&s->snap, ns, memory_order_release);
  snap_retire(s, old);
  pthread_mutex_unlock(store_lock(s));
  return 0;
}

int hls_push_segment(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container, const uint8_t *data, size_t size, double duration) {
  hls_store_t *s = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
  if (!s) return -1;
  return hls_push_segment_at(s, data, size, duration);
}

void hls_llhls_enable(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container, double part_target) {
  hls_store_t *s = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
  hls_snapshot_t *old;
  hls_snapshot_t *ns;
  if (!s) return;
  pthread_mutex_lock(store_lock(s));
  old = atomic_load_explicit(&s->snap, memory_order_acquire);
  ns = snap_clone(old);
  if (!ns) {
    pthread_mutex_unlock(store_lock(s));
    return;
  }
  ns->part_target = part_target;
  ns->live_msn = ns->next_seq;
  atomic_store_explicit(&s->snap, ns, memory_order_release);
  pthread_mutex_unlock(store_lock(s));
  snap_retire_async(old);
}

static int live_buf_ensure(hls_snapshot_t *ns, size_t need) {
  uint8_t *nb;
  if (ns->live_data && ns->live_cap >= need) return 0;
  nb = seg_buf_alloc(need);
  if (!nb) return -1;
  if (ns->live_data && ns->live_len) memcpy(nb, ns->live_data, ns->live_len);
  seg_buf_unref(ns->live_data);
  ns->live_data = nb;
  ns->live_cap = seg_pool_class_cap(seg_pool_class_for(need));
  return 0;
}

static hls_part_pushed_cb g_part_pushed_cb;
static hls_segment_done_cb g_segment_done_cb;

void hls_set_part_pushed_cb(hls_part_pushed_cb cb) { g_part_pushed_cb = cb; }
void hls_set_segment_done_cb(hls_segment_done_cb cb) { g_segment_done_cb = cb; }
void hls_set_store_closing_cb(hls_store_closing_cb cb) { g_store_closing_cb = cb; }
void hls_set_init_codecs_cb(hls_init_codecs_fn cb) { g_init_codecs_cb = cb; }

int hls_push_part_at(hls_store_t *s, const uint8_t *data, size_t size, double duration, int independent) {
  hls_snapshot_t *old;
  hls_snapshot_t *ns;
  int n;
  uint32_t live_msn;

  pthread_mutex_lock(store_lock(s));
  old = atomic_load_explicit(&s->snap, memory_order_acquire);
  ns = snap_clone(old);
  if (!ns) {
    pthread_mutex_unlock(store_lock(s));
    return -1;
  }
  if (ns->live_parts.count >= HLS_MAX_PARTS || live_buf_ensure(ns, ns->live_len + size) < 0) {
    snap_free(ns);
    pthread_mutex_unlock(store_lock(s));
    return -1;
  }
  n = ns->live_parts.count;
  ns->live_parts.offset[n] = ns->live_len;
  ns->live_parts.size[n] = size;
  ns->live_parts.duration[n] = duration;
  ns->live_parts.independent[n] = (uint8_t)(independent ? 1 : 0);
  ns->live_parts.count = n + 1;
  memcpy(ns->live_data + ns->live_len, data, size);
  ns->live_len += size;
  live_msn = ns->live_msn;
  atomic_store_explicit(&s->snap, ns, memory_order_release);
  snap_retire(s, old);
  pthread_mutex_unlock(store_lock(s));
  if (g_part_pushed_cb) g_part_pushed_cb(s, live_msn, data, size);
  return 0;
}

int hls_push_part(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container, const uint8_t *data, size_t size, double duration, int independent) {
  hls_store_t *s = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
  if (!s) return -1;
  return hls_push_part_at(s, data, size, duration, independent);
}

int hls_push_segment_ll_at(hls_store_t *s, double duration) {
  hls_snapshot_t *old;
  hls_snapshot_t *ns;
  int idx;
  uint32_t seq;
  uint8_t *copy = NULL;

  pthread_mutex_lock(store_lock(s));
  old = atomic_load_explicit(&s->snap, memory_order_acquire);
  ns = snap_clone(old);
  if (!ns) {
    pthread_mutex_unlock(store_lock(s));
    return -1;
  }
  if (ns->live_len) {
    copy = seg_buf_alloc(ns->live_len);
    if (!copy) {
      snap_free(ns);
      pthread_mutex_unlock(store_lock(s));
      return -1;
    }
    memcpy(copy, ns->live_data, ns->live_len);
  }
  if (ns->count >= s->max_segs) {
    seg_buf_unref(ns->segs[ns->head].data);
    ns->segs[ns->head].data = NULL;
    ns->head = (ns->head + 1) % HLS_MAX_SEGS;
    ns->count--;
    ns->oldest_seq++;
  }
  seq = ns->next_seq;
  idx = (ns->head + ns->count) % HLS_MAX_SEGS;
  ns->segs[idx].data = copy;
  ns->segs[idx].size = ns->live_len;
  ns->segs[idx].duration = duration;
  ns->segs[idx].seq = seq;
  ns->segs[idx].start_ms = ns->cum_ms;
  ns->cum_ms += (uint64_t)(duration * 1000.0 + 0.5);
  ns->segs[idx].parts = ns->live_parts;
  ns->next_seq++;
  ns->count++;
  if (duration > ns->td_hw) ns->td_hw = duration;
  seg_buf_unref(ns->live_data); /* next cycle allocates fresh, older snapshots keep their own ref */
  ns->live_data = NULL;
  ns->live_cap = 0;
  ns->live_len = 0;
  ns->live_parts.count = 0;
  ns->live_msn = ns->next_seq;
  atomic_store_explicit(&s->snap, ns, memory_order_release);
  snap_retire(s, old);
  pthread_mutex_unlock(store_lock(s));
  if (g_segment_done_cb) g_segment_done_cb(s, seq);
  return 0;
}

int hls_push_segment_ll(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container, double duration) {
  hls_store_t *s = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
  if (!s) return -1;
  return hls_push_segment_ll_at(s, duration);
}

int hls_store_ready(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container) {
  const hls_store_t *s = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
  const hls_snapshot_t *snap;
  if (!s) return 0;
  snap = atomic_load_explicit(&s->snap, memory_order_acquire);
  return snap && snap->count > 0;
}

int hls_ll_store_ready(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container) {
  const hls_store_t *s = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
  const hls_snapshot_t *snap;
  if (!s) return 0;
  snap = atomic_load_explicit(&s->snap, memory_order_acquire);
  return snap && snap->part_target > 0.0 && (snap->count > 0 || snap->live_parts.count > 0);
}

int hls_part_available(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container, uint32_t want_seg, int want_part) {
  const hls_store_t *s = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
  const hls_snapshot_t *snap;
  if (!s) return 0;
  snap = atomic_load_explicit(&s->snap, memory_order_acquire);
  if (!snap) return 0;
  if (snap->live_msn == want_seg && snap->live_parts.count > want_part) return 1;
  if (snap->count > 0) {
    uint32_t oldest = snap->oldest_seq;
    uint32_t last = oldest + (uint32_t)snap->count - 1u;
    if (want_seg >= oldest && want_seg <= last) {
      const hls_seg_t *seg = &snap->segs[(snap->head + (int)(want_seg - oldest)) % HLS_MAX_SEGS];
      return seg->parts.count > want_part || (want_part == 0 && seg->parts.count == 0);
    }
  }
  return 0;
}
