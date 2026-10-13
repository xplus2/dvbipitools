/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "ts_push_int.h"
#include "../reactor/conn.h"
#include "../segment/pidlock.h"

#include "lib/demux/psi/section_asm.h"
#include "lib/demux/tspack.h"

#include <string.h>

static void spts_lock(ts_sub_t *s) {
  pidlock_snapshot(s->spts_psi, s->spts_allowed, &s->spts_n_allowed,
                   (int)(sizeof s->spts_allowed / sizeof s->spts_allowed[0]));
  s->spts_pmt_seen = psi_pmt_parsed(s->spts_psi);
  s->spts_locked = 1;
}

static void lcevc_lock(ts_sub_t *s, const psi_t *tp) {
  int n;
  const psi_es_t *es;
  unsigned count = 0;
  const unsigned *pids = NULL;

  if (s->lcevc_locked) return;
  es = psi_es(tp, &n);
  for (int i = 0; i < n; i++) {
    if (es[i].cls == PID_VIDEO) {
      pids = es[i].lcevc_pid;
      count = (unsigned)es[i].lcevc_pid_count;
      break;
    }
  }
  pidlock_apply_lcevc(&s->lcevc, &s->filter, pids, (int)count);
  s->lcevc_locked = 1;
}

/* PMT parsed again: relock if locked program's pids differ. PMT without ES keeps old lock */
static void spts_check_change(ts_sub_t *s) {
  unsigned allowed[sizeof s->spts_allowed / sizeof s->spts_allowed[0]];
  int n_allowed;
  int n;

  s->spts_pmt_seen = psi_pmt_parsed(s->spts_psi);
  psi_es(s->spts_psi, &n);
  if (n == 0) return;
  pidlock_snapshot(s->spts_psi, allowed, &n_allowed, (int)(sizeof allowed / sizeof allowed[0]));
  if (n_allowed == s->spts_n_allowed && !memcmp(allowed, s->spts_allowed, (size_t)n_allowed * sizeof allowed[0])) return;
  s->filter = s->filter_base;
  s->lcevc_locked = 0;
  spts_lock(s);
  lcevc_lock(s, s->spts_psi);
}

/* spts admission: locks onto one program from live PAT/PMT, relocks on PMT change, then admits only that program's own pids.
   1 = admit (still subject to filter's excludes too), 0 = drop (pre-lock, or not part of locked program) */
static int spts_admit(ts_sub_t *s, const unsigned char *pkt, unsigned pid) {
  if (psi_wants_pid(s->spts_psi, pid)) psi_feed(s->spts_psi, pkt);
  if (!s->spts_locked) {
    if (psi_ready(s->spts_psi)) {
      spts_lock(s);
      lcevc_lock(s, s->spts_psi);
    } else return 0;
  } else if (psi_pmt_parsed(s->spts_psi) != s->spts_pmt_seen) {
    spts_check_change(s);
  }
  return pidlock_allowed(s->spts_allowed, s->spts_n_allowed, pid);
}

static void filter_psi_track(ts_sub_t *s, const unsigned char *pkt, unsigned pid) {
  if (!s->filter_psi) return;
  if (psi_wants_pid(s->filter_psi, pid)) psi_feed(s->filter_psi, pkt);
  if (psi_have_pmt(s->filter_psi)) lcevc_lock(s, s->filter_psi);
}

/* NULL: drop packet. out serves PAT and PMT, never both for one packet. *n_pkts: 188B packets at out */
static const unsigned char *rewrite_psi(ts_sub_t *s, const unsigned char *pkt, unsigned pid, unsigned char *rw, unsigned char *out, size_t *n_pkts) {
  const psi_t *tp = s->spts ? s->spts_psi : s->filter_psi;
  *n_pkts = 1;
  if (s->spts && pid == 0) return pidlock_rewrite_pat(tp, &s->cc_pat, pkt, pid, out);
  return pidlock_rewrite_pmt(tp, &s->filter, &s->cc_pmt, pkt, pid, rw, out, n_pkts);
}

void ts_push_rawaudio_emit(void *vctx, const unsigned char *data, size_t len) {
  ts_sub_t *s = vctx;
  switch (s->proto) {
    case CONN_PROTO_H1:
      ts_push_ring_enqueue(s, data, len);
      break;
#ifdef HAVE_HTTP2
    case CONN_PROTO_H2:
      ts_push_h2_enqueue((int)(s - g_ts_subs), data, len);
      break;
#endif
#ifdef HAVE_HTTP3
    case CONN_PROTO_H3:
      ts_push_h3_enqueue((int)(s - g_ts_subs), data, len);
      break;
#endif
    default:
      break;
  }
}

void ts_push_drop_sub(const ts_sub_t *s, int idx) {
  if (s->proto == CONN_PROTO_H1) {
    conn_t *c = conn_for_fd_gen(s->fd, s->conn_gen);
    if (c) conn_request_close(c);
  }
  ts_push_unsubscribe_by_idx(idx);
}

void ts_push_feed_pkt(capture_ctx_t *ctx, const uint8_t *pkt) {
  unsigned pid = tspack_pid(pkt);
  unsigned char pmt_rw[PSI_SECTION_ASM_BUF_LEN];
  unsigned char pmt_pkt[PMT_FILTER_PKT_MAX * 188];
  int i;
  i = atomic_load_explicit(capture_ts_push_head_ptr(ctx), memory_order_acquire);
  while (i != -1) {
    ts_sub_t *s = &g_ts_subs[i];
    int next = atomic_load_explicit(&s->ctx_next, memory_order_relaxed);
    const unsigned char *out;
    size_t out_len;
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
    out = rewrite_psi(s, pkt, pid, pmt_rw, pmt_pkt, &out_len);
    if (!out) goto next_sub;
    out_len *= 188;
    switch (s->proto) {
      case CONN_PROTO_H1:
        ts_push_ring_enqueue(s, out, out_len);
        break;
#ifdef HAVE_HTTP2
      case CONN_PROTO_H2:
        ts_push_h2_enqueue(i, out, out_len);
        break;
#endif
#ifdef HAVE_HTTP3
      case CONN_PROTO_H3:
        ts_push_h3_enqueue(i, out, out_len);
        break;
#endif
      default:
        break;
    }
  next_sub:
    i = next;
  }
}
