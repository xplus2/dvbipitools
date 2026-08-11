/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_DEMUX_PSI_PRIV_H
#define DVBIPITOOLS_LIB_DEMUX_PSI_PRIV_H

#include "section_asm.h"
#include "psi.h"

#define TS_PID_PAT 0x0000
#define TS_PID_CAT 0x0001
#define TS_PID_NIT 0x0010
#define TS_PID_SDT 0x0011

typedef struct {
  unsigned program_number, pmt_pid;
  psi_section_asm_t asm_;
} pmt_cand_t;

struct psi {
  psi_section_asm_t pat, sdt, nit, cat;
  int have_pat, have_pmt, have_sdt, have_nit, have_cat;
  unsigned program_number, pmt_pid, pcr_pid, nit_pid;
  unsigned emm_pid, ca_system_id; /* from CAT's first CA_descriptor, 0 if none */
  unsigned char scrambling_mode; /* from PMT program_info's scrambling_descriptor, 0 if none */
  unsigned pmt_ca_system_id; /* PMT program_info's own first CA_descriptor, 0 if none */
  unsigned tsid, onid;
  psi_es_t es[PSI_MAX_ES];
  int es_count, audio_count;
  unsigned ecm[PSI_MAX_ES];
  int ecm_count;
  char service_name[PSI_NAME], provider_name[PSI_NAME], network_name[PSI_NAME];
  psi_program_t pat_programs[PSI_MAX_PROGRAMS];
  int pat_program_count;
  pmt_cand_t pmt_cand[PSI_MAX_PROGRAMS];
  int pmt_cand_count;
  unsigned preferred_pmt_pid; /* 0 = none, auto-select whichever candidate resolves first */
  int pmt_locked;             /* program_number/pmt_pid finalized, pmt_cand[pmt_lock_idx] is the live one */
  int pmt_lock_idx;

  int multi_mode; /* every candidate resolves independently, see psi_enable_multi_program() */
  psi_multi_program_t multi[PSI_MAX_PROGRAMS];
  int multi_count;

  pid_class_t class_by_pid[8192]; /* direct pid->class, rebuilt on parse_pat/parse_pmt */

  /* overflow logging, edge-triggered: logged once when a repeat first exceeds
     a cap, cleared once a repeat no longer does - avoids spamming every PAT/PMT cycle */
  int pat_program_overflow_logged;
  int pmt_cand_overflow_logged;
  int es_overflow_logged;
};

/* psi.c */
pmt_cand_t *find_cand(psi_t *c, unsigned pmt_pid);
void rebuild_class_table(psi_t *c);

/* descriptors.c */
const unsigned char *find_desc(const unsigned char *d, size_t len, unsigned tag, size_t *dlen);
void copy_name(char *dst, size_t dstsz, const unsigned char *src, size_t len);
void add_ecm(psi_t *c, unsigned pid);
void classify(psi_es_t *e, const unsigned char *desc, size_t dlen);
void decode_service_desc(const unsigned char *d, size_t dll, char *provider_dst, char *service_dst);

/* parse.c */
void parse_pat(psi_t *c);
int parse_pmt(psi_t *c, pmt_cand_t *cand);
void parse_sdt(psi_t *c);
void parse_nit(psi_t *c);
void parse_cat(psi_t *c);

#endif
