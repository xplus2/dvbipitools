/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_CAS_CAS_GROUP_H
#define DVBIPITOOLS_LIB_CAS_CAS_GROUP_H

#include <stddef.h>

#include "lib/cas/ecmg_client/ecmg_client.h"
#include "lib/scrambler/scrambler.h"

#define CAS_GROUP_MAX_VENDORS 8
#define CAS_CORE_MAX_PIDS 32 /* matches dipitvhead's OUT_MAX_ES: cannot be exceeded by remux */

/* fails cas_group_failed() after this many seconds with no cas_group_clock_tick() call at all */
#define CAS_CORE_CLOCK_DETECT_TIMEOUT_S 8.0

typedef struct {
  int ecmg_connected;
  unsigned emmg_clients;
  unsigned long cryptoperiod_transitions_total;
  unsigned long ecm_total;
  unsigned long ecm_errors_total;
  unsigned long emm_total;
  unsigned long emm_dropped_total; /* oversized or evicted from a full send queue */
  unsigned long long scrambled_packets_total;
  unsigned long long unexpected_clear_packets_total;
} cas_metrics_t;

typedef struct {
  const char *ecmg_host;
  unsigned ecmg_port;
  unsigned ecmg_version;
  unsigned super_cas_id;
  unsigned ecm_id;
  unsigned ecm_pid;
  unsigned emm_pid;
  unsigned emmg_port;
  unsigned emmg_max_conns; /* 0 = library default (8) */
  ecmg_outage_mode_t outage_mode; /* per-vendor: frozen/cycling/silent on ECMG loss */
  int required;                  /* down triggers global fallback regardless of other vendors */
} cas_group_vendor_cfg_t;

typedef struct {
  scramble_algo_t algo;    /* global: one scrambler, one CW, one algorithm */
  int legacy_csa1;         /* signal mode 0x01 not 0x02, checksum CW[3]/CW[7]. same cipher */
  unsigned cp_duration_ms; /* global: one crypto-period clock */
  unsigned pids[CAS_CORE_MAX_PIDS];
  size_t pid_count;
  int fallback_clear; /* total outage (or a required vendor down): clear instead of frozen */
  cas_group_vendor_cfg_t vendors[CAS_GROUP_MAX_VENDORS];
  size_t vendor_count;
} cas_group_cfg_t;

typedef struct cas_group cas_group_t;

cas_group_t *cas_group_start(const cas_group_cfg_t *cfg, unsigned flush_pid);
void cas_group_stop(cas_group_t *g);

int cas_group_failed(cas_group_t *g);
void cas_group_tick_alive(cas_group_t *g);
void cas_group_clock_tick(cas_group_t *g, unsigned long delta_ms);

void cas_group_scramble_packet(cas_group_t *g, unsigned out_pid, double now, unsigned char pkt188[188], scrambler_emit_cb emit, void *ctx);
void cas_group_flush(cas_group_t *g, scrambler_emit_cb emit, void *ctx);

/* N CA_descriptor(ecm)s (one per vendor) + one scrambling_descriptor. 0 on overflow. */
size_t cas_group_prog_desc(cas_group_t *g, unsigned char *out, size_t cap);
/* one CAT section, N CA_descriptor(emm)s. 0 on overflow. */
size_t cas_group_build_cat(cas_group_t *g, unsigned char *out, size_t cap);

size_t cas_group_vendor_count(cas_group_t *g);
unsigned cas_group_vendor_ecm_pid(cas_group_t *g, size_t idx);
unsigned cas_group_vendor_emm_pid(cas_group_t *g, size_t idx);
unsigned cas_group_vendor_super_cas_id(cas_group_t *g, size_t idx);
/* 0 = filled, -1 = not due / none yet */
int cas_group_vendor_ecm_due(cas_group_t *g, size_t idx, double now, unsigned char *out, size_t cap, size_t *out_len);
int cas_group_vendor_next_emm(cas_group_t *g, size_t idx, unsigned char *out, size_t cap, size_t *out_len);

void cas_group_vendor_metrics(cas_group_t *g, size_t idx, cas_metrics_t *out);
/* shared scramble engine, one value across all vendors */
void cas_group_shared_metrics(cas_group_t *g, unsigned long long *scrambled_packets_total, unsigned long long *unexpected_clear_packets_total);

/* pure logic, exposed for unit tests. required[i]/alive[i] parallel arrays, vendor_count long. */
int cas_group_fallback_active_calc(size_t vendor_count, const int *required, const int *alive);

/* CW[3]=(CW[0]+CW[1]+CW[2])%256, CW[7] same for CW[4..6]. Pure logic, exposed for tests. */
void csa1_apply_cw_checksum(unsigned char cw[8]);

#endif
