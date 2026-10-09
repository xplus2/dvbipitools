/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/helper/log.h"
#include "lib/sys/ioutil.h"
#include "lib/mux/psi_build.h"

#include "../version.h"
#include "pes.h"
#include "psi.h"
#include "tspacketizer.h"

#define PID_PAT 0x0000
#define PID_CAT 0x0001 /* fixed per ISO/IEC 13818-1, never signalled */
#define PID_NIT 0x0010
#define PID_SDT 0x0011
#define PID_EIT 0x0012

#define INTERVAL_PAT_PMT 9000UL  /* 100ms @ 90kHz */
#define INTERVAL_SDT 180000UL    /* 2s */
#define INTERVAL_NIT 900000UL    /* 10s */
#define INTERVAL_EIT 90000UL     /* 1s */
#define PTS_DELAY_90K 27000UL    /* 300ms PTS lead over PCR */
#define PCR_MAX_GAP_90K 3150UL   /* 35ms, DVB limit is 40ms */
#define IDLE_INTERVAL_PAT_CAT_S 0.1
#define IDLE_INTERVAL_NIT_S 10.0
#define EIT_DURATION_S 180       /* nominal placeholder, real remaining time is unknown */

struct tspacketizer {
  tspacketizer_cfg_t cfg;
  unsigned pmt_pid;
  unsigned audio_pid;
  unsigned char cc_pat;
  unsigned char cc_pmt;
  unsigned char cc_sdt;
  unsigned char cc_nit;
  unsigned char cc_eit;
  unsigned char cc_audio;
  unsigned char cc_cat;
  unsigned char cc_ecm[ARGS_MAX_CAS_VENDORS];
  unsigned char cc_emm[ARGS_MAX_CAS_VENDORS];
  log_throttle_t cas_desc_throttle;
  unsigned ver_pat;
  unsigned ver_pmt;
  unsigned ver_sdt;
  unsigned ver_nit;
  unsigned ver_eit;
  char artist[256];
  char title[256];
  int meta_changed;
  time_t event_start;
  uint64_t last_pat;
  uint64_t last_sdt;
  uint64_t last_nit;
  uint64_t last_eit;
  uint64_t last_cat;
  double idle_last_pat;
  double idle_last_cat;
  double idle_last_nit;
  int disc_pending;
  uint64_t pts_base;
  uint64_t pts_end;
  cas_t *cas;
};

typedef struct {
  cas_t *cas;
  unsigned pid;
  double now;
  ts_packet_cb cb;
  void *ctx;
} cas_relay_t;

static void cas_relay_cb(void *ctx, const unsigned char *pkt188) {
  cas_relay_t *r = ctx;
  cas_scramble_packet(r->cas, r->pid, r->now, (unsigned char *)pkt188, r->cb, r->ctx);
}

tspacketizer_t *tspacketizer_new(const tspacketizer_cfg_t *cfg) {
  tspacketizer_t *t = calloc(1, sizeof *t);
  if (!t) return NULL;
  t->cfg = *cfg;
  t->pmt_pid = cfg->pmt_pid ? cfg->pmt_pid : 0x0100;
  t->audio_pid = cfg->audio_pid ? cfg->audio_pid : TSPACKETIZER_PID_AUDIO;
  t->last_pat = t->last_sdt = t->last_nit = t->last_eit = UINT64_MAX;
  t->idle_last_pat = t->idle_last_cat = t->idle_last_nit = -1.0;
  return t;
}

void tspacketizer_free(tspacketizer_t *t) { free(t); }

void tspacketizer_set_metadata(tspacketizer_t *t, const char *artist, const char *title) {
  bufcpy(t->artist, sizeof t->artist, artist);
  bufcpy(t->title, sizeof t->title, title);
  t->meta_changed = 1;
}

void tspacketizer_mark_discontinuity(tspacketizer_t *t) { t->disc_pending = 1; }

void tspacketizer_set_cas(tspacketizer_t *t, cas_t *cas) {
  t->cas = cas;
  t->last_cat = UINT64_MAX;
}

static const char *provider_name(const tspacketizer_t *t) {
  return (t->cfg.provider_name && t->cfg.provider_name[0]) ? t->cfg.provider_name : TOOL_NAME;
}

static int due(uint64_t now, uint64_t *last, uint64_t interval) {
  if (*last == UINT64_MAX || now - *last >= interval) {
    *last = now;
    return 1;
  }
  return 0;
}

static int due_s(double now, double *last, double interval) {
  if (*last < 0.0 || now - *last >= interval) {
    *last = now;
    return 1;
  }
  return 0;
}

