/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/demux/tspack.h"

#include "../pcrclock.h"
#include "../pesstamp.h"
#include "../releaseq.h"
#include "priv.h"

/* es[] index for a source pid, or -1 if not carried (dropped) */
static int find_es(remux_t *r, unsigned in_pid) {
  if (r->last_es_idx >= 0 && r->last_es_idx < r->es_count && r->es[r->last_es_idx].in_pid == in_pid) return r->last_es_idx;
  for (int i = 0; i < r->es_count; i++)
    if (r->es[i].in_pid == in_pid) {
      r->last_es_idx = i;
      return i;
    }
  return -1;
}

static int es_carries_pes(const out_es_t *es) {
  if (!es || es->is_ca != CA_PASS_NONE) return 0;
  switch (es->stream_type) {
    case 0x05:
    case 0x0A:
    case 0x0B:
    case 0x0C:
    case 0x0D:
    case 0x14:
    case 0x86:
      return 0;
    default:
      return 1;
  }
}

static void count_shift(remux_t *r, int sr) {
  if (!r->tsm_cur) return;
  if (sr == PESSTAMP_FOUND) r->tsm_cur->pes_retimed_total++;
  else if (sr == PESSTAMP_SPLIT || sr == PESSTAMP_SCRAMBLED) r->tsm_cur->pes_retime_skipped_total++;
}

static void rebase_packet(remux_t *r, const out_es_t *es, unsigned out_pid, unsigned char *out, double now) {
  uint64_t pcr;
  if (out_pid == r->pcr_pid_out && pcr_packet_read(out, &pcr)) {
    unsigned long long before = r->tm->relatches;
    pcr_packet_write(out, timemap_pcr(r->tm, pcr, now));
    if (r->tsm_cur) {
      r->tsm_cur->pcr_rewritten_total++;
      r->tsm_cur->retime_relatches_total += r->tm->relatches - before;
    }
  }
  if (es_carries_pes(es)) count_shift(r, pesstamp_shift(out, (int64_t)timemap_k90(r->tm)));
}

static void emit_out(remux_t *r, unsigned out_pid, unsigned char *out, double now, remux_packet_cb cb, void *ctx) {
  if (r->cas) {
    cas_pcr_tick(r->cas, out_pid, out);
    cas_scramble_packet(r->cas, out_pid, now, out, cb, ctx);
  } else {
    cb(ctx, out);
  }
}

static void release_head(remux_t *r, int idx, double now, remux_packet_cb cb, void *ctx) {
  unsigned char out[188];
  int has;
  uint64_t tag;
  const unsigned char *h = releaseq_head(r->hold[idx], &has, &tag);
  if (!h) return;
  memcpy(out, h, 188);
  releaseq_pop(r->hold[idx]);
  emit_out(r, r->es[idx].out_pid, out, now, cb, ctx);
}

static int64_t diff90(uint64_t a, uint64_t b) {
  uint64_t d = (a - b) & (((uint64_t)1 << 33) - 1);
  return d >= ((uint64_t)1 << 32) ? (int64_t)d - ((int64_t)1 << 33) : (int64_t)d;
}

static int hold_enqueue(remux_t *r, const out_es_t *es, const unsigned char *out, double now, remux_packet_cb cb, void *ctx) {
  int idx = (int)(es - r->es);
  pes_stamp_t st;
  if (!r->hold_clock || !es_carries_pes(es) || !r->hold[idx]) return 0;
  if (pesstamp_read(out, &st) == PESSTAMP_FOUND && (st.has_dts || st.has_pts)) {
    r->hold_has_tag[idx] = 1;
    r->hold_tag[idx] = st.has_dts ? st.dts : st.pts;
  }
  if (releaseq_push(r->hold[idx], out, r->hold_has_tag[idx], r->hold_tag[idx])) {
    release_head(r, idx, now, cb, ctx);
    r->hold_forced++;
    if (r->tsm_cur) r->tsm_cur->hold_forced_total++;
    releaseq_push(r->hold[idx], out, r->hold_has_tag[idx], r->hold_tag[idx]);
  }
  return 1;
}

static uint64_t regen_lead(const remux_t *r, uint64_t stamp, double now) {
  uint64_t lead = r->hold_lead90;
  if (r->have_last_pcr && now - r->last_pcr_wall >= 0.0 && now - r->last_pcr_wall < 0.5) {
    uint64_t pcr27 = pcr_add(r->last_pcr27, (uint64_t)((now - r->last_pcr_wall) * (double)PCR_CLOCK_HZ));
    int64_t nat = diff90(stamp, pcr27 / 300);
    if (nat > 0 && nat <= 90000 && (uint64_t)nat > lead) lead = (uint64_t)nat;
  }
  return lead;
}

