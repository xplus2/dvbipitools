/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_CAS_CAS_SCRAMBLE_ENGINE_H
#define DVBIPITOOLS_LIB_CAS_CAS_SCRAMBLE_ENGINE_H

#include <stddef.h>

#include "lib/scrambler/scrambler.h"

#define CAS_SCRAMBLE_ENGINE_MAX_PIDS 32 /* matches CAS_CORE_MAX_PIDS */

typedef struct {
  unsigned pid;
  int current_parity;
  int flip_pending;
  double flip_deadline_wall;
} cas_pid_state_t;

/* one step of per-pid PUSI-deferred-flip state machine, pure/testable. have_target=0:
   no CW published yet, no-op. force_flip_s: CAS_FORCE_FLIP_S at real call site,
   param here for testing. */
#define CAS_FORCE_FLIP_S 2.0
void cas_pid_apply(cas_pid_state_t *ps, int have_target, int target_parity, int pusi, double now, double force_flip_s);

/* scrambler_t + per-pid parity-flip state, decoupled from CW source.
   cas_core: fed from one ecmg_client. cas_group: fed from shared group CW.
   one scramble per packet, any vendor count. */
typedef struct cas_scramble_engine cas_scramble_engine_t;

cas_scramble_engine_t *cas_scramble_engine_start(scramble_algo_t algo, const unsigned *pids, size_t pid_count, unsigned flush_pid);
void cas_scramble_engine_stop(cas_scramble_engine_t *e);

/* parity: SCRAMBLE_PARITY_EVEN/ODD. len 0: mark that slot unusable (no key material yet). */
void cas_scramble_engine_set_cw(cas_scramble_engine_t *e, int parity, const unsigned char *cw, size_t len, scrambler_emit_cb emit, void *ctx);

/* have_source 0: no CW source (ecmg not started, group not generating), passthrough,
   no unexpected-clear bump. have_target/target_parity: caller's policy. cw_valid:
   caller's outage-fallback decision. */
void cas_scramble_engine_scramble_packet(cas_scramble_engine_t *e, unsigned out_pid, int have_source, int have_target, int target_parity, int cw_valid, double now, unsigned char pkt188[188], scrambler_emit_cb emit, void *ctx);

void cas_scramble_engine_flush(cas_scramble_engine_t *e, scrambler_emit_cb emit, void *ctx);

void cas_scramble_engine_get_metrics(cas_scramble_engine_t *e, unsigned long long *scrambled_packets_total, unsigned long long *unexpected_clear_packets_total);

#endif
