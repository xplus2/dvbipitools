/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIDESCRAMBLE_PIPELINE_H
#define DIPIDESCRAMBLE_PIPELINE_H

#include <stdint.h>

#include "args.h"
#include "biss_ca_state.h"
#include "device.h"
#include "emmcache.h"
#include "ipiclient.h"
#include "lib/demux/psi/psi.h"
#include "lib/demux/psi/section_asm.h"
#include "lib/mux/flv/flv.h"
#include "lib/mux/mkv/mkv.h"
#include "lib/net/rtmp/rtmpout.h"
#include "lib/scrambler/scrambler.h"

typedef struct {
  int outfd[DIPIDESCRAMBLE_MAX_OUT]; /* plain file targets, unused (mkv_t owns fd) under -f mkv/mka */
  int n_outfd;
  mkv_t *mkv; /* NULL unless -f mkv|mka */
  unsigned long long mkv_bytes;
  flv_t *flv; /* NULL: no rtmp(s) target */
  rtmpout_t *rtmp[DIPIDESCRAMBLE_MAX_OUT];
  int rtmp_had_error[DIPIDESCRAMBLE_MAX_OUT];
  int n_rtmp;
  unsigned long long packets;
  psi_t *psi;
  unsigned ecm_pid, emm_pid; /* 0 = not yet resolved */
  int cas_logged;
  const char *cas_mode; /* NULL until cas_logged: "classic"/"biss1e"/"biss-ca" */
  uint64_t ecm_total;
  uint64_t ecm_errors_total;
  uint64_t emm_total;
  uint64_t cryptoperiod_transitions_total;
  uint64_t scrambled_packets_total;
  uint64_t unexpected_clear_packets_total;
  uint64_t key_load_errors_total;
  uint64_t output_errors_total;
  device_state_t *dev; /* NULL until classic ECM/EMM CAS resolved */
  biss_ca_state_t *biss_ca; /* NULL unless BISS Mode CA resolved */
  emmcache_t *cache;
  const char *emm_file;
  ipiclient_t *ipi; /* NULL unless -u given and classic CAS resolved */
  const config_t *cfg;
  psi_section_asm_t ecm_asm, emm_asm;
  scrambler_t *scr; /* NULL until scrambling_mode resolved */
  int cw_len;
  int have_cw[2]; /* indexed by SCRAMBLE_PARITY_EVEN/_ODD */
  unsigned char last_cw[2][16];
  /* set by emit_downstream(). a void scrambler_emit_cb can't return an error code. this is how a failed mkv_feed/write reaches pkt_cb's int return */
  int emit_failed;
  int fatal; /* CAS/BISS scheme could not be resolved (missing key material, unsupported mode) */
} loop_ctx_t;

/* CAS resolve -> ECM/EMM reassembly -> CW resolve -> descramble in place. lc must be
   zero-initialized by caller before first calling it (lc->psi/lc->cache/lc->cfg/lc->outfd
   or lc->mkv/lc->flv set up first).
   0 ok, 1 stop (lc->fatal or lc->emit_failed explains why) */
int pkt_cb(void *v, const unsigned char *pkt);

/* drains any packets scrambler_set_key() queued for last crypto-period's batch */
void pipeline_flush(loop_ctx_t *lc);

/* flv_tag_cb, fans out to lc->rtmp[0..n_rtmp), registered on lc->flv by main() */
void rtmp_fanout_cb(void *ctx, flv_tag_type_t type, uint32_t timestamp_ms, const unsigned char *data, size_t len);

#endif