static size_t emit_cas_ecm_emm(tspacketizer_t *t, size_t vi, double now, unsigned char *sec, size_t seccap, unsigned char *ptr0, ts_packet_cb cb, void *ctx) {
  size_t len, count = 0;
  if (cas_vendor_ecm_due(t->cas, vi, now, sec, seccap, &len) == 0)
    count += ts_packet_emit(cas_vendor_ecm_pid(t->cas, vi), &t->cc_ecm[vi], ptr0, sec, len, 0, 0, cb, ctx);
  while (cas_vendor_next_emm(t->cas, vi, sec, seccap, &len) == 0)
    count += ts_packet_emit(cas_vendor_emm_pid(t->cas, vi), &t->cc_emm[vi], ptr0, sec, len, 0, 0, cb, ctx);
  return count;
}

size_t tspacketizer_feed(tspacketizer_t *t, uint64_t pts_90k, uint32_t dur_90k, double now, const unsigned char *frame, size_t frame_len, ts_packet_cb cb, void *ctx) {
  unsigned char sec[4096];
  unsigned char pesbuf[8192];
  unsigned char prog_desc[64] = {0};
  unsigned char ptr0 = 0x00;
  size_t n, count = 0, prog_desc_len = 0;

  if (t->disc_pending) t->pts_base = t->pts_end - pts_90k;
  pts_90k += t->pts_base;
  if (due(pts_90k, &t->last_pat, INTERVAL_PAT_PMT)) {
    if (t->cas) {
      prog_desc_len = cas_prog_desc(t->cas, prog_desc, sizeof prog_desc);
      if (!prog_desc_len) log_throttled(&t->cas_desc_throttle, LOG_THROTTLE_WINDOW_S, "PMT: CA descriptor build failed, PMT not sent");
    }
    if (t->cfg.standalone) {
      n = psi_build_pat(t->cfg.tsid, t->ver_pat, t->cfg.sid, t->pmt_pid, sec, sizeof sec);
      if (n) count += ts_packet_emit(PID_PAT, &t->cc_pat, &ptr0, sec, n, 0, 0, cb, ctx);
    }
    n = (t->cas && !prog_desc_len) ? 0 : psi_build_pmt(t->ver_pmt, t->cfg.sid, t->pmt_pid, t->cfg.stream_type, t->audio_pid, t->cfg.aac_profile_level, prog_desc, prog_desc_len, sec, sizeof sec);
    if (n) count += ts_packet_emit(t->pmt_pid, &t->cc_pmt, &ptr0, sec, n, 0, 0, cb, ctx);
  }
  if (t->cfg.standalone) {
    int timer_due;
    int meta_due;

    if (t->cas && due(pts_90k, &t->last_cat, INTERVAL_PAT_PMT)) {
      n = cas_build_cat(t->cas, sec, sizeof sec);
      if (n) count += ts_packet_emit(PID_CAT, &t->cc_cat, &ptr0, sec, n, 0, 0, cb, ctx);
    }
    if (due(pts_90k, &t->last_sdt, INTERVAL_SDT)) {
      n = psi_build_sdt(t->ver_sdt, t->cfg.tsid, t->cfg.onid, t->cfg.sid, 0x02, provider_name(t), t->cfg.service_name, sec, sizeof sec);
      if (n) count += ts_packet_emit(PID_SDT, &t->cc_sdt, &ptr0, sec, n, 0, 0, cb, ctx);
    }
    if (t->cfg.network_name[0] && due(pts_90k, &t->last_nit, INTERVAL_NIT)) {
      n = psi_build_nit(t->ver_nit, t->cfg.onid, t->cfg.tsid, t->cfg.network_name, sec, sizeof sec);
      if (n) count += ts_packet_emit(PID_NIT, &t->cc_nit, &ptr0, sec, n, 0, 0, cb, ctx);
    }

    timer_due = due(pts_90k, &t->last_eit, INTERVAL_EIT);
    meta_due = t->meta_changed || timer_due;
    if (meta_due) {
      n = tspacketizer_build_eit(t, sec, sizeof sec);
      if (n) count += ts_packet_emit(PID_EIT, &t->cc_eit, &ptr0, sec, n, 0, 0, cb, ctx);
    }
    if (t->cas) {
      size_t n_vendors = cas_vendor_count(t->cas);
      for (size_t vi = 0; vi < n_vendors; vi++) count += emit_cas_ecm_emm(t, vi, now, sec, sizeof sec, &ptr0, cb, ctx);
    }
  }
  n = pes_build(pts_90k + PTS_DELAY_90K, frame, frame_len, pesbuf, sizeof pesbuf);
  if (n) {
    if (t->cas) {
      cas_relay_t relay = {t->cas, t->audio_pid, now, cb, ctx};
      count += ts_packet_emit_pcr(t->audio_pid, &t->cc_audio, pesbuf, n, pts_90k, t->disc_pending, cas_relay_cb, &relay);
    } else {
      count += ts_packet_emit_pcr(t->audio_pid, &t->cc_audio, pesbuf, n, pts_90k, t->disc_pending, cb, ctx);
    }
    t->disc_pending = 0;
    t->pts_end = pts_90k + dur_90k;
    if (dur_90k > PCR_MAX_GAP_90K) {
      unsigned extra = (unsigned)((dur_90k + PCR_MAX_GAP_90K - 1) / PCR_MAX_GAP_90K) - 1;
      for (unsigned i = 1; i <= extra; i++) {
        ts_packet_emit_pcr_only(t->audio_pid, &t->cc_audio, pts_90k + (uint64_t)dur_90k * i / (extra + 1), cb, ctx);
        count++;
      }
    }
  }

  return count;
}

