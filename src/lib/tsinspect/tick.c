/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <pthread.h>
#include <string.h>

#include "priv.h"

#define PUBLISH_S 0.25
#define PID_WINDOW_S 1.0
#define PID_MISSING_WINDOWS 5
#define PCR_WINDOW_S 30.0
#define CAT_GRACE_S 2.0
#define PTS_MAX_GAP_S 0.7
#define STALL_S 1.0
#define MGB_PKT_BITS 1504.0
#define MGB2_SLICE_S 0.1
#define EIT_PF_MAX_S 2.0
#define TDT_MAX_S 30.0
#define EIT_OTHER_MAX_S 10.0

typedef struct {
  psi_obs_id_t id;
  double limit_s;
  int only_if_seen;
} table_limit_t;

static const table_limit_t LIMITS[] = {
{PSI_OBS_PAT, 0.5, 0},
{PSI_OBS_PMT, 0.5, 0},
{PSI_OBS_SDT, 2.0, 0},
{PSI_OBS_NIT, 10.0, 0},
{PSI_OBS_SDT_OTHER, 10.0, 1},
{PSI_OBS_NIT_OTHER, 10.0, 1},
};
#define N_LIMITS (sizeof LIMITS / sizeof LIMITS[0])

static uint64_t *table_errors(tsinspect_counters_t *c, psi_obs_id_t id) {
  switch (id) {
    case PSI_OBS_PAT:       return &c->pat_errors;
    case PSI_OBS_PMT:       return &c->pmt_errors;
    case PSI_OBS_SDT:       return &c->sdt_errors;
    case PSI_OBS_SDT_OTHER: return &c->sdt_other_errors;
    case PSI_OBS_NIT_OTHER: return &c->nit_other_errors;
    default:                return &c->nit_errors;
  }
}

static void check_tables(tsinspect_t *t) {
  for (size_t i = 0; i < N_LIMITS; i++) {
    psi_obs_id_t id = LIMITS[i].id;
    double last;
    if (id == PSI_OBS_PMT) last = psi_pmt_oldest_seen(t->psi);
    else if (id == PSI_OBS_SDT_OTHER || id == PSI_OBS_NIT_OTHER) last = psi_obs_oldest_other(&t->x->obs, id);
    else if (id == PSI_OBS_SDT || id == PSI_OBS_NIT) last = t->x->obs.t[id].last_seen ? psi_obs_oldest_section(&t->x->obs, id) : 0.0;
    else last = t->x->obs.t[id].last_seen;
    double ref = last > 0.0 ? last : t->start;
    if (LIMITS[i].only_if_seen && last == 0.0) continue;
    if (t->now - ref > LIMITS[i].limit_s) {
      if (!t->table_flag[id]) {
        t->table_flag[id] = 1;
        (*table_errors(&t->counters, id))++;
      }
    } else t->table_flag[id] = 0;
  }
}

static void check_cat(tsinspect_t *t) {
  if (!t->x) return;
  if (t->counters.scrambled_packets && t->scrambled_since == 0.0) t->scrambled_since = t->now;
  if (t->scrambled_since != 0.0 && t->x->obs.t[PSI_OBS_CAT].last_seen == 0.0 && t->now - t->scrambled_since > CAT_GRACE_S) {
    if (!t->cat_flag) {
      t->cat_flag = 1;
      t->counters.cat_errors++;
    }
  } else t->cat_flag = 0;
}

