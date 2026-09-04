/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "segstore_int.h"
#include "reactor/internal.h"
#include "version.h"
#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

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
  for (int i = 0; i < SEG_POOL_CLASSES; i++)
    seg_pool[i].slots = calloc((size_t)n, sizeof *seg_pool[i].slots);
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
    if (!last || now - last < (int64_t)SEG_POOL_IDLE_MS)
      continue;
    for (int j = 0; j < g_seg_pool_cap; j++) {
      seg_buf_hdr_t *h = atomic_exchange_explicit(&c->slots[j], NULL, memory_order_acquire);
      free(h);
    }
  }
}

void seg_buf_release_cb(void *arg) { seg_buf_unref((uint8_t *)arg); }

static hls_store_t *g_stores;
static int g_stores_n;
/* zero-init == PTHREAD_MUTEX_INITIALIZER on glibc/Linux (this project's only target).
   kept out of hls_store_t: hls_store_open() memsets on reuse, mutex storage must never be touched except via pthread_mutex_* calls */
static pthread_mutex_t *g_store_locks;
/* guard store identity and free-slot claim only. content (segs/live_data/counts/...) guarded by store's g_store_locks[] entry */
static pthread_mutex_t g_table_mtx = PTHREAD_MUTEX_INITIALIZER;

static hls_store_closing_cb g_store_closing_cb;

void hls_store_init(int max_channels) {
  int n = max_channels > 0 ? max_channels : 1;
  if (n > HLS_MAX_STORES) n = HLS_MAX_STORES;
  g_stores = calloc((size_t)n, sizeof *g_stores);
  g_store_locks = calloc((size_t)n, sizeof *g_store_locks);
  if (!g_stores || !g_store_locks) {
    log_line(TOOL_NAME ": out of memory sizing store table (%d entries)", n);
    free(g_stores);
    free(g_store_locks);
    g_stores = NULL;
    g_store_locks = NULL;
    return;
  }
  g_stores_n = n;
}

pthread_mutex_t *store_lock(const hls_store_t *s) { return &g_store_locks[s - g_stores]; }

/* caller must hold g_table_mtx. container in key: ts, fmp4 segmenters coexist per channel */
static hls_store_t *find_store(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, seg_container_t container) {
  for (int i = 0; i < g_stores_n; i++)
    if (g_stores[i].open && g_stores[i].cap_ctx == ctx && g_stores[i].pmt_pid == pmt_pid && g_stores[i].container == container &&
        pid_filter_equal(&g_stores[i].filter, filter))
      return &g_stores[i];
  return NULL;
}

hls_store_t *find_store_locked(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, seg_container_t container) {
  hls_store_t *s;
  pthread_mutex_lock(&g_table_mtx);
  s = find_store(ctx, filter, pmt_pid, container);
  if (s) pthread_mutex_lock(store_lock(s));
  pthread_mutex_unlock(&g_table_mtx);
  return s;
}

/* RFC8216 4.3.3.1: TARGETDURATION must not change. td_hw immune to ring eviction shrink. */
int hls_target_duration(const hls_store_t *s) {
  int td = (int)ceil(s->td_hw);
  return td > 0 ? td : 1;
}

/* caller must hold store's lock */
static void evict_oldest(hls_store_t *s) {
  if (s->count == 0) return;
  seg_buf_unref(s->segs[s->head].data);
  s->segs[s->head].data = NULL;
  s->head = (s->head + 1) % HLS_MAX_SEGS;
  s->count--;
  s->oldest_seq++;
}

static void free_all_segs(hls_store_t *s) {
  while (s->count > 0)
    evict_oldest(s);
}

/* caller must hold store's lock. pop slots until count < max_segs, collect freed
   ptrs into out: caller seg_buf_unref()s after unlock, keeps free() off lock's critical path */
static int evict_to_fit_collect(hls_store_t *s, uint8_t **out, int max_out) {
  int n = 0;
  while (s->count >= s->max_segs && n < max_out) {
    out[n++] = s->segs[s->head].data;
    s->segs[s->head].data = NULL;
    s->head = (s->head + 1) % HLS_MAX_SEGS;
    s->count--;
    s->oldest_seq++;
  }
  return n;
}

