/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_CAS_CAS_CORE_H
#define DVBIPITOOLS_LIB_CAS_CAS_CORE_H

#include <stddef.h>
#include <time.h>

#include "biss/ca_engine.h"
#include "cas_group.h"
#include "cas_scramble_engine.h"
#include "lib/scrambler/scrambler.h"

#define BISS_CA_SYSTEM_ID 0x2602    /* EBU Tech 3292, BISS2 Mode 1/E */
#define BISS_CA_PID 0x1FFF          /* sentinel: no real ECM stream */
#define BISS_CA_ECM_PID_BASE 0x1FFA /* auto-allocated, scanned upward against pids in use */

/* dispatches to whichever of these is non-NULL: exactly one at a time, or none if cas isn't active */
typedef struct {
  cas_group_t *group;                /* NULL unless Simulcrypt (ECMG/EMMG-driven) */
  cas_scramble_engine_t *biss_engine; /* NULL unless BISS1/BISS2 Mode 1/E */
  biss_ca_engine_t *biss_ca;          /* NULL unless BISS2 Mode CA */
} cas_core_t;

static inline double cas_core_mono(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

void cas_core_hex_format(const unsigned char *in, size_t len, char *out);
void cas_core_format_super_cas_id(unsigned id, char *out);

/* stops+frees whichever of core's 3 engines is set (safe if all NULL). caller frees core itself */
void cas_core_stop(cas_core_t *core);
int cas_core_failed(const cas_core_t *core);
void cas_core_scramble_packet(cas_core_t *core, unsigned out_pid, double now, unsigned char pkt188[188], scrambler_emit_cb emit, void *ctx);
void cas_core_flush(cas_core_t *core, scrambler_emit_cb emit, void *ctx);
void cas_core_get_metrics(cas_core_t *core, cas_metrics_t *out);
void cas_core_vendor_metrics(cas_core_t *core, size_t idx, cas_metrics_t *out);
unsigned cas_core_vendor_super_cas_id(cas_core_t *core, size_t idx);
size_t cas_core_prog_desc(cas_core_t *core, unsigned char *out, size_t cap);
size_t cas_core_build_cat(cas_core_t *core, unsigned char *out, size_t cap);
size_t cas_core_vendor_count(cas_core_t *core);
unsigned cas_core_vendor_ecm_pid(cas_core_t *core, size_t idx);
unsigned cas_core_vendor_emm_pid(cas_core_t *core, size_t idx);
int cas_core_vendor_ecm_due(cas_core_t *core, size_t idx, double now, unsigned char *out, size_t cap, size_t *out_len);
int cas_core_vendor_next_emm(cas_core_t *core, size_t idx, unsigned char *out, size_t cap, size_t *out_len);
void cas_core_reload_receivers(cas_core_t *core);

/* fixed EVEN parity: SW never rotates, both parities would be identical. cw_len picks algo's own convention (BISS1_KEY_LEN or BISS_KEY_LEN).
   pids/pid_count: pids to scramble
   flush_pid: emitted immediately, never CSA2-batch-delayed
   label: logged
   log_prefix: prepended to progress log lines (may be "").
   fills *out, 0 ok, -1 OOM/engine start failed */
int cas_core_start_biss(scramble_algo_t algo, const unsigned char *cw, size_t cw_len, const unsigned *pids, size_t pid_count, unsigned flush_pid, const char *label, const char *log_prefix, cas_core_t *out);

typedef struct {
  int biss1_enabled;
  const unsigned char *biss1_cw; /* BISS1_KEY_LEN */
  int biss2_emit_esw;
  const unsigned char *biss2_esw_id; /* BISS_KEY_LEN */
  const unsigned char *biss2_sw;     /* BISS_KEY_LEN */
} cas_biss_cfg_t;

/* dispatches to BISS1 or BISS2 Mode 1/E per cfg, logs ESW first if biss2_emit_esw set. fills *out, 0 ok, -1 OOM/engine start failed */
int cas_core_start_biss_dispatch(const cas_biss_cfg_t *cfg, const unsigned *pids, size_t pid_count, unsigned flush_pid, const char *log_prefix, cas_core_t *out);

typedef struct {
  int session_id_given;
  unsigned session_id;
  const char *receivers_dir;
  unsigned onid;
  unsigned cp_duration_ms;
} cas_biss_ca_cfg_t;

/* fills *out, 0 ok, -1 OOM/no OpenSSL (session_id generation)/engine start failed */
int cas_core_start_biss_ca_dispatch(const cas_biss_ca_cfg_t *cfg, const unsigned *pids, size_t pid_count, unsigned flush_pid, const char *log_prefix, cas_core_t *out);

#endif
