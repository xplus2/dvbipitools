/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "lib/cas/cas_core.h"
#include "lib/cas/cas_group.h"
#include "lib/helper/log.h"
#include "../mux/pcrclock.h"
#include "../mux/timemap.h"
#include "../version.h"
#include "cas.h"

#define CAS_PCR_DETECT_TIMEOUT_S CAS_CORE_CLOCK_DETECT_TIMEOUT_S /* startup grace period to observe a real PCR */
#define PCR_TICKS_PER_MS (PCR_CLOCK_HZ / 1000)

struct cas {
  unsigned pcr_out_pid;
  int have_pcr;
  uint64_t last_pcr27;
  double last_pcr_wall;
  uint64_t pcr_rem_ticks;
  cas_core_t core;
};

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
    double wall_delta_s = now - c->last_pcr_wall;
    if (wall_delta_s > 0.0 && timemap_pcr_plausible(c->last_pcr27, pcr27, wall_delta_s)) {
      uint64_t ticks = c->pcr_rem_ticks + pcr_sub(pcr27, c->last_pcr27);
      c->pcr_rem_ticks = ticks % PCR_TICKS_PER_MS;
      deliver_clock_tick(c, (unsigned long)(ticks / PCR_TICKS_PER_MS));
    } else {
      c->pcr_rem_ticks = 0;
      log_code("TW400199", TOOL_NAME ": cas: PCR discontinuity on pid 0x%x, resyncing", c->pcr_out_pid);
    }
  }
  c->last_pcr27 = pcr27;
  c->last_pcr_wall = now;
}

static void add_pid(unsigned *pids, size_t *count, size_t cap, unsigned pid) {
  if (*count >= cap) {
    log_code("TW700141", TOOL_NAME ": cas: pid 0x%x dropped, already at the %zu pid cap", pid, cap);
    return;
  }
  for (size_t i = 0; i < *count; i++) {
    if (pids[i] == pid) return;
  }
  pids[(*count)++] = pid;
}

static void add_program_cas_pids(const config_t *cfg, const out_es_t *es, int es_count, unsigned *out, size_t *count, size_t cap) {
  for (int i = 0; i < es_count; i++) {
    if (!es[i].src) continue;
    switch (es[i].src->cls) {
      case PID_VIDEO: if (cfg->cas_pids_video) add_pid(out, count, cap, es[i].out_pid); break;
      case PID_AUDIO: if (cfg->cas_pids_audio) add_pid(out, count, cap, es[i].out_pid); break;
      case PID_LCEVC: if (cfg->cas_pids_lcevc) add_pid(out, count, cap, es[i].out_pid); break;
      default: break;
    }
  }
}

size_t cas_resolve_pids_multi(const config_t *cfg, const out_es_t *const *es_lists, const int *es_counts, unsigned n_programs, unsigned *out, size_t cap) {
  size_t count = 0;
  for (size_t k = 0; k < cfg->cas_pid_count; k++) add_pid(out, &count, cap, cfg->cas_pids[k]);
  if (cfg->cas_pids_video || cfg->cas_pids_audio || cfg->cas_pids_lcevc) {
    for (unsigned p = 0; p < n_programs; p++)
      add_program_cas_pids(cfg, es_lists[p], es_counts[p], out, &count, cap);
  }
  return count;
}

size_t cas_resolve_pids(const config_t *cfg, const out_es_t *es, int es_count, unsigned *out, size_t cap) {
  return cas_resolve_pids_multi(cfg, &es, &es_count, 1, out, cap);
}

