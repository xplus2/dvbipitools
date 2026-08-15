/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/cas/biss/biss.h"
#include "lib/cas/biss/ca.h"
#include "lib/cas/biss/ca_engine.h"
#include "lib/cas/cas_group.h"
#include "lib/cas/cas_scramble_engine.h"
#include "lib/cas/ecmg_client/ecmg_client.h"
#include "lib/log.h"
#include "lib/mux/cadescbuild.h"
#include "lib/mux/psi_build.h"

#include "../version.h"

#include "cas.h"

#define CAS_PCR_DETECT_TIMEOUT_S CAS_CORE_CLOCK_DETECT_TIMEOUT_S /* startup grace period to observe a real PCR */
#define PCR_CLOCK_HZ 27000000ULL
#define BISS_CA_SYSTEM_ID 0x2602 /* EBU Tech 3292, BISS2 Mode 1/E */
#define BISS_CA_PID 0x1FFF       /* sentinel: no real ECM stream */
#define BISS_CA_ECM_PID_BASE 0x1FFA /* auto-allocated, scanned upward against --cas-pids */
#define BISS_CA_EMM_PID_BASE 0x1FFB

struct cas {
  unsigned pcr_out_pid;
  int have_pcr;
  uint64_t last_pcr27;
  double last_pcr_wall;
  cas_group_t *group;                 /* NULL unless Simulcrypt (ECMG/EMMG-driven) */
  cas_scramble_engine_t *biss_engine;  /* NULL unless BISS1/BISS2 Mode 1/E */
  biss_ca_engine_t *biss_ca;           /* NULL unless BISS2 Mode CA */
};

static double mono(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

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
  if (c->biss_ca)
    biss_ca_engine_clock_tick(c->biss_ca, delta_ms);
  else
    cas_group_clock_tick(c->group, delta_ms);
}

/* wall-clock here is a plausibility fence only, never the primary clock */
static void pcr_sample(cas_t *c, uint64_t pcr27) {
  double now = mono();

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
  size_t i;
  if (*count >= cap) {
    log_line(TOOL_NAME ": cas: pid 0x%x dropped, already at the %zu pid cap", pid, cap);
    return;
  }
  for (i = 0; i < *count; i++)
    if (pids[i] == pid)
      return;
  pids[(*count)++] = pid;
}

static void add_program_cas_pids(const config_t *cfg, const out_es_t *es, int es_count, unsigned *out, size_t *count, size_t cap) {
  int i;
  for (i = 0; i < es_count; i++) {
    if (cfg->cas_pids_video && es[i].src->cls == PID_VIDEO)
      add_pid(out, count, cap, es[i].out_pid);
    if (cfg->cas_pids_audio && es[i].src->cls == PID_AUDIO)
      add_pid(out, count, cap, es[i].out_pid);
  }
}

size_t cas_resolve_pids_multi(const config_t *cfg, const out_es_t *const *es_lists, const int *es_counts, unsigned n_programs, unsigned *out, size_t cap) {
  size_t count = 0, k;
  unsigned p;

  for (k = 0; k < cfg->cas_pid_count; k++)
    add_pid(out, &count, cap, cfg->cas_pids[k]);
  if (cfg->cas_pids_video || cfg->cas_pids_audio)
    for (p = 0; p < n_programs; p++)
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
  size_t i;
  memset(gcfg, 0, sizeof *gcfg);
  gcfg->algo = (cfg->cas_algo == CAS_ALGO_CISSA) ? SCRAMBLE_ALGO_CISSA : SCRAMBLE_ALGO_CSA2;
  gcfg->legacy_csa1 = (cfg->cas_algo == CAS_ALGO_CSA1);
  gcfg->cp_duration_ms = cfg->cas_cp_duration_ms;
  gcfg->fallback_clear = cfg->cas_fallback_clear;
  if (cfg->n_cas_vendors > CAS_GROUP_MAX_VENDORS)
    log_line(TOOL_NAME ": cas: %u vendors configured, only the first %d will be started", cfg->n_cas_vendors, CAS_GROUP_MAX_VENDORS);
  gcfg->vendor_count = cfg->n_cas_vendors < CAS_GROUP_MAX_VENDORS ? cfg->n_cas_vendors : CAS_GROUP_MAX_VENDORS;
  for (i = 0; i < gcfg->vendor_count; i++) {
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
    gv->required = v->required;
    gv->outage_mode = map_outage_mode(v->resilience);
  }
}

