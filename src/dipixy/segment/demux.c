/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include "lib/demux/psi/section_asm.h"
#include "lib/demux/tspack.h"
#include "pidlock.h"

#include <string.h>

static int audio_codec_supported(codec_t c) {
  return c == CODEC_AAC || c == CODEC_AAC_LATM || c == CODEC_AC3 || c == CODEC_EAC3 || c == CODEC_MP2A || c == CODEC_OPUS;
}

/* called once, when video_pid_known first -> true: snapshots locked own pids, drops pre-lock stuff in s->buf */
static void lock_program_pids(hls_seg_ctx_t *s) {
  pidlock_snapshot(s->demux.psi, s->demux.allowed_pids, &s->demux.n_allowed,
                   (int)(sizeof s->demux.allowed_pids / sizeof s->demux.allowed_pids[0]));
  s->len = 0;
}

static void lcevc_lock(hls_seg_ctx_t *s, int n, const psi_es_t *es) {
  unsigned count = 0;
  const unsigned *pids = NULL;
  for (int i = 0; i < n; i++) if (es[i].pid == s->demux.video_pid) {
    pids = es[i].lcevc_pid;
    count = (unsigned)es[i].lcevc_pid_count;
    break;
  }
  hls_set_lcevc_pids(s->cap_ctx, &s->filter, s->pmt_pid, &s->lcevc, s->container, pids, (int)count);
  pidlock_apply_lcevc(&s->lcevc, &s->filter, pids, (int)count);
  if (s->container == SEG_CONTAINER_FMP4 && count > 0) {
    lcevc_resolved_t r = lcevc_select_resolve(&s->lcevc, pids, (int)count);
    if (r.kind == LCEVC_RESOLVE_ONE) {
      s->demux.lcevc_pid_known = 1;
      s->demux.lcevc_pid = r.pid;
      pes_track(s->demux.pes, r.pid);
    }
  }
}

static void feed_one(hls_seg_ctx_t *s, const unsigned char *pkt, unsigned char *pmt_rw, unsigned char *pmt_pkt) {
  unsigned pid = tspack_pid(pkt);
  int pusi = 0;
  const unsigned char *out_pkt;
  if (pid_filter_excludes(&s->filter, pid)) return;
  if (psi_wants_pid(s->demux.psi, pid)) psi_feed(s->demux.psi, pkt);
  if (!s->demux.video_pid_known && psi_ready(s->demux.psi)) {
    int n, i;
    const psi_es_t *es = psi_es(s->demux.psi, &n);
    for (i = 0; i < n; i++) if (es[i].cls == PID_VIDEO) {
      s->demux.video_pid = es[i].pid;
      s->demux.video_codec = es[i].codec;
      s->video.es.codec = es[i].codec;
      pes_track(s->demux.pes, s->demux.video_pid);
      s->demux.video_pid_known = 1;
      break;
    }
    if (s->demux.video_pid_known && s->container == SEG_CONTAINER_FMP4) for (i = 0; i < n; i++) {
      if (es[i].cls == PID_AUDIO && audio_codec_supported(es[i].codec)) {
        s->demux.audio_pid = es[i].pid;
        s->demux.audio_codec = es[i].codec;
        s->audio.es_audio.codec = es[i].codec;
        pes_track(s->demux.pes, s->demux.audio_pid);
        s->demux.audio_pid_known = 1;
        s->audio.audio_present = 1;
        break;
      }
    }
    if (s->demux.video_pid_known) {
      lcevc_lock(s, n, es);
      lock_program_pids(s);
    }
  }

  if (!s->demux.video_pid_known) return; /* still probing PAT/PMT, not yet locked onto a program */
  if (!pidlock_allowed(s->demux.allowed_pids, s->demux.n_allowed, pid)) return; /* MPTS: another program's pid, not part of this demuxed output */
  if (pid == s->demux.video_pid) {
    const unsigned char *pl;
    size_t plen;
    tspack_payload(pkt, &pl, &plen, &pusi);
  }

  out_pkt = s->container == SEG_CONTAINER_TS ? pidlock_rewrite_pmt(s->demux.psi, &s->filter, &s->cc_pmt, pkt, pid, pmt_rw, pmt_pkt) : pkt;
  if (buf_reserve(&s->buf, &s->cap, s->len + 188) < 0) {
    log_throttled(&s->oom_drop_throttle, LOG_THROTTLE_WINDOW_S, "hls: buf_reserve failed, ts packet dropped");
    return;
  }
  memcpy(s->buf + s->len, out_pkt, 188);
  s->len += 188;

  /* pes_feed's callback may trim s->buf. packet bytes always survive as new tail: offset valid only post-call */
  pes_feed(s->demux.pes, pkt);
  if (pusi) {
    s->pending_pes_off = s->len - 188;
    s->have_pending_pes = 1;
  }
}

void hls_seg_on_pes(void *ctx, unsigned pid, int has_pts, uint64_t pts, int has_dts, uint64_t dts, const unsigned char *data, size_t len) {
  hls_seg_ctx_t *s = ctx;
  if (pid == s->demux.video_pid)
    handle_video_pes(s, has_pts, pts, has_dts, dts, data, len);
  else if (s->demux.audio_pid_known && pid == s->demux.audio_pid)
    handle_audio_pes(s, has_pts, pts, data, len);
  else if (s->demux.lcevc_pid_known && pid == s->demux.lcevc_pid)
    handle_lcevc_pes(s, has_pts, pts, data, len);
}

/* several segmenters possible per source now, one per distinct ?filter= */
void hls_seg_feed_all(capture_ctx_t *ctx, const unsigned char *pkt) {
  const _Atomic(void *) *head = capture_hls_seg_head_ptr(ctx);
  hls_seg_ctx_t *s = atomic_load_explicit(head, memory_order_acquire);
  unsigned char pmt_rw[PSI_SECTION_ASM_BUF_LEN];
  unsigned char pmt_pkt[188];
  while (s) {
    hls_seg_ctx_t *next = atomic_load_explicit(&s->chain_next, memory_order_relaxed);
    feed_one(s, pkt, pmt_rw, pmt_pkt); /* safe unlocked: ctx has one fixed owning pump thread, sweep_idle waits b4 freeing */
    s = next;
  }
}
