/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/demux/crc32.h"
#include "lib/demux/psi/section_asm.h"
#include "lib/helper/signal.h"

#include "priv.h"

#define CC_SEEN 0x10
#define CC_DUP 0x20
#define PCR_MODULUS (((uint64_t)1 << 33) * 300ULL)
#define PCR_MAX_TICKS 2700000ULL
#define PCR_ACCURACY_US 0.5
#define PID_NULL 0x1FFF
#define PID_EIT 0x12

static void check_cc(tsinspect_t *t, unsigned pid, unsigned afc, unsigned cc, int disc) {
  unsigned char *s = &t->cc[pid];
  unsigned char win = *s & CC_WIN;
  unsigned last;

  if (!(*s & CC_SEEN)) {
    *s = (unsigned char)(win | CC_SEEN | cc);
    return;
  }
  last = *s & 0x0F;
  if (afc & 1) {
    if (cc == ((last + 1) & 0x0F)) {
      *s = (unsigned char)(win | CC_SEEN | cc);
      return;
    }
    if (cc == last && !(*s & CC_DUP)) {
      *s |= CC_DUP;
      t->counters.duplicate_packets++;
      return;
    }
  } else if (cc == last) return;
  if (!disc) t->counters.continuity_errors++;
  *s = (unsigned char)(win | CC_SEEN | cc);
}

static void check_pcr(tsinspect_t *t, const unsigned char *pkt, int disc) {
  uint64_t pcr;
  if (pkt[4] < 7 || !(pkt[5] & 0x10)) return;
  pcr = (((uint64_t)pkt[6] << 25) | ((uint64_t)pkt[7] << 17) | ((uint64_t)pkt[8] << 9) | ((uint64_t)pkt[9] << 1) | (pkt[10] >> 7)) * 300ULL + (((uint64_t)(pkt[10] & 0x01) << 8) | pkt[11]);
  if (t->pcr_have && !disc) {
    uint64_t ticks = (pcr + PCR_MODULUS - t->pcr_last) % PCR_MODULUS;
    if (ticks > PCR_MODULUS / 2) {
      t->counters.pcr_discontinuity_errors++;
    } else {
      double interval = (double)ticks / 27000000.0;
      if (t->rx_ns) {
        if (t->pcr_rx_last && t->rx_ns > t->pcr_rx_last) {
          double dev = (double)(t->rx_ns - t->pcr_rx_last) / 1000.0 - (double)ticks / 27.0;
          if (dev < 0.0) dev = -dev;
          if (dev > t->pcr_jit_cur) t->pcr_jit_cur = dev;
          if (dev > PCR_ACCURACY_US) t->counters.pcr_accuracy_errors++;
        }
        t->pcr_rx_last = t->rx_ns;
        t->rx_used = 1;
      }
      if (ticks > PCR_MAX_TICKS) {
        t->counters.pcr_repetition_errors++;
        t->counters.pcr_discontinuity_errors++;
      }
      if (interval > t->pcr_max_cur) t->pcr_max_cur = interval;
    }
  }
  t->pcr_last = pcr;
  if (t->rx_ns) t->pcr_rx_last = t->rx_ns;
  t->pcr_have = 1;
}

static void check_pts(tsinspect_t *t, const unsigned char *pkt, unsigned afc, unsigned slot) {
  size_t off = afc == 3 ? 5 + (size_t)pkt[4] : 4;
  const unsigned char *pl;
  if (off + 14 > 188) return;
  pl = pkt + off;
  if (pl[0] || pl[1] || pl[2] != 1 || !(pl[7] & 0x80)) return;
  t->x->pts[slot].last = t->now;
}

static void eit_section(tsinspect_t *t, const unsigned char *b, eit_slot_t *slots, int *n) {
  unsigned sid = ((unsigned)b[3] << 8) | b[4];
  unsigned sec = b[6];
  eit_slot_t *e = NULL;

  if (sec > 1) return;
  for (int i = 0; i < *n; i++) if (slots[i].sid == sid) e = &slots[i];
  if (!e) {
    if (*n == PSI_MAX_PROGRAMS) return;
    e = &slots[(*n)++];
    e->sid = sid;
    e->last[0] = e->last[1] = t->now;
  }
  e->last[sec] = t->now;
}