static void note_ref_pids(tsinspect_t *t, const psi_es_t *es, int n, unsigned pcr) {
  unsigned short cur[PSI_MAX_ES + 1] = {0};
  int cn = 0;
  for (int i = 0; i <= n && cn <= PSI_MAX_ES; i++) {
    unsigned pid = i < n ? es[i].pid : pcr;
    int dup = 0;
    if (pid == 0 || pid >= 8192) continue;
    for (int k = 0; k < cn; k++) dup |= cur[k] == pid;
    if (!dup) cur[cn++] = (unsigned short)pid;
  }
  if (t->ref_valid) {
    for (int i = 0; i < cn; i++) {
      int found = 0;
      for (int k = 0; k < t->ref_n; k++) found |= t->ref[k] == cur[i];
      t->counters.pid_added += !found;
    }
    for (int k = 0; k < t->ref_n; k++) {
      int found = 0;
      for (int i = 0; i < cn; i++) found |= t->ref[k] == cur[i];
      t->counters.pid_removed += !found;
    }
  }
  memcpy(t->ref, cur, (size_t)cn * sizeof cur[0]);
  t->ref_n = cn;
  t->ref_valid = 1;
}

static void check_unreferenced(tsinspect_t *t) {
  for (unsigned pid = 0; pid < 8192; pid++) {
    int unref = (t->cc[pid] & CC_WIN) && psi_classify(t->psi, pid) == PID_UNKNOWN;
    int unlisted = unref && !(t->x->known[pid >> 3] & (1u << (pid & 7)));
    if (unref && !t->x->unref_flag[pid]) t->counters.unreferenced_pids++;
    t->x->unref_flag[pid] = (unsigned char)unref;
    if (unlisted && !t->x->unlisted_flag[pid]) t->counters.unreferenced_unlisted_pids++;
    t->x->unlisted_flag[pid] = (unsigned char)unlisted;
  }
}

static void check_referenced_pids(tsinspect_t *t) {
  const psi_es_t *es;
  int n = 0;
  unsigned pcr = psi_pcr_pid(t->psi);
  es = psi_es(t->psi, &n);
  if (!t->ref_valid || t->x->obs.t[PSI_OBS_PMT].changes != t->last_pmt_changes) {
    t->last_pmt_changes = (unsigned)t->x->obs.t[PSI_OBS_PMT].changes;
    memset(t->x->miss_run, 0, sizeof t->x->miss_run);
    if (t->x->obs.t[PSI_OBS_PMT].last_seen != 0.0) note_ref_pids(t, es, n, pcr);
  }
  for (int i = 0; i <= n; i++) {
    unsigned pid = i < n ? es[i].pid : pcr;
    int dup = 0;
    if (pid == 0 || pid >= 8192) continue;
    for (int k = 0; k < i && k < n; k++) dup |= es[k].pid == pid;
    if (dup) continue;
    if (t->cc[pid] & CC_WIN) t->x->miss_run[pid] = 0;
    else if (t->x->miss_run[pid] < 255 && ++t->x->miss_run[pid] == PID_MISSING_WINDOWS) t->counters.referenced_pid_missing++;
  }
  if (t->level >= METRICS_INSPECT_TS_FULL) check_unreferenced(t);
  for (unsigned i = 0; i < 8192; i++) t->cc[i] &= (unsigned char)~CC_WIN;
}

static void rebuild_pts_slots(tsinspect_t *t) {
  const psi_es_t *es;
  int n = 0;
  memset(t->x->pts_idx, 0, sizeof t->x->pts_idx);
  t->pts_n = 0;
  es = psi_es(t->psi, &n);
  for (int i = 0; i < n && t->pts_n < PSI_MAX_ES; i++) {
    if ((es[i].cls != PID_VIDEO && es[i].cls != PID_AUDIO) || es[i].pid >= 8192 || t->x->pts_idx[es[i].pid]) continue;
    t->x->pts[t->pts_n].pid = es[i].pid;
    t->x->pts[t->pts_n].last = t->now;
    t->x->pts[t->pts_n].flag = 0;
    t->pts_n++;
    t->x->pts_idx[es[i].pid] = (unsigned char)t->pts_n;
  }
  t->pts_built = 1;
  t->last_pts_changes = (unsigned)t->x->obs.t[PSI_OBS_PMT].changes;
}

