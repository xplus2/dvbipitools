/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/cas/cas_group.h"
#include "lib/log.h"

#include "cas.h"

struct cas {
  int have_clock;
  uint64_t last_pts90k;
  uint64_t rem_90k; /* carry for exact 90kHz -> ms conversion, no drift */
  cas_group_t *group;
};

unsigned long cas_90k_to_ms(uint64_t delta_90k, uint64_t *rem_inout) {
  uint64_t total = delta_90k + *rem_inout;
  *rem_inout = total % 90;
  return (unsigned long)(total / 90);
}

static ecmg_outage_mode_t map_outage_mode(cas_outage_mode_t m) {
  switch (m) {
  case CAS_OUTAGE_CYCLING:
    return ECMG_OUTAGE_CYCLING;
  case CAS_OUTAGE_SILENT:
    return ECMG_OUTAGE_SILENT;
  default:
    return ECMG_OUTAGE_FROZEN;
  }
}

cas_t *cas_start(const config_t *cfg, const unsigned *audio_pids, size_t n_audio_pids) {
  cas_t *c;
  cas_group_cfg_t gcfg;
  size_t i;

  if (n_audio_pids == 0) {
    log_line("cas: no audio pids to scramble");
    return NULL;
  }
  if (n_audio_pids > CAS_CORE_MAX_PIDS) {
    log_line("cas: %zu audio pids exceeds the %d cas can scramble in one session", n_audio_pids, CAS_CORE_MAX_PIDS);
    return NULL;
  }

  c = calloc(1, sizeof *c);
  if (!c)
    return NULL;

  memset(&gcfg, 0, sizeof gcfg);
  gcfg.algo = (cfg->cas_algo == CAS_ALGO_CISSA) ? SCRAMBLE_ALGO_CISSA : SCRAMBLE_ALGO_CSA2;
  gcfg.cp_duration_ms = cfg->cas_cp_duration_ms;
  gcfg.fallback_clear = cfg->cas_fallback_clear;
  if (cfg->n_cas_vendors > CAS_GROUP_MAX_VENDORS)
    log_line("cas: %u vendors configured, only the first %d will be started", cfg->n_cas_vendors, CAS_GROUP_MAX_VENDORS);
  gcfg.vendor_count = cfg->n_cas_vendors < CAS_GROUP_MAX_VENDORS ? cfg->n_cas_vendors : CAS_GROUP_MAX_VENDORS;
  for (i = 0; i < gcfg.vendor_count; i++) {
    const cas_vendor_t *v = &cfg->cas_vendors[i];
    cas_group_vendor_cfg_t *gv = &gcfg.vendors[i];
    gv->ecmg_host = v->ecmg_host;
    gv->ecmg_port = v->ecmg_port;
    gv->ecmg_version = v->ecmg_version;
    gv->super_cas_id = v->super_cas_id;
    gv->ecm_id = v->ecm_id;
    gv->ecm_pid = v->ecm_pid;
    gv->emm_pid = v->emm_pid;
    gv->emmg_port = v->emmg_port;
    gv->required = v->required;
    gv->outage_mode = map_outage_mode(v->resilience);
  }
  for (i = 0; i < n_audio_pids; i++)
    gcfg.pids[i] = audio_pids[i];
  gcfg.pid_count = n_audio_pids;

  c->group = cas_group_start(&gcfg, audio_pids[0]);
  if (!c->group) {
    free(c);
    return NULL;
  }
  return c;
}

void cas_stop(cas_t *c) {
  if (!c)
    return;
  cas_group_stop(c->group);
  free(c);
}

int cas_failed(cas_t *c) { return cas_group_failed(c->group); }

void cas_clock_tick(cas_t *c, uint64_t pts_90k) {
  cas_group_tick_alive(c->group);
  if (!c->have_clock) {
    c->have_clock = 1;
    cas_group_clock_tick(c->group, 0);
  } else {
    unsigned long delta_ms;
    uint64_t rem = c->rem_90k;
    delta_ms = cas_90k_to_ms(pts_90k - c->last_pts90k, &rem);
    cas_group_clock_tick(c->group, delta_ms);
    c->rem_90k = rem;
  }
  c->last_pts90k = pts_90k;
}

void cas_scramble_packet(cas_t *c, unsigned out_pid, double now, unsigned char pkt188[188], scrambler_emit_cb emit, void *ctx) {
  cas_group_scramble_packet(c->group, out_pid, now, pkt188, emit, ctx);
}

void cas_flush(cas_t *c, scrambler_emit_cb emit, void *ctx) {
  if (c)
    cas_group_flush(c->group, emit, ctx);
}

void cas_get_metrics(cas_t *c, cas_metrics_t *out) {
  memset(out, 0, sizeof *out);
  if (!c)
    return;
  cas_group_shared_metrics(c->group, &out->scrambled_packets_total, &out->unexpected_clear_packets_total);
}

void cas_vendor_metrics(cas_t *c, size_t idx, cas_metrics_t *out) { cas_group_vendor_metrics(c->group, idx, out); }
unsigned cas_vendor_super_cas_id(cas_t *c, size_t idx) { return cas_group_vendor_super_cas_id(c->group, idx); }

size_t cas_prog_desc(cas_t *c, unsigned char *out, size_t cap) { return cas_group_prog_desc(c->group, out, cap); }

size_t cas_build_cat(cas_t *c, unsigned char *out, size_t cap) { return cas_group_build_cat(c->group, out, cap); }

size_t cas_vendor_count(cas_t *c) { return cas_group_vendor_count(c->group); }
unsigned cas_vendor_ecm_pid(cas_t *c, size_t idx) { return cas_group_vendor_ecm_pid(c->group, idx); }
unsigned cas_vendor_emm_pid(cas_t *c, size_t idx) { return cas_group_vendor_emm_pid(c->group, idx); }

int cas_vendor_ecm_due(cas_t *c, size_t idx, double now, unsigned char *out, size_t cap, size_t *out_len) { return cas_group_vendor_ecm_due(c->group, idx, now, out, cap, out_len); }

int cas_vendor_next_emm(cas_t *c, size_t idx, unsigned char *out, size_t cap, size_t *out_len) { return cas_group_vendor_next_emm(c->group, idx, out, cap, out_len); }
