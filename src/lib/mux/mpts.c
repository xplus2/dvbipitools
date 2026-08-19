/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../demux/crc32.h"
#include "../log.h"

#include "mpts.h"

#define MPTS_INTERVAL_PAT_CAT 0.1  /* 100ms, matches the old standalone-mode PAT/PMT/CAT rate */
#define MPTS_INTERVAL_SDT 2.0
#define MPTS_INTERVAL_NIT 10.0
#define MPTS_INTERVAL_EIT 1.0

struct mpts {
  unsigned tsid, onid;
  const char *network_name;
  psi_pat_entry_t entries[MPTS_MAX_PROGRAMS];
  void *program_ctx[MPTS_MAX_PROGRAMS];
  double last_eit[MPTS_MAX_PROGRAMS];
  unsigned n_programs;
  const mpts_program_ops_t *program_ops;

  unsigned char cc_pat, cc_cat, cc_nit, cc_sdt, cc_eit;
  unsigned char cc_ecm[MPTS_MAX_CAS_VENDORS], cc_emm[MPTS_MAX_CAS_VENDORS];
  unsigned ver_pat, ver_nit, ver_sdt;
  uint32_t sdt_sig;  /* crc of composed service list - version bumps only on real change, EN 300 468 5.2.3 */
  int sdt_primed;    /* 0 until the first composite SDT has actually gone out */
  double last_pat, last_cat, last_nit, last_sdt;

  void *cas_ctx;
  const mpts_cas_ops_t *cas_ops;
  mpts_cas_vendor_pid_t cas_vendors[MPTS_MAX_CAS_VENDORS];
  size_t n_cas_vendors;
};

static int due_s(double now, double *last, double interval) {
  if (*last < 0.0 || now - *last >= interval) {
    *last = now;
    return 1;
  }
  return 0;
}

mpts_t *mpts_new(unsigned tsid, unsigned onid, const char *network_name, const psi_pat_entry_t *entries, unsigned n_programs, const mpts_program_ops_t *program_ops) {
  mpts_t *m;

  if (n_programs > MPTS_MAX_PROGRAMS) {
    log_line("mpts: %u programs requested, exceeds MPTS_MAX_PROGRAMS (%u)", n_programs, MPTS_MAX_PROGRAMS);
    return NULL;
  }
  m = calloc(1, sizeof *m);
  if (!m)
    return NULL;
  m->tsid = tsid;
  m->onid = onid;
  m->network_name = network_name;
  m->n_programs = n_programs;
  m->program_ops = program_ops;
  for (unsigned i = 0; i < n_programs; i++) {
    m->entries[i] = entries[i];
    m->last_eit[i] = -1.0;
  }
  m->last_pat = m->last_cat = m->last_nit = m->last_sdt = -1.0;
  return m;
}

void mpts_free(mpts_t *m) { free(m); }

void mpts_set_program(mpts_t *m, unsigned idx, void *program_ctx) {
  if (idx >= m->n_programs)
    return;
  m->program_ctx[idx] = program_ctx;
  m->last_sdt = -1.0; /* active set changed: force immediate composite-SDT resend */
  if (!program_ctx)
    m->last_eit[idx] = -1.0; /* force immediate resend once this program reconnects */
}

void mpts_set_cas(mpts_t *m, void *cas_ctx, const mpts_cas_ops_t *cas_ops, const mpts_cas_vendor_pid_t *vendors, size_t n_vendors) {
  m->cas_ctx = cas_ctx;
  m->cas_ops = cas_ops;
  m->n_cas_vendors = n_vendors < MPTS_MAX_CAS_VENDORS ? n_vendors : MPTS_MAX_CAS_VENDORS;
  for (size_t i = 0; i < m->n_cas_vendors; i++)
    m->cas_vendors[i] = vendors[i];
  m->last_cat = -1.0;
}

