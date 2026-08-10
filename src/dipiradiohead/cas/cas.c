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
#include "lib/log.h"
#include "lib/mux/cadescbuild.h"
#include "lib/mux/psi_build.h"

#include "cas.h"

#define BISS_CA_SYSTEM_ID 0x2602 /* EBU Tech 3292, BISS2 Mode 1/E */
#define BISS_CA_PID 0x1FFF       /* sentinel: no real ECM stream */
#define BISS_CA_ECM_PID_BASE 0x1FFA /* auto-allocated, scanned upward against audio_pids */
#define BISS_CA_EMM_PID_BASE 0x1FFB

struct cas {
  int have_clock;
  uint64_t last_pts90k;
  uint64_t rem_90k; /* carry for exact 90kHz -> ms conversion, no drift */
  cas_group_t *group;                 /* NULL unless Simulcrypt (ECMG/EMMG-driven) */
  cas_scramble_engine_t *biss_engine;  /* NULL unless BISS1/BISS2 Mode 1/E */
  biss_ca_engine_t *biss_ca;           /* NULL unless BISS2 Mode CA */
};

static double mono(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
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

/* fixed EVEN parity: SW never rotates, both parities would be identical anyway */
static cas_t *cas_start_biss(scramble_algo_t algo, const unsigned char *cw, size_t cw_len, const unsigned *audio_pids, size_t n_audio_pids, const char *label) {
  cas_t *c = calloc(1, sizeof *c);
  if (!c)
    return NULL;

  c->biss_engine = cas_scramble_engine_start(algo, audio_pids, n_audio_pids, audio_pids[0]);
  if (!c->biss_engine) {
    free(c);
    return NULL;
  }
  cas_scramble_engine_set_cw(c->biss_engine, SCRAMBLE_PARITY_EVEN, cw, cw_len, NULL, NULL);

  log_line("biss: %s active, %zu audio pid(s) scrambled, no ECMG/EMMG", label, n_audio_pids);
  return c;
}

static void biss_log_emit_esw(const config_t *cfg) {
  unsigned char esw[BISS_KEY_LEN];
  char hex[BISS_KEY_LEN * 2 + 1];
  if (biss_esw_encrypt(cfg->biss2_esw_id, cfg->biss2_sw, esw) == 0) {
    hex_format(esw, BISS_KEY_LEN, hex);
    log_line("biss: ESW for this SW under the given --biss2-emit-esw ID: %s", hex);
  } else {
    log_line("biss: failed to compute ESW (no OpenSSL in this build?)");
  }
}

static cas_t *cas_start_biss_dispatch(const config_t *cfg, const unsigned *audio_pids, size_t n_audio_pids) {
  if (cfg->biss1_enabled)
    return cas_start_biss(SCRAMBLE_ALGO_CSA2, cfg->biss1_cw, BISS1_KEY_LEN, audio_pids, n_audio_pids, "BISS1 Mode 1");
  if (cfg->biss2_emit_esw)
    biss_log_emit_esw(cfg);
  return cas_start_biss(SCRAMBLE_ALGO_CISSA, cfg->biss2_sw, BISS_KEY_LEN, audio_pids, n_audio_pids, "BISS2 Mode 1/E");
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
  return 0x1FFE; /* pathological: audio_pids occupies the whole top of the pid space */
}

static cas_t *cas_start_biss_ca_dispatch(const config_t *cfg, const unsigned *audio_pids, size_t n_audio_pids) {
  cas_t *c;
  biss_ca_engine_cfg_t ecfg;
  unsigned esid;

  if (cfg->biss2_ca_session_id_given) {
    esid = cfg->biss2_ca_session_id;
  } else {
    unsigned char rnd[2];
    if (biss_ca_random(rnd, sizeof rnd) != 0) {
      log_line("biss-ca: failed to generate a random entitlement_session_id (no OpenSSL in this build?)");
      return NULL;
    }
    esid = ((unsigned)rnd[0] << 8) | rnd[1];
  }

  c = calloc(1, sizeof *c);
  if (!c)
    return NULL;

  memset(&ecfg, 0, sizeof ecfg);
  ecfg.receivers_dir = cfg->biss2_ca_receivers_dir;
  ecfg.esid = esid;
  ecfg.onid = cfg->onid;
  ecfg.sw_period_ms = cfg->cas_cp_duration_ms;
  ecfg.ecm_pid = pick_free_pid(BISS_CA_ECM_PID_BASE, audio_pids, n_audio_pids);
  ecfg.emm_pid = pick_free_pid(ecfg.ecm_pid + 1, audio_pids, n_audio_pids);
  ecfg.pids = audio_pids;
  ecfg.pid_count = n_audio_pids;
  ecfg.flush_pid = audio_pids[0];

  c->biss_ca = biss_ca_engine_start(&ecfg);
  if (!c->biss_ca) {
    free(c);
    return NULL;
  }
  return c;
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

  if (cfg->biss2_enabled || cfg->biss1_enabled)
    return cas_start_biss_dispatch(cfg, audio_pids, n_audio_pids);
  if (cfg->biss2_ca_enabled)
    return cas_start_biss_ca_dispatch(cfg, audio_pids, n_audio_pids);

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
  cas_scramble_engine_stop(c->biss_engine);
  biss_ca_engine_stop(c->biss_ca);
  cas_group_stop(c->group);
  free(c);
}

int cas_failed(cas_t *c) { return (c->biss_engine || c->biss_ca) ? 0 : cas_group_failed(c->group); }

void cas_clock_tick(cas_t *c, uint64_t pts_90k) {
  if (c->biss_engine)
    return; /* fixed-key BISS: no crypto period */
  if (!c->biss_ca)
    cas_group_tick_alive(c->group);
  if (!c->have_clock) {
    c->have_clock = 1;
    if (c->biss_ca)
      biss_ca_engine_clock_tick(c->biss_ca, 0);
    else
      cas_group_clock_tick(c->group, 0);
  } else {
    unsigned long delta_ms;
    uint64_t rem = c->rem_90k;
    delta_ms = cas_90k_to_ms(pts_90k - c->last_pts90k, &rem);
    if (c->biss_ca)
      biss_ca_engine_clock_tick(c->biss_ca, delta_ms);
    else
      cas_group_clock_tick(c->group, delta_ms);
    c->rem_90k = rem;
  }
  c->last_pts90k = pts_90k;
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
