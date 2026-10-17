/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include "lib/demux/psi/section_asm.h"
#include "lib/demux/tspack.h"
#include "pidlock.h"

#include <string.h>
#include <time.h>

static int audio_codec_supported(codec_t c) {
  return c == CODEC_AAC
      || c == CODEC_AAC_LATM
      || c == CODEC_AC3  || c == CODEC_EAC3
      || c == CODEC_MP2A
      || c == CODEC_OPUS
      || c == CODEC_DTS
      || c == CODEC_DTS_HD
      || c == CODEC_DTS_HD_MA
      || c == CODEC_TRUEHD
      || c == CODEC_AC4;
}

/* snapshots locked own pids, drops pre-lock stuff in s->buf */
static void lock_program_pids(hls_seg_ctx_t *s) {
  pidlock_snapshot(s->demux.psi, s->demux.allowed_pids, &s->demux.n_allowed,
                   (int)(sizeof s->demux.allowed_pids / sizeof s->demux.allowed_pids[0]));
  s->len = 0;
}

static void lcevc_lock(hls_seg_ctx_t *s, int n, const psi_es_t *es) {
  unsigned count = 0;
  const unsigned *pids = NULL;
  for (int i = 0; i < n; i++) {
    if (es[i].pid == s->demux.video_pid) {
      pids = es[i].lcevc_pid;
      count = (unsigned)es[i].lcevc_pid_count;
      break;
    }
  }
  hls_set_lcevc_pids(s->cap_ctx, &s->filter, s->pmt_pid, &s->lcevc, s->container, pids, (int)count);
  pidlock_apply_lcevc(&s->lcevc, &s->drop, pids, (int)count);
  if (s->container == SEG_CONTAINER_FMP4 && count > 0) {
    lcevc_resolved_t r = lcevc_select_resolve(&s->lcevc, pids, (int)count);
    if (r.kind == LCEVC_RESOLVE_ONE) {
      s->demux.lcevc_pid_known = 1;
      s->demux.lcevc_pid = r.pid;
      pes_track(s->demux.pes, r.pid);
    }
  }
}

typedef struct {
  unsigned video_pid;
  codec_t video_codec;
  int has_audio;
  unsigned audio_pid;
  codec_t audio_codec;
} program_sel_t;

/* 1 if PMT carries video. audio only picked for fmp4 */
static int select_program(const hls_seg_ctx_t *s, const psi_es_t *es, int n, program_sel_t *sel) {
  int i;
  memset(sel, 0, sizeof *sel);
  for (i = 0; i < n; i++) {
    if (es[i].cls == PID_VIDEO) {
      sel->video_pid = es[i].pid;
      sel->video_codec = es[i].codec;
      break;
    }
  }
  if (i == n) return 0;
  if (s->container != SEG_CONTAINER_FMP4) return 1;
  for (i = 0; i < n; i++) {
    if (es[i].cls == PID_AUDIO && audio_codec_supported(es[i].codec)) {
      sel->has_audio = 1;
      sel->audio_pid = es[i].pid;
      sel->audio_codec = es[i].codec;
      break;
    }
  }
  return 1;
}

static void apply_selection(hls_seg_ctx_t *s, const program_sel_t *sel) {
  s->demux.video_pid = sel->video_pid;
  s->demux.video_codec = sel->video_codec;
  s->video.es.codec = sel->video_codec;
  pes_track(s->demux.pes, s->demux.video_pid);
  s->demux.video_pid_known = 1;
  if (!sel->has_audio) return;
  s->demux.audio_pid = sel->audio_pid;
  s->demux.audio_codec = sel->audio_codec;
  s->audio.es.codec = sel->audio_codec;
  pes_track(s->demux.pes, s->demux.audio_pid);
  s->demux.audio_pid_known = 1;
  s->audio.present = 1;
}

static int selection_unchanged(const hls_seg_ctx_t *s, const program_sel_t *sel) {
  if (sel->video_pid != s->demux.video_pid || sel->video_codec != s->demux.video_codec) return 0;
  if (sel->has_audio != s->demux.audio_pid_known) return 0;
  return !sel->has_audio || (sel->audio_pid == s->demux.audio_pid && sel->audio_codec == s->demux.audio_codec);
}

