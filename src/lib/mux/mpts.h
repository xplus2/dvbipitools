/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_MUX_MPTS_H
#define DVBIPITOOLS_LIB_MUX_MPTS_H

#include "psi_build.h"
#include "tspacket_write.h"

#define MPTS_MAX_PROGRAMS 32
#define MPTS_MAX_CAS_VENDORS 8 /* matches CAS_GROUP_MAX_VENDORS */

/* per-program pull hooks against program_ctx from mpts_set_program(). decouples dipiradiohead's and dipitvhead's program types from mpts.c.
   get_sdt_info: fills *out, 0=include this tick, nonzero=omit. out's provider/service_name must stay valid till mpts_tick() returns.
   active programs combine into one composite SDT sub_table (EN 300 468 §5.2.3), not 1 per program. */
typedef struct {
  int (*get_sdt_info)(void *program_ctx, psi_sdt_entry_t *out);
  size_t (*build_eit)(void *program_ctx, unsigned char *out, size_t cap);
  int (*eit_pending)(const void *program_ctx);
} mpts_program_ops_t;

/* CAS pull hooks, against whatever cas_ctx mpts_set_cas() was given.
   dipiradiohead's and dipitvhead's cas_t are different, unrelated opaque types despite sharing function names.
   vendor_idx: which CAS vendor (0..n_vendors-1 passed to mpts_set_cas()) this call is for. */
typedef struct {
  size_t (*build_cat)(void *cas_ctx, unsigned char *out, size_t cap);
  int (*ecm_due)(void *cas_ctx, size_t vendor_idx, double now_s, unsigned char *out, size_t cap, size_t *out_len);
  int (*next_emm)(void *cas_ctx, size_t vendor_idx, unsigned char *out, size_t cap, size_t *out_len);
} mpts_cas_ops_t;

typedef struct {
  unsigned ecm_pid;
  unsigned emm_pid;
} mpts_cas_vendor_pid_t;

typedef struct mpts mpts_t;

/* network_name "" = no NIT. entries: fixed program list, program_number+pmt_pid, max MPTS_MAX_PROGRAMS
   (NULL returned if n_programs exceeds it). PAT always lists all entries regardless of active state.
   program_ops must outlive MPTS. */
mpts_t *mpts_new(unsigned tsid, unsigned onid, const char *network_name, const psi_pat_entry_t *entries, unsigned n_programs, const mpts_program_ops_t *program_ops);
void mpts_free(mpts_t *m);

/* NULL = inactive: skip for SDT/EIT, keep in PAT. program_ctx passed back through program_ops.
   forces immediate composite-SDT resend (version bump). active set changed. */
void mpts_set_program(mpts_t *m, unsigned idx, void *program_ctx);

/* cas_ops/vendors must outlive mpts. n_vendors capped at MPTS_MAX_CAS_VENDORS. */
void mpts_set_cas(mpts_t *m, void *cas_ctx, const mpts_cas_ops_t *cas_ops, const mpts_cas_vendor_pid_t *vendors, size_t n_vendors);

/* emits whatever's due: PAT/CAT/NIT/composite-SDT on fixed interval, each program's EIT on its own interval (or now, if eit_pending()), CAS ECM/EMM.
   call every ~100ms. now_s: caller's clock, not read internally (testability). ret: packets emitted. */
size_t mpts_tick(mpts_t *m, double now_s, ts_packet_cb cb, void *ctx);

#endif
