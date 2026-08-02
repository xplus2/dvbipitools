/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/log.h"
#include "lib/mux/psi_build.h"
#include "lib/scrambler/scrambler.h"

#include "../mux/cadescbuild.h"
#include "../version.h"

#include "cas.h"
#include "ecmg_client.h"
#include "emmg_server.h"

#define CAS_PCR_DETECT_TIMEOUT_S 8.0 /* startup grace period to observe a real PCR */
#define CAS_ECM_DEFAULT_REP_MS 500 /* ECM resend cadence until ECM_rep_period is known */
#define PCR_CLOCK_HZ 27000000ULL

struct cas {
  config_t cfg;
  unsigned ca_system_id;
  unsigned char scrambling_mode;
  scrambler_t *scr;

  unsigned pcr_out_pid;
  double pcr_wait_deadline;
  int have_pcr;
  int fatal;
  uint64_t last_pcr27;
  double last_pcr_wall;
  unsigned lookahead_ms;
  atomic_ulong cp_clock_ms;

  ecmg_client_t *ecmg;
  emmg_server_t *emmg;

  cas_pid_state_t pid[ARGS_MAX_CAS_PIDS];
  size_t pid_count;

  unsigned long cw_cache_epoch;
  unsigned char cw_cache[2][ECMG_MAX_CW_LEN];
  size_t cw_cache_len[2];

  double last_ecm_send;
};