static void lock_program(hls_seg_ctx_t *s, const psi_es_t *es, int n, const program_sel_t *sel) {
  apply_selection(s, sel);
  lcevc_lock(s, n, es);
  lock_program_pids(s);
}

/* locked program's PIDs or codecs changed: restart on new layout, window and init start over */
static void relock_program(hls_seg_ctx_t *s, const psi_es_t *es, int n, const program_sel_t *sel) {
  pes_t *np = pes_new(hls_seg_on_pes, s);
  uint64_t base_ms = 0;
  if (!np) {
    log_throttled(&s->oom_drop_throttle, LOG_THROTTLE_WINDOW_S, "YW500237", "hls: pes_new failed, program change not applied");
    return;
  }
  pes_free(s->demux.pes);
  s->demux.pes = np;
  if (s->store && hls_store_discontinuity_at(s->store, &base_ms) < 0)
    log_throttled(&s->seg_push_fail_throttle, LOG_THROTTLE_WINDOW_S, "YW700153", "hls: discontinuity not recorded, old segments stay listed");
  fmp4_track_stores_sync(s);
  video_reset(s);
  audio_reset(s);
  fmp4_reset(s, base_ms);
  s->psi_pat_ok = 0;
  s->psi_pmt_n = 0;
  s->demux.audio_pid_known = 0;
  s->demux.lcevc_pid_known = 0;
  s->drop = s->filter;
  lock_program(s, es, n, sel);
}

/* PMT parsed again: relock if locked program's layout differs. PMT without video keeps old lock */
static void check_program_change(hls_seg_ctx_t *s) {
  unsigned allowed[sizeof s->demux.allowed_pids / sizeof s->demux.allowed_pids[0]];
  program_sel_t sel;
  int n_allowed;
  int n;
  const psi_es_t *es = psi_es(s->demux.psi, &n);

  s->demux.pmt_seen = psi_pmt_parsed(s->demux.psi);
  if (!select_program(s, es, n, &sel)) return;
  pidlock_snapshot(s->demux.psi, allowed, &n_allowed, (int)(sizeof allowed / sizeof allowed[0]));
  if (selection_unchanged(s, &sel) && n_allowed == s->demux.n_allowed && !memcmp(allowed, s->demux.allowed_pids, (size_t)n_allowed * sizeof allowed[0])) return;
  relock_program(s, es, n, &sel);
  log_code("YW700154", "hls: program layout changed, relocked on video pid 0x%04x", sel.video_pid);
}

/* never from inside PES callback */
static void splice_restart(hls_seg_ctx_t *s) {
  program_sel_t sel;
  int n;
  const psi_es_t *es = psi_es(s->demux.psi, &n);
  s->splice_pending = 0;
  if (!select_program(s, es, n, &sel)) return;
  relock_program(s, es, n, &sel);
  log_code("YW700161", "hls: video timestamp jump, segmenter restarted on video pid 0x%04x", sel.video_pid);
}

static uint64_t wall_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_REALTIME, &t);
  return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

/* no keyframe within buf_max: scrambled, unsupported codec or silent video pid. restart at next kf */
static void drop_stalled_buf(hls_seg_ctx_t *s) {
  log_throttled(&s->oom_drop_throttle, LOG_THROTTLE_WINDOW_S, "YW500238", "hls: no video keyframe within buffer cap, segment data dropped");
  s->len = 0;
  s->have_pending_pes = 0;
  s->video.seg_open = 0;
  s->part.part_open = 0;
  s->part.have_last_au = 0;
}

