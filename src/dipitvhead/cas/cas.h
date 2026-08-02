/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_CAS_CAS_H
#define DIPITVHEAD_CAS_CAS_H

#include <stddef.h>
#include <stdint.h>

#include "lib/demux/psi.h"

#include "../args.h"

typedef struct cas cas_t;

/* pcr_out_pid: remux.c's mapped output PCR pid. NULL on failure: psi has no PCR_PID (0x1FFF).
   A declared-but-silent PCR pid fails later via cas_failed(), not here. */
cas_t *cas_start(const config_t *cfg, const psi_t *psi, unsigned pcr_out_pid);
void cas_stop(cas_t *c);

/* 1 once the PCR-detection grace period passed with no PCR seen: fatal, caller must stop */
int cas_failed(cas_t *c);
unsigned cas_pcr_pid(cas_t *c);

/* call per emitted packet, in this order:
   1) cas_pcr_tick() - no-op unless out_pid == cas_pcr_pid(c)
   2) cas_scramble_packet() - no-op unless out_pid is in --cas-pids */
void cas_pcr_tick(cas_t *c, unsigned out_pid, const unsigned char pkt188[188]);
void cas_scramble_packet(cas_t *c, unsigned out_pid, unsigned char pkt188[188]);

/* CA_descriptor(ecm)+scrambling_descriptor for PMT program_info. 0 on overflow. */
size_t cas_prog_desc(cas_t *c, unsigned char *out, size_t cap);
/* CAT section with a CA_descriptor(emm). 0 on overflow. */
size_t cas_build_cat(cas_t *c, unsigned char *out, size_t cap);

/* 0 = filled out/out_len with the current ECM (due for resend), -1 = not due / none yet */
int cas_ecm_due(cas_t *c, unsigned char *out, size_t cap, size_t *out_len);
/* 0 = filled, -1 = queue empty */
int cas_next_emm(cas_t *c, unsigned char *out, size_t cap, size_t *out_len);

/* pure helpers below, exposed only so unit tests can exercise them with synthetic buffers - remux.c has no business calling these */

/* ISO/IEC 13818-1 2.4.3.5. 1 = filled pcr27, 0 = no PCR in this packet */
int cas_parse_pcr(const unsigned char pkt188[188], uint64_t *pcr27);

#define CAS_PCR_MODULUS (((uint64_t)1 << 33) * 300ULL)

/* wraparound-corrects (new-last) and sanity-bounds it against wall_delta_s (a plausibility
   fence, never the primary clock). 1 = plausible (fills pcr_delta_s_out), 0 = discontinuity */
int cas_pcr_plausible(uint64_t last_pcr27, uint64_t new_pcr27, double wall_delta_s, double *pcr_delta_s_out);

typedef struct {
  unsigned pid;
  int current_parity;
  int flip_pending;
  double flip_deadline_wall;
} cas_pid_state_t;

/* one step of the per-pid PUSI-deferred-flip state machine. have_target=0: no CW published
   yet, no-op. force_flip_s: CAS_FORCE_FLIP_S at the real call site, a param here for testing. */
#define CAS_FORCE_FLIP_S 2.0
void cas_pid_apply(cas_pid_state_t *ps, int have_target, int target_parity, int pusi, double now, double force_flip_s);

#endif
