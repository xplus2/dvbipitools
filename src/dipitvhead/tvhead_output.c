/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "lib/log.h"
#include "lib/signal.h"
#include "tvhead_priv.h"
#include "version.h"

/* pace/account once per datagram not per packet; keeps burst_limit's sleep off per-packet path */
void flush_batch(out_ctx_t *o) {
  size_t n = (size_t)o->batch_count * 188;

  if (o->batch_count == 0)
    return;
  bitrate_pace(o->pacer);
  if (o->rtp) {
    rtpheader_build(o->rtph, (uint32_t)(mono_seconds() * 90000.0), o->batch, 12);
    if (mcast_send(o->mc, o->batch, 12 + n) < 0) {
      o->had_error = 1;
      o->errors++;
    }
  } else if (mcast_send(o->mc, o->batch + 12, n) < 0) {
    o->had_error = 1;
    o->errors++;
  }
  bitrate_account_n(o->pacer, (unsigned)o->batch_count);
  o->batch_count = 0;
}

void packet_cb(void *ctx, const unsigned char *pkt188) {
  out_ctx_t *o = ctx;
  memcpy(o->batch + 12 + (size_t)o->batch_count * 188, pkt188, 188);
  o->batch_count++;
  o->packets++;
  if (o->batch_count == TS_PER_DGRAM)
    flush_batch(o);
}

void send_null_packet(out_ctx_t *o) {
  unsigned char pkt[188];
  memset(pkt, 0xFF, sizeof pkt);
  pkt[0] = 0x47;
  pkt[1] = 0x1F;
  pkt[2] = 0xFF;
  pkt[3] = 0x10;
  packet_cb(o, pkt);
}

int remux_cb(void *v, const unsigned char *pkt) {
  feed_ctx_t *f = v;
  remux_feed(f->rx, f->now, pkt, packet_cb, f->out, f->tsm);
  return 0;
}

/* common + output + input + TS-integrity + CAS metrics, on mx's own interval
   (metrics_exporter_due gates/no-ops when disabled) */
void emit_metrics(metrics_exporter_t *mx, double now, const out_ctx_t *out, unsigned configured_services, unsigned active_services,
                   const input_metrics_t *inputs, unsigned n_inputs, const ts_metrics_t *tsm, cas_t *cas) {
  metrics_writer_t w;

  if (!metrics_exporter_due(mx, now))
    return;
  if (metrics_exporter_begin(mx, &w, TOOL_VERSION))
    return;
  metrics_writer_put(&w, METRICS_ID_OUTPUT_PACKETS_TOTAL, NULL, out->packets);
  metrics_writer_put(&w, METRICS_ID_OUTPUT_BYTES_TOTAL, NULL, out->packets * 188ULL);
  metrics_writer_put(&w, METRICS_ID_OUTPUT_ERRORS_TOTAL, NULL, out->errors);
  metrics_writer_put(&w, METRICS_ID_CONFIGURED_SERVICES, NULL, configured_services);
  metrics_writer_put(&w, METRICS_ID_ACTIVE_SERVICES, NULL, active_services);
  metrics_writer_put(&w, METRICS_ID_TV_SOURCE_PROGRAM_UP, NULL, active_services > 0 ? 1 : 0);
  metrics_writer_put_inputs(&w, inputs, n_inputs);
  metrics_writer_put(&w, METRICS_ID_TS_PACKETS_TOTAL, NULL, tsm->ts_packets);
  metrics_writer_put(&w, METRICS_ID_TS_SYNC_ERRORS_TOTAL, NULL, tsm->ts_sync_errors);
  metrics_writer_put(&w, METRICS_ID_TS_CONTINUITY_ERRORS_TOTAL, NULL, tsm->ts_continuity_errors);
  metrics_writer_put(&w, METRICS_ID_TS_DISCONTINUITIES_TOTAL, NULL, tsm->ts_discontinuities);
  metrics_writer_put(&w, METRICS_ID_PCR_DISCONTINUITIES_TOTAL, NULL, tsm->pcr_discontinuities);
  {
    psi_table_t t;
    for (t = 0; t < PSI_TABLE_COUNT; t++) {
      metrics_writer_put(&w, METRICS_ID_PSI_SECTIONS_TOTAL, psi_table_name(t), tsm->psi_sections_total[t]);
      metrics_writer_put(&w, METRICS_ID_PSI_ERRORS_TOTAL, psi_table_name(t), tsm->psi_errors_total[t]);
    }
  }
  metrics_writer_put(&w, METRICS_ID_TV_SOURCE_PMT_UPDATES_TOTAL, NULL, tsm->pmt_updates_total);
  metrics_writer_put(&w, METRICS_ID_TV_REMUX_PACKETS_TOTAL, NULL, tsm->remux_packets_total);
  metrics_writer_put(&w, METRICS_ID_TV_REMUX_DROPPED_PACKETS_TOTAL, NULL, tsm->remux_dropped_packets_total);
  metrics_writer_put(&w, METRICS_ID_TV_AIT_SECTIONS_TOTAL, NULL, tsm->ait_sections_total);
  metrics_writer_put(&w, METRICS_ID_TV_EIT_QUEUE_DROPS_TOTAL, NULL, tsm->eit_queue_drops_total);
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

/* steady-state: read, remux, send, until stop/hard error. returns 0 clean stop, -1 error */
int run_output(tvsrc_t *src, remux_t *rx, out_ctx_t *out, const config_t *cfg, cas_t *cas, metrics_exporter_t *mx, input_metrics_t *im,
                ts_metrics_t *tsm) {
  unsigned char buf[65536];
  tspack_t pz;
  feed_ctx_t fc;
  double start = mono_seconds(), last_stat = 0;
  net_err_reason_t reason;

  memset(&pz, 0, sizeof pz);
  fc.rx = rx;
  fc.out = out;
  fc.tsm = tsm;

  while (!signal_stop_requested()) {
    int stuff_n, k;
    double now;
    ssize_t n;
    reason = NET_ERR_OTHER;
    n = tvsrc_read(src, buf, sizeof buf, &reason);
    input_metrics_note_read(im, n, reason);
    if (n < 0) {
      cas_flush(cas, packet_cb, out);
      return -1;
    }
    now = mono_seconds();
    if (n > 0) {
      fc.now = now;
      tspack_feed(&pz, buf, (size_t)n, remux_cb, &fc);
    }
    if (out->had_error) {
      cas_flush(cas, packet_cb, out);
      return -1;
    }
    if (cas && cas_failed(cas)) {
      log_line("cas: fatal error, stopping");
      cas_flush(cas, packet_cb, out);
      return -1;
    }
    if (cas && signal_reload_requested())
      cas_reload_receivers(cas);
    stuff_n = bitrate_stuff_due(out->pacer);
    for (k = 0; k < stuff_n; k++)
      send_null_packet(out);
    if (out->had_error) {
      cas_flush(cas, packet_cb, out);
      return -1;
    }
    if (cfg->verbose && now - last_stat >= 1.0) {
      fprintf(stderr, "\r%.0fs, %llu TS packets\033[K", now - start, out->packets);
      fflush(stderr);
      last_stat = now;
    }
    emit_metrics(mx, now, out, 1, 1, im, 1, tsm, cas);
  }
  cas_flush(cas, packet_cb, out);
  return 0;
}