/* open/close rare (once per stream lifecycle, not per-request): held under table_mtx + store lock, no sharding */
void hls_store_open(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, double seg_target, int max_segs, seg_container_t container) {
  hls_store_t *s;
  if (max_segs < 2) max_segs = 2;
  if (max_segs > HLS_MAX_SEGS) max_segs = HLS_MAX_SEGS;
  pthread_mutex_lock(&g_table_mtx);
  s = find_store(ctx, filter, pmt_pid, container);
  if (!s) for (int i = 0; i < g_stores_n; i++) if (!g_stores[i].open) {
    s = &g_stores[i];
    break;
  }
  if (!s) {
    pthread_mutex_unlock(&g_table_mtx);
    return; /* out of slots */
  }
  pthread_mutex_lock(store_lock(s));
  free_all_segs(s);
  free(s->live_data);
  memset(s, 0, sizeof *s);
  s->cap_ctx = ctx;
  s->filter = *filter;
  s->pmt_pid = pmt_pid;
  s->seg_target = seg_target;
  s->td_hw = 0.0; /* set by first push. unreachable at count==0 anyway */
  s->max_segs = max_segs;
  s->container = container;
  s->open = 1;
  s->opened_at = time(NULL);
  atomic_init(&s->lldash_sub_head, -1);
  pthread_mutex_unlock(store_lock(s));
  pthread_mutex_unlock(&g_table_mtx);
}

void hls_store_close(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, seg_container_t container) {
  hls_store_t *s;
  pthread_mutex_lock(&g_table_mtx);
  s = find_store(ctx, filter, pmt_pid, container);
  if (s) {
    pthread_mutex_lock(store_lock(s));
    free_all_segs(s);
    s->init_size = 0;
    free(s->live_data);
    s->live_data = NULL;
    s->open = 0;
    pthread_mutex_unlock(store_lock(s));
    if (g_store_closing_cb) g_store_closing_cb(s);
  }
  pthread_mutex_unlock(&g_table_mtx);
}

int hls_set_init_segment(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, seg_container_t container, codec_t video_codec, const uint8_t *data, size_t size) {
  hls_store_t *s;
  if (size > HLS_INIT_SEG_MAX) {
    log_line("hls_set_init_segment: %zu exceeds HLS_INIT_SEG_MAX %d", size, HLS_INIT_SEG_MAX);
    return -1;
  }

  s = find_store_locked(ctx, filter, pmt_pid, container);
  if (!s) return -1;
  memcpy(s->init_data, data, size);
  s->init_size = size;
  s->init_gen++;
  s->video_codec = video_codec;
  pthread_mutex_unlock(store_lock(s));
  return 0;
}

int hls_push_segment(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, seg_container_t container, const uint8_t *data, size_t size, double duration) {
  uint8_t *copy;
  uint8_t *evicted[HLS_MAX_SEGS];
  int nevicted;
  hls_store_t *s;
  int idx;
  uint32_t seq;
  copy = seg_buf_alloc(size);
  if (!copy) return -1;
  memcpy(copy, data, size);
  s = find_store_locked(ctx, filter, pmt_pid, container);
  if (!s) {
    seg_buf_unref(copy);
    return -1;
  }
  nevicted = evict_to_fit_collect(s, evicted, HLS_MAX_SEGS);
  seq = s->next_seq++;
  idx = (s->head + s->count) % HLS_MAX_SEGS;
  s->segs[idx].data = copy;
  s->segs[idx].size = size;
  s->segs[idx].duration = duration;
  s->segs[idx].seq = seq;
  s->segs[idx].start_ms = s->cum_ms;
  s->cum_ms += (uint64_t)(duration * 1000.0 + 0.5);
  s->segs[idx].parts.count = 0; /* ring slot reused: clear a prior cycle's parts */
  s->count++;
  if (duration > s->td_hw) s->td_hw = duration;
  pthread_mutex_unlock(store_lock(s));
  for (int i = 0; i < nevicted; i++) seg_buf_unref(evicted[i]);
  return 0;
}

void hls_llhls_enable(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, seg_container_t container, double part_target) {
  hls_store_t *s = find_store_locked(ctx, filter, pmt_pid, container);
  if (!s)
    return;
  s->part_target = part_target;
  s->live_msn = s->next_seq;
  pthread_mutex_unlock(store_lock(s));
}

static int live_reserve(hls_store_t *s, size_t need) {
  return growbuf_reserve((void **)&s->live_data, &s->live_cap, 1, need, 65536);
}

static hls_part_pushed_cb g_part_pushed_cb;
static hls_segment_done_cb g_segment_done_cb;

