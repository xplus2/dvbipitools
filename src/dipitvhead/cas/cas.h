/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_CAS_CAS_H
#define DIPITVHEAD_CAS_CAS_H

#include <stddef.h>
#include <stdint.h>

#include "lib/cas/cas_group.h"
#include "lib/demux/psi.h"
#include "lib/scrambler/scrambler.h"

#include "../args.h"
#include "../mux/pmtbuild.h"

typedef struct cas cas_t;

/* es/es_count: remux.c's source-pid->output-pid map (remux_es()), resolves --cas-pids
   "video"/"audio" keywords. NULL: no PCR_PID in psi, or --cas-pids resolves to 0 pids. */
cas_t *cas_start(const config_t *cfg, const psi_t *psi, const out_es_t *es, int es_count, unsigned pcr_out_pid);
void cas_stop(cas_t *c);

/* MPTS: no single program's PCR is trustworthy as mux clock (any one can drop) - crypto-period
   timing runs off cas_wall_tick() instead. es_lists[p]/es_counts[p]: program p's remux_es().
   NULL: --cas-pids resolves to 0 pids everywhere. */
cas_t *cas_start_multi(const config_t *cfg, const out_es_t *const *es_lists, const int *es_counts, unsigned n_programs);

/* MPTS crypto-period clock: wall time, not any pid's PCR. now_s: caller's monotonic clock.
   call every tick regardless of live programs - keeps ticking through all-down. */
void cas_wall_tick(cas_t *c, double now_s);

/* 1: PCR-detection grace period passed, none seen - fatal, caller stops */
int cas_failed(cas_t *c);
unsigned cas_pcr_pid(cas_t *c);

/* call per emitted packet, in order:
   1) cas_pcr_tick() - no-op unless out_pid == cas_pcr_pid(c)
   2) cas_scramble_packet() - no-op unless out_pid in --cas-pids. emits exactly once,
      immediate or batched (CSA2 SIMD backend, same-parity fill) - see
      scrambler_encrypt_packet_queued. always immediate on cas_pcr_pid(c): batching
      never delays PCR. */
void cas_pcr_tick(cas_t *c, unsigned out_pid, const unsigned char pkt188[188]);
void cas_scramble_packet(cas_t *c, unsigned out_pid, double now, unsigned char pkt188[188], scrambler_emit_cb emit, void *ctx);

/* flushes any packets held for batching, in order. call at stream end. */
void cas_flush(cas_t *c, scrambler_emit_cb emit, void *ctx);

/* shared scramble engine counters, one value across all vendors */
void cas_get_metrics(cas_t *c, cas_metrics_t *out);
/* per-vendor counters (ecmg/emmg), see cas_group_vendor_metrics() */
void cas_vendor_metrics(cas_t *c, size_t idx, cas_metrics_t *out);
unsigned cas_vendor_super_cas_id(cas_t *c, size_t idx);

/* N CA_descriptor(ecm)s (one per vendor) + one scrambling_descriptor for PMT program_info. 0: overflow. */
size_t cas_prog_desc(cas_t *c, unsigned char *out, size_t cap);
/* one CAT section, N CA_descriptor(emm)s (one per vendor). 0: overflow. */
size_t cas_build_cat(cas_t *c, unsigned char *out, size_t cap);

size_t cas_vendor_count(cas_t *c);
unsigned cas_vendor_ecm_pid(cas_t *c, size_t idx);
unsigned cas_vendor_emm_pid(cas_t *c, size_t idx);
/* 0: filled out/out_len, ECM due. -1: not due / none yet / silent+disconnected */
int cas_vendor_ecm_due(cas_t *c, size_t idx, double now, unsigned char *out, size_t cap, size_t *out_len);
/* 0: filled. -1: queue empty */
int cas_vendor_next_emm(cas_t *c, size_t idx, unsigned char *out, size_t cap, size_t *out_len);

/* SIGHUP: rescan --biss2-ca-receivers, force an SK rotation if the entitled set changed
   (revocation). no-op outside BISS-CA mode - caller decides when SIGHUP fired */
void cas_reload_receivers(cas_t *c);

/* below: pure helpers, exposed for unit tests only - remux.c has no business calling these */

/* resolves --cas-pids (pids + video/audio keywords) against es to a deduped output-pid
   list, capped at cap. returns count written. */
size_t cas_resolve_pids(const config_t *cfg, const out_es_t *es, int es_count, unsigned *out, size_t cap);

/* same, across programs (video/audio keywords resolved per program) */
size_t cas_resolve_pids_multi(const config_t *cfg, const out_es_t *const *es_lists, const int *es_counts, unsigned n_programs, unsigned *out, size_t cap);

/* ISO/IEC 13818-1 2.4.3.5. 1: filled pcr27. 0: no PCR in packet */
int cas_parse_pcr(const unsigned char pkt188[188], uint64_t *pcr27);

#define CAS_PCR_MODULUS (((uint64_t)1 << 33) * 300ULL)

/* wraparound-corrects (new-last), sanity-bounds against wall_delta_s (plausibility fence,
   never primary clock). 1: plausible, fills pcr_delta_s_out. 0: discontinuity */
int cas_pcr_plausible(uint64_t last_pcr27, uint64_t new_pcr27, double wall_delta_s, double *pcr_delta_s_out);

#endif
