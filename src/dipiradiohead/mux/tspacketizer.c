/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lib/ioutil.h"
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
#define EIT_DURATION_S 180       /* nominal placeholder, real remaining time is unknown */

struct tspacketizer {
  tspacketizer_cfg_t cfg;
  unsigned pmt_pid, audio_pid;
  unsigned char cc_pat, cc_pmt, cc_sdt, cc_nit, cc_eit, cc_audio, cc_cat;
  unsigned char cc_ecm[ARGS_MAX_CAS_VENDORS], cc_emm[ARGS_MAX_CAS_VENDORS];
  unsigned ver_pat, ver_pmt, ver_sdt, ver_nit, ver_eit;
  char artist[256], title[256];
  int meta_changed;
  uint64_t last_pat, last_sdt, last_nit, last_eit, last_cat;
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
  if (!t)
    return NULL;
  t->cfg = *cfg;
  t->pmt_pid = cfg->pmt_pid ? cfg->pmt_pid : 0x0100;
  t->audio_pid = cfg->audio_pid ? cfg->audio_pid : TSPACKETIZER_PID_AUDIO;
  t->last_pat = t->last_sdt = t->last_nit = t->last_eit = UINT64_MAX;
  return t;
}

void tspacketizer_free(tspacketizer_t *t) { free(t); }

void tspacketizer_set_metadata(tspacketizer_t *t, const char *artist, const char *title) {
  bufcpy(t->artist, sizeof t->artist, artist);
  bufcpy(t->title, sizeof t->title, title);
  t->meta_changed = 1;
}

void tspacketizer_set_cas(tspacketizer_t *t, cas_t *cas) {
  t->cas = cas;
  t->last_cat = UINT64_MAX;
}

static int due(uint64_t now, uint64_t *last, uint64_t interval) {
  if (*last == UINT64_MAX || now - *last >= interval) {
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

size_t tspacketizer_feed(tspacketizer_t *t, uint64_t pts_90k, double now, const unsigned char *frame, size_t frame_len, ts_packet_cb cb, void *ctx) {
  unsigned char sec[4096], pesbuf[8192], prog_desc[32];
  unsigned char ptr0 = 0x00;
  size_t n, count = 0, prog_desc_len = 0;

  if (due(pts_90k, &t->last_pat, INTERVAL_PAT_PMT)) {
    if (t->cas)
      prog_desc_len = cas_prog_desc(t->cas, prog_desc, sizeof prog_desc);
    if (t->cfg.standalone) {
      n = psi_build_pat(t->cfg.tsid, t->ver_pat, t->cfg.sid, t->pmt_pid, sec, sizeof sec);
      if (n)
        count += ts_packet_emit(PID_PAT, &t->cc_pat, &ptr0, sec, n, 0, 0, cb, ctx);
    }
    n = psi_build_pmt(t->ver_pmt, t->cfg.sid, t->pmt_pid, t->cfg.stream_type, t->audio_pid, prog_desc, prog_desc_len, sec, sizeof sec);
    if (n)
      count += ts_packet_emit(t->pmt_pid, &t->cc_pmt, &ptr0, sec, n, 0, 0, cb, ctx);
  }
  if (t->cfg.standalone) {
    if (t->cas && due(pts_90k, &t->last_cat, INTERVAL_PAT_PMT)) {
      n = cas_build_cat(t->cas, sec, sizeof sec);
      if (n)
        count += ts_packet_emit(PID_CAT, &t->cc_cat, &ptr0, sec, n, 0, 0, cb, ctx);
    }
    if (due(pts_90k, &t->last_sdt, INTERVAL_SDT)) {
      n = psi_build_sdt(t->ver_sdt, t->cfg.tsid, t->cfg.onid, t->cfg.sid, 0x02, TOOL_NAME, t->cfg.service_name, sec, sizeof sec);
      if (n)
        count += ts_packet_emit(PID_SDT, &t->cc_sdt, &ptr0, sec, n, 0, 0, cb, ctx);
    }
    if (t->cfg.network_name[0] && due(pts_90k, &t->last_nit, INTERVAL_NIT)) {
      n = psi_build_nit(t->ver_nit, t->cfg.onid, t->cfg.tsid, t->cfg.network_name, sec, sizeof sec);
      if (n)
        count += ts_packet_emit(PID_NIT, &t->cc_nit, &ptr0, sec, n, 0, 0, cb, ctx);
    }

    int timer_due = due(pts_90k, &t->last_eit, INTERVAL_EIT);
    int meta_due = t->meta_changed || timer_due;
    if (meta_due) {
      n = tspacketizer_build_eit(t, sec, sizeof sec);
      if (n)
        count += ts_packet_emit(PID_EIT, &t->cc_eit, &ptr0, sec, n, 0, 0, cb, ctx);
    }
    if (t->cas) {
      size_t vi, n_vendors = cas_vendor_count(t->cas);
      for (vi = 0; vi < n_vendors; vi++)
        count += emit_cas_ecm_emm(t, vi, now, sec, sizeof sec, &ptr0, cb, ctx);
    }
  }
  n = pes_build(pts_90k, frame, frame_len, pesbuf, sizeof pesbuf);
  if (n) {
    if (t->cas) {
      cas_relay_t relay = {t->cas, t->audio_pid, now, cb, ctx};
      count += ts_packet_emit(t->audio_pid, &t->cc_audio, NULL, pesbuf, n, 1, pts_90k, cas_relay_cb, &relay);
    } else {
      count += ts_packet_emit(t->audio_pid, &t->cc_audio, NULL, pesbuf, n, 1, pts_90k, cb, ctx);
    }
  }

  return count;
}

int tspacketizer_get_sdt_info(tspacketizer_t *t, psi_sdt_entry_t *out) {
  out->service_id = t->cfg.sid;
  out->service_type = 0x02;
  out->provider = TOOL_NAME;
  out->service_name = t->cfg.service_name;
  return 0;
}

size_t tspacketizer_build_eit(tspacketizer_t *t, unsigned char *out, size_t cap) {
  if (t->meta_changed) {
    t->ver_eit = (t->ver_eit + 1) & 0x1F;
    t->meta_changed = 0;
  }
  return psi_build_eit(t->ver_eit, t->cfg.sid, t->cfg.tsid, t->cfg.onid, t->artist, t->title, EIT_DURATION_S, out, cap);
}

int tspacketizer_eit_pending(const tspacketizer_t *t) { return t->meta_changed; }
