/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include "biss/biss.h"
#include "biss/ca.h"
#include "lib/helper/log.h"
#include "lib/mux/cadescbuild.h"
#include "lib/mux/psi_build.h"

#include "cas_core.h"

void cas_core_hex_format(const unsigned char *in, size_t len, char *out) {
  static const char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    out[2 * i] = digits[in[i] >> 4];
    out[2 * i + 1] = digits[in[i] & 0xF];
  }
  out[2 * len] = '\0';
}

/* "0x" + 8 lowercase hex digits + NUL, out must hold 11 bytes */
void cas_core_format_super_cas_id(unsigned id, char *out) {
  static const char digits[] = "0123456789abcdef";
  out[0] = '0';
  out[1] = 'x';
  for (int i = 0; i < 8; i++)
    out[2 + i] = digits[(id >> (28 - 4 * i)) & 0xF];
  out[10] = '\0';
}

void cas_core_stop(cas_core_t *core) {
  cas_scramble_engine_stop(core->biss_engine);
  biss_ca_engine_stop(core->biss_ca);
  cas_group_stop(core->group);
}

int cas_core_failed(const cas_core_t *core) { return (core->biss_engine || core->biss_ca) ? 0 : cas_group_failed(core->group); }

void cas_core_scramble_packet(cas_core_t *core, unsigned out_pid, double now, unsigned char pkt188[188], scrambler_emit_cb emit, void *ctx) {
  if (core->biss_engine) {
    cas_scramble_engine_scramble_packet(core->biss_engine, out_pid, 1, 1, SCRAMBLE_PARITY_EVEN, 1, now, pkt188, emit, ctx);
    return;
  }
  if (core->biss_ca) {
    biss_ca_engine_scramble_packet(core->biss_ca, out_pid, now, pkt188, emit, ctx);
    return;
  }
  cas_group_scramble_packet(core->group, out_pid, now, pkt188, emit, ctx);
}

void cas_core_flush(cas_core_t *core, scrambler_emit_cb emit, void *ctx) {
  if (core->biss_engine) {
    cas_scramble_engine_flush(core->biss_engine, emit, ctx);
    return;
  }
  if (core->biss_ca) {
    biss_ca_engine_flush(core->biss_ca, emit, ctx);
    return;
  }
  cas_group_flush(core->group, emit, ctx);
}

void cas_core_get_metrics(cas_core_t *core, cas_metrics_t *out) {
  memset(out, 0, sizeof *out);
  if (core->biss_engine) {
    cas_scramble_engine_get_metrics(core->biss_engine, &out->scrambled_packets_total, &out->unexpected_clear_packets_total);
    return;
  }
  if (core->biss_ca) {
    biss_ca_engine_get_metrics(core->biss_ca, &out->scrambled_packets_total, &out->unexpected_clear_packets_total);
    return;
  }
  cas_group_shared_metrics(core->group, &out->scrambled_packets_total, &out->unexpected_clear_packets_total);
}

/* BISS (1/E and CA): no per-vendor breakdown, one shared engine */
void cas_core_vendor_metrics(cas_core_t *core, size_t idx, cas_metrics_t *out) {
  memset(out, 0, sizeof *out);
  if (!core->biss_engine && !core->biss_ca)
    cas_group_vendor_metrics(core->group, idx, out);
}

unsigned cas_core_vendor_super_cas_id(cas_core_t *core, size_t idx) {
  return (core->biss_engine || core->biss_ca) ? 0 : cas_group_vendor_super_cas_id(core->group, idx);
}

size_t cas_core_prog_desc(cas_core_t *core, unsigned char *out, size_t cap) {
  if (core->biss_engine)
    return cadescbuild_ca_descriptor(BISS_CA_SYSTEM_ID, BISS_CA_PID, out, cap);
  if (core->biss_ca)
    return biss_ca_engine_prog_desc(core->biss_ca, out, cap);
  return cas_group_prog_desc(core->group, out, cap);
}

size_t cas_core_build_cat(cas_core_t *core, unsigned char *out, size_t cap) {
  if (core->biss_engine)
    return psi_build_cat(0, NULL, 0, out, cap); /* empty CAT, no EMM in BISS 1/E */
  if (core->biss_ca)
    return biss_ca_engine_build_cat(core->biss_ca, out, cap);
  return cas_group_build_cat(core->group, out, cap);
}

size_t cas_core_vendor_count(cas_core_t *core) { return core->biss_ca ? 1 : (core->biss_engine ? 0 : cas_group_vendor_count(core->group)); }

unsigned cas_core_vendor_ecm_pid(cas_core_t *core, size_t idx) {
  return core->biss_ca ? biss_ca_engine_ecm_pid(core->biss_ca) : (core->biss_engine ? 0 : cas_group_vendor_ecm_pid(core->group, idx));
}

unsigned cas_core_vendor_emm_pid(cas_core_t *core, size_t idx) {
  return core->biss_ca ? biss_ca_engine_emm_pid(core->biss_ca) : (core->biss_engine ? 0 : cas_group_vendor_emm_pid(core->group, idx));
}

