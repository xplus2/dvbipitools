/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "ts_push_int.h"
#include "../reactor/conn.h"

#include "lib/demux/psi/section_asm.h"
#include "lib/demux/tspack.h"
#include "lib/mux/pmt_filter.h"

/* mirrors hls/segment.c's lock_program_pids(): snapshot locked program's own
   pids (PAT, its PMT, PCR, every ES) once PSI resolves */
static void spts_lock(ts_sub_t *s) {
  int n;
  const psi_es_t *es;
  unsigned pcr_pid;

  s->spts_n_allowed = 0;
  s->spts_allowed[s->spts_n_allowed++] = 0; /* PAT */
  s->spts_allowed[s->spts_n_allowed++] = psi_pmt_pid(s->spts_psi);
  pcr_pid = psi_pcr_pid(s->spts_psi);
  if (pcr_pid) s->spts_allowed[s->spts_n_allowed++] = pcr_pid;
  es = psi_es(s->spts_psi, &n);
  for (int i = 0; i < n && s->spts_n_allowed < (int)(sizeof s->spts_allowed / sizeof s->spts_allowed[0]); i++)
    s->spts_allowed[s->spts_n_allowed++] = es[i].pid;

  s->spts_locked = 1;
}

static int spts_pid_allowed(const ts_sub_t *s, unsigned pid) {
  for (int i = 0; i < s->spts_n_allowed; i++) if (s->spts_allowed[i] == pid) return 1;
  return 0;
}

/* spts admission: locks onto one program from live PAT/PMT, then admits only that program's own pids.
   1 = admit (still subject to filter's excludes too), 0 = drop (pre-lock, or not part of locked program) */
static int spts_admit(ts_sub_t *s, const unsigned char *pkt, unsigned pid) {
  if (!s->spts_locked) {
    if (psi_wants_pid(s->spts_psi, pid)) psi_feed(s->spts_psi, pkt);
    if (psi_ready(s->spts_psi)) spts_lock(s);
    else return 0;
  }
  return spts_pid_allowed(s, pid);
}

static void filter_psi_track(ts_sub_t *s, const unsigned char *pkt, unsigned pid) {
  if (s->filter_psi && psi_wants_pid(s->filter_psi, pid)) psi_feed(s->filter_psi, pkt);
}

/* silent fallback to pkt: no section yet, or rewrite doesn't fit one packet. cc_pmt advances only on success */
static const unsigned char *maybe_rewrite_pmt(ts_sub_t *s, const unsigned char *pkt, unsigned pid, unsigned char *rw, unsigned char *out188) {
  const psi_t *tp = s->spts ? s->spts_psi : s->filter_psi;
  const unsigned char *sec;
  unsigned drop_pids[PID_FILTER_MAX];
  size_t sl, rl;

  if (!tp || s->filter.count == 0 || !psi_have_pmt(tp) || pid != psi_pmt_pid(tp)) return pkt;
  sec = psi_pmt_section(tp, &sl);
  if (!sec) return pkt;
  for (int k = 0; k < s->filter.count; k++) drop_pids[k] = s->filter.pids[k];
  rl = pmt_filter_rewrite(sec, sl, drop_pids, (size_t)s->filter.count, rw, PSI_SECTION_ASM_BUF_LEN);
  if (!rl) return pkt;
  s->cc_pmt = (s->cc_pmt + 1) & 0x0F;
  if (!pmt_filter_emit_packet(out188, pid, s->cc_pmt, rw, rl)) return pkt;
  return out188;
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
    if (atomic_load_explicit(&s->alive, memory_order_acquire) != TS_SUB_ALIVE)
      goto next_sub;
    if (atomic_load_explicit(&s->ctx, memory_order_relaxed) != ctx)
      goto next_sub;
    if (!atomic_load_explicit(&s->ready, memory_order_acquire))
      goto next_sub;
    if (s->rawaudio) {
      rawaudio_demux_feed(s->rawaudio, pkt);
      goto next_sub;
    }
    filter_psi_track(s, pkt, pid);
    if (s->spts && !spts_admit(s, pkt, pid))
      goto next_sub;
    if (pid_filter_excludes(&s->filter, pid))
      goto next_sub;
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