static void hex_format(const unsigned char *in, size_t len, char *out) {
  static const char digits[] = "0123456789abcdef";
  size_t i;
  for (i = 0; i < len; i++) {
    out[2 * i] = digits[in[i] >> 4];
    out[2 * i + 1] = digits[in[i] & 0xF];
  }
  out[2 * len] = '\0';
}

/* fixed EVEN parity: SW never rotates, both parities would be identical anyway */
static cas_t *cas_start_biss(scramble_algo_t algo, const unsigned char *cw, size_t cw_len, const unsigned *pids, size_t pid_count, unsigned pcr_out_pid, const char *label) {
  cas_t *c = calloc(1, sizeof *c);
  if (!c)
    return NULL;
  c->pcr_out_pid = pcr_out_pid;

  c->biss_engine = cas_scramble_engine_start(algo, pids, pid_count, pcr_out_pid);
  if (!c->biss_engine) {
    free(c);
    return NULL;
  }
  cas_scramble_engine_set_cw(c->biss_engine, SCRAMBLE_PARITY_EVEN, cw, cw_len, NULL, NULL);

  log_line(TOOL_NAME ": biss: %s active, %zu pid(s) scrambled, no ECMG/EMMG", label, pid_count);
  return c;
}

static void biss_log_emit_esw(const config_t *cfg) {
  unsigned char esw[BISS_KEY_LEN];
  char hex[BISS_KEY_LEN * 2 + 1];
  if (biss_esw_encrypt(cfg->biss2_esw_id, cfg->biss2_sw, esw) == 0) {
    hex_format(esw, BISS_KEY_LEN, hex);
    log_line(TOOL_NAME ": biss: ESW for this SW under the given --biss2-emit-esw ID: %s", hex);
  } else {
    log_line(TOOL_NAME ": biss: failed to compute ESW (no OpenSSL in this build?)");
  }
}

static cas_t *cas_start_biss_dispatch(const config_t *cfg, const unsigned *pids, size_t pid_count, unsigned pcr_out_pid) {
  if (cfg->biss1_enabled)
    return cas_start_biss(SCRAMBLE_ALGO_CSA2, cfg->biss1_cw, BISS1_KEY_LEN, pids, pid_count, pcr_out_pid, "BISS1 Mode 1");
  if (cfg->biss2_emit_esw)
    biss_log_emit_esw(cfg);
  return cas_start_biss(SCRAMBLE_ALGO_CISSA, cfg->biss2_sw, BISS_KEY_LEN, pids, pid_count, pcr_out_pid, "BISS2 Mode 1/E");
}

static unsigned pick_free_pid(unsigned start, const unsigned *avoid, size_t avoid_count) {
  unsigned pid;
  for (pid = start; pid < 0x1FFF; pid++) {
    size_t i;
    int collide = 0;
    for (i = 0; i < avoid_count; i++)
      if (avoid[i] == pid) {
        collide = 1;
        break;
      }
    if (!collide)
      return pid;
  }
  return 0x1FFE; /* pathological: --cas-pids occupies the whole top of the pid space */
}

static cas_t *cas_start_biss_ca_dispatch(const config_t *cfg, const unsigned *pids, size_t pid_count, unsigned pcr_out_pid) {
  cas_t *c;
  biss_ca_engine_cfg_t ecfg;
  unsigned esid;

  if (cfg->biss2_ca_session_id_given) {
    esid = cfg->biss2_ca_session_id;
  } else {
    unsigned char rnd[2];
    if (biss_ca_random(rnd, sizeof rnd) != 0) {
      log_line(TOOL_NAME ": biss-ca: failed to generate a random entitlement_session_id (no OpenSSL in this build?)");
      return NULL;
    }
    esid = ((unsigned)rnd[0] << 8) | rnd[1];
  }

  c = calloc(1, sizeof *c);
  if (!c)
    return NULL;
  c->pcr_out_pid = pcr_out_pid;

  memset(&ecfg, 0, sizeof ecfg);
  ecfg.receivers_dir = cfg->biss2_ca_receivers_dir;
  ecfg.esid = esid;
  ecfg.onid = cfg->onid;
  ecfg.sw_period_ms = cfg->cas_cp_duration_ms;
  ecfg.ecm_pid = pick_free_pid(BISS_CA_ECM_PID_BASE, pids, pid_count);
  ecfg.emm_pid = pick_free_pid(ecfg.ecm_pid + 1, pids, pid_count);
  ecfg.pids = pids;
  ecfg.pid_count = pid_count;
  ecfg.flush_pid = pcr_out_pid;

  c->biss_ca = biss_ca_engine_start(&ecfg);
  if (!c->biss_ca) {
    free(c);
    return NULL;
  }
  return c;
}

