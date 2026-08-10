/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_CAS_BISS_CA_ENGINE_H
#define DVBIPITOOLS_LIB_CAS_BISS_CA_ENGINE_H

#include <stddef.h>

#include "../cas_scramble_engine.h"

typedef struct biss_ca_engine biss_ca_engine_t;

typedef struct {
  const char *receivers_dir; /* PEM pubkey directory, one file per receiver/group */
  unsigned esid;              /* entitlement_session_id */
  unsigned onid;               /* original_network_id, mirrors PSI/SI */
  unsigned long sw_period_ms;  /* SW rotation period ms, >= 1000 (spec T_ECM_change_min) */
  unsigned ecm_pid;            /* caller-allocated, collision-checked */
  unsigned emm_pid;
  const unsigned *pids;
  size_t pid_count;
  unsigned flush_pid;
} biss_ca_engine_cfg_t;

/* NULL: zero usable receiver keys in receivers_dir, or scramble engine start failed */
biss_ca_engine_t *biss_ca_engine_start(const biss_ca_engine_cfg_t *cfg);
void biss_ca_engine_stop(biss_ca_engine_t *e);

void biss_ca_engine_clock_tick(biss_ca_engine_t *e, unsigned long delta_ms);
void biss_ca_engine_scramble_packet(biss_ca_engine_t *e, unsigned out_pid, double now, unsigned char pkt188[188], scrambler_emit_cb emit, void *ctx);
void biss_ca_engine_flush(biss_ca_engine_t *e, scrambler_emit_cb emit, void *ctx);
void biss_ca_engine_get_metrics(biss_ca_engine_t *e, unsigned long long *scrambled_packets_total, unsigned long long *unexpected_clear_packets_total);

unsigned biss_ca_engine_ecm_pid(const biss_ca_engine_t *e);
unsigned biss_ca_engine_emm_pid(const biss_ca_engine_t *e);

/* PMT program_info CA_descriptor: real ECM_PID + bissca_entitlement_session_id_descriptor. 0 on overflow */
size_t biss_ca_engine_prog_desc(const biss_ca_engine_t *e, unsigned char *out, size_t cap);
/* CAT: CA_descriptor pointing at the EMM_PID + bissca_entitlement_session_id_descriptor. 0 on overflow */
size_t biss_ca_engine_build_cat(const biss_ca_engine_t *e, unsigned char *out, size_t cap);

/* -1 not due/no data, 0 ok, section in out */
int biss_ca_engine_ecm_due(biss_ca_engine_t *e, double now, unsigned char *out, size_t cap, size_t *out_len);
int biss_ca_engine_emm_due(biss_ca_engine_t *e, double now, unsigned char *out, size_t cap, size_t *out_len);

/* SIGHUP: rescan receivers_dir. 1 set changed (caller: force_sk_rotation for revocation),
   0 unchanged, -1 dir unreadable, old list kept */
int biss_ca_engine_reload_receivers(biss_ca_engine_t *e);

/* skips ahead to the next scramble_packet() call rotating SK, forcing new EMMs out sooner */
void biss_ca_engine_force_sk_rotation(biss_ca_engine_t *e);

size_t biss_ca_engine_receiver_count(const biss_ca_engine_t *e);

#endif