static void check_pts_gaps(tsinspect_t *t) {
  if (!t->pts_built || t->x->obs.t[PSI_OBS_PMT].changes != t->last_pts_changes) rebuild_pts_slots(t);
  for (int i = 0; i < t->pts_n; i++) {
    if (t->now - t->x->pts[i].last > PTS_MAX_GAP_S) {
      if (!t->x->pts[i].flag) {
        t->x->pts[i].flag = 1;
        t->counters.pts_errors++;
      }
    } else {
      t->x->pts[i].flag = 0;
    }
  }
}

static void check_eit(tsinspect_t *t) {
  for (int i = 0; i < t->eit_n; i++) {
    for (int s = 0; s < 2; s++) {
      if (t->now - t->x->eit[i].last[s] > EIT_PF_MAX_S) {
        if (!t->x->eit[i].flag[s]) {
          t->x->eit[i].flag[s] = 1;
          t->counters.eit_errors++;
        }
      } else t->x->eit[i].flag[s] = 0;
    }
  }
}

static void check_eit_other(tsinspect_t *t) {
  for (int i = 0; i < t->eit_other_n; i++) {
    for (int s = 0; s < 2; s++) {
      if (t->now - t->x->eit_other[i].last[s] > EIT_OTHER_MAX_S) {
        if (!t->x->eit_other[i].flag[s]) {
          t->x->eit_other[i].flag[s] = 1;
          t->counters.eit_other_errors++;
        }
      } else t->x->eit_other[i].flag[s] = 0;
    }
  }
}

static void check_tdt(tsinspect_t *t) {
  double ref = t->tdt_last > 0.0 ? t->tdt_last : t->start;
  if (t->now - ref > TDT_MAX_S) {
    if (!t->tdt_flag) {
      t->tdt_flag = 1;
      t->counters.tdt_errors++;
    }
  } else t->tdt_flag = 0;
}

static void update_mgb(tsinspect_t *t) {
  uint64_t pk = t->counters.packets;

  if (t->mgb1_start == 0.0) {
    t->mgb1_start = t->now;
    t->mgb1_pk = pk;
  } else if (t->now - t->mgb1_start >= 1.0) {
    t->mgb1_bps = (double)(pk - t->mgb1_pk) * MGB_PKT_BITS / (t->now - t->mgb1_start);
    t->mgb1_valid = 1;
    t->mgb1_start = t->now;
    t->mgb1_pk = pk;
  }
  if (t->mgb2_n == 0 || t->now - t->mgb2_t[(t->mgb2_head + MGB2_SLICES) % (MGB2_SLICES + 1)] >= MGB2_SLICE_S) {
    int slot = (t->mgb2_head + t->mgb2_n) % (MGB2_SLICES + 1);
    if (t->mgb2_n == MGB2_SLICES + 1) {
      t->mgb2_head = (t->mgb2_head + 1) % (MGB2_SLICES + 1);
      slot = (t->mgb2_head + MGB2_SLICES) % (MGB2_SLICES + 1);
    } else t->mgb2_n++;
    t->mgb2_t[slot] = t->now;
    t->mgb2_p[slot] = pk;
    if (t->mgb2_n == MGB2_SLICES + 1) {
      t->mgb2_bps = (double)(pk - t->mgb2_p[t->mgb2_head]) * MGB_PKT_BITS / (t->now - t->mgb2_t[t->mgb2_head]);
      t->mgb2_valid = 1;
    }
  }
}

static void check_stall(tsinspect_t *t) {
  if (t->counters.packets != t->seen_packets) {
    double gap = t->now - t->last_packet;
    if (t->last_packet != 0.0 && gap > t->gap_max_cur) t->gap_max_cur = gap;
    t->stalled = 0;
    t->last_packet = t->now;
    t->seen_packets = t->counters.packets;
  } else if (t->last_packet != 0.0 && t->now - t->last_packet > STALL_S) {
    if (!t->stalled) {
      t->stalled = 1;
      t->counters.stalls++;
    }
    t->counters.stall_ms += (uint64_t)((t->now - t->prev_now) * 1000.0);
  }
}

