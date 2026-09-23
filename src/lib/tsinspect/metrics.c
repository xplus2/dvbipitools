/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <pthread.h>
#include <time.h>

#include "lib/helper/ioutil.h"
#include "lib/helper/signal.h"
#include "priv.h"

static const char *const TABLE_NAMES[] = {"pat", "pmt", "cat", "sdt", "nit"};

static void put_labeled(metrics_writer_t *w, metrics_id_t id, const char *stream, const char *name, uint64_t v) {
  char label[METRICS_LABEL_MAX + 1];
  const char sep[2] = {METRICS_LABEL_SEP, '\0'};
  sbuf_t b;
  sbuf_init(&b, label, sizeof label);
  sbuf_add(&b, stream);
  sbuf_add(&b, sep);
  sbuf_add(&b, name);
  metrics_writer_put(w, id, label, v);
}

void put_header_series(metrics_writer_t *w, const char *stream, const tsinspect_counters_t *c, const tspack_sync_t *sync, int sync_used) {
  metrics_writer_put(w, METRICS_ID_TS_PACKETS_TOTAL, stream, c->packets);
  metrics_writer_put(w, METRICS_ID_TS_BYTES_TOTAL, stream, c->packets * 188);
  metrics_writer_put(w, METRICS_ID_TS_NULL_PACKETS_TOTAL, stream, c->null_packets);
  metrics_writer_put(w, METRICS_ID_TS_SCRAMBLED_PACKETS_TOTAL, stream, c->scrambled_packets);
  metrics_writer_put(w, METRICS_ID_TS_CLEAR_PACKETS_TOTAL, stream, c->clear_packets);
  metrics_writer_put(w, METRICS_ID_TS_TRANSPORT_ERROR_PACKETS_TOTAL, stream, c->transport_errors);
  metrics_writer_put(w, METRICS_ID_TS_CONTINUITY_ERRORS_TOTAL, stream, c->continuity_errors);
  metrics_writer_put(w, METRICS_ID_TS_DUPLICATE_PACKETS_TOTAL, stream, c->duplicate_packets);
  metrics_writer_put(w, METRICS_ID_TS_DISCONTINUITIES_TOTAL, stream, c->discontinuity_indicators);
  if (sync_used) {
    metrics_writer_put(w, METRICS_ID_TS_SYNC_ERRORS_TOTAL, stream, sync->byte_errors);
    metrics_writer_put(w, METRICS_ID_TS_SYNC_LOSS_TOTAL, stream, sync->losses);
  }
}

