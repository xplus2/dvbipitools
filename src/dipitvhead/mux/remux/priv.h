/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_MUX_REMUX_PRIV_H
#define DIPITVHEAD_MUX_REMUX_PRIV_H

#include <stdint.h>

#include "lib/demux/psi/section_asm.h"
#include "lib/helper/log.h"

#include "../../cas/cas.h"
#include "../aitbuild.h"
#include "../releaseq.h"
#include "../scte35stamp.h"
#include "../remux.h"

#define EIT_QUEUE_CAP 16 /* distinct (table_id, section_number) sections held at once */

typedef struct {
  unsigned char table_id;
  unsigned char section_number;
  unsigned char data[4096];
  size_t len;
} eit_section_t;

struct remux {
  const config_t *cfg; /* borrowed */
  dipitvhead_input_t input;
  out_program_pids_t pids;
  int standalone;
  unsigned src_service_id; /* source's own service_id, for filtering captured EIT sections */

  out_es_t es[OUT_PROGRAM_ES_CAP];
  int es_count;
  unsigned pcr_pid_out;
  cas_t *cas;
  timemap_t *tm;
  remux_clock_fn hold_clock;
  remux_latch_fn hold_latch;
  void *hold_clock_ctx;
  uint64_t hold_lead90;
  uint64_t hold_lead_cfg90;
  int hold_ref;
  releaseq_t *hold[OUT_PROGRAM_ES_CAP];
  scte35stamp_t *scte[OUT_PROGRAM_ES_CAP];
  uint64_t hold_tag[OUT_PROGRAM_ES_CAP];
  unsigned char hold_has_tag[OUT_PROGRAM_ES_CAP];
  unsigned long long hold_forced;
  ts_metrics_t *tsm_cur;

  int send_sdt;
  int send_nit;
  int send_ait;
  unsigned service_type;
  char service_name[256];
  char provider_name[PSI_NAME];
  char network_name[256];
  unsigned char ait_pmt_entry[16];
  size_t ait_pmt_entry_len;
  unsigned char ait_section[320];
  size_t ait_section_len;

  psi_versions_t *pv;
  psi_t *watch;                 /* owned, tracks source PMT after discovery. NULL: no live tracking */
  psi_t *watch_prev;            /* owned, pre-move watch: es[].src may still point into it until next remap */
  unsigned watch_pmt_pid;
  unsigned long watch_parsed;
  unsigned char src_pmt[1024];  /* source of PMT es[] */
  size_t src_pmt_len;
  unsigned emm_pid_in;          /* from discovery CAT, no re-read */
  unsigned emm_sysid_in;
  int reconnect_wanted;

  unsigned char last_pmt[4096];
  size_t last_pmt_len;
  int have_last_pmt;
  log_throttle_t pmt_desc_truncated_throttle;
  log_throttle_t cas_desc_throttle;
  log_throttle_t retime_skip_throttle;
  log_throttle_t relatch_throttle;
  log_throttle_t hold_forced_throttle;
  log_throttle_t cc_error_throttle;

  unsigned char cc_pat;
  unsigned char cc_pmt;
  unsigned char cc_sdt;
  unsigned char cc_nit;
  unsigned char cc_ait;
  unsigned char cc_cat;
  unsigned char cc_eit;
  unsigned char cc_tdt;
  unsigned char cc_ecm[ARGS_MAX_CAS_VENDORS];
  unsigned char cc_emm[ARGS_MAX_CAS_VENDORS];
  cc_offsets_t *ccm;
  int last_es_idx; /* MRU 1-entry cache: consecutive packets usually share a pid */

  double last_pat;
  double last_sdt;
  double last_nit;
  double last_ait;
  double last_cat;
  double last_tdt;
  double last_tot;

  /* per-pid TS-integrity, source-side: resets on reconnect, unlike caller-owned cumulative ts_metrics_t */
  unsigned char cc_state[8192]; /* 0x10=seen, 0x20=dup used, low nibble=last cc */
  unsigned pcr_pid_in;          /* source's PCR pid at discovery, 0=none */
  int have_last_pcr;
  uint64_t last_pcr27;
  double last_pcr_wall;

  /* source EIT reassembled into drainable queue (remux_emit_eit()). eit_drain_off: offset into head section mid-emit */
  psi_section_asm_t eit_asm;
  eit_section_t eit_queue[EIT_QUEUE_CAP];
  unsigned char eit_order[EIT_QUEUE_CAP]; /* permutation of slots: [0,count) queued oldest first, rest free */
  int eit_queue_count;
  size_t eit_drain_off;
};

/* NULL if this program carries no entry of that kind */
const out_es_t *find_ca_passthrough(const remux_t *r, ca_pass_t is_ca);

/* reassemble source EIT, filter to own service_id, enqueue */
void capture_eit_section(remux_t *r, const unsigned char *pkt188, ts_metrics_t *tsm);

/* source PAT/PMT packets to watch psi, remaps es[] at src PMT change */
void watch_source_pmt(remux_t *r, double now, const unsigned char *pkt188, remux_packet_cb cb, void *ctx, ts_metrics_t *tsm);

void flush_hold_slot(remux_t *r, int idx, double now, remux_packet_cb cb, void *ctx);

void send_psi_tables(remux_t *r, double now, remux_packet_cb cb, void *ctx, ts_metrics_t *tsm);

#endif
