/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "ts_push_int.h"
#include "../reactor/conn.h"
#include "../segment/pidlock.h"

#include "lib/demux/psi/section_asm.h"
#include "lib/demux/tspack.h"

static void spts_lock(ts_sub_t *s) {
  pidlock_snapshot(s->spts_psi, s->spts_allowed, &s->spts_n_allowed,
                   (int)(sizeof s->spts_allowed / sizeof s->spts_allowed[0]));
  s->spts_locked = 1;
}

static void lcevc_lock(ts_sub_t *s, const psi_t *tp) {
  int n;
  const psi_es_t *es;
  unsigned count = 0;
  const unsigned *pids = NULL;

  if (s->lcevc_locked) return;
  es = psi_es(tp, &n);
  for (int i = 0; i < n; i++) if (es[i].cls == PID_VIDEO) {
    pids = es[i].lcevc_pid;
    count = (unsigned)es[i].lcevc_pid_count;
    break;
  }
  pidlock_apply_lcevc(&s->lcevc, &s->filter, pids, (int)count);
  s->lcevc_locked = 1;
}

/* spts admission: locks onto one program from live PAT/PMT, then admits only that program's own pids.
   1 = admit (still subject to filter's excludes too), 0 = drop (pre-lock, or not part of locked program) */
static int spts_admit(ts_sub_t *s, const unsigned char *pkt, unsigned pid) {
  if (!s->spts_locked) {
    if (psi_wants_pid(s->spts_psi, pid)) psi_feed(s->spts_psi, pkt);
    if (psi_ready(s->spts_psi)) {
      spts_lock(s);
      lcevc_lock(s, s->spts_psi);
    } else return 0;
  }
  return pidlock_allowed(s->spts_allowed, s->spts_n_allowed, pid);
}

static void filter_psi_track(ts_sub_t *s, const unsigned char *pkt, unsigned pid) {
  if (!s->filter_psi) return;
  if (psi_wants_pid(s->filter_psi, pid)) psi_feed(s->filter_psi, pkt);
  if (psi_have_pmt(s->filter_psi)) lcevc_lock(s, s->filter_psi);
}

static const unsigned char *maybe_rewrite_pmt(ts_sub_t *s, const unsigned char *pkt, unsigned pid, unsigned char *rw, unsigned char *out188) {
  const psi_t *tp = s->spts ? s->spts_psi : s->filter_psi;
  return pidlock_rewrite_pmt(tp, &s->filter, &s->cc_pmt, pkt, pid, rw, out188);
}

void ts_push_rawaudio_emit(void *vctx, const unsigned char *data, size_t len) {
  ts_sub_t *s = vctx;
  switch (s->proto) {
    case 1:
      ts_push_ring_enqueue(s, data, len);
      break;
#ifdef HAVE_HTTP2
    case 2:
      ts_push_h2_enqueue((int)(s - g_ts_subs), data, len);
      break;
#endif
#ifdef HAVE_HTTP3
    case 3:
      ts_push_h3_enqueue((int)(s - g_ts_subs), data, len);
      break;
#endif
    default:
      break;
  }
}

void ts_push_drop_sub(const ts_sub_t *s, int idx) {
  if (s->proto == 1) {
    conn_t *c = conn_for_fd(s->fd);
    if (c) conn_request_close(c);
  }
  ts_push_unsubscribe_by_idx(idx);
  (void)s;
}

void ts_push_feed_pkt(capture_ctx_t *ctx, const uint8_t *pkt) {
  unsigned pid = tspack_pid(pkt);
  unsigned char pmt_rw[PSI_SECTION_ASM_BUF_LEN];
  unsigned char pmt_pkt[188];
  int i;
  i = atomic_load_explicit(capture_ts_push_head_ptr(ctx), memory_order_acquire);
  while (i != -1) {
    ts_sub_t *s = &g_ts_subs[i];
    int next = atomic_load_explicit(&s->ctx_next, memory_order_relaxed);
    const unsigned char *out;
    if (atomic_load_explicit(&s->alive, memory_order_acquire) != TS_SUB_ALIVE) goto next_sub;
    if (atomic_load_explicit(&s->ctx, memory_order_relaxed) != ctx) goto next_sub;
    if (!atomic_load_explicit(&s->ready, memory_order_acquire)) goto next_sub;
    if (s->rawaudio) {
      rawaudio_demux_feed(s->rawaudio, pkt);
      goto next_sub;
    }
    filter_psi_track(s, pkt, pid);
    if (s->spts && !spts_admit(s, pkt, pid)) goto next_sub;
    if (pid_filter_excludes(&s->filter, pid)) goto next_sub;
    out = maybe_rewrite_pmt(s, pkt, pid, pmt_rw, pmt_pkt);
    switch (s->proto) {
      case 1:
        ts_push_ring_enqueue(s, out, 188);
        break;
#ifdef HAVE_HTTP2
      case 2:
        ts_push_h2_enqueue(i, out, 188);
        break;
#endif
#ifdef HAVE_HTTP3
      case 3:
        ts_push_h3_enqueue(i, out, 188);
        break;
#endif
      default:
        break;
    }
  next_sub:
    i = next;
  }
}