static double mono(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static cas_pid_state_t *find_pid_state(cas_t *c, unsigned pid) {
  size_t i;
  for (i = 0; i < c->pid_count; i++)
    if (c->pid[i].pid == pid)
      return &c->pid[i];
  return NULL;
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

void cas_pid_apply(cas_pid_state_t *ps, int have_target, int target_parity, int pusi, double now, double force_flip_s) {
  if (!have_target)
    return;
  if (target_parity != ps->current_parity && !ps->flip_pending) {
    ps->flip_pending = 1;
    ps->flip_deadline_wall = now + force_flip_s;
  }
  if (ps->flip_pending && (pusi || now >= ps->flip_deadline_wall)) {
    ps->current_parity = target_parity;
    ps->flip_pending = 0;
  }
}

static void cas_lazy_start(cas_t *c) {
  ecmg_client_cfg_t ecfg;
  emmg_server_cfg_t mcfg;

  memset(&ecfg, 0, sizeof ecfg);
  ecfg.host = c->cfg.cas_ecmg_host;
  ecfg.port = c->cfg.cas_ecmg_port;
  if (c->cfg.cas_ecmg_version) {
    ecfg.version_min = ecfg.version_max = (unsigned char)c->cfg.cas_ecmg_version;
  } else {
    ecfg.version_min = 2;
    ecfg.version_max = 3;
  }
  ecfg.super_cas_id = c->cfg.cas_super_cas_id;
  ecfg.ecm_id = c->cfg.cas_ecm_id;
  ecfg.cp_duration_ms = c->cfg.cas_cp_duration_ms;
  ecfg.algo = (c->cfg.cas_algo == CAS_ALGO_CISSA) ? SCRAMBLE_ALGO_CISSA : SCRAMBLE_ALGO_CSA2;
  switch (c->cfg.cas_resilience) {
  case CAS_RESILIENCE_CYCLING:
    ecfg.resilience = ECMG_RESILIENCE_CYCLING;
    break;
  case CAS_RESILIENCE_UNSCRAMBLED:
    ecfg.resilience = ECMG_RESILIENCE_UNSCRAMBLED;
    break;
  default:
    ecfg.resilience = ECMG_RESILIENCE_FROZEN;
    break;
  }

  c->ecmg = ecmg_client_start(&ecfg, &c->cp_clock_ms, c->cfg.cas_cp_duration_ms, c->lookahead_ms);
  if (!c->ecmg) {
    log_line(TOOL_NAME ": cas: ECMG client failed to start");
    c->fatal = 1;
    return;
  }

  mcfg.port = c->cfg.cas_emmg_port;
  c->emmg = emmg_server_start(&mcfg);
  if (!c->emmg) {
    log_line(TOOL_NAME ": cas: EMMG server failed to start");
    c->fatal = 1;
  }
}

/* wall-clock here is a plausibility fence only, never the primary clock */
static void pcr_sample(cas_t *c, uint64_t pcr27) {
  double now = mono();

  if (!c->have_pcr) {
    c->have_pcr = 1;
    log_line(TOOL_NAME ": cas: first PCR observed on pid 0x%x, starting ECMG/EMMG", c->pcr_out_pid);
    cas_lazy_start(c);
  } else {
    double pcr_delta_s;
    double wall_delta_s = now - c->last_pcr_wall;
    if (cas_pcr_plausible(c->last_pcr27, pcr27, wall_delta_s, &pcr_delta_s)) {
      atomic_fetch_add_explicit(&c->cp_clock_ms, (unsigned long)(pcr_delta_s * 1000.0 + 0.5), memory_order_relaxed);
    } else {
      log_line(TOOL_NAME ": cas: PCR discontinuity on pid 0x%x, resyncing", c->pcr_out_pid);
    }
  }
  c->last_pcr27 = pcr27;
  c->last_pcr_wall = now;
}

cas_t *cas_start(const config_t *cfg, const psi_t *psi, unsigned pcr_out_pid) {
  cas_t *c;
  size_t i;
  unsigned lm;

  if (psi_pcr_pid(psi) == 0x1FFF) {
    log_line(TOOL_NAME ": cas: source declares no PCR_PID, cannot drive crypto-period timing");
    return NULL;
  }
  c = calloc(1, sizeof *c);
  if (!c)
    return NULL;
  c->cfg = *cfg;
  c->pcr_out_pid = pcr_out_pid;
  c->ca_system_id = cfg->cas_super_cas_id >> 16;
  c->scrambling_mode = (cfg->cas_algo == CAS_ALGO_CISSA) ? CADESC_SCRAMBLING_MODE_CISSA : CADESC_SCRAMBLING_MODE_CSA2;
  c->scr = scrambler_new((cfg->cas_algo == CAS_ALGO_CISSA) ? SCRAMBLE_ALGO_CISSA : SCRAMBLE_ALGO_CSA2);
  if (!c->scr) {
    free(c);
    return NULL;
  }
  for (i = 0; i < cfg->cas_pid_count; i++)
    c->pid[i].pid = cfg->cas_pids[i];
  c->pid_count = cfg->cas_pid_count;
  c->pcr_wait_deadline = mono() + CAS_PCR_DETECT_TIMEOUT_S;
  c->last_ecm_send = -1.0;
  lm = cfg->cas_cp_duration_ms / 4;
  if (lm > 2000)
    lm = 2000;
  if (lm < 100)
    lm = 100;
  c->lookahead_ms = lm;
  return c;
}

void cas_stop(cas_t *c) {
  if (!c)
    return;
  if (c->ecmg)
    ecmg_client_stop(c->ecmg);
  if (c->emmg)
    emmg_server_stop(c->emmg);
  scrambler_free(c->scr);
  free(c);
}

int cas_failed(cas_t *c) { return c->fatal; }

unsigned cas_pcr_pid(cas_t *c) { return c->pcr_out_pid; }

void cas_pcr_tick(cas_t *c, unsigned out_pid, const unsigned char pkt188[188]) {
  uint64_t pcr27;

  if (!c->have_pcr && !c->fatal && mono() >= c->pcr_wait_deadline) {
    c->fatal = 1;
    log_line(TOOL_NAME ": cas: no PCR observed on pid 0x%x within %.0fs, giving up", c->pcr_out_pid, CAS_PCR_DETECT_TIMEOUT_S);
    return;
  }
  if (out_pid != c->pcr_out_pid || !cas_parse_pcr(pkt188, &pcr27))
    return;
  pcr_sample(c, pcr27);
}

static void refresh_cw_cache(cas_t *c) {
  unsigned long epoch = ecmg_client_cw_epoch(c->ecmg);
  if (epoch == c->cw_cache_epoch)
    return;
  c->cw_cache_epoch = epoch;
  if (ecmg_client_get_cw(c->ecmg, 0, c->cw_cache[0], sizeof c->cw_cache[0], &c->cw_cache_len[0]) < 0)
    c->cw_cache_len[0] = 0;
  if (ecmg_client_get_cw(c->ecmg, 1, c->cw_cache[1], sizeof c->cw_cache[1], &c->cw_cache_len[1]) < 0)
    c->cw_cache_len[1] = 0;
  if (c->cw_cache_len[0])
    scrambler_set_key(c->scr, SCRAMBLE_PARITY_EVEN, c->cw_cache[0], c->cw_cache_len[0]);
  if (c->cw_cache_len[1])
    scrambler_set_key(c->scr, SCRAMBLE_PARITY_ODD, c->cw_cache[1], c->cw_cache_len[1]);
}

void cas_scramble_packet(cas_t *c, unsigned out_pid, unsigned char pkt188[188]) {
  cas_pid_state_t *ps = find_pid_state(c, out_pid);
  int pusi;

  if (!ps || !c->ecmg)
    return;
  refresh_cw_cache(c);
  pusi = (pkt188[1] & 0x40) != 0;
  cas_pid_apply(ps, c->cw_cache_epoch != 0, ecmg_client_target_parity(c->ecmg), pusi, mono(), CAS_FORCE_FLIP_S);

  if (!c->cw_cache_len[ps->current_parity] || !ecmg_client_cw_valid(c->ecmg))
    return;
  scrambler_encrypt_packet(c->scr, pkt188, ps->current_parity);
}

size_t cas_prog_desc(cas_t *c, unsigned char *out, size_t cap) {
  size_t n1 = cadescbuild_ca_descriptor(c->ca_system_id, c->cfg.cas_ecm_pid, out, cap);
  size_t n2;
  if (!n1)
    return 0;
  n2 = cadescbuild_scrambling_descriptor(c->scrambling_mode, out + n1, cap - n1);
  if (!n2)
    return 0;
  return n1 + n2;
}

size_t cas_build_cat(cas_t *c, unsigned char *out, size_t cap) {
  unsigned char desc[16];
  size_t dn = cadescbuild_ca_descriptor(c->ca_system_id, c->cfg.cas_emm_pid, desc, sizeof desc);
  if (!dn)
    return 0;
  return psi_build_cat(0, desc, dn, out, cap);
}

int cas_ecm_due(cas_t *c, unsigned char *out, size_t cap, size_t *out_len) {
  double now = mono();
  unsigned rep_ms;
  if (!c->ecmg)
    return -1;
  rep_ms = ecmg_client_ecm_rep_period_ms(c->ecmg);
  if (!rep_ms)
    rep_ms = CAS_ECM_DEFAULT_REP_MS;
  if (c->last_ecm_send >= 0.0 && now - c->last_ecm_send < (double)rep_ms / 1000.0)
    return -1;
  if (ecmg_client_get_ecm(c->ecmg, out, cap, out_len) < 0)
    return -1;
  c->last_ecm_send = now;
  return 0;
}

int cas_next_emm(cas_t *c, unsigned char *out, size_t cap, size_t *out_len) {
  if (!c->emmg)
    return -1;
  return emmg_server_dequeue_emm(c->emmg, out, cap, out_len);
}
