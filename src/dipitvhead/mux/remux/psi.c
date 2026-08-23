/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>

#include "lib/mux/psi_build.h"
#include "lib/mux/tspacket_write.h"

#include "priv.h"

#define INTERVAL_PAT_PMT_S 0.1
#define INTERVAL_SDT_S 2.0
#define INTERVAL_NIT_S 10.0
#define INTERVAL_AIT_S 0.5

const char *psi_table_name(psi_table_t table) {
  static const char *const names[PSI_TABLE_COUNT] = {"PAT", "PMT", "CAT", "SDT", "NIT"};
  if ((unsigned)table >= PSI_TABLE_COUNT)
    return "PAT";
  return names[table];
}

static int due(double now, double *last, double interval) {
  if (*last < 0.0 || now - *last >= interval) {
    *last = now;
    return 1;
  }
  return 0;
}

static void psi_note(ts_metrics_t *tsm, psi_table_t table, size_t n) {
  if (!tsm)
    return;
  if (n)
    tsm->psi_sections_total[table]++;
  else
    tsm->psi_errors_total[table]++;
}

/* tracks PMT change metrics and caches sec[0..n) as the last-seen PMT for next diff.
   have_last_pmt false (fresh remux_t, nothing to compare against) -> never "changed" */
static void track_pmt_metrics(remux_t *r, ts_metrics_t *tsm, const unsigned char *sec, size_t n) {
  int changed = r->have_last_pmt && (n != r->last_pmt_len || memcmp(sec, r->last_pmt, n) != 0);
  if (changed)
    tsm->pmt_updates_total++;
  if (n <= sizeof r->last_pmt) {
    memcpy(r->last_pmt, sec, n);
    r->last_pmt_len = n;
    r->have_last_pmt = 1;
  }
}

void send_psi_tables(remux_t *r, double now, remux_packet_cb cb, void *ctx, ts_metrics_t *tsm) {
  unsigned char sec[4096];
  unsigned char ptr0 = 0x00;
  size_t n;

  if (due(now, &r->last_pat, INTERVAL_PAT_PMT_S)) {
    unsigned char prog_desc[32];
    size_t prog_desc_len = r->cas ? cas_prog_desc(r->cas, prog_desc, sizeof prog_desc) : remux_source_ca_descriptor(r, prog_desc, sizeof prog_desc);
    if (r->standalone) {
      n = psi_build_pat(r->cfg.tsid, 0, r->input.sid, r->pids.pmt_pid, sec, sizeof sec);
      psi_note(tsm, PSI_TABLE_PAT, n);
      if (n)
        ts_packet_emit(OUT_PID_PAT, &r->cc_pat, &ptr0, sec, n, 0, 0, cb, ctx);
    }
    n = pmtbuild_pmt(0, r->input.sid, r->pcr_pid_out, prog_desc_len ? prog_desc : NULL, prog_desc_len, r->es, r->es_count, r->send_ait ? r->ait_pmt_entry : NULL, r->send_ait ? r->ait_pmt_entry_len : 0, sec, sizeof sec);
    psi_note(tsm, PSI_TABLE_PMT, n);
    if (n) {
      /* diffing itself metrics-only, skipped whole when tsm NULL, not just its counter.
         "updated" relative to this remux_t's own history: fresh remux_t (reconnect) has
         no prior PMT, first build here never counts */
      if (tsm)
        track_pmt_metrics(r, tsm, sec, n);
      ts_packet_emit(r->pids.pmt_pid, &r->cc_pmt, &ptr0, sec, n, 0, 0, cb, ctx);
    }
    /* r->cas, source EMM passthrough: mutually exclusive (remux_new()) */
    if (r->standalone && (r->cas || find_ca_passthrough(r, 2)) && due(now, &r->last_cat, INTERVAL_PAT_PMT_S)) {
      if (r->cas) {
        n = cas_build_cat(r->cas, sec, sizeof sec);
      } else {
        unsigned char emm_desc[16];
        size_t emm_desc_len = remux_source_emm_descriptor(r, emm_desc, sizeof emm_desc);
        n = emm_desc_len ? psi_build_cat(0, emm_desc, emm_desc_len, sec, sizeof sec) : 0;
      }
      psi_note(tsm, PSI_TABLE_CAT, n);
      if (n)
        ts_packet_emit(OUT_PID_CAT, &r->cc_cat, &ptr0, sec, n, 0, 0, cb, ctx);
    }
  }
  if (r->standalone) {
    if (r->send_sdt && due(now, &r->last_sdt, INTERVAL_SDT_S)) {
      n = psi_build_sdt(0, r->cfg.tsid, r->cfg.onid, r->input.sid, 0x01, r->provider_name, r->service_name, sec, sizeof sec);
      psi_note(tsm, PSI_TABLE_SDT, n);
      if (n)
        ts_packet_emit(OUT_PID_SDT, &r->cc_sdt, &ptr0, sec, n, 0, 0, cb, ctx);
    }
    if (r->send_nit && due(now, &r->last_nit, INTERVAL_NIT_S)) {
      n = psi_build_nit(0, r->cfg.onid, r->cfg.tsid, r->network_name, sec, sizeof sec);
      psi_note(tsm, PSI_TABLE_NIT, n);
      if (n)
        ts_packet_emit(OUT_PID_NIT, &r->cc_nit, &ptr0, sec, n, 0, 0, cb, ctx);
    }
  }
  if (r->send_ait && due(now, &r->last_ait, INTERVAL_AIT_S)) {
    ts_packet_emit(r->pids.ait_pid, &r->cc_ait, &ptr0, r->ait_section, r->ait_section_len, 0, 0, cb, ctx);
    if (tsm)
      tsm->ait_sections_total++;
  }

  if (r->standalone && r->cas) {
    size_t n_vendors = cas_vendor_count(r->cas);
    for (size_t vi = 0; vi < n_vendors; vi++) {
      size_t len;
      if (cas_vendor_ecm_due(r->cas, vi, now, sec, sizeof sec, &len) == 0)
        ts_packet_emit(cas_vendor_ecm_pid(r->cas, vi), &r->cc_ecm[vi], &ptr0, sec, len, 0, 0, cb, ctx);
      while (cas_vendor_next_emm(r->cas, vi, sec, sizeof sec, &len) == 0)
        ts_packet_emit(cas_vendor_emm_pid(r->cas, vi), &r->cc_emm[vi], &ptr0, sec, len, 0, 0, cb, ctx);
    }
  }
}
