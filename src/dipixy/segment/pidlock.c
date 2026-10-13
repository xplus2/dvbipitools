/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "pidlock.h"

#include "lib/demux/psi/section_asm.h"
#include "lib/mux/pmt_filter.h"
#include "lib/mux/psi_build.h"

void pidlock_snapshot(const psi_t *psi, unsigned *allowed, int *n_allowed, int cap) {
  int n;
  const psi_es_t *es;
  unsigned pcr_pid;
  int cnt = 0;
  allowed[cnt++] = 0;
  allowed[cnt++] = psi_pmt_pid(psi);
  pcr_pid = psi_pcr_pid(psi);
  if (pcr_pid) allowed[cnt++] = pcr_pid;
  es = psi_es(psi, &n);
  for (int i = 0; i < n; i++) {
    if (cnt >= cap) break;
    allowed[cnt++] = es[i].pid;
  }
  *n_allowed = cnt;
}

int pidlock_allowed(const unsigned *allowed, int n_allowed, unsigned pid) {
  for (int i = 0; i < n_allowed; i++) {
    if (allowed[i] == pid) return 1;
  }
  return 0;
}

void pidlock_apply_lcevc(const lcevc_select_t *lcevc, pid_filter_t *filter, const unsigned *pids, int count) {
  lcevc_resolved_t r = lcevc_select_resolve(lcevc, pids, count);
  for (int k = 0; k < count; k++) {
    int keep = r.kind == LCEVC_RESOLVE_ALL || (r.kind == LCEVC_RESOLVE_ONE && pids[k] == r.pid);
    if (!keep) pid_filter_add(filter, pids[k]);
  }
}

const unsigned char *pidlock_rewrite_pmt(const psi_t *tp, const pid_filter_t *filter, unsigned char *cc_pmt,
  const unsigned char *pkt, unsigned pid, unsigned char *rw, unsigned char *out, size_t *n_pkts) {

  const unsigned char *sec;
  unsigned drop_pids[PID_FILTER_MAX];
  size_t sl;
  size_t rl;

  *n_pkts = 1;
  if (!tp || filter->count == 0 || !psi_have_pmt(tp) || pid != psi_pmt_pid(tp)) return pkt;
  sec = psi_pmt_section(tp, &sl);
  if (!sec) return pkt;
  if (!(pkt[1] & 0x40)) {
    *n_pkts = 0;
    return NULL;
  }
  for (int k = 0; k < filter->count; k++) drop_pids[k] = filter->pids[k];
  /* cppcheck-suppress uninitvar -- drop_pids[k] for k<filter->count always written above */
  rl = pmt_filter_rewrite(sec, sl, drop_pids, (size_t)filter->count, rw, PSI_SECTION_ASM_BUF_LEN);
  if (!rl) return pkt;
  *n_pkts = pmt_filter_emit_packets(out, PMT_FILTER_PKT_MAX * 188, pid, cc_pmt, rw, rl);
  if (!*n_pkts) {
    *n_pkts = 1;
    return pkt;
  }
  return out;
}

const unsigned char *pidlock_rewrite_pat(const psi_t *tp, unsigned char *cc_pat, const unsigned char *pkt, unsigned pid, unsigned char *out188) {
  unsigned char sec[32];
  const unsigned char *src;
  size_t sl;
  size_t rl;

  if (pid != 0 || !tp || !psi_ready(tp)) return pkt;
  if (!(pkt[1] & 0x40)) return NULL;
  src = psi_pat_section(tp, &sl);
  if (!src || sl < 8) return pkt;
  rl = psi_build_pat(psi_transport_stream_id(tp), (src[5] >> 1) & 0x1F, psi_program_number(tp), psi_pmt_pid(tp), sec, sizeof sec);
  if (!rl) return pkt;
  *cc_pat = (*cc_pat + 1) & 0x0F;
  if (!pmt_filter_emit_packet(out188, 0, *cc_pat, sec, rl)) return pkt;
  return out188;
}