int cas_core_vendor_ecm_due(cas_core_t *core, size_t idx, double now, unsigned char *out, size_t cap, size_t *out_len) {
  if (core->biss_ca)
    return biss_ca_engine_ecm_due(core->biss_ca, now, out, cap, out_len);
  return core->biss_engine ? -1 : cas_group_vendor_ecm_due(core->group, idx, now, out, cap, out_len);
}

int cas_core_vendor_next_emm(cas_core_t *core, size_t idx, unsigned char *out, size_t cap, size_t *out_len) {
  if (core->biss_ca)
    return biss_ca_engine_emm_due(core->biss_ca, cas_core_mono(), out, cap, out_len);
  return core->biss_engine ? -1 : cas_group_vendor_next_emm(core->group, idx, out, cap, out_len);
}

void cas_core_reload_receivers(cas_core_t *core) {
  if (!core->biss_ca)
    return;
  if (biss_ca_engine_reload_receivers(core->biss_ca) > 0)
    biss_ca_engine_force_sk_rotation(core->biss_ca);
}

int cas_core_start_biss(scramble_algo_t algo, const unsigned char *cw, size_t cw_len, const unsigned *pids, size_t pid_count, unsigned flush_pid, const char *label, const char *log_prefix, cas_core_t *out) {
  memset(out, 0, sizeof *out);
  out->biss_engine = cas_scramble_engine_start(algo, pids, pid_count, flush_pid);
  if (!out->biss_engine)
    return -1;
  cas_scramble_engine_set_cw(out->biss_engine, SCRAMBLE_PARITY_EVEN, cw, cw_len, NULL, NULL);
  log_line("%sbiss: %s active, %zu pid(s) scrambled, no ECMG/EMMG", log_prefix, label, pid_count);
  return 0;
}

int cas_core_start_biss_dispatch(const cas_biss_cfg_t *cfg, const unsigned *pids, size_t pid_count, unsigned flush_pid, const char *log_prefix, cas_core_t *out) {
  if (cfg->biss1_enabled)
    return cas_core_start_biss(SCRAMBLE_ALGO_CSA2, cfg->biss1_cw, BISS1_KEY_LEN, pids, pid_count, flush_pid, "BISS1 Mode 1", log_prefix, out);
  if (cfg->biss2_emit_esw) {
    unsigned char esw[BISS_KEY_LEN];
    char hex[BISS_KEY_LEN * 2 + 1];
    if (biss_esw_encrypt(cfg->biss2_esw_id, cfg->biss2_sw, esw) == 0) {
      cas_core_hex_format(esw, BISS_KEY_LEN, hex);
      log_line("%sbiss: ESW for this SW under the given --biss2-emit-esw ID: %s", log_prefix, hex);
    } else {
      log_line("%sbiss: failed to compute ESW (no OpenSSL in this build?)", log_prefix);
    }
  }
  return cas_core_start_biss(SCRAMBLE_ALGO_CISSA, cfg->biss2_sw, BISS_KEY_LEN, pids, pid_count, flush_pid, "BISS2 Mode 1/E", log_prefix, out);
}

static unsigned pick_free_pid(unsigned start, const unsigned *avoid, size_t avoid_count) {
  for (unsigned pid = start; pid < 0x1FFF; pid++) {
    int collide = 0;
    for (size_t i = 0; i < avoid_count; i++)
      if (avoid[i] == pid) {
        collide = 1;
        break;
      }
    if (!collide)
      return pid;
  }
  return 0x1FFE; /* pathological: avoid[] fills top of pid space */
}

int cas_core_start_biss_ca_dispatch(const cas_biss_ca_cfg_t *cfg, const unsigned *pids, size_t pid_count, unsigned flush_pid, const char *log_prefix, cas_core_t *out) {
  biss_ca_engine_cfg_t ecfg;
  unsigned esid;
  memset(out, 0, sizeof *out);
  if (cfg->session_id_given) {
    esid = cfg->session_id;
  } else {
    unsigned char rnd[2];
    if (biss_ca_random(rnd, sizeof rnd) != 0) {
      log_line("%sbiss-ca: failed to generate a random entitlement_session_id (no OpenSSL in this build?)", log_prefix);
      return -1;
    }
    esid = ((unsigned)rnd[0] << 8) | rnd[1];
  }
  memset(&ecfg, 0, sizeof ecfg);
  ecfg.receivers_dir = cfg->receivers_dir;
  ecfg.esid = esid;
  ecfg.onid = cfg->onid;
  ecfg.sw_period_ms = cfg->cp_duration_ms;
  ecfg.ecm_pid = pick_free_pid(BISS_CA_ECM_PID_BASE, pids, pid_count);
  ecfg.emm_pid = pick_free_pid(ecfg.ecm_pid + 1, pids, pid_count);
  ecfg.pids = pids;
  ecfg.pid_count = pid_count;
  ecfg.flush_pid = flush_pid;

  out->biss_ca = biss_ca_engine_start(&ecfg);
  return out->biss_ca ? 0 : -1;
}
