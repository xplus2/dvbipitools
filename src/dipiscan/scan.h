/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPISCAN_SCAN_H
#define DIPISCAN_SCAN_H

#include <stdio.h>
#include <sys/types.h>

#include "lib/demux/psi/psi.h"

#include "args.h"

/* 0 = done, 1 = stopped early by SIGINT/SIGTERM */
int scan_run(const config_t *cfg, FILE *out);

/* candidate group address at idx i in cfg->start, cfg->end */
void addr_at(const config_t *cfg, unsigned i, char *buf, size_t n);

typedef enum { PROBE_NONE, PROBE_UNNAMED, PROBE_NAMED } probe_kind_t;

typedef struct {
  unsigned sid;
  char name[PSI_NAME]; /* "(no SDT)" if this program's name never arrived */
} scan_program_t;

typedef struct {
  probe_kind_t kind;
  int rtp_wrapped; /* -1 unknown (no data), 0 udp, 1 rtp */
  char name[PSI_NAME];
  unsigned pkts;
  unsigned tsid, onid, sid;
  scan_program_t programs[PSI_MAX_PROGRAMS]; /* -M only */
  int program_count;                         /* -M only */
} probe_result_t;

/* -M: every PAT-listed program named */
int multi_all_named(const psi_t *psi);

typedef struct {
  psi_t *psi;
  unsigned pkts;
  int multi;
} probe_ctx_t;

/* tspack_feed() callback: 1 = stop early (name(s) resolved), 0 = keep reading */
int probe_cb(void *v, const unsigned char *pkt);

typedef ssize_t (*chan_read_fn)(void *ctx, unsigned char *buf, size_t cap);

/* read until named (or, with multi, every PAT program resolved), timeout, or interrupted */
void probe_common(chan_read_fn rf, void *rctx, int timeout_ms, int multi, probe_result_t *r);

#endif