static void regen_stage(remux_t *r, const out_es_t *es, unsigned char *out, double now) {
  const uint64_t mod = (uint64_t)1 << 33;
  pes_stamp_t st;
  uint64_t p27;
  uint64_t stamp;
  if (!r->tm || !r->hold_clock || !es_carries_pes(es)) return;
  if (pesstamp_read(out, &st) != PESSTAMP_FOUND || !(st.has_dts || st.has_pts)) return;
  stamp = st.has_dts ? st.dts : st.pts;
  r->hold_lead90 = regen_lead(r, stamp, now);
  if (!r->hold_clock(r->hold_clock_ctx, &p27)) {
    r->hold_latch(r->hold_clock_ctx, ((stamp + mod - r->hold_lead90) % mod) * 300);
    if (!r->hold_clock(r->hold_clock_ctx, &p27)) return;
    timemap_regen_first(r->tm, 0);
  } else if (!timemap_regen_active(r->tm)) {
    timemap_regen_first(r->tm, (p27 / 300 + r->hold_lead90 + mod - stamp) % mod);
  }
  if ((int)(es - r->es) == r->hold_ref) {
    unsigned long long before = r->tm->rg_relatches;
    timemap_regen_sample(r->tm, stamp, p27 / 300, r->hold_lead90);
    if (r->tsm_cur) r->tsm_cur->retime_relatches_total += r->tm->rg_relatches - before;
  }
  count_shift(r, pesstamp_shift(out, (int64_t)timemap_k90(r->tm)));
}

static void finish_stamps(const remux_t *r, unsigned char *out) {
  if (!r->tm || r->cfg.pcr_mode == PCR_MODE_PRESERVE) return;
  afstamp_clear_discontinuity(out);
  afstamp_shift(out, (int64_t)timemap_k90(r->tm));
}

typedef struct {
  remux_t *r;
  unsigned pid;
  double now;
  remux_packet_cb cb;
  void *ctx;
} scte_emit_t;

static void scte_emit(void *c, unsigned char *pkt) {
  scte_emit_t *e = c;
  emit_out(e->r, e->pid, pkt, e->now, e->cb, e->ctx);
}

/* out cc = in cc + per-pid offset, so upstream dups/gaps show through. offset re-chosen after
   reconnect: first pkt continues last one sent, no discontinuity flag needed */
static void remap_cc(const remux_t *r, unsigned out_pid, unsigned char out[188]) {
  cc_offsets_t *m = r->ccm;
  unsigned i = out_pid - r->pids.video_pid;
  unsigned in = out[3] & 0x0F;
  int payload = (out[3] & 0x10) != 0;
  unsigned cc;
  if (!m || i >= OUT_PROGRAM_ES_CAP) return;
  if ((m->last[i] & 0x30) == 0x30) m->off[i] = (unsigned char)((m->last[i] + payload - in) & 0x0F);
  cc = (in + m->off[i]) & 0x0F;
  out[3] = (unsigned char)((out[3] & 0xF0) | cc);
  m->last[i] = (unsigned char)(0x10 | cc);
}

static void forward_packet(remux_t *r, const out_es_t *es, unsigned out_pid, const unsigned char *pkt188, double now, remux_packet_cb cb, void *ctx) {
  unsigned char out[188];
  memcpy(out, pkt188, 188);
  out[1] = (unsigned char)((out[1] & 0xE0) | ((out_pid >> 8) & 0x1F));
  out[2] = (unsigned char)out_pid;
  if (r->tm && r->cfg.pcr_mode == PCR_MODE_REBASE) rebase_packet(r, es, out_pid, out, now);
  if (r->cfg.pcr_mode == PCR_MODE_REGENERATE) regen_stage(r, es, out, now);
  finish_stamps(r, out);
  remap_cc(r, out_pid, out);
  if (es && r->scte[es - r->es] && r->tm) {
    scte_emit_t e = {r, out_pid, now, cb, ctx};
    unsigned long long before = r->scte[es - r->es]->patched;
    scte35stamp_feed(r->scte[es - r->es], out, (int64_t)timemap_k90(r->tm), scte_emit, &e);
    if (r->tsm_cur) r->tsm_cur->scte35_adjusted_total += r->scte[es - r->es]->patched - before;
    return;
  }
  if (hold_enqueue(r, es, out, now, cb, ctx)) return;
  emit_out(r, out_pid, out, now, cb, ctx);
}

void flush_hold_slot(remux_t *r, int idx, double now, remux_packet_cb cb, void *ctx) {
  while (r->hold[idx] && releaseq_len(r->hold[idx])) release_head(r, idx, now, cb, ctx);
}

void remux_release(remux_t *r, double now_s, remux_packet_cb cb, void *ctx, int all, ts_metrics_t *tsm) {
  r->tsm_cur = tsm;
  for (int idx = 0; idx < r->es_count; idx++) {
    const releaseq_t *q = r->hold[idx];
    while (q && releaseq_len(q)) {
      int has;
      uint64_t tag;
      uint64_t pcr;
      if (!all) {
        releaseq_head(q, &has, &tag);
        if (!r->hold_clock(r->hold_clock_ctx, &pcr)) break;
        if (has && diff90(tag, pcr / 300) > (int64_t)r->hold_lead90) break;
        if (has && tsm) {
          long long lead_us = diff90(tag, pcr / 300) * 100 / 9;
          if (!tsm->release_lead_seen || lead_us < tsm->release_lead_min_us) tsm->release_lead_min_us = lead_us;
          if (!tsm->release_lead_seen || lead_us > tsm->release_lead_max_us) tsm->release_lead_max_us = lead_us;
          tsm->release_lead_seen = 1;
        }
      }
      release_head(r, idx, now_s, cb, ctx);
    }
  }
}