void tsinspect_put_metrics(tsinspect_t *t, metrics_writer_t *w, const char *stream, double now_mono, double now_wall) {
  pub_t p;
  const tsinspect_counters_t *c = &p.c;
  double skew = now_wall - now_mono;

  pthread_mutex_lock(&t->pub_lock);
  p = t->pub;
  pthread_mutex_unlock(&t->pub_lock);
  put_header_series(w, stream, c, &p.sync, p.sync_used);
  metrics_writer_put(w, METRICS_ID_TS_INPUT_STALLS_TOTAL, stream, c->stalls);
  metrics_writer_put(w, METRICS_ID_TS_INPUT_STALL_MILLISECONDS_TOTAL, stream, c->stall_ms);
  metrics_writer_put(w, METRICS_ID_TS_MAX_INTERPACKET_GAP_MILLISECONDS, stream, (uint64_t)p.gap_max_ms);
  if (p.last_packet > 0.0) metrics_writer_put(w, METRICS_ID_TS_LAST_PACKET_TIMESTAMP_SECONDS, stream, (uint64_t)(p.last_packet + skew));
  if (p.mgb1_valid) metrics_writer_put(w, METRICS_ID_TS_BITRATE_MGB1_BITS_PER_SECOND, stream, (uint64_t)p.mgb1_bps);
  if (p.mgb2_valid) metrics_writer_put(w, METRICS_ID_TS_BITRATE_MGB2_BITS_PER_SECOND, stream, (uint64_t)p.mgb2_bps);
  if (t->level >= METRICS_INSPECT_TS_MEDIUM && t->x) metrics_writer_put(w, METRICS_ID_TS_SI_CRC_ERRORS_TOTAL, stream, c->si_crc_errors);
  if (t->level >= METRICS_INSPECT_TS_MEDIUM && p.rx_used) {
    metrics_writer_put(w, METRICS_ID_TS_PCR_JITTER_MAX_MICROSECONDS, stream, (uint64_t)p.pcr_jitter_us);
    metrics_writer_put(w, METRICS_ID_TS_PCR_ACCURACY_ERRORS_TOTAL, stream, c->pcr_accuracy_errors);
  }
  if (t->level >= METRICS_INSPECT_TS_FULL && p.psi_bound) {
    metrics_writer_put(w, METRICS_ID_TS_UNREFERENCED_PIDS_TOTAL, stream, c->unreferenced_pids);
    metrics_writer_put(w, METRICS_ID_TS_UNREFERENCED_PACKETS_TOTAL, stream, c->unreferenced_packets);
    if (p.known_set) metrics_writer_put(w, METRICS_ID_TS_UNREFERENCED_UNLISTED_PIDS_TOTAL, stream, c->unreferenced_unlisted_pids);
    for (unsigned i = 0; i < p.n_pid; i++) {
      char pid[8];
      sbuf_t pb;
      sbuf_init(&pb, pid, sizeof pid);
      sbuf_add_uint(&pb, p.pid_list[i]);
      put_labeled(w, METRICS_ID_TS_PID_PACKETS_TOTAL, stream, pid, p.pid_pkts[i]);
      put_labeled(w, METRICS_ID_TS_PID_SCRAMBLED_PACKETS_TOTAL, stream, pid, p.pid_scr[i]);
    }
    for (unsigned i = 0; i < p.n_svc; i++) {
      char svc[8];
      sbuf_t sb;
      sbuf_init(&sb, svc, sizeof svc);
      sbuf_add_uint(&sb, p.svc_list[i]);
      put_labeled(w, METRICS_ID_TS_SERVICE_PACKETS_TOTAL, stream, svc, p.svc_pkts[i]);
      put_labeled(w, METRICS_ID_TS_SERVICE_SCRAMBLED_PACKETS_TOTAL, stream, svc, p.svc_scr[i]);
    }
  }
  if (p.relay) {
    metrics_writer_put(w, METRICS_ID_TS_PCR_REPETITION_ERRORS_TOTAL, stream, c->pcr_repetition_errors);
    metrics_writer_put(w, METRICS_ID_PCR_DISCONTINUITIES_TOTAL, stream, c->pcr_discontinuity_errors);
    metrics_writer_put(w, METRICS_ID_TS_PCR_MAX_INTERVAL_MILLISECONDS, stream, (uint64_t)(p.pcr_max_s * 1000.0));
  }
  if (!p.psi_bound) return;
  if (p.have_tsid) metrics_writer_put(w, METRICS_ID_TS_TRANSPORT_STREAM_ID, stream, p.tsid);
  metrics_writer_put(w, METRICS_ID_TS_PAT_ERRORS_TOTAL, stream, c->pat_errors);
  metrics_writer_put(w, METRICS_ID_TS_PMT_ERRORS_TOTAL, stream, c->pmt_errors);
  metrics_writer_put(w, METRICS_ID_TS_CAT_ERRORS_TOTAL, stream, c->cat_errors);
  metrics_writer_put(w, METRICS_ID_TS_SDT_ERRORS_TOTAL, stream, c->sdt_errors);
  metrics_writer_put(w, METRICS_ID_TS_NIT_ERRORS_TOTAL, stream, c->nit_errors);
  metrics_writer_put(w, METRICS_ID_TS_SDT_OTHER_ERRORS_TOTAL, stream, c->sdt_other_errors);
  metrics_writer_put(w, METRICS_ID_TS_NIT_OTHER_ERRORS_TOTAL, stream, c->nit_other_errors);
  metrics_writer_put(w, METRICS_ID_TS_EIT_ERRORS_TOTAL, stream, c->eit_errors);
  metrics_writer_put(w, METRICS_ID_TS_EIT_OTHER_ERRORS_TOTAL, stream, c->eit_other_errors);
  metrics_writer_put(w, METRICS_ID_TS_RST_ERRORS_TOTAL, stream, c->rst_errors);
  metrics_writer_put(w, METRICS_ID_TS_TDT_ERRORS_TOTAL, stream, c->tdt_errors);
  metrics_writer_put(w, METRICS_ID_TS_REFERENCED_PID_MISSING_TOTAL, stream, c->referenced_pid_missing);
  metrics_writer_put(w, METRICS_ID_TS_PCR_REPETITION_ERRORS_TOTAL, stream, c->pcr_repetition_errors);
  metrics_writer_put(w, METRICS_ID_PCR_DISCONTINUITIES_TOTAL, stream, c->pcr_discontinuity_errors);
  metrics_writer_put(w, METRICS_ID_TS_PCR_MAX_INTERVAL_MILLISECONDS, stream, (uint64_t)(p.pcr_max_s * 1000.0));
  metrics_writer_put(w, METRICS_ID_TS_PTS_ERRORS_TOTAL, stream, c->pts_errors);
  metrics_writer_put(w, METRICS_ID_TS_PID_ADDED_TOTAL, stream, c->pid_added);
  metrics_writer_put(w, METRICS_ID_TS_PID_REMOVED_TOTAL, stream, c->pid_removed);
  for (int i = 0; i < 5; i++) {
    const psi_obs_stat_t *s = &p.psi[i];
    put_labeled(w, METRICS_ID_TS_TABLE_CRC_ERRORS_TOTAL, stream, TABLE_NAMES[i], s->crc_errors);
    put_labeled(w, METRICS_ID_TS_TABLE_VERSION_CHANGES_TOTAL, stream, TABLE_NAMES[i], s->changes);
    if (s->last_seen > 0.0) put_labeled(w, METRICS_ID_TS_TABLE_LAST_SEEN_TIMESTAMP_SECONDS, stream, TABLE_NAMES[i], (uint64_t)(s->last_seen + skew));
  }
  if (t->level >= METRICS_INSPECT_TS_MEDIUM) put_labeled(w, METRICS_ID_TS_TABLE_CRC_ERRORS_TOTAL, stream, "bat", p.psi[PSI_OBS_BAT].crc_errors);
}

void tsinspect_set_put(metrics_writer_t *w, void *ctx) {
  const tsinspect_set_t *set = ctx;
  double mono = mono_seconds();
  double wall = (double)time(NULL);
  char label[16];
  for (unsigned i = 0; i < set->n_in; i++) {
    if (!set->in[i]) continue;
    tsinspect_stream_label(label, sizeof label, 0, i);
    tsinspect_put_metrics(set->in[i], w, label, mono, wall);
  }
  for (unsigned i = 0; i < set->n_out; i++) {
    if (!set->out[i]) continue;
    tsinspect_stream_label(label, sizeof label, 1, i);
    tsinspect_put_metrics(set->out[i], w, label, mono, wall);
  }
}

void tsinspect_stream_label(char *out, size_t cap, int output, unsigned index) {
  sbuf_t b;
  sbuf_init(&b, out, cap);
  sbuf_add(&b, output ? "output" : "input");
  sbuf_add_uint(&b, index);
}