int tspacketizer_set_codec(tspacketizer_t *t, unsigned stream_type, unsigned aac_profile_level) {
  if (t->cfg.stream_type == stream_type && t->cfg.aac_profile_level == aac_profile_level) return 0;
  if (t->cfg.stream_type) t->ver_pmt = (t->ver_pmt + 1) & 0x1F;
  t->cfg.stream_type = stream_type;
  t->cfg.aac_profile_level = aac_profile_level;
  return 1;
}

size_t tspacketizer_idle(tspacketizer_t *t, double now, ts_packet_cb cb, void *ctx) {
  unsigned char sec[4096];
  unsigned char ptr0 = 0x00;
  size_t n, count = 0;

  if (!t->cfg.standalone) return 0;
  if (due_s(now, &t->idle_last_pat, IDLE_INTERVAL_PAT_CAT_S)) {
    n = psi_build_pat(t->cfg.tsid, t->ver_pat, t->cfg.sid, t->pmt_pid, sec, sizeof sec);
    if (n) count += ts_packet_emit(PID_PAT, &t->cc_pat, &ptr0, sec, n, 0, 0, cb, ctx);
  }
  if (t->cas && due_s(now, &t->idle_last_cat, IDLE_INTERVAL_PAT_CAT_S)) {
    n = cas_build_cat(t->cas, sec, sizeof sec);
    if (n) count += ts_packet_emit(PID_CAT, &t->cc_cat, &ptr0, sec, n, 0, 0, cb, ctx);
  }
  if (t->cfg.network_name[0] && due_s(now, &t->idle_last_nit, IDLE_INTERVAL_NIT_S)) {
    n = psi_build_nit(t->ver_nit, t->cfg.onid, t->cfg.tsid, t->cfg.network_name, sec, sizeof sec);
    if (n) count += ts_packet_emit(PID_NIT, &t->cc_nit, &ptr0, sec, n, 0, 0, cb, ctx);
  }
  if (t->cas) {
    size_t n_vendors = cas_vendor_count(t->cas);
    for (size_t vi = 0; vi < n_vendors; vi++) count += emit_cas_ecm_emm(t, vi, now, sec, sizeof sec, &ptr0, cb, ctx);
  }
  return count;
}

int tspacketizer_get_sdt_info(tspacketizer_t *t, psi_sdt_entry_t *out) {
  out->service_id = t->cfg.sid;
  out->service_type = 0x02;
  out->provider = provider_name(t);
  out->service_name = t->cfg.service_name;
  return 0;
}

size_t tspacketizer_build_eit(tspacketizer_t *t, unsigned char *out, size_t cap) {
  size_t n, f;

  if (t->meta_changed) {
    t->ver_eit = (t->ver_eit + 1) & 0x1F;
    t->meta_changed = 0;
    t->event_start = time(NULL);
  }
  n = psi_build_eit(t->ver_eit, t->cfg.sid, t->cfg.tsid, t->cfg.onid, t->artist, t->title, EIT_DURATION_S, t->event_start, out, cap);
  if (!n) return 0;
  f = psi_build_eit_following(t->ver_eit, t->cfg.sid, t->cfg.tsid, t->cfg.onid, out + n, cap - n);
  return f ? n + f : 0;
}

int tspacketizer_eit_pending(const tspacketizer_t *t) { return t->meta_changed; }
