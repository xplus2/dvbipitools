/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/cas/cas_core.h"
#include "lib/cas/cas_group.h"
#include "lib/cas/ecmg_client/ecmg_client.h"
#include "lib/helper/log.h"

#include "../version.h"

#include "cas.h"

#define CAS_PCR_DETECT_TIMEOUT_S CAS_CORE_CLOCK_DETECT_TIMEOUT_S /* startup grace period to observe a real PCR */
#define PCR_CLOCK_HZ 27000000ULL

struct cas {
  unsigned pcr_out_pid;
  int have_pcr;
  uint64_t last_pcr27;
  double last_pcr_wall;
  cas_core_t core;
};

int cas_parse_pcr(const unsigned char pkt188[188], uint64_t *pcr27) {
  unsigned afc = (pkt188[3] >> 4) & 0x3;
  uint64_t base;
  unsigned ext;
  if (afc != 0x2 && afc != 0x3)
    return 0;
  if (pkt188[4] < 1 || !(pkt188[5] & 0x10) || pkt188[4] < 7)
    return 0;
  base = ((uint64_t)pkt188[6] << 25) | ((uint64_t)pkt188[7] << 17) | ((uint64_t)pkt188[8] << 9) | ((uint64_t)pkt188[9] << 1) | (pkt188[10] >> 7);
  ext = ((unsigned)(pkt188[10] & 0x01) << 8) | pkt188[11];
  *pcr27 = base * 300 + ext;
  return 1;
}

int cas_pcr_plausible(uint64_t last_pcr27, uint64_t new_pcr27, double wall_delta_s, double *pcr_delta_s_out) {
  uint64_t delta_ticks = (new_pcr27 + CAS_PCR_MODULUS - last_pcr27) % CAS_PCR_MODULUS;
  double pcr_delta_s = (double)delta_ticks / (double)PCR_CLOCK_HZ;
  if (wall_delta_s > 0.0 && pcr_delta_s > 0.0 && pcr_delta_s < 60.0 && pcr_delta_s < wall_delta_s * 5.0 + 0.5 && pcr_delta_s > wall_delta_s * 0.2 - 0.1) {
    *pcr_delta_s_out = pcr_delta_s;
    return 1;
  }
  return 0;
}

static void deliver_clock_tick(cas_t *c, unsigned long delta_ms) {
  if (c->core.biss_ca)
    biss_ca_engine_clock_tick(c->core.biss_ca, delta_ms);
  else
    cas_group_clock_tick(c->core.group, delta_ms);
}

/* wall-clock is a plausibility fence only, never primary clock */
static void pcr_sample(cas_t *c, uint64_t pcr27) {
  double now = cas_core_mono();

  if (!c->have_pcr) {
    c->have_pcr = 1;
    log_line(TOOL_NAME ": cas: first PCR observed on pid 0x%x, starting ECMG/EMMG", c->pcr_out_pid);
    deliver_clock_tick(c, 0);
  } else {
    double pcr_delta_s;
    double wall_delta_s = now - c->last_pcr_wall;
    if (cas_pcr_plausible(c->last_pcr27, pcr27, wall_delta_s, &pcr_delta_s)) {
      deliver_clock_tick(c, (unsigned long)(pcr_delta_s * 1000.0 + 0.5));
    } else {
      log_line(TOOL_NAME ": cas: PCR discontinuity on pid 0x%x, resyncing", c->pcr_out_pid);
    }
  }
  c->last_pcr27 = pcr27;
  c->last_pcr_wall = now;
}

static void add_pid(unsigned *pids, size_t *count, size_t cap, unsigned pid) {
  if (*count >= cap) {
    log_line(TOOL_NAME ": cas: pid 0x%x dropped, already at the %zu pid cap", pid, cap);
    return;
  }
  for (size_t i = 0; i < *count; i++)
    if (pids[i] == pid)
      return;
  pids[(*count)++] = pid;
}

static void add_program_cas_pids(const config_t *cfg, const out_es_t *es, int es_count, unsigned *out, size_t *count, size_t cap) {
  for (int i = 0; i < es_count; i++) {
    if (cfg->cas_pids_video && es[i].src->cls == PID_VIDEO)
      add_pid(out, count, cap, es[i].out_pid);
    if (cfg->cas_pids_audio && es[i].src->cls == PID_AUDIO)
      add_pid(out, count, cap, es[i].out_pid);
  }
}