cas_t *cas_start(const config_t *cfg, const psi_t *psi, const out_es_t *es, int es_count, unsigned pcr_out_pid) {
  cas_t *c;
  cas_group_cfg_t gcfg;

  if (cfg->biss2_enabled || cfg->biss1_enabled) {
    unsigned pids[CAS_CORE_MAX_PIDS];
    size_t pid_count = cas_resolve_pids(cfg, es, es_count, pids, CAS_CORE_MAX_PIDS);
    if (pid_count == 0) {
      log_line(TOOL_NAME ": biss: --cas-pids resolved to no pids, nothing to scramble");
      return NULL;
    }
    return cas_start_biss_dispatch(cfg, pids, pid_count, pcr_out_pid);
  }
  if (cfg->biss2_ca_enabled) {
    unsigned pids[CAS_CORE_MAX_PIDS];
    size_t pid_count = cas_resolve_pids(cfg, es, es_count, pids, CAS_CORE_MAX_PIDS);
    if (pid_count == 0) {
      log_line(TOOL_NAME ": biss-ca: --cas-pids resolved to no pids, nothing to scramble");
      return NULL;
    }
    return cas_start_biss_ca_dispatch(cfg, pids, pid_count, pcr_out_pid);
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

  c->group = cas_group_start(&gcfg, pcr_out_pid);
  if (!c->group) {
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
    size_t pid_count = cas_resolve_pids_multi(cfg, es_lists, es_counts, n_programs, pids, CAS_CORE_MAX_PIDS);
    if (pid_count == 0) {
      log_line(TOOL_NAME ": biss: --cas-pids resolved to no pids across any program, nothing to scramble");
      return NULL;
    }
    return cas_start_biss_dispatch(cfg, pids, pid_count, 0x1FFF);
  }
  if (cfg->biss2_ca_enabled) {
    unsigned pids[CAS_CORE_MAX_PIDS];
    size_t pid_count = cas_resolve_pids_multi(cfg, es_lists, es_counts, n_programs, pids, CAS_CORE_MAX_PIDS);
    if (pid_count == 0) {
      log_line(TOOL_NAME ": biss-ca: --cas-pids resolved to no pids across any program, nothing to scramble");
      return NULL;
    }
    return cas_start_biss_ca_dispatch(cfg, pids, pid_count, 0x1FFF);
  }

  c = calloc(1, sizeof *c);
  if (!c)
    return NULL;
  c->pcr_out_pid = 0x1FFF; /* no single pid drives the clock here - see cas_wall_tick() */

  fill_group_cfg(cfg, &gcfg);
  gcfg.pid_count = cas_resolve_pids_multi(cfg, es_lists, es_counts, n_programs, gcfg.pids, CAS_CORE_MAX_PIDS);
  if (gcfg.pid_count == 0) {
    log_line(TOOL_NAME ": cas: --cas-pids resolved to no pids across any program, nothing to scramble");
    free(c);
    return NULL;
  }

  c->group = cas_group_start(&gcfg, 0x1FFF);
  if (!c->group) {
    free(c);
    return NULL;
  }
  return c;
}

void cas_stop(cas_t *c) {
  if (!c)
    return;
  cas_scramble_engine_stop(c->biss_engine);
  biss_ca_engine_stop(c->biss_ca);
  cas_group_stop(c->group);
  free(c);
}

int cas_failed(cas_t *c) { return (c->biss_engine || c->biss_ca) ? 0 : cas_group_failed(c->group); }

unsigned cas_pcr_pid(cas_t *c) { return c->pcr_out_pid; }

void cas_pcr_tick(cas_t *c, unsigned out_pid, const unsigned char pkt188[188]) {
  uint64_t pcr27;

  if (c->biss_engine)
    return; /* fixed-key BISS: no crypto period */
  if (!c->biss_ca)
    cas_group_tick_alive(c->group);
  if (out_pid != c->pcr_out_pid || !cas_parse_pcr(pkt188, &pcr27))
    return;
  pcr_sample(c, pcr27);
}

void cas_wall_tick(cas_t *c, double now_s) {
  if (c->biss_engine)
    return;
  if (!c->biss_ca)
    cas_group_tick_alive(c->group);
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
  if (c->biss_engine) {
    cas_scramble_engine_scramble_packet(c->biss_engine, out_pid, 1, 1, SCRAMBLE_PARITY_EVEN, 1, now, pkt188, emit, ctx);
    return;
  }
  if (c->biss_ca) {
    biss_ca_engine_scramble_packet(c->biss_ca, out_pid, now, pkt188, emit, ctx);
    return;
  }
  cas_group_scramble_packet(c->group, out_pid, now, pkt188, emit, ctx);
}

void cas_flush(cas_t *c, scrambler_emit_cb emit, void *ctx) {
  if (!c)
    return;
  if (c->biss_engine) {
    cas_scramble_engine_flush(c->biss_engine, emit, ctx);
    return;
  }
  if (c->biss_ca) {
    biss_ca_engine_flush(c->biss_ca, emit, ctx);
    return;
  }
  cas_group_flush(c->group, emit, ctx);
}

void cas_get_metrics(cas_t *c, cas_metrics_t *out) {
  memset(out, 0, sizeof *out);
  if (!c)
    return;
  if (c->biss_engine) {
    cas_scramble_engine_get_metrics(c->biss_engine, &out->scrambled_packets_total, &out->unexpected_clear_packets_total);
    return;
  }
  if (c->biss_ca) {
    biss_ca_engine_get_metrics(c->biss_ca, &out->scrambled_packets_total, &out->unexpected_clear_packets_total);
    return;
  }
  cas_group_shared_metrics(c->group, &out->scrambled_packets_total, &out->unexpected_clear_packets_total);
}

/* BISS (1/E and CA): no per-vendor breakdown, one shared engine */
void cas_vendor_metrics(cas_t *c, size_t idx, cas_metrics_t *out) {
  memset(out, 0, sizeof *out);
  if (!c->biss_engine && !c->biss_ca)
    cas_group_vendor_metrics(c->group, idx, out);
}
unsigned cas_vendor_super_cas_id(cas_t *c, size_t idx) { return (c->biss_engine || c->biss_ca) ? 0 : cas_group_vendor_super_cas_id(c->group, idx); }

size_t cas_prog_desc(cas_t *c, unsigned char *out, size_t cap) {
  if (c->biss_engine)
    return cadescbuild_ca_descriptor(BISS_CA_SYSTEM_ID, BISS_CA_PID, out, cap);
  if (c->biss_ca)
    return biss_ca_engine_prog_desc(c->biss_ca, out, cap);
  return cas_group_prog_desc(c->group, out, cap);
}

size_t cas_build_cat(cas_t *c, unsigned char *out, size_t cap) {
  if (c->biss_engine)
    return psi_build_cat(0, NULL, 0, out, cap); /* empty CAT, no EMM in BISS 1/E */
  if (c->biss_ca)
    return biss_ca_engine_build_cat(c->biss_ca, out, cap);
  return cas_group_build_cat(c->group, out, cap);
}

size_t cas_vendor_count(cas_t *c) { return c->biss_ca ? 1 : (c->biss_engine ? 0 : cas_group_vendor_count(c->group)); }
unsigned cas_vendor_ecm_pid(cas_t *c, size_t idx) { return c->biss_ca ? biss_ca_engine_ecm_pid(c->biss_ca) : (c->biss_engine ? 0 : cas_group_vendor_ecm_pid(c->group, idx)); }
unsigned cas_vendor_emm_pid(cas_t *c, size_t idx) { return c->biss_ca ? biss_ca_engine_emm_pid(c->biss_ca) : (c->biss_engine ? 0 : cas_group_vendor_emm_pid(c->group, idx)); }

int cas_vendor_ecm_due(cas_t *c, size_t idx, double now, unsigned char *out, size_t cap, size_t *out_len) {
  if (c->biss_ca)
    return biss_ca_engine_ecm_due(c->biss_ca, now, out, cap, out_len);
  return c->biss_engine ? -1 : cas_group_vendor_ecm_due(c->group, idx, now, out, cap, out_len);
}

int cas_vendor_next_emm(cas_t *c, size_t idx, unsigned char *out, size_t cap, size_t *out_len) {
  if (c->biss_ca)
    return biss_ca_engine_emm_due(c->biss_ca, mono(), out, cap, out_len);
  return c->biss_engine ? -1 : cas_group_vendor_next_emm(c->group, idx, out, cap, out_len);
}

void cas_reload_receivers(cas_t *c) {
  if (!c || !c->biss_ca)
    return;
  if (biss_ca_engine_reload_receivers(c->biss_ca) > 0)
    biss_ca_engine_force_sk_rotation(c->biss_ca);
}