static void psi_cache_note(hls_seg_ctx_t *s, unsigned pid, const unsigned char *pkts, size_t n_pkts) {
  size_t sec_end;
  if (!(pkts[1] & 0x40) || pkts[3] & 0x20 || pkts[4] > 182) return;
  sec_end = 5 + (size_t)pkts[4] + 3 + (((size_t)(pkts[6 + pkts[4]] & 0x0F) << 8) | pkts[7 + pkts[4]]);
  if (n_pkts == 1 && sec_end > 188) return; /* section continues in packets not seen here */
  if (pid == 0) {
    if (n_pkts != 1) return;
    memcpy(s->psi_pat, pkts, 188);
    s->psi_pat_ok = 1;
  } else if (n_pkts <= PMT_FILTER_PKT_MAX) {
    memcpy(s->psi_pmt, pkts, n_pkts * 188);
    s->psi_pmt_n = (unsigned)n_pkts;
  }
}

static void feed_one(hls_seg_ctx_t *s, const unsigned char *pkt, unsigned char *pmt_rw, unsigned char *pmt_pkt) {
  unsigned pid = tspack_pid(pkt);
  int pusi = 0;
  size_t n_pkts = 1;
  const unsigned char *out_pkt;
  if (s->splice_pending) splice_restart(s);
  if (pid_filter_excludes(&s->drop, pid)) return;
  if (psi_wants_pid(s->demux.psi, pid)) psi_feed(s->demux.psi, pkt);
  if (!s->demux.video_pid_known) {
    if (psi_ready(s->demux.psi)) {
      int n;
      program_sel_t sel;
      const psi_es_t *es = psi_es(s->demux.psi, &n);
      if (select_program(s, es, n, &sel)) {
        s->demux.pmt_seen = psi_pmt_parsed(s->demux.psi);
        lock_program(s, es, n, &sel);
      }
    }
  } else if (psi_pmt_parsed(s->demux.psi) != s->demux.pmt_seen) {
    check_program_change(s);
  }

  if (!s->demux.video_pid_known) return; /* still probing PAT/PMT, not yet locked onto a program */
  if (!pidlock_allowed(s->demux.allowed_pids, s->demux.n_allowed, pid)) return; /* MPTS: another program's pid, not part of this demuxed output */
  if (pid == s->demux.video_pid) {
    const unsigned char *pl;
    size_t plen;
    tspack_payload(pkt, &pl, &plen, &pusi);
  }

  if (s->container == SEG_CONTAINER_TS) {
    out_pkt = pid == 0 ? pidlock_rewrite_pat(s->demux.psi, &s->cc_pat, pkt, pid, pmt_pkt)
                       : pidlock_rewrite_pmt(s->demux.psi, &s->drop, &s->cc_pmt, pkt, pid, pmt_rw, pmt_pkt, &n_pkts);
    if (!out_pkt) return;
    if (s->len + n_pkts * 188 > s->buf_max) drop_stalled_buf(s);
    if (buf_reserve(&s->buf, &s->cap, s->len + n_pkts * 188) < 0) {
      log_throttled(&s->oom_drop_throttle, LOG_THROTTLE_WINDOW_S, "YW500214", "hls: buf_reserve failed, ts packet dropped");
      return;
    }
    memcpy(s->buf + s->len, out_pkt, n_pkts * 188);
    if (pid == 0) hls_seg_stamp_cc(&s->buf_cc_pat, s->buf + s->len, n_pkts, pid);
    else if (pid == psi_pmt_pid(s->demux.psi)) hls_seg_stamp_cc(&s->buf_cc_pmt, s->buf + s->len, n_pkts, pid);
    s->len += n_pkts * 188;
    if (pid == 0 || pid == psi_pmt_pid(s->demux.psi)) psi_cache_note(s, pid, out_pkt, n_pkts);
  }

  /* pes_feed's callback may trim s->buf. packet bytes always survive as new tail: offset valid only post-call */
  pes_feed(s->demux.pes, pkt);
  if (pusi) {
    s->pending_pes_off = s->len >= 188 ? s->len - 188 : 0;
    s->pending_pes_wall_ms = wall_ms();
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
  unsigned char pmt_pkt[PMT_FILTER_PKT_MAX * 188];
  while (s) {
    hls_seg_ctx_t *next = atomic_load_explicit(&s->chain_next, memory_order_relaxed);
    feed_one(s, pkt, pmt_rw, pmt_pkt); /* safe unlocked: ctx has one fixed owning pump thread, sweep_idle waits b4 freeing */
    s = next;
  }
}