static void si_section(tsinspect_t *t, unsigned pid, const unsigned char *b, size_t n) {
  unsigned tid = b[0];

  if (pid == PID_EIT) {
    if (t->x->obs.crc_bat && tid >= 0x4E && tid <= 0x6F && n >= 12 && crc32_mpeg(b, n) != 0) t->counters.si_crc_errors++;
    if (tid == 0x4E && n >= 14) eit_section(t, b, t->x->eit, &t->eit_n);
    else if (tid == 0x4F && n >= 14) eit_section(t, b, t->x->eit_other, &t->eit_other_n);
  } else if (pid == PID_EIT + 1) {
    if (tid != 0x71) t->counters.rst_errors++;
  } else if (tid == 0x70 || tid == 0x73) {
    t->tdt_last = t->now;
    if (tid == 0x73 && t->x->obs.crc_bat && n >= 12 && crc32_mpeg(b, n) != 0) t->counters.si_crc_errors++;
  } else t->counters.tdt_errors++;
}

static void si_packet(tsinspect_t *t, unsigned pid, const unsigned char *pkt, unsigned afc) {
  size_t off = afc == 3 ? 5 + (size_t)pkt[4] : 4;
  psi_section_asm_t *a = &t->x->si_asm[pid - PID_EIT];
  if (off >= 188) return;
  if (psi_section_asm_feed(a, pkt + off, 188 - off, (pkt[1] & 0x40) != 0) && a->expect >= 8) si_section(t, pid, a->buf, a->expect);
}

static void count_detail(detail_t *d, unsigned svc, unsigned pid, int scrambled) {
  unsigned s;
  if (!svc) return;
  s = d->pid_slot[pid];
  if (!s && d->n_pid < PID_DETAIL_MAX) {
    d->pid_list[d->n_pid] = pid;
    s = d->pid_slot[pid] = (unsigned char)++d->n_pid;
  }
  if (s) {
    d->pid_pkts[s - 1]++;
    if (scrambled) d->pid_scr[s - 1]++;
  }
  s = d->svc_slot[svc];
  if (!s && d->n_svc < SVC_DETAIL_MAX) {
    d->svc_list[d->n_svc] = svc;
    s = d->svc_slot[svc] = (unsigned char)++d->n_svc;
  }
  if (s) {
    d->svc_pkts[s - 1]++;
    if (scrambled) d->svc_scr[s - 1]++;
  }
}

static inline void packet_body(tsinspect_t *t, const unsigned char *pkt, int full) {
  tsinspect_counters_t *c = &t->counters;
  unsigned pid, afc, cc;
  int disc;
  if (pkt[0] != 0x47) return;
  c->packets++;
  if (pkt[1] & 0x80) {
    c->transport_errors++;
    return;
  }
  pid = tspack_pid(pkt);
  if (t->own_psi && psi_wants_pid(t->psi, pid)) psi_feed(t->psi, pkt);
  if (pid == PID_NULL) {
    c->null_packets++;
    return;
  }
  if (pkt[3] & 0xC0) c->scrambled_packets++;
  else c->clear_packets++;
  if (t->x && t->x->d && t->psi) count_detail(t->x->d, psi_service_of_pid(t->psi, pid), pid, pkt[3] & 0xC0);
  afc = (pkt[3] >> 4) & 0x3;
  cc = pkt[3] & 0x0F;
  disc = afc >= 2 && pkt[4] > 0 && (pkt[5] & 0x80);
  if (disc) c->discontinuity_indicators++;
  if (afc) check_cc(t, pid, afc, cc, disc);
  t->cc[pid] |= CC_WIN;
  if (full && t->psi && t->x->obs.t[PSI_OBS_PMT].last_seen != 0.0 && psi_classify(t->psi, pid) == PID_UNKNOWN) c->unreferenced_packets++;
  if (t->relay && t->pcr_pid == PID_NONE && afc >= 2 && pkt[4] >= 7 && (pkt[5] & 0x10)) t->pcr_pid = pid;
  if (pid == t->pcr_pid && afc >= 2) check_pcr(t, pkt, disc);
  if (!t->x) return;
  if (pid - PID_EIT <= 2 && (afc & 1)) si_packet(t, pid, pkt, afc);
  if ((pkt[1] & 0x40) && (afc & 1) && t->x->pts_idx[pid] && !(pkt[3] & 0xC0)) check_pts(t, pkt, afc, (unsigned)t->x->pts_idx[pid] - 1);
}

void packet_base(tsinspect_t *t, const unsigned char *pkt) { packet_body(t, pkt, 0); }

void packet_full(tsinspect_t *t, const unsigned char *pkt) { packet_body(t, pkt, 1); }

void tsinspect_packet(tsinspect_t *t, const unsigned char *pkt) { t->packet(t, pkt); }

void tsinspect_grid(tsinspect_t *t, const unsigned char *buf, size_t len) {
  t->sync_used = 1;
  for (; len >= 188; buf += 188, len -= 188) {
    if (buf[0] == 0x47) {
      tspack_sync_good(&t->sync);
      tsinspect_packet(t, buf);
    } else tspack_sync_bad(&t->sync);
  }
}
