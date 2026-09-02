/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include "lib/demux/psi/section_asm.h"
#include "lib/demux/tspack.h"
#include "lib/mux/pmt_filter.h"

#include <string.h>

static int audio_codec_supported(codec_t c) {
  return c == CODEC_AAC || c == CODEC_AAC_LATM || c == CODEC_AC3 || c == CODEC_EAC3 || c == CODEC_MP2A;
}

static int program_pid_allowed(const hls_seg_ctx_t *s, unsigned pid) {
  for (int i = 0; i < s->n_allowed; i++)
    if (s->allowed_pids[i] == pid)
      return 1;
  return 0;
}

/* called once, when video_pid_known first -> true: snapshots locked own pids, drops pre-lock stuff in s->buf */
static void lock_program_pids(hls_seg_ctx_t *s) {
  int n;
  const psi_es_t *es;
  unsigned pcr_pid;
  s->n_allowed = 0;
  s->allowed_pids[s->n_allowed++] = 0; /* PAT */
  s->allowed_pids[s->n_allowed++] = psi_pmt_pid(s->psi);
  pcr_pid = psi_pcr_pid(s->psi);
  if (pcr_pid)
    s->allowed_pids[s->n_allowed++] = pcr_pid;
  es = psi_es(s->psi, &n);
  for (int i = 0; i < n && s->n_allowed < (int)(sizeof s->allowed_pids / sizeof s->allowed_pids[0]); i++)
    s->allowed_pids[s->n_allowed++] = es[i].pid;

  s->len = 0;
}

/* silent fallback to pkt: no section yet, or rewrite doesn't fit one packet. cc_pmt advances only on success */
static const unsigned char *maybe_rewrite_pmt(hls_seg_ctx_t *s, const unsigned char *pkt, unsigned pid, unsigned char *rw, unsigned char *out188) {
  const unsigned char *sec;
  unsigned drop_pids[PID_FILTER_MAX];
  size_t sl, rl;
  if (s->container != SEG_CONTAINER_TS || s->filter.count == 0 || !psi_have_pmt(s->psi) || pid != psi_pmt_pid(s->psi))
    return pkt;
  sec = psi_pmt_section(s->psi, &sl);
  if (!sec) return pkt;
  for (int k = 0; k < s->filter.count; k++) drop_pids[k] = s->filter.pids[k];
  rl = pmt_filter_rewrite(sec, sl, drop_pids, (size_t)s->filter.count, rw, PSI_SECTION_ASM_BUF_LEN);
  if (!rl) return pkt;
  s->cc_pmt = (s->cc_pmt + 1) & 0x0F;
  if (!pmt_filter_emit_packet(out188, pid, s->cc_pmt, rw, rl)) return pkt;
  return out188;
}

static void feed_one(hls_seg_ctx_t *s, const unsigned char *pkt, unsigned char *pmt_rw, unsigned char *pmt_pkt) {
  unsigned pid = tspack_pid(pkt);
  int pusi = 0;
  const unsigned char *out_pkt;
  if (pid_filter_excludes(&s->filter, pid)) return;
  if (psi_wants_pid(s->psi, pid)) psi_feed(s->psi, pkt);
  if (!s->video_pid_known && psi_ready(s->psi)) {
    int n, i;
    const psi_es_t *es = psi_es(s->psi, &n);
    for (i = 0; i < n; i++) {
      if (es[i].cls == PID_VIDEO) {
        s->video_pid = es[i].pid;
        s->video_codec = es[i].codec;
        s->es.codec = es[i].codec;
        pes_track(s->pes, s->video_pid);
        s->video_pid_known = 1;
        break;
      }
    }
    if (s->video_pid_known && s->container == SEG_CONTAINER_FMP4) {
      for (i = 0; i < n; i++) {
        if (es[i].cls == PID_AUDIO && audio_codec_supported(es[i].codec)) {
          s->audio_pid = es[i].pid;
          s->audio_codec = es[i].codec;
          s->es_audio.codec = es[i].codec;
          pes_track(s->pes, s->audio_pid);
          s->audio_pid_known = 1;
          s->audio_present = 1;
          break;
        }
      }
    }
    if (s->video_pid_known)
      lock_program_pids(s);
  }

  if (!s->video_pid_known)
    return; /* still probing PAT/PMT, not yet locked onto a program */

  if (!program_pid_allowed(s, pid))
    return; /* MPTS: another program's pid, not part of this demuxed output */

  if (pid == s->video_pid) {
    const unsigned char *pl;
    size_t plen;
    tspack_payload(pkt, &pl, &plen, &pusi);
  }

  out_pkt = maybe_rewrite_pmt(s, pkt, pid, pmt_rw, pmt_pkt);

  if (buf_reserve(&s->buf, &s->cap, s->len + 188) < 0) {
    log_throttled(&s->oom_drop_throttle, LOG_THROTTLE_WINDOW_S, "hls: buf_reserve failed, ts packet dropped");
    return;
  }
  memcpy(s->buf + s->len, out_pkt, 188);
  s->len += 188;

  /* pes_feed's callback may trim s->buf. packet bytes always survive as new tail: offset valid only post-call */
  pes_feed(s->pes, pkt);
  if (pusi) {
    s->pending_pes_off = s->len - 188;
    s->have_pending_pes = 1;
  }
}

void hls_seg_on_pes(void *ctx, unsigned pid, int has_pts, uint64_t pts, int has_dts, uint64_t dts, const unsigned char *data, size_t len) {
  hls_seg_ctx_t *s = ctx;
  if (pid == s->video_pid)
    handle_video_pes(s, has_pts, pts, has_dts, dts, data, len);
  else if (s->audio_pid_known && pid == s->audio_pid)
    handle_audio_pes(s, has_pts, pts, data, len);
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
