/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"
#include "../reactor/wake.h"
#include "lib/helper/log.h"

#include <stdatomic.h>
#include <string.h>

static hls_init_codecs_fn g_init_codecs_cb;

static hls_snapshot_t *snap_begin(hls_store_t *s, hls_snapshot_t **old) {
  hls_snapshot_t *ns;
  pthread_mutex_lock(store_lock(s));
  *old = atomic_load_explicit(&s->snap, memory_order_acquire);
  ns = snap_clone_into(s->spare, *old);
  if (!ns) pthread_mutex_unlock(store_lock(s));
  else if (ns == s->spare) s->spare = NULL;
  return ns;
}

static void snap_commit(hls_store_t *s, hls_snapshot_t *old, hls_snapshot_t *ns) {
  atomic_store_explicit(&s->snap, ns, memory_order_release);
  snap_retire(s, old);
  pthread_mutex_unlock(store_lock(s));
}

static void snap_abort(hls_store_t *s, hls_snapshot_t *ns) {
  snap_recycle(s, ns);
  pthread_mutex_unlock(store_lock(s));
}

/* append-only write hits slot outside every sharer's window: cow only on eviction */
static int ring_make_room(hls_snapshot_t *ns, int max_segs) {
  if (ns->count < max_segs) return 0;
  if (!ring_cow(ns)) return -1;
  seg_buf_unref(ns->ring->segs[ns->head].data);
  ns->ring->segs[ns->head].data = NULL;
  ns->head = (ns->head + 1) % HLS_MAX_SEGS;
  ns->count--;
  ns->oldest_seq++;
  return 0;
}

int hls_set_init_segment_at(hls_store_t *s, codec_t video_codec, const uint8_t *data, size_t size) {
  hls_snapshot_t *old;
  hls_snapshot_t *ns;
  uint8_t *nd;
  if (size > HLS_INIT_SEG_MAX) {
    log_code("YE400204", "hls_set_init_segment: %zu exceeds HLS_INIT_SEG_MAX %d", size, HLS_INIT_SEG_MAX);
    return -1;
  }
  ns = snap_begin(s, &old);
  if (!ns) return -1;
  nd = seg_buf_alloc(size);
  if (!nd) {
    snap_abort(s, ns);
    return -1;
  }
  memcpy(nd, data, size);
  seg_buf_unref(ns->init_data);
  ns->init_data = nd;
  ns->init_size = size;
  ns->init_gen++;
  ns->video_codec = video_codec;
  if (g_init_codecs_cb)
    g_init_codecs_cb(ns->init_data, ns->init_size, video_codec, ns->vcodec_str, sizeof ns->vcodec_str, ns->acodec_str, sizeof ns->acodec_str);
  else {
    ns->vcodec_str[0] = '\0';
    ns->acodec_str[0] = '\0';
  }
  snap_commit(s, old, ns);
  return 0;
}

int hls_set_audio_params_at(hls_store_t *s, unsigned rate, hls_chcfg_t scheme, unsigned value) {
  hls_snapshot_t *old;
  hls_snapshot_t *ns;
  ns = snap_begin(s, &old);
  if (!ns) return -1;
  ns->audio_rate = rate;
  ns->audio_chcfg = scheme;
  ns->audio_chcfg_value = value;
  snap_commit(s, old, ns);
  return 0;
}

int hls_set_video_info_at(hls_store_t *s, const hls_video_info_t *info) {
  hls_snapshot_t *old;
  hls_snapshot_t *ns;
  ns = snap_begin(s, &old);
  if (!ns) return -1;
  ns->video = *info;
  snap_commit(s, old, ns);
  return 0;
}

int hls_set_lcevc_pids_at(hls_store_t *s, const unsigned *lcevc_pid, int lcevc_pid_count) {
  hls_snapshot_t *old;
  hls_snapshot_t *ns;
  if (lcevc_pid_count > PSI_LCEVC_MAX_LINKS) lcevc_pid_count = PSI_LCEVC_MAX_LINKS;
  ns = snap_begin(s, &old);
  if (!ns) return -1;
  for (int i = 0; i < lcevc_pid_count; i++) ns->lcevc_pid[i] = lcevc_pid[i];
  ns->lcevc_pid_count = lcevc_pid_count;
  snap_commit(s, old, ns);
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

  ns = snap_begin(s, &old);
  if (!ns || ring_make_room(ns, s->max_segs) < 0) {
    if (ns) snap_abort(s, ns);
    seg_buf_unref(copy);
    return -1;
  }
  idx = (ns->head + ns->count) % HLS_MAX_SEGS;
  ns->ring->segs[idx].data = copy;
  ns->ring->segs[idx].size = size;
  ns->ring->segs[idx].duration = duration;
  ns->ring->segs[idx].seq = ns->next_seq;
  ns->ring->segs[idx].start_ms = ns->cum_ms;
  ns->ring->segs[idx].parts.count = 0;
  ns->ring->segs[idx].disc = ns->disc_pending;
  ns->disc_pending = 0;
  ns->next_seq++;
  ns->cum_ms += (uint64_t)(duration * 1000.0 + 0.5);
  ns->count++;
  if (duration > ns->td_hw) ns->td_hw = duration;
  snap_commit(s, old, ns);
  reactor_wake_waiters();
  return 0;
}