int remux_hold_due_ms(const remux_t *r) {
  int best = -1;
  uint64_t pcr;
  if (!r->hold_clock || !r->hold_clock(r->hold_clock_ctx, &pcr)) return -1;
  for (int idx = 0; idx < r->es_count; idx++) {
    int has;
    uint64_t tag;
    int ms;
    if (!r->hold[idx] || !releaseq_len(r->hold[idx])) continue;
    releaseq_head(r->hold[idx], &has, &tag);
    if (has) {
      int64_t wait = diff90(tag, pcr / 300) - (int64_t)r->hold_lead90;
      ms = wait <= 0 ? 0 : (int)(wait / 90 + 1);
    } else {
      ms = 0;
    }
    if (best < 0 || ms < best) best = ms;
  }
  return best;
}

unsigned long long remux_hold_forced(const remux_t *r) { return r->hold_forced; }

/* continuity_counter gaps, source-side. cc advances on payload packets only (2.4.3.3),
   same has_payload test as forward_packet() */
static void track_continuity(remux_t *r, unsigned pid, const unsigned char pkt188[188], ts_metrics_t *tsm) {
  int has_payload = (pkt188[3] & 0x10) != 0;
  int has_adapt = (pkt188[3] & 0x20) != 0;
  int disc_flag = has_adapt && pkt188[4] >= 1 && (pkt188[5] & 0x80) != 0;
  unsigned char cc = pkt188[3] & 0x0F;
  unsigned char state = r->cc_state[pid];

  if (disc_flag && tsm) tsm->ts_discontinuities++;
  if (!has_payload) return;
  if ((state & 0x10) && !disc_flag && tsm && cc != (unsigned char)((state + 1) & 0x0F)) tsm->ts_continuity_errors++;
  r->cc_state[pid] = (unsigned char)(0x10 | cc);
}

static void track_pcr(remux_t *r, double now_s, const unsigned char pkt188[188], ts_metrics_t *tsm) {
  unsigned afc = (pkt188[3] >> 4) & 0x3;
  uint64_t base;
  uint64_t pcr27;
  unsigned ext;
  if (afc != 0x2 && afc != 0x3) return;
  if (pkt188[4] < 1 || !(pkt188[5] & 0x10) || pkt188[4] < 7) return;
  base = ((uint64_t)pkt188[6] << 25) | ((uint64_t)pkt188[7] << 17) | ((uint64_t)pkt188[8] << 9) | ((uint64_t)pkt188[9] << 1) | (pkt188[10] >> 7);
  ext = ((unsigned)(pkt188[10] & 0x01) << 8) | pkt188[11];
  pcr27 = base * 300 + ext;

  if (r->have_last_pcr && tsm && !tsm->ts_checks_off && !timemap_pcr_plausible(r->last_pcr27, pcr27, now_s - r->last_pcr_wall)) tsm->pcr_discontinuities++;
  r->last_pcr27 = pcr27;
  r->last_pcr_wall = now_s;
  r->have_last_pcr = 1;
}

void remux_feed(remux_t *r, double now_s, const unsigned char *pkt188, remux_packet_cb cb, void *ctx, ts_metrics_t *tsm) {
  unsigned in_pid;
  int idx;

  r->tsm_cur = tsm;
  send_psi_tables(r, now_s, cb, ctx, tsm);
  if (r->standalone) remux_emit_eit(r, OUT_PID_EIT, &r->cc_eit, 1, cb, ctx);

  if (pkt188[0] != 0x47) {
    if (tsm && !tsm->ts_checks_off) tsm->ts_sync_errors++;
    return;
  }
  in_pid = tspack_pid(pkt188);
  if (r->watch && (in_pid == OUT_PID_PAT || in_pid == r->watch_pmt_pid)) watch_source_pmt(r, now_s, pkt188, cb, ctx, tsm);
  if (tsm && !tsm->ts_checks_off) {
    tsm->ts_packets++;
    track_continuity(r, in_pid, pkt188, tsm);
  }
  if (r->pcr_pid_in && in_pid == r->pcr_pid_in && ((tsm && !tsm->ts_checks_off) || r->hold_clock)) track_pcr(r, now_s, pkt188, tsm);

  if (in_pid == OUT_PID_EIT) {
    if (!r->input.strip_eit) {
      capture_eit_section(r, pkt188, tsm);
      if (tsm) tsm->remux_packets_total++;
    } else if (tsm) tsm->remux_dropped_packets_total++;
    return;
  }

  idx = find_es(r, in_pid);
  if (idx < 0) {
    if (tsm) tsm->remux_dropped_packets_total++;
    return; /* PAT/PMT/SDT/NIT/unrecognized: not carried, we build our own or drop */
  }
  forward_packet(r, &r->es[idx], r->es[idx].out_pid, pkt188, now_s, cb, ctx);
  if (tsm) tsm->remux_packets_total++;
}
