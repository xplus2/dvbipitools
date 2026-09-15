/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"
#include "lib/helper/log.h"

#include <stdatomic.h>
#include <string.h>

static hls_init_codecs_fn g_init_codecs_cb;

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

int hls_push_segment(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container, const uint8_t *data, size_t size, double duration) {
  hls_store_t *s = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
  if (!s) return -1;
  return hls_push_segment_at(s, data, size, duration);
}

void hls_llhls_enable(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container, double part_target) {
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

int hls_push_part(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container, const uint8_t *data, size_t size, double duration, int independent) {
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

int hls_push_segment_ll(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container, double duration) {
  hls_store_t *s = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
  if (!s) return -1;
  return hls_push_segment_ll_at(s, duration);
}

void hls_set_init_codecs_cb(hls_init_codecs_fn cb) { g_init_codecs_cb = cb; }

int hls_store_ready(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container) {
  const hls_store_t *s = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
  const hls_snapshot_t *snap;
  if (!s) return 0;
  snap = atomic_load_explicit(&s->snap, memory_order_acquire);
  return snap && snap->count > 0;
}

int hls_ll_store_ready(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container) {
  const hls_store_t *s = hls_store_find(ctx, filter, pmt_pid, lcevc, container);
  const hls_snapshot_t *snap;
  if (!s) return 0;
  snap = atomic_load_explicit(&s->snap, memory_order_acquire);
  return snap && snap->part_target > 0.0 && (snap->count > 0 || snap->live_parts.count > 0);
}

int hls_part_available(const capture_ctx_t *ctx, const pid_filter_t *filter, unsigned pmt_pid, const lcevc_select_t *lcevc, seg_container_t container, uint32_t want_seg, int want_part) {
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