void hls_set_part_pushed_cb(hls_part_pushed_cb cb) { g_part_pushed_cb = cb; }
void hls_set_segment_done_cb(hls_segment_done_cb cb) { g_segment_done_cb = cb; }
void hls_set_store_closing_cb(hls_store_closing_cb cb) { g_store_closing_cb = cb; }

int hls_push_part(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, seg_container_t container, const uint8_t *data, size_t size, double duration, int independent) {
  hls_store_t *s;
  int n;
  uint32_t live_msn;
  s = find_store_locked(ctx, filter, pmt_pid, container);
  if (!s) return -1;
  if (s->live_parts.count >= HLS_MAX_PARTS || live_reserve(s, s->live_len + size) < 0) {
    pthread_mutex_unlock(store_lock(s));
    return -1;
  }
  n = s->live_parts.count;
  s->live_parts.offset[n] = s->live_len;
  s->live_parts.size[n] = size;
  s->live_parts.duration[n] = duration;
  s->live_parts.independent[n] = (uint8_t)(independent ? 1 : 0);
  s->live_parts.count = n + 1;
  memcpy(s->live_data + s->live_len, data, size);
  s->live_len += size;
  live_msn = s->live_msn;
  pthread_mutex_unlock(store_lock(s));
  if (g_part_pushed_cb) g_part_pushed_cb(s, live_msn, data, size);
  return 0;
}

int hls_push_segment_ll(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, seg_container_t container, double duration) {
  hls_store_t *s;
  uint8_t *evicted[HLS_MAX_SEGS];
  int nevicted;
  int idx;
  uint32_t seq;
  uint8_t *copy = NULL;

  s = find_store_locked(ctx, filter, pmt_pid, container);
  if (!s) return -1;
  if (s->live_len) {
    copy = seg_buf_alloc(s->live_len);
    if (!copy) {
      pthread_mutex_unlock(store_lock(s));
      return -1;
    }
    memcpy(copy, s->live_data, s->live_len);
  }
  nevicted = evict_to_fit_collect(s, evicted, HLS_MAX_SEGS);

  seq = s->next_seq++;
  idx = (s->head + s->count) % HLS_MAX_SEGS;
  s->segs[idx].data = copy;
  s->segs[idx].size = s->live_len;
  s->segs[idx].duration = duration;
  s->segs[idx].seq = seq;
  s->segs[idx].start_ms = s->cum_ms;
  s->cum_ms += (uint64_t)(duration * 1000.0 + 0.5);
  s->segs[idx].parts = s->live_parts;
  s->count++;
  if (duration > s->td_hw) s->td_hw = duration;
  s->live_len = 0;
  s->live_parts.count = 0;
  s->live_msn = s->next_seq;
  pthread_mutex_unlock(store_lock(s));
  for (int i = 0; i < nevicted; i++) seg_buf_unref(evicted[i]);
  if (g_segment_done_cb) g_segment_done_cb(s, seq);
  return 0;
}

int hls_store_ready(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, seg_container_t container) {
  const hls_store_t *s = find_store_locked(ctx, filter, pmt_pid, container);
  int ready;
  if (!s) return 0;
  ready = s->count > 0;
  pthread_mutex_unlock(store_lock(s));
  return ready;
}

int hls_ll_store_ready(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, seg_container_t container) {
  const hls_store_t *s = find_store_locked(ctx, filter, pmt_pid, container);
  int ready;
  if (!s) return 0;
  ready = s->part_target > 0.0 && (s->count > 0 || s->live_parts.count > 0);
  pthread_mutex_unlock(store_lock(s));
  return ready;
}

int hls_part_available(capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, seg_container_t container, uint32_t want_seg, int want_part) {
  const hls_store_t *s = find_store_locked(ctx, filter, pmt_pid, container);
  int found = 0;
  if (!s) return 0;
  if (s->live_msn == want_seg && s->live_parts.count > want_part) found = 1;
  else if (s->count > 0) {
    uint32_t oldest = s->oldest_seq, last = oldest + (uint32_t)s->count - 1u;
    if (want_seg >= oldest && want_seg <= last) {
      const hls_seg_t *seg = &s->segs[(s->head + (int)(want_seg - oldest)) % HLS_MAX_SEGS];
      found = seg->parts.count > want_part || (want_part == 0 && seg->parts.count == 0);
    }
  }
  pthread_mutex_unlock(store_lock(s));
  return found;
}