size_t mpts_tick(mpts_t *m, double now_s, ts_packet_cb cb, void *ctx) {
  unsigned char sec[4096];
  unsigned char ptr0 = 0x00;
  psi_sdt_entry_t sdt_entries[MPTS_MAX_PROGRAMS];
  size_t n, count = 0;

  if (due_s(now_s, &m->last_pat, MPTS_INTERVAL_PAT_CAT)) {
    n = psi_build_pat_multi(m->tsid, m->ver_pat, m->entries, m->n_programs, sec, sizeof sec);
    if (n)
      count += ts_packet_emit(0x0000, &m->cc_pat, &ptr0, sec, n, 0, 0, cb, ctx);
  }
  if (m->cas_ops && due_s(now_s, &m->last_cat, MPTS_INTERVAL_PAT_CAT)) {
    n = m->cas_ops->build_cat(m->cas_ctx, sec, sizeof sec);
    if (n)
      count += ts_packet_emit(0x0001, &m->cc_cat, &ptr0, sec, n, 0, 0, cb, ctx);
  }
  if (m->network_name && m->network_name[0] && due_s(now_s, &m->last_nit, MPTS_INTERVAL_NIT)) {
    n = psi_build_nit(m->ver_nit, m->onid, m->tsid, m->network_name, sec, sizeof sec);
    if (n)
      count += ts_packet_emit(0x0010, &m->cc_nit, &ptr0, sec, n, 0, 0, cb, ctx);
  }

  if (due_s(now_s, &m->last_sdt, MPTS_INTERVAL_SDT)) {
    unsigned n_sdt = 0;
    for (unsigned i = 0; i < m->n_programs; i++) {
      void *pctx = m->program_ctx[i];
      if (pctx && m->program_ops->get_sdt_info(pctx, &sdt_entries[n_sdt]) == 0)
        n_sdt++;
    }
    if (n_sdt) {
      uint32_t sig;

      n = psi_build_sdt_multi(0, m->tsid, m->onid, sdt_entries, n_sdt, sec, sizeof sec);
      /* exclude trailing CRC32 itself. crc32_mpeg is self-verifying, hashing it in always gives 0 */
      sig = n > 4 ? crc32_mpeg(sec, n - 4) : 0;
      if (m->sdt_primed && sig != m->sdt_sig)
        m->ver_sdt = (m->ver_sdt + 1) & 0x1F;
      m->sdt_sig = sig;
      m->sdt_primed = 1;
      n = psi_build_sdt_multi(m->ver_sdt, m->tsid, m->onid, sdt_entries, n_sdt, sec, sizeof sec);
      if (n)
        count += ts_packet_emit(0x0011, &m->cc_sdt, &ptr0, sec, n, 0, 0, cb, ctx);
    }
  }

  for (unsigned i = 0; i < m->n_programs; i++) {
    void *pctx = m->program_ctx[i];
    int eit_due;

    if (!pctx)
      continue;
    eit_due = due_s(now_s, &m->last_eit[i], MPTS_INTERVAL_EIT);
    if (m->program_ops->eit_pending(pctx) || eit_due) {
      n = m->program_ops->build_eit(pctx, sec, sizeof sec);
      if (n)
        count += ts_packet_emit(0x0012, &m->cc_eit, &ptr0, sec, n, 0, 0, cb, ctx);
      m->last_eit[i] = now_s;
    }
  }

  if (m->cas_ops) {
    size_t vi, len;
    for (vi = 0; vi < m->n_cas_vendors; vi++) {
      if (m->cas_ops->ecm_due(m->cas_ctx, vi, now_s, sec, sizeof sec, &len) == 0)
        count += ts_packet_emit(m->cas_vendors[vi].ecm_pid, &m->cc_ecm[vi], &ptr0, sec, len, 0, 0, cb, ctx);
      while (m->cas_ops->next_emm(m->cas_ctx, vi, sec, sizeof sec, &len) == 0)
        count += ts_packet_emit(m->cas_vendors[vi].emm_pid, &m->cc_emm[vi], &ptr0, sec, len, 0, 0, cb, ctx);
    }
  }

  return count;
}