static void tick_psi(tsinspect_t *t) {
  check_tables(t);
  if (t->x->obs.t[PSI_OBS_PMT].last_seen != 0.0) check_pts_gaps(t);
  t->pcr_pid = psi_pcr_pid(t->psi);
  if (t->pcr_pid == 0) t->pcr_pid = PID_NONE;
  if (t->now - t->win_start >= PID_WINDOW_S) {
    check_referenced_pids(t);
    t->win_start = t->now;
  }
}

void publish(tsinspect_t *t) {
  pub_t p;
  p.c = t->counters;
  p.sync = t->sync;
  p.sync_used = t->sync_used;
  p.psi_bound = t->psi != NULL;
  p.relay = t->relay;
  if (t->x) memcpy(p.psi, t->x->obs.t, sizeof p.psi);
  else memset(p.psi, 0, sizeof p.psi);
  p.pcr_max_s = tsinspect_pcr_max_interval(t);
  p.gap_max_ms = tsinspect_max_gap_ms(t);
  p.last_packet = t->last_packet;
  p.pcr_jitter_us = t->pcr_jit_cur > t->pcr_jit_prev ? t->pcr_jit_cur : t->pcr_jit_prev;
  p.rx_used = t->rx_used;
  p.mgb1_bps = t->mgb1_bps;
  p.mgb1_valid = t->mgb1_valid;
  p.mgb2_bps = t->mgb2_bps;
  p.mgb2_valid = t->mgb2_valid;
  p.known_set = t->x && t->x->known_set;
  if (t->x && t->x->d) {
    const detail_t *d = t->x->d;
    p.n_pid = d->n_pid;
    p.n_svc = d->n_svc;
    memcpy(p.pid_list, d->pid_list, d->n_pid * sizeof p.pid_list[0]);
    memcpy(p.pid_pkts, d->pid_pkts, d->n_pid * sizeof p.pid_pkts[0]);
    memcpy(p.pid_scr, d->pid_scr, d->n_pid * sizeof p.pid_scr[0]);
    memcpy(p.svc_list, d->svc_list, d->n_svc * sizeof p.svc_list[0]);
    memcpy(p.svc_pkts, d->svc_pkts, d->n_svc * sizeof p.svc_pkts[0]);
    memcpy(p.svc_scr, d->svc_scr, d->n_svc * sizeof p.svc_scr[0]);
  } else {
    p.n_pid = 0;
    p.n_svc = 0;
  }
  p.have_tsid = t->psi && psi_have_pat(t->psi);
  p.tsid = p.have_tsid ? psi_transport_stream_id(t->psi) : 0;
  pthread_mutex_lock(&t->pub_lock);
  t->pub = p;
  pthread_mutex_unlock(&t->pub_lock);
  t->pub_at = t->now;
}

void tsinspect_tick(tsinspect_t *t, double now) {
  t->now = now;
  if (t->start == 0.0) {
    t->start = now;
    t->win_start = now;
    t->pcr_win_start = now;
    t->prev_now = now;
  }
  if (now - t->pcr_win_start >= PCR_WINDOW_S) {
    t->pcr_max_prev = t->pcr_max_cur;
    t->pcr_max_cur = 0.0;
    t->pcr_jit_prev = t->pcr_jit_cur;
    t->pcr_jit_cur = 0.0;
    t->gap_max_prev = t->gap_max_cur;
    t->gap_max_cur = 0.0;
    t->pcr_win_start = now;
  }
  check_stall(t);
  update_mgb(t);
  if (t->x) {
    check_eit(t);
    check_eit_other(t);
    check_tdt(t);
  }
  t->prev_now = now;
  check_cat(t);
  if (t->psi) tick_psi(t);
  if (t->pub_at == 0.0 || now - t->pub_at >= PUBLISH_S) publish(t);
}