size_t cas_resolve_pids_multi(const config_t *cfg, const out_es_t *const *es_lists, const int *es_counts, unsigned n_programs, unsigned *out, size_t cap) {
  size_t count = 0;

  for (size_t k = 0; k < cfg->cas_pid_count; k++)
    add_pid(out, &count, cap, cfg->cas_pids[k]);
  if (cfg->cas_pids_video || cfg->cas_pids_audio)
    for (unsigned p = 0; p < n_programs; p++)
      add_program_cas_pids(cfg, es_lists[p], es_counts[p], out, &count, cap);
  return count;
}

size_t cas_resolve_pids(const config_t *cfg, const out_es_t *es, int es_count, unsigned *out, size_t cap) {
  return cas_resolve_pids_multi(cfg, &es, &es_count, 1, out, cap);
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

static void fill_group_cfg(const config_t *cfg, cas_group_cfg_t *gcfg) {
  memset(gcfg, 0, sizeof *gcfg);
  gcfg->algo = (cfg->cas_algo == CAS_ALGO_CISSA) ? SCRAMBLE_ALGO_CISSA : SCRAMBLE_ALGO_CSA2;
  gcfg->legacy_csa1 = (cfg->cas_algo == CAS_ALGO_CSA1);
  gcfg->cp_duration_ms = cfg->cas_cp_duration_ms;
  gcfg->fallback_clear = cfg->cas_fallback_clear;
  if (cfg->n_cas_vendors > CAS_GROUP_MAX_VENDORS)
    log_line(TOOL_NAME ": cas: %u vendors configured, only the first %d will be started", cfg->n_cas_vendors, CAS_GROUP_MAX_VENDORS);
  gcfg->vendor_count = cfg->n_cas_vendors < CAS_GROUP_MAX_VENDORS ? cfg->n_cas_vendors : CAS_GROUP_MAX_VENDORS;
  for (size_t i = 0; i < gcfg->vendor_count; i++) {
    const cas_vendor_t *v = &cfg->cas_vendors[i];
    cas_group_vendor_cfg_t *gv = &gcfg->vendors[i];
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

cas_t *cas_start(const config_t *cfg, const psi_t *psi, const out_es_t *es, int es_count, unsigned pcr_out_pid) {
  cas_t *c;
  cas_group_cfg_t gcfg;

  if (cfg->biss2_enabled || cfg->biss1_enabled) {
    unsigned pids[CAS_CORE_MAX_PIDS];
    cas_biss_cfg_t bc = biss_cfg_of(cfg);
    size_t pid_count = cas_resolve_pids(cfg, es, es_count, pids, CAS_CORE_MAX_PIDS);
    if (pid_count == 0) {
      log_line(TOOL_NAME ": biss: --cas-pids resolved to no pids, nothing to scramble");
      return NULL;
    }
    c = calloc(1, sizeof *c);
    if (!c)
      return NULL;
    c->pcr_out_pid = pcr_out_pid;
    if (cas_core_start_biss_dispatch(&bc, pids, pid_count, pcr_out_pid, TOOL_NAME ": ", &c->core) != 0) {
      free(c);
      return NULL;
    }
    return c;
  }
  if (cfg->biss2_ca_enabled) {
    unsigned pids[CAS_CORE_MAX_PIDS];
    cas_biss_ca_cfg_t bc = biss_ca_cfg_of(cfg);
    size_t pid_count = cas_resolve_pids(cfg, es, es_count, pids, CAS_CORE_MAX_PIDS);
    if (pid_count == 0) {
      log_line(TOOL_NAME ": biss-ca: --cas-pids resolved to no pids, nothing to scramble");
      return NULL;
    }
    c = calloc(1, sizeof *c);
    if (!c)
      return NULL;
    c->pcr_out_pid = pcr_out_pid;
    if (cas_core_start_biss_ca_dispatch(&bc, pids, pid_count, pcr_out_pid, TOOL_NAME ": ", &c->core) != 0) {
      free(c);
      return NULL;
    }
    return c;
  }

  if (psi_pcr_pid(psi) == 0x1FFF) {
    log_line(TOOL_NAME ": cas: source declares no PCR_PID, cannot drive crypto-period timing");
    return NULL;
  }
  c = calloc(1, sizeof *c);
  if (!c)
    return NULL;
  c->pcr_out_pid = pcr_out_pid;

  fill_group_cfg(cfg, &gcfg);
  gcfg.pid_count = cas_resolve_pids(cfg, es, es_count, gcfg.pids, CAS_CORE_MAX_PIDS);
  if (gcfg.pid_count == 0) {
    log_line(TOOL_NAME ": cas: --cas-pids resolved to no pids, nothing to scramble");
    free(c);
    return NULL;
  }

  c->core.group = cas_group_start(&gcfg, pcr_out_pid);
  if (!c->core.group) {
    free(c);
    return NULL;
  }
  return c;
}

cas_t *cas_start_multi(const config_t *cfg, const out_es_t *const *es_lists, const int *es_counts, unsigned n_programs) {
  cas_t *c;
  cas_group_cfg_t gcfg;

  if (cfg->biss2_enabled || cfg->biss1_enabled) {
    unsigned pids[CAS_CORE_MAX_PIDS];
    cas_biss_cfg_t bc = biss_cfg_of(cfg);
    size_t pid_count = cas_resolve_pids_multi(cfg, es_lists, es_counts, n_programs, pids, CAS_CORE_MAX_PIDS);
    if (pid_count == 0) {
      log_line(TOOL_NAME ": biss: --cas-pids resolved to no pids across any program, nothing to scramble");
      return NULL;
    }
    c = calloc(1, sizeof *c);
    if (!c)
      return NULL;
    c->pcr_out_pid = 0x1FFF; /* MPTS, no single pid drives clock: see cas_wall_tick() */
    if (cas_core_start_biss_dispatch(&bc, pids, pid_count, 0x1FFF, TOOL_NAME ": ", &c->core) != 0) {
      free(c);
      return NULL;
    }
    return c;
  }
  if (cfg->biss2_ca_enabled) {
    unsigned pids[CAS_CORE_MAX_PIDS];
    cas_biss_ca_cfg_t bc = biss_ca_cfg_of(cfg);
    size_t pid_count = cas_resolve_pids_multi(cfg, es_lists, es_counts, n_programs, pids, CAS_CORE_MAX_PIDS);
    if (pid_count == 0) {
      log_line(TOOL_NAME ": biss-ca: --cas-pids resolved to no pids across any program, nothing to scramble");
      return NULL;
    }
    c = calloc(1, sizeof *c);
    if (!c)
      return NULL;
    c->pcr_out_pid = 0x1FFF;
    if (cas_core_start_biss_ca_dispatch(&bc, pids, pid_count, 0x1FFF, TOOL_NAME ": ", &c->core) != 0) {
      free(c);
      return NULL;
    }
    return c;
  }

  c = calloc(1, sizeof *c);
  if (!c)
    return NULL;
  c->pcr_out_pid = 0x1FFF; /* MPTS, no single pid drives clock: see cas_wall_tick() */

  fill_group_cfg(cfg, &gcfg);
  gcfg.pid_count = cas_resolve_pids_multi(cfg, es_lists, es_counts, n_programs, gcfg.pids, CAS_CORE_MAX_PIDS);
  if (gcfg.pid_count == 0) {
    log_line(TOOL_NAME ": cas: --cas-pids resolved to no pids across any program, nothing to scramble");
    free(c);
    return NULL;
  }

  c->core.group = cas_group_start(&gcfg, 0x1FFF);
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

unsigned cas_pcr_pid(cas_t *c) { return c->pcr_out_pid; }

void cas_pcr_tick(cas_t *c, unsigned out_pid, const unsigned char pkt188[188]) {
  uint64_t pcr27;

  if (c->core.biss_engine)
    return; /* fixed-key BISS: no crypto period */
  if (!c->core.biss_ca)
    cas_group_tick_alive(c->core.group);
  if (out_pid != c->pcr_out_pid || !cas_parse_pcr(pkt188, &pcr27))
    return;
  pcr_sample(c, pcr27);
}

void cas_wall_tick(cas_t *c, double now_s) {
  if (c->core.biss_engine)
    return;
  if (!c->core.biss_ca)
    cas_group_tick_alive(c->core.group);
  if (!c->have_pcr) {
    c->have_pcr = 1;
    deliver_clock_tick(c, 0);
  } else {
    double delta_s = now_s - c->last_pcr_wall;
    if (delta_s > 0.0)
      deliver_clock_tick(c, (unsigned long)(delta_s * 1000.0 + 0.5));
  }
  c->last_pcr_wall = now_s;
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
