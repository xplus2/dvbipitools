/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_TSINSPECT_PRIV_H
#define DVBIPITOOLS_LIB_TSINSPECT_PRIV_H

#include <pthread.h>
#include <stdint.h>

#include "lib/demux/psi/section_asm.h"
#include "inspect.h"

#define PID_NONE 0x2000
#define CC_WIN 0x40
#define MGB2_SLICES 10
#define PID_DETAIL_MAX 32
#define SVC_DETAIL_MAX PSI_MAX_PROGRAMS

typedef struct {
  unsigned char pid_slot[8192];
  unsigned char svc_slot[65536];
  unsigned n_pid, n_svc;
  unsigned pid_list[PID_DETAIL_MAX];
  uint64_t pid_pkts[PID_DETAIL_MAX];
  uint64_t pid_scr[PID_DETAIL_MAX];
  unsigned svc_list[SVC_DETAIL_MAX];
  uint64_t svc_pkts[SVC_DETAIL_MAX];
  uint64_t svc_scr[SVC_DETAIL_MAX];
} detail_t;

typedef struct {
  unsigned pid;
  double last;
  int flag;
} pts_slot_t;

typedef struct {
  unsigned sid;
  double last[2];
  int flag[2];
} eit_slot_t;

typedef struct {
  tsinspect_counters_t c;
  tspack_sync_t sync;
  int sync_used;
  int psi_bound;
  int relay;
  psi_obs_stat_t psi[PSI_OBS_COUNT];
  double pcr_jitter_us;
  double mgb1_bps, mgb2_bps;
  int mgb1_valid, mgb2_valid;
  int known_set;
  unsigned n_pid, n_svc;
  unsigned pid_list[PID_DETAIL_MAX];
  uint64_t pid_pkts[PID_DETAIL_MAX];
  uint64_t pid_scr[PID_DETAIL_MAX];
  unsigned svc_list[SVC_DETAIL_MAX];
  uint64_t svc_pkts[SVC_DETAIL_MAX];
  uint64_t svc_scr[SVC_DETAIL_MAX];
  int rx_used;
  int have_tsid;
  unsigned tsid;
  double pcr_max_s;
  double gap_max_ms;
  double last_packet;
} pub_t;

typedef struct {
  psi_obs_t obs;
  psi_section_asm_t si_asm[3];
  eit_slot_t eit[PSI_MAX_PROGRAMS];
  eit_slot_t eit_other[PSI_MAX_PROGRAMS];
  pts_slot_t pts[PSI_MAX_ES];
  unsigned char pts_idx[8192];
  unsigned char miss_run[8192];
  unsigned char unref_flag[8192];
  unsigned char unlisted_flag[8192];
  unsigned char known[8192 / 8];
  int known_set;
  detail_t *d;
} ext_t;

struct tsinspect {
  metrics_inspect_ts_t level;
  tsinspect_counters_t counters;
  tspack_sync_t sync;
  ext_t *x;
  void (*packet)(tsinspect_t *, const unsigned char *);
  psi_t *psi;
  int own_psi;
  int relay;
  double now;
  double start;
  int table_flag[PSI_OBS_COUNT];
  double scrambled_since;
  int cat_flag;
  double win_start;
  unsigned last_pmt_changes;
  unsigned pcr_pid;
  int pcr_have;
  uint64_t pcr_last;
  double pcr_max_cur, pcr_max_prev, pcr_win_start;
  uint64_t rx_ns;
  uint64_t pcr_rx_last;
  double pcr_jit_cur, pcr_jit_prev;
  int rx_used;
  int eit_n;
  double tdt_last;
  int eit_other_n;
  int tdt_flag;
  int sync_used;
  pthread_mutex_t pub_lock;
  pub_t pub;
  double pub_at;
  double mgb1_start;
  uint64_t mgb1_pk;
  double mgb1_bps;
  int mgb1_valid;
  double mgb2_t[MGB2_SLICES + 1];
  uint64_t mgb2_p[MGB2_SLICES + 1];
  int mgb2_n;
  int mgb2_head;
  double mgb2_bps;
  int mgb2_valid;
  double prev_now;
  double last_packet;
  uint64_t seen_packets;
  int stalled;
  double gap_max_cur, gap_max_prev;
  unsigned short ref[PSI_MAX_ES + 1];
  int ref_n;
  int ref_valid;
  int pts_n;
  int pts_built;
  unsigned last_pts_changes;
  unsigned char cc[8192];
};

void packet_base(tsinspect_t *t, const unsigned char *pkt);
void packet_full(tsinspect_t *t, const unsigned char *pkt);
void publish(tsinspect_t *t);
void put_header_series(metrics_writer_t *w, const char *stream, const tsinspect_counters_t *c, const tspack_sync_t *sync, int sync_used);

#endif
