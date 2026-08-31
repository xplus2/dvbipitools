/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "lib/helper/log.h"
#include "lib/mux/psi_build.h"
#include "lib/mux/tspacket_write.h"
#include "lib/helper/signal.h"

#include "priv.h"

/* EIT: real passthrough via remux_emit_eit(), never through mpts_t.
   build_eit/eit_pending stay no-ops. */
static int mpts_program_get_sdt_info(void *ctx, psi_sdt_entry_t *out) {
  return remux_get_sdt_info((remux_t *)ctx, out);
}
static size_t mpts_program_build_eit(void *ctx, unsigned char *out, size_t cap) {
  (void)ctx;
  (void)out;
  (void)cap;
  return 0;
}
static int mpts_program_eit_pending(const void *ctx) {
  (void)ctx;
  return 0;
}
const mpts_program_ops_t mpts_program_ops = {mpts_program_get_sdt_info, mpts_program_build_eit, mpts_program_eit_pending};

static size_t mpts_cas_build_cat(void *ctx, unsigned char *out, size_t cap) { return cas_build_cat((cas_t *)ctx, out, cap); }
static int mpts_cas_ecm_due(void *ctx, size_t vendor_idx, double now_s, unsigned char *out, size_t cap, size_t *out_len) { return cas_vendor_ecm_due((cas_t *)ctx, vendor_idx, now_s, out, cap, out_len); }
static int mpts_cas_next_emm(void *ctx, size_t vendor_idx, unsigned char *out, size_t cap, size_t *out_len) { return cas_vendor_next_emm((cas_t *)ctx, vendor_idx, out, cap, out_len); }
static const mpts_cas_ops_t mpts_cas_ops = {mpts_cas_build_cat, mpts_cas_ecm_due, mpts_cas_next_emm};

void tvhead_mpts_set_cas(mpts_t *mpts, cas_t *cas) {
  mpts_cas_vendor_pid_t vendors[MPTS_MAX_CAS_VENDORS];
  size_t n = cas_vendor_count(cas);
  if (n > MPTS_MAX_CAS_VENDORS)
    n = MPTS_MAX_CAS_VENDORS;
  for (size_t i = 0; i < n; i++) {
    vendors[i].ecm_pid = cas_vendor_ecm_pid(cas, i);
    vendors[i].emm_pid = cas_vendor_emm_pid(cas, i);
  }
  mpts_set_cas(mpts, cas, &mpts_cas_ops, vendors, n);
}

int check_cas_discovery_gate(const config_t *cfg, mpts_program_t *progs, unsigned n, mpts_t *mpts,
                              double cas_gate_deadline, cas_t **cas_out) {
  unsigned ready_count = 0;
  for (unsigned i = 0; i < n; i++)
    if (progs[i].rx)
      ready_count++;
  if (ready_count == n) {
    const out_es_t *es_lists[ARGS_MAX_INPUTS];
    int es_counts[ARGS_MAX_INPUTS];
    cas_t *cas;
    for (unsigned i = 0; i < n; i++)
      es_lists[i] = remux_es(progs[i].rx, &es_counts[i]);
    cas = cas_start_multi(cfg, es_lists, es_counts, n);
    if (!cas) {
      log_line("cas: failed to start");
      return -1;
    }
    tvhead_mpts_set_cas(mpts, cas);
    for (unsigned i = 0; i < n; i++)
      remux_set_cas(progs[i].rx, cas);
    *cas_out = cas;
  } else if (mono_seconds() >= cas_gate_deadline) {
    log_line("cas: --cas-pids-video/--cas-pids-audio need every -i discovered within %.0fs:", CAS_KEYWORD_DISCOVERY_TIMEOUT_S);
    for (unsigned i = 0; i < n; i++)
      if (!progs[i].rx)
        log_line("  input %u: %s", i, progs[i].psi ? "still discovering" : "not connected");
    return -1;
  }
  return 0;
}

void emit_source_cat_passthrough(const mpts_program_t *progs, unsigned n, unsigned char *cat_cc, ts_metrics_t *tsm_p, remux_packet_cb cb, void *ctx) {
  unsigned char cat_desc[ARGS_MAX_INPUTS * 6];
  size_t cat_desc_len = 0;
  int have_desc = 0;
  unsigned char sec[4096];
  unsigned char ptr0 = 0x00;
  size_t sl;
  for (unsigned i = 0; i < n; i++) {
    size_t dl;
    if (!progs[i].rx || cat_desc_len + 6 > sizeof cat_desc)
      continue;
    dl = remux_source_emm_descriptor(progs[i].rx, cat_desc + cat_desc_len, sizeof cat_desc - cat_desc_len);
    if (dl) {
      cat_desc_len += dl;
      have_desc = 1;
    }
  }
  if (!have_desc)
    return;

  sl = psi_build_cat(0, cat_desc, cat_desc_len, sec, sizeof sec);
  if (sl) {
    ts_packet_emit(OUT_PID_CAT, cat_cc, &ptr0, sec, sl, 0, 0, cb, ctx);
    if (tsm_p)
      tsm_p->psi_sections_total[PSI_TABLE_CAT]++;
  } else if (tsm_p) {
    tsm_p->psi_errors_total[PSI_TABLE_CAT]++;
  }
}
