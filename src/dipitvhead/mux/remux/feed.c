/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/demux/tspack.h"

#include "priv.h"

/* es[] index for a source pid, or -1 if not carried (dropped) */
static int find_es(remux_t *r, unsigned in_pid) {
  if (r->last_es_idx >= 0 && r->last_es_idx < r->es_count && r->es[r->last_es_idx].in_pid == in_pid)
    return r->last_es_idx;

  for (int i = 0; i < r->es_count; i++)
    if (r->es[i].in_pid == in_pid) {
      r->last_es_idx = i;
      return i;
    }
  return -1;
}

static void forward_packet(remux_t *r, unsigned out_pid, unsigned char *cc, const unsigned char *pkt188, double now, remux_packet_cb cb, void *ctx) {
  unsigned char out[188];
  /* ISO 13818-1 2.4.3.3: AFC '10' (no payload) never bumps cc. PCR pid paces off these, bumping fakes a discontinuity. */
  int has_payload = (pkt188[3] & 0x10) != 0;
  memcpy(out, pkt188, 188);
  out[1] = (unsigned char)((out[1] & 0xE0) | ((out_pid >> 8) & 0x1F));
  out[2] = (unsigned char)out_pid;
  if (has_payload)
    *cc = (unsigned char)((*cc + 1) & 0x0F);
  out[3] = (unsigned char)((out[3] & 0xF0) | *cc);
  if (r->cas) {
    cas_pcr_tick(r->cas, out_pid, out);
    cas_scramble_packet(r->cas, out_pid, now, out, cb, ctx);
  } else {
    cb(ctx, out);
  }
}

/* continuity_counter gaps, source-side. cc advances on payload packets only (2.4.3.3),
   same has_payload test as forward_packet() */
static void track_continuity(remux_t *r, unsigned pid, const unsigned char pkt188[188], ts_metrics_t *tsm) {
  int has_payload = (pkt188[3] & 0x10) != 0;
  int has_adapt = (pkt188[3] & 0x20) != 0;
  int disc_flag = has_adapt && pkt188[4] >= 1 && (pkt188[5] & 0x80) != 0;
  unsigned char cc = pkt188[3] & 0x0F;
  unsigned char state = r->cc_state[pid];

  if (disc_flag && tsm)
    tsm->ts_discontinuities++;
  if (!has_payload)
    return;
  if ((state & 0x10) && !disc_flag && tsm && cc != (unsigned char)((state + 1) & 0x0F))
    tsm->ts_continuity_errors++;
  r->cc_state[pid] = (unsigned char)(0x10 | cc);
}

#define TS_PCR_MODULUS (((uint64_t)1 << 33) * 300ULL)
#define TS_PCR_CLOCK_HZ 27000000ULL

/* same plausibility fence as cas_pcr_plausible(): cas.h forbids remux.c calling it
   directly, own copy of the ISO 13818-1 2.4.3.5 arithmetic */
static int ts_pcr_plausible(uint64_t last_pcr27, uint64_t new_pcr27, double wall_delta_s) {
  uint64_t delta_ticks = (new_pcr27 + TS_PCR_MODULUS - last_pcr27) % TS_PCR_MODULUS;
  double pcr_delta_s = (double)delta_ticks / (double)TS_PCR_CLOCK_HZ;
  return wall_delta_s > 0.0 && pcr_delta_s > 0.0 && pcr_delta_s < 60.0 && pcr_delta_s < wall_delta_s * 5.0 + 0.5 && pcr_delta_s > wall_delta_s * 0.2 - 0.1;
}

static void track_pcr(remux_t *r, double now_s, const unsigned char pkt188[188], ts_metrics_t *tsm) {
  unsigned afc = (pkt188[3] >> 4) & 0x3;
  uint64_t base, pcr27;
  unsigned ext;

  if (afc != 0x2 && afc != 0x3)
    return;
  if (pkt188[4] < 1 || !(pkt188[5] & 0x10) || pkt188[4] < 7)
    return;
  base = ((uint64_t)pkt188[6] << 25) | ((uint64_t)pkt188[7] << 17) | ((uint64_t)pkt188[8] << 9) | ((uint64_t)pkt188[9] << 1) | (pkt188[10] >> 7);
  ext = ((unsigned)(pkt188[10] & 0x01) << 8) | pkt188[11];
  pcr27 = base * 300 + ext;

  if (r->have_last_pcr && tsm && !ts_pcr_plausible(r->last_pcr27, pcr27, now_s - r->last_pcr_wall))
    tsm->pcr_discontinuities++;
  r->last_pcr27 = pcr27;
  r->last_pcr_wall = now_s;
  r->have_last_pcr = 1;
}

void remux_feed(remux_t *r, double now_s, const unsigned char *pkt188, remux_packet_cb cb, void *ctx, ts_metrics_t *tsm) {
  unsigned in_pid;
  int idx;

  send_psi_tables(r, now_s, cb, ctx, tsm);

  if (pkt188[0] != 0x47) {
    if (tsm)
      tsm->ts_sync_errors++;
    return;
  }
  in_pid = tspack_pid(pkt188);
  if (tsm) {
    tsm->ts_packets++;
    track_continuity(r, in_pid, pkt188, tsm);
    if (r->pcr_pid_in && in_pid == r->pcr_pid_in)
      track_pcr(r, now_s, pkt188, tsm);
  }

  if (in_pid == OUT_PID_EIT) {
    if (!r->input.strip_eit) {
      if (r->standalone)
        forward_packet(r, OUT_PID_EIT, &r->cc_eit, pkt188, now_s, cb, ctx);
      else
        capture_eit_section(r, pkt188, tsm);
      if (tsm)
        tsm->remux_packets_total++;
    } else if (tsm) {
      tsm->remux_dropped_packets_total++;
    }
    return;
  }

  idx = find_es(r, in_pid);
  if (idx < 0) {
    if (tsm)
      tsm->remux_dropped_packets_total++;
    return; /* PAT/PMT/SDT/NIT/unrecognized: not carried, we build our own or drop */
  }
  forward_packet(r, r->es[idx].out_pid, &r->cc_es[idx], pkt188, now_s, cb, ctx);
  if (tsm)
    tsm->remux_packets_total++;
}
