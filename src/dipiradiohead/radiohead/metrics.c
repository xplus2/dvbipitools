/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>

#include "../mux/tspacketizer.h"
#include "../version.h"
#include "priv.h"

/* common + output + input + CAS + radio metrics, on mx's own interval
   (metrics_exporter_due gates/no-ops when disabled) */
void emit_metrics(metrics_exporter_t *mx, double now, const out_ctx_t *out, unsigned configured_services, unsigned active_services,
                   const input_metrics_t *inputs, unsigned n_inputs, const radio_metrics_t *rm, cas_t *cas) {
  metrics_writer_t w;
  unsigned c;

  if (!metrics_exporter_due(mx, now))
    return;
  if (metrics_exporter_begin(mx, &w, TOOL_VERSION))
    return;
  metrics_writer_put(&w, METRICS_ID_OUTPUT_PACKETS_TOTAL, NULL, out->packets);
  metrics_writer_put(&w, METRICS_ID_OUTPUT_BYTES_TOTAL, NULL, out->packets * 188ULL);
  metrics_writer_put(&w, METRICS_ID_OUTPUT_ERRORS_TOTAL, NULL, out->errors);
  metrics_writer_put(&w, METRICS_ID_CONFIGURED_SERVICES, NULL, configured_services);
  metrics_writer_put(&w, METRICS_ID_ACTIVE_SERVICES, NULL, active_services);
  metrics_writer_put_inputs(&w, inputs, n_inputs);
  for (c = 0; c <= SRC_AAC_LATM; c++)
    metrics_writer_put(&w, METRICS_ID_RADIO_AUDIO_FRAMES_TOTAL, codec_name((source_codec_t)c), rm->frames_total[c]);
  metrics_writer_put(&w, METRICS_ID_RADIO_AUDIO_FRAMING_ERRORS_TOTAL, NULL, rm->framing_errors_total);
  metrics_writer_put(&w, METRICS_ID_RADIO_METADATA_UPDATES_TOTAL, NULL, rm->metadata_updates_total);
  if (cas) {
    cas_metrics_t cm;
    size_t i, n = cas_vendor_count(cas);
    cas_get_metrics(cas, &cm);
    metrics_writer_put(&w, METRICS_ID_CAS_SCRAMBLED_PACKETS_TOTAL, NULL, cm.scrambled_packets_total);
    metrics_writer_put(&w, METRICS_ID_CAS_UNEXPECTED_CLEAR_PACKETS_TOTAL, NULL, cm.unexpected_clear_packets_total);
    for (i = 0; i < n; i++) {
      char label[16];
      cas_metrics_t vm;
      cas_vendor_metrics(cas, i, &vm);
      snprintf(label, sizeof label, "0x%08x", cas_vendor_super_cas_id(cas, i));
      metrics_writer_put(&w, METRICS_ID_CAS_ECMG_CONNECTED, label, vm.ecmg_connected ? 1 : 0);
      metrics_writer_put(&w, METRICS_ID_CAS_EMMG_CLIENTS, label, vm.emmg_clients);
      metrics_writer_put(&w, METRICS_ID_CAS_CRYPTOPERIOD_TRANSITIONS_TOTAL, label, vm.cryptoperiod_transitions_total);
      metrics_writer_put(&w, METRICS_ID_CAS_ECM_TOTAL, label, vm.ecm_total);
      metrics_writer_put(&w, METRICS_ID_CAS_ECM_ERRORS_TOTAL, label, vm.ecm_errors_total);
      metrics_writer_put(&w, METRICS_ID_CAS_EMM_TOTAL, label, vm.emm_total);
      metrics_writer_put(&w, METRICS_ID_CAS_EMM_DROPPED_TOTAL, label, vm.emm_dropped_total);
    }
  }
  metrics_exporter_send(mx, &w);
}

/* mpts.c is tool-agnostic (shared with dipitvhead). they adapt our concrete types to its void*-based ops vtables. */
static int mpts_program_get_sdt_info(void *ctx, psi_sdt_entry_t *out) {
  return tspacketizer_get_sdt_info((tspacketizer_t *)ctx, out);
}
static size_t mpts_program_build_eit(void *ctx, unsigned char *out, size_t cap) {
  return tspacketizer_build_eit((tspacketizer_t *)ctx, out, cap);
}
static int mpts_program_eit_pending(const void *ctx) {
  return tspacketizer_eit_pending((const tspacketizer_t *)ctx);
}
const mpts_program_ops_t mpts_program_ops = {
    mpts_program_get_sdt_info, mpts_program_build_eit, mpts_program_eit_pending};

static size_t mpts_cas_build_cat(void *ctx, unsigned char *out, size_t cap) {
  return cas_build_cat((cas_t *)ctx, out, cap);
}
static int mpts_cas_ecm_due(void *ctx, size_t vendor_idx, double now_s, unsigned char *out, size_t cap, size_t *out_len) {
  return cas_vendor_ecm_due((cas_t *)ctx, vendor_idx, now_s, out, cap, out_len);
}
static int mpts_cas_next_emm(void *ctx, size_t vendor_idx, unsigned char *out, size_t cap, size_t *out_len) {
  return cas_vendor_next_emm((cas_t *)ctx, vendor_idx, out, cap, out_len);
}
static const mpts_cas_ops_t mpts_cas_ops = {mpts_cas_build_cat, mpts_cas_ecm_due, mpts_cas_next_emm};

void radiohead_mpts_set_cas(mpts_t *mpts, cas_t *cas) {
  mpts_cas_vendor_pid_t vendors[MPTS_MAX_CAS_VENDORS];
  size_t i, n = cas_vendor_count(cas);
  if (n > MPTS_MAX_CAS_VENDORS)
    n = MPTS_MAX_CAS_VENDORS;
  for (i = 0; i < n; i++) {
    vendors[i].ecm_pid = cas_vendor_ecm_pid(cas, i);
    vendors[i].emm_pid = cas_vendor_emm_pid(cas, i);
  }
  mpts_set_cas(mpts, cas, &mpts_cas_ops, vendors, n);
}