void hls_llhls_enable(const stream_key_t *k, double part_target) {
  hls_store_t *s = hls_store_find(k);
  hls_snapshot_t *old;
  hls_snapshot_t *ns;
  if (!s || !(ns = snap_begin(s, &old))) return;
  ns->part_target = part_target;
  ns->live_msn = ns->next_seq;
  snap_commit(s, old, ns);
  if (k->container == SEG_CONTAINER_FMP4) {
    hls_llhls_enable(STREAM_KEY_AS(k, SEG_CONTAINER_FMP4_VIDEO), part_target);
    hls_llhls_enable(STREAM_KEY_AS(k, SEG_CONTAINER_FMP4_AUDIO), part_target);
  }
}

static int live_buf_ensure(hls_snapshot_t *ns, size_t need) {
  uint8_t *nb;
  if (ns->live_data && ns->live_cap >= need) return 0;
  nb = seg_buf_alloc(need);
  if (!nb) return -1;
  if (ns->live_data && ns->live_len) memcpy(nb, ns->live_data, ns->live_len);
  seg_buf_unref(ns->live_data);
  ns->live_data = nb;
  ns->live_cap = seg_buf_cap(need);
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

  ns = snap_begin(s, &old);
  if (!ns) return -1;
  if (ns->live_parts.count >= HLS_MAX_PARTS || live_buf_ensure(ns, ns->live_len + size) < 0) {
    snap_abort(s, ns);
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
  snap_commit(s, old, ns);
  reactor_wake_waiters();
  if (g_part_pushed_cb) g_part_pushed_cb(s, live_msn, n, data, size);
  return 0;
}

int hls_push_segment_ll_at(hls_store_t *s, double duration) {
  hls_snapshot_t *old;
  hls_snapshot_t *ns;
  int idx;
  uint32_t seq;

  ns = snap_begin(s, &old);
  if (!ns) return -1;
  if (ring_make_room(ns, s->max_segs) < 0) {
    snap_abort(s, ns);
    return -1;
  }
  seq = ns->next_seq;
  idx = (ns->head + ns->count) % HLS_MAX_SEGS;
  ns->ring->segs[idx].data = ns->live_data;
  ns->ring->segs[idx].size = ns->live_len;
  ns->ring->segs[idx].duration = duration;
  ns->ring->segs[idx].seq = seq;
  ns->ring->segs[idx].start_ms = ns->cum_ms;
  ns->cum_ms += (uint64_t)(duration * 1000.0 + 0.5);
  ns->ring->segs[idx].parts = ns->live_parts;
  ns->ring->segs[idx].disc = ns->disc_pending;
  ns->disc_pending = 0;
  ns->next_seq++;
  ns->count++;
  if (duration > ns->td_hw) ns->td_hw = duration;
  ns->live_data = NULL;
  ns->live_cap = 0;
  ns->live_len = 0;
  ns->live_parts.count = 0;
  ns->live_msn = ns->next_seq;
  snap_commit(s, old, ns);
  reactor_wake_waiters();
  if (g_segment_done_cb) g_segment_done_cb(s, seq);
  return 0;
}

int hls_store_discontinuity_at(hls_store_t *s, uint64_t *base_ms) {
  hls_snapshot_t *old;
  hls_snapshot_t *ns;
  uint32_t dropped_msn;
  int had_live;

  pthread_mutex_lock(store_lock(s));
  old = atomic_load_explicit(&s->snap, memory_order_acquire);
  if (!old) {
    pthread_mutex_unlock(store_lock(s));
    *base_ms = 0;
    return 0;
  }
  ns = snap_clone(old);
  if (!ns || !ring_cow(ns)) {
    snap_abort(s, ns);
    return -1;
  }
  for (int i = 0; i < HLS_MAX_SEGS; i++) {
    seg_buf_unref(ns->ring->segs[i].data);
    ns->ring->segs[i].data = NULL;
  }
  had_live = ns->live_parts.count > 0;
  dropped_msn = ns->live_msn;
  seg_buf_unref(ns->live_data);
  ns->live_data = NULL;
  ns->live_cap = 0;
  ns->live_len = 0;
  ns->live_parts.count = 0;
  ns->head = 0;
  ns->count = 0;
  ns->next_seq++;
  ns->oldest_seq = ns->next_seq;
  if (ns->part_target > 0.0) ns->live_msn = ns->next_seq;
  ns->epoch++;
  ns->disc_pending = 1;
  ns->epoch_base_ms = ns->cum_ms;
  *base_ms = ns->cum_ms;
  snap_commit(s, old, ns);
  if (had_live && g_segment_done_cb) g_segment_done_cb(s, dropped_msn);
  return 0;
}

int hls_store_sync_at(hls_store_t *dst, const hls_store_t *src) {
  const hls_snapshot_t *cur;
  hls_snapshot_t *old;
  hls_snapshot_t *ns = snap_clone(NULL);
  if (!ns) return -1;

  pthread_mutex_lock(store_lock(src));
  cur = atomic_load_explicit(&src->snap, memory_order_acquire);
  if (cur) {
    ns->next_seq = cur->next_seq;
    ns->oldest_seq = cur->next_seq;
    ns->cum_ms = cur->cum_ms;
    ns->epoch = cur->epoch;
    ns->disc_pending = cur->disc_pending;
    ns->epoch_base_ms = cur->epoch_base_ms;
    ns->avail_ms = cur->avail_ms;
    ns->td_hw = cur->td_hw;
    ns->part_target = cur->part_target;
    if (cur->part_target > 0.0) ns->live_msn = cur->next_seq;
  }
  pthread_mutex_unlock(store_lock(src));

  pthread_mutex_lock(store_lock(dst));
  old = atomic_load_explicit(&dst->snap, memory_order_acquire);
  if (old) ns->init_gen = old->init_gen;
  snap_commit(dst, old, ns);
  return 0;
}

int hls_store_anchor_at(hls_store_t *s, uint64_t wall_ms) {
  hls_snapshot_t *old;
  hls_snapshot_t *ns;
  pthread_mutex_lock(store_lock(s));
  old = atomic_load_explicit(&s->snap, memory_order_acquire);
  if (old && old->avail_ms) {
    pthread_mutex_unlock(store_lock(s));
    return 0;
  }
  ns = snap_clone(old);
  if (!ns) {
    pthread_mutex_unlock(store_lock(s));
    return -1;
  }
  ns->avail_ms = wall_ms;
  snap_commit(s, old, ns);
  return 0;
}

void hls_set_init_codecs_cb(hls_init_codecs_fn cb) { g_init_codecs_cb = cb; }

int hls_store_ready_at(const hls_store_t *s) {
  const hls_snapshot_t *snap;
  if (!s) return 0;
  snap = atomic_load_explicit(&s->snap, memory_order_acquire);
  return snap && snap->count > 0;
}

int hls_ll_store_ready_at(const hls_store_t *s) {
  const hls_snapshot_t *snap;
  if (!s) return 0;
  snap = atomic_load_explicit(&s->snap, memory_order_acquire);
  return snap && snap->part_target > 0.0 && (snap->count > 0 || snap->live_parts.count > 0);
}

int hls_part_available_at(const hls_store_t *s, uint32_t want_seg, int want_part) {
  const hls_snapshot_t *snap;
  if (!s) return 0;
  snap = atomic_load_explicit(&s->snap, memory_order_acquire);
  if (!snap) return 0;
  if (want_part < 0) return want_seg < snap->live_msn;
  for (;;) {
    uint32_t oldest = snap->oldest_seq;
    const hls_seg_t *seg;
    if (snap->live_msn == want_seg) return snap->live_parts.count > want_part;
    if (snap->count <= 0 || want_seg < oldest || want_seg > oldest + (uint32_t)snap->count - 1u) return 0;
    seg = &snap->ring->segs[(snap->head + (int)(want_seg - oldest)) % HLS_MAX_SEGS];
    if (seg->parts.count > want_part || (want_part == 0 && seg->parts.count == 0)) return 1;
    if (seg->parts.count < want_part) return 0;
    want_seg++; /* closed with exactly want_part parts: M.P is M+1.0 */
    want_part = 0;
  }
}
