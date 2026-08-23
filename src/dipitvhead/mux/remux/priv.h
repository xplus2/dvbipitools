/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_MUX_REMUX_PRIV_H
#define DIPITVHEAD_MUX_REMUX_PRIV_H

#include <stdint.h>

#include "lib/demux/psi/section_asm.h"

#include "../../cas/cas.h"
#include "../aitbuild.h"
#include "../remux.h"

#define EIT_QUEUE_CAP 16 /* distinct (table_id, section_number) sections held at once */

typedef struct {
  unsigned char table_id;
  unsigned char section_number;
  unsigned char data[4096];
  size_t len;
} eit_section_t;

struct remux {
  config_t cfg;
  dipitvhead_input_t input;
  out_program_pids_t pids;
  int standalone;
  unsigned src_service_id; /* source's own service_id, for filtering captured EIT sections */

  out_es_t es[OUT_PROGRAM_ES_CAP];
  int es_count;
  unsigned pcr_pid_out;
  cas_t *cas;

  int send_sdt;
  int send_nit;
  int send_ait;
  char service_name[256];
  char provider_name[PSI_NAME];
  char network_name[256];
  unsigned char ait_pmt_entry[16];
  size_t ait_pmt_entry_len;
  unsigned char ait_section[300];
  size_t ait_section_len;

  unsigned char last_pmt[4096];
  size_t last_pmt_len;
  int have_last_pmt;

  unsigned char cc_pat;
  unsigned char cc_pmt;
  unsigned char cc_sdt;
  unsigned char cc_nit;
  unsigned char cc_eit;
  unsigned char cc_ait;
  unsigned char cc_cat;
  unsigned char cc_ecm[ARGS_MAX_CAS_VENDORS];
  unsigned char cc_emm[ARGS_MAX_CAS_VENDORS];
  unsigned char cc_es[OUT_PROGRAM_ES_CAP];
  int last_es_idx; /* MRU 1-entry cache: consecutive packets usually share a pid */

  double last_pat;
  double last_sdt;
  double last_nit;
  double last_ait;
  double last_cat;

  /* per-pid TS-integrity, source-side: resets on reconnect, unlike caller-owned cumulative ts_metrics_t */
  unsigned char cc_state[8192]; /* bit 0x10=seen, low nibble=last continuity_counter */
  unsigned pcr_pid_in;          /* source's PCR pid at discovery, 0=none */
  int have_last_pcr;
  uint64_t last_pcr27;
  double last_pcr_wall;

  /* non-standalone: source EIT reassembled into a drainable queue (remux_emit_eit()).
     eit_drain_off: offset into eit_queue[0] mid-emit */
  psi_section_asm_t eit_asm;
  eit_section_t eit_queue[EIT_QUEUE_CAP];
  int eit_queue_count;
  size_t eit_drain_off;
};

/* is_ca 1 (ECM) or 2 (EMM) entry, NULL if this program carries none */
const out_es_t *find_ca_passthrough(const remux_t *r, int is_ca);

/* non-standalone: reassemble source EIT, filter to own service_id, enqueue */
void capture_eit_section(remux_t *r, const unsigned char *pkt188, ts_metrics_t *tsm);

void send_psi_tables(remux_t *r, double now, remux_packet_cb cb, void *ctx, ts_metrics_t *tsm);

#endif
