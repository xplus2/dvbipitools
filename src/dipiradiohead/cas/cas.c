/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/cas/cas_core.h"
#include "lib/cas/cas_group.h"
#include "lib/cas/ecmg_client/ecmg_client.h"
#include "lib/helper/log.h"

#include "cas.h"

struct cas {
  int have_clock;
  uint64_t last_pts90k;
  uint64_t rem_90k; /* carry for exact 90kHz -> ms conversion, no drift */
  cas_core_t core;
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

static cas_biss_cfg_t biss_cfg_of(const config_t *cfg) {
  cas_biss_cfg_t bc;
  bc.biss1_enabled = cfg->biss1_enabled;
  bc.biss1_cw = cfg->biss1_cw;
  bc.biss2_emit_esw = cfg->biss2_emit_esw;
  bc.biss2_esw_id = cfg->biss2_esw_id;
  bc.biss2_sw = cfg->biss2_sw;
  return bc;
}

static cas_biss_ca_cfg_t biss_ca_cfg_of(const config_t *cfg) {
  cas_biss_ca_cfg_t bc;
  bc.session_id_given = cfg->biss2_ca_session_id_given;
  bc.session_id = cfg->biss2_ca_session_id;
  bc.receivers_dir = cfg->biss2_ca_receivers_dir;
  bc.onid = cfg->onid;
  bc.cp_duration_ms = cfg->cas_cp_duration_ms;
  return bc;
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

  if (cfg->biss2_enabled || cfg->biss1_enabled) {
    cas_biss_cfg_t bc = biss_cfg_of(cfg);
    c = calloc(1, sizeof *c);
    if (!c)
      return NULL;
    if (cas_core_start_biss_dispatch(&bc, audio_pids, n_audio_pids, audio_pids[0], "", &c->core) != 0) {
      free(c);
      return NULL;
    }
    return c;
  }
  if (cfg->biss2_ca_enabled) {
    cas_biss_ca_cfg_t bc = biss_ca_cfg_of(cfg);
    c = calloc(1, sizeof *c);
    if (!c)
      return NULL;
    if (cas_core_start_biss_ca_dispatch(&bc, audio_pids, n_audio_pids, audio_pids[0], "", &c->core) != 0) {
      free(c);
      return NULL;
    }
    return c;
  }

  c = calloc(1, sizeof *c);
  if (!c)
    return NULL;

  memset(&gcfg, 0, sizeof gcfg);
  gcfg.algo = (cfg->cas_algo == CAS_ALGO_CISSA) ? SCRAMBLE_ALGO_CISSA : SCRAMBLE_ALGO_CSA2;
  gcfg.legacy_csa1 = (cfg->cas_algo == CAS_ALGO_CSA1);
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
    gv->emmg_max_conns = v->emmg_max_conns;
    gv->required = v->required;
    gv->outage_mode = map_outage_mode(v->resilience);
    gv->cwenc_algorithm = v->cwenc_algorithm;
    gv->cwenc_aes_mode = v->cwenc_aes_mode;
    gv->cwenc_fixed_key_hex = v->cwenc_fixed_key_hex;
    gv->cwenc_key_list_a_path = v->cwenc_key_list_a_path;
    gv->cwenc_key_list_b_path = v->cwenc_key_list_b_path;
  }
  for (i = 0; i < n_audio_pids; i++)
    gcfg.pids[i] = audio_pids[i];
  gcfg.pid_count = n_audio_pids;

  c->core.group = cas_group_start(&gcfg, audio_pids[0]);
  if (!c->core.group) {
    free(c);
    return NULL;
  }
  return c;
}

void cas_stop(cas_t *c) {
  if (!c)
    return;
  cas_core_stop(&c->core);
  free(c);
}

int cas_failed(const cas_t *c) { return cas_core_failed(&c->core); }

void cas_clock_tick(cas_t *c, uint64_t pts_90k) {
  if (c->core.biss_engine)
    return; /* fixed-key BISS: no crypto period */
  if (!c->core.biss_ca)
    cas_group_tick_alive(c->core.group);
  if (!c->have_clock) {
    c->have_clock = 1;
    if (c->core.biss_ca)
      biss_ca_engine_clock_tick(c->core.biss_ca, 0);
    else
      cas_group_clock_tick(c->core.group, 0);
  } else {
    unsigned long delta_ms;
    uint64_t rem = c->rem_90k;
    delta_ms = cas_90k_to_ms(pts_90k - c->last_pts90k, &rem);
    if (c->core.biss_ca)
      biss_ca_engine_clock_tick(c->core.biss_ca, delta_ms);
    else
      cas_group_clock_tick(c->core.group, delta_ms);
    c->rem_90k = rem;
  }
  c->last_pts90k = pts_90k;
}

void cas_scramble_packet(cas_t *c, unsigned out_pid, double now, unsigned char pkt188[188], scrambler_emit_cb emit, void *ctx) {
  cas_core_scramble_packet(&c->core, out_pid, now, pkt188, emit, ctx);
}

void cas_flush(cas_t *c, scrambler_emit_cb emit, void *ctx) {
  if (!c)
    return;
  cas_core_flush(&c->core, emit, ctx);
}

void cas_get_metrics(cas_t *c, cas_metrics_t *out) {
  if (!c) {
    memset(out, 0, sizeof *out);
    return;
  }
  cas_core_get_metrics(&c->core, out);
}

void cas_vendor_metrics(cas_t *c, size_t idx, cas_metrics_t *out) { cas_core_vendor_metrics(&c->core, idx, out); }
unsigned cas_vendor_super_cas_id(cas_t *c, size_t idx) { return cas_core_vendor_super_cas_id(&c->core, idx); }

size_t cas_prog_desc(cas_t *c, unsigned char *out, size_t cap) { return cas_core_prog_desc(&c->core, out, cap); }
size_t cas_build_cat(cas_t *c, unsigned char *out, size_t cap) { return cas_core_build_cat(&c->core, out, cap); }

size_t cas_vendor_count(cas_t *c) { return cas_core_vendor_count(&c->core); }
unsigned cas_vendor_ecm_pid(cas_t *c, size_t idx) { return cas_core_vendor_ecm_pid(&c->core, idx); }
unsigned cas_vendor_emm_pid(cas_t *c, size_t idx) { return cas_core_vendor_emm_pid(&c->core, idx); }

int cas_vendor_ecm_due(cas_t *c, size_t idx, double now, unsigned char *out, size_t cap, size_t *out_len) {
  return cas_core_vendor_ecm_due(&c->core, idx, now, out, cap, out_len);
}

int cas_vendor_next_emm(cas_t *c, size_t idx, unsigned char *out, size_t cap, size_t *out_len) {
  return cas_core_vendor_next_emm(&c->core, idx, out, cap, out_len);
}

void cas_reload_receivers(cas_t *c) {
  if (!c)
    return;
  cas_core_reload_receivers(&c->core);
}
