/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPITVHEAD_REMUX_H
#define DIPITVHEAD_REMUX_H

#include <stddef.h>

#include "lib/demux/psi.h"
#include "lib/mux/psi_build.h"

#include "../args.h"

#include "pmtbuild.h"

typedef struct remux remux_t;
typedef void (*remux_packet_cb)(void *ctx, const unsigned char *pkt188);

/* tables remux.c itself can build - PAT/CAT/SDT/NIT only apply standalone (SPTS);
   MPTS's own PAT/NIT are mpts.c's responsibility, not counted here */
typedef enum { PSI_TABLE_PAT, PSI_TABLE_PMT, PSI_TABLE_CAT, PSI_TABLE_SDT, PSI_TABLE_NIT, PSI_TABLE_COUNT } psi_table_t;

const char *psi_table_name(psi_table_t table);

/* cumulative, caller-owned (survives remux_t reconnects - a fresh remux_t's
   per-pid CC/PCR tracking state does not, and should not, carry over) */
typedef struct {
  unsigned long long ts_packets;
  unsigned long long ts_sync_errors;
  unsigned long long ts_continuity_errors;
  unsigned long long ts_discontinuities;
  unsigned long long pcr_discontinuities;
  unsigned long long psi_sections_total[PSI_TABLE_COUNT];
  unsigned long long psi_errors_total[PSI_TABLE_COUNT];
  unsigned long long remux_packets_total;
  unsigned long long remux_dropped_packets_total;
  unsigned long long ait_sections_total;
  unsigned long long pmt_updates_total;
  unsigned long long eit_queue_drops_total; /* eit_queue_put: EIT_QUEUE_CAP reached, section discarded */
} ts_metrics_t;

/* pids: borrowed. standalone: SPTS, +PAT/CAT/SDT/NIT/AIT/ECM/EMM. non-standalone (MPTS): only
   PMT/AIT/ES, SDT via remux_get_sdt_info(). EIT: see remux_emit_eit(). NULL: failed (logged) */
remux_t *remux_new(const config_t *cfg, const dipitvhead_input_t *input, const psi_t *psi, const out_program_pids_t *pids, int standalone);
void remux_free(remux_t *r);

unsigned remux_pcr_pid_out(const remux_t *r);

/* source-pid->output-pid map. borrowed, valid for r's lifetime */
const out_es_t *remux_es(const remux_t *r, int *count);

struct cas;
/* NULL detaches */
void remux_set_cas(remux_t *r, struct cas *cas);

/* now_s: caller's clock, not read internally - testability, avoids a clock read per packet
   (see mpts_tick()). also drives PMT/AIT (standalone: +PAT/CAT/SDT/NIT) resend.
   tsm: accumulates this call's TS-integrity counters (nullable) */
void remux_feed(remux_t *r, double now_s, const unsigned char *pkt188, remux_packet_cb cb, void *ctx, ts_metrics_t *tsm);

/* non-standalone only. 0: filled *out. nonzero: nothing to send */
int remux_get_sdt_info(remux_t *r, psi_sdt_entry_t *out);

/* EIT: pid 0x0012, never mpts-owned. standalone: verbatim forward. non-standalone: drains a
   reassembled per-service_id section queue. max_packets: small (1-2)/tick, spreads a large
   section across ticks instead of one PCR-blocking burst. returns packets emitted. */
size_t remux_emit_eit(remux_t *r, unsigned pid, unsigned char *cc, size_t max_packets, remux_packet_cb cb, void *ctx);
int remux_eit_pending(const remux_t *r);

#endif
