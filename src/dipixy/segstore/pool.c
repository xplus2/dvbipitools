/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"
#include "lib/helper/ioutil.h"

#include <stdatomic.h>
#include <stdlib.h>

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

int seg_pool_class_for(size_t size) {
  size_t cap = 4096;
  int cls = 0;
  while (cap < size && cls < SEG_POOL_CLASSES - 1) {
    cap <<= 1;
    cls++;
  }
  return cls;
}

size_t seg_pool_class_cap(int cls) { return (size_t)4096 << cls; }

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