static void fill_group_cfg(const config_t *cfg, cas_group_cfg_t *gcfg) {
  cas_core_fill_group_cfg(cfg->cas_algo, cfg->cas_cp_duration_ms, cfg->cas_fallback_clear, cfg->cas_vendors, cfg->n_cas_vendors, TOOL_NAME ": ", gcfg);
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

static void log_no_pids(const config_t *cfg, int multi) {
  if (cfg->biss2_enabled || cfg->biss1_enabled) {
    if (multi)
      log_code("TA000581", TOOL_NAME ": biss: --cas-pids resolved to no pids across any program, nothing to scramble");
    else
      log_code("TA000577", TOOL_NAME ": biss: --cas-pids resolved to no pids, nothing to scramble");
  } else if (cfg->biss2_ca_enabled) {
    if (multi)
      log_code("TA000582", TOOL_NAME ": biss-ca: --cas-pids resolved to no pids across any program, nothing to scramble");
    else
      log_code("TA000578", TOOL_NAME ": biss-ca: --cas-pids resolved to no pids, nothing to scramble");
  } else if (multi) {
    log_code("TA000583", TOOL_NAME ": cas: --cas-pids resolved to no pids across any program, nothing to scramble");
  } else {
    log_code("TA000580", TOOL_NAME ": cas: --cas-pids resolved to no pids, nothing to scramble");
  }
}

/* pcr_out_pid 0x1FFF for MPTS: no single pid drives clock, see cas_wall_tick() */
static cas_t *start_resolved(const config_t *cfg, const unsigned *pids, size_t pid_count, unsigned pcr_out_pid, int multi) {
  cas_t *c;
  int rc;

  if (pid_count == 0) {
    log_no_pids(cfg, multi);
    return NULL;
  }
  c = calloc(1, sizeof *c);
  if (!c) return NULL;
  c->pcr_out_pid = pcr_out_pid;
  if (cfg->biss2_enabled || cfg->biss1_enabled) {
    cas_biss_cfg_t bc = biss_cfg_of(cfg);
    rc = cas_core_start_biss_dispatch(&bc, pids, pid_count, pcr_out_pid, TOOL_NAME ": ", &c->core);
  } else if (cfg->biss2_ca_enabled) {
    cas_biss_ca_cfg_t bc = biss_ca_cfg_of(cfg);
    rc = cas_core_start_biss_ca_dispatch(&bc, pids, pid_count, pcr_out_pid, TOOL_NAME ": ", &c->core);
  } else {
    cas_group_cfg_t gcfg;
    fill_group_cfg(cfg, &gcfg);
    memcpy(gcfg.pids, pids, pid_count * sizeof *pids);
    gcfg.pid_count = pid_count;
    c->core.group = cas_group_start(&gcfg, pcr_out_pid);
    rc = c->core.group ? 0 : -1;
  }
  if (rc != 0) {
    free(c);
    return NULL;
  }
  return c;
}

cas_t *cas_start(const config_t *cfg, const psi_t *psi, const out_es_t *es, int es_count, unsigned pcr_out_pid) {
  unsigned pids[CAS_CORE_MAX_PIDS];
  size_t pid_count;

  if (!cfg->biss2_enabled && !cfg->biss1_enabled && !cfg->biss2_ca_enabled && psi_pcr_pid(psi) == 0x1FFF) {
    log_code("TA000579", TOOL_NAME ": cas: source declares no PCR_PID, cannot drive crypto-period timing");
    return NULL;
  }
  pid_count = cas_resolve_pids(cfg, es, es_count, pids, CAS_CORE_MAX_PIDS);
  return start_resolved(cfg, pids, pid_count, pcr_out_pid, 0);
}

cas_t *cas_start_multi(const config_t *cfg, const out_es_t *const *es_lists, const int *es_counts, unsigned n_programs) {
  unsigned pids[CAS_CORE_MAX_PIDS];
  size_t pid_count = cas_resolve_pids_multi(cfg, es_lists, es_counts, n_programs, pids, CAS_CORE_MAX_PIDS);
  return start_resolved(cfg, pids, pid_count, 0x1FFF, 1);
}

void cas_stop(cas_t *c) {
  if (!c) return;
  cas_core_stop(&c->core);
  free(c);
}

int cas_failed(const cas_t *c) { return cas_core_failed(&c->core); }

unsigned cas_pcr_pid(cas_t *c) { return c->pcr_out_pid; }

void cas_pcr_tick(cas_t *c, unsigned out_pid, const unsigned char pkt188[188]) {
  uint64_t pcr27;
  if (c->core.biss_engine) return; /* fixed-key BISS: no crypto period */
  if (!c->core.biss_ca) cas_group_tick_alive(c->core.group);
  if (out_pid != c->pcr_out_pid || !pcr_packet_read(pkt188, &pcr27)) return;
  pcr_sample(c, pcr27);
}

void cas_wall_tick(cas_t *c, double now_s) {
  double delta_s;
  if (c->core.biss_engine) return;
  if (!c->core.biss_ca) cas_group_tick_alive(c->core.group);
  if (!c->have_pcr) {
    c->have_pcr = 1;
    c->last_pcr_wall = now_s;
    deliver_clock_tick(c, 0);
    return;
  }
  delta_s = now_s - c->last_pcr_wall;
  if (delta_s < 0.0) {
    c->last_pcr_wall = now_s;
  } else if (delta_s >= 0.001) {
    unsigned long ms = (unsigned long)(delta_s * 1000.0);
    deliver_clock_tick(c, ms);
    c->last_pcr_wall += (double)ms / 1000.0; /* keep sub-ms remainder */
  }
}

void cas_scramble_packet(cas_t *c, unsigned out_pid, double now, unsigned char pkt188[188], scrambler_emit_cb emit, void *ctx) {
  cas_core_scramble_packet(&c->core, out_pid, now, pkt188, emit, ctx);
}

size_t cas_add_pids(cas_t *c, const unsigned *pids, size_t n) {
  size_t failed = 0;
  for (size_t i = 0; i < n; i++)
    if (cas_core_add_pid(&c->core, pids[i]) != 0) failed++;
  return failed;
}

void cas_flush(cas_t *c, scrambler_emit_cb emit, void *ctx) {
  if (!c) return;
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
  if (!c) return;
  cas_core_reload_receivers(&c->core);
}
