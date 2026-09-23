/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_TSINSPECT_INSPECT_H
#define DVBIPITOOLS_LIB_TSINSPECT_INSPECT_H

#include <stddef.h>
#include <stdint.h>

#include "lib/demux/psi/psi.h"
#include "lib/demux/tspack.h"
#include "lib/helper/argutil.h"
#include "lib/metrics/protocol.h"

typedef struct {
  uint64_t packets;
  uint64_t null_packets;
  uint64_t scrambled_packets;
  uint64_t clear_packets;
  uint64_t transport_errors;         /* TR 101 290 2.1 */
  uint64_t continuity_errors;        /* TR 101 290 1.4 */
  uint64_t duplicate_packets;
  uint64_t discontinuity_indicators;
  uint64_t pat_errors;               /* TR 101 290 1.3 */
  uint64_t pmt_errors;               /* TR 101 290 1.5 */
  uint64_t referenced_pid_missing;   /* TR 101 290 1.6 */
  uint64_t cat_errors;               /* TR 101 290 2.6 */
  uint64_t sdt_errors;               /* TR 101 290 3.5 */
  uint64_t nit_errors;               /* TR 101 290 3.1 */
  uint64_t sdt_other_errors;         /* TR 101 290 3.5b */
  uint64_t nit_other_errors;         /* TR 101 290 3.1b */
  uint64_t pcr_repetition_errors;    /* TR 101 290 2.3a */
  uint64_t pcr_discontinuity_errors; /* TR 101 290 2.3b */
  uint64_t pts_errors;               /* TR 101 290 2.5 */
  uint64_t pcr_accuracy_errors;      /* TR 101 290 2.4 */
  uint64_t eit_errors;               /* TR 101 290 3.6 */
  uint64_t eit_other_errors;         /* TR 101 290 3.6b */
  uint64_t rst_errors;               /* TR 101 290 3.7 */
  uint64_t tdt_errors;               /* TR 101 290 3.8 */
  uint64_t si_crc_errors;            /* TR 101 290 2.2 */
  uint64_t unreferenced_pids;        /* TR 101 290 3.4 */
  uint64_t unreferenced_packets;
  uint64_t unreferenced_unlisted_pids; /* TR 101 290 3.4 */
  uint64_t pid_added;
  uint64_t pid_removed;
  uint64_t stalls;
  uint64_t stall_ms;
} tsinspect_counters_t;

typedef struct tsinspect tsinspect_t;

tsinspect_t *tsinspect_new(metrics_inspect_ts_t level);
tsinspect_t *tsinspect_new_light(metrics_inspect_ts_t level);
tsinspect_t *tsinspect_new_relay(metrics_inspect_ts_t level);
void tsinspect_free(tsinspect_t *t);
int tsinspect_enable_own_psi(tsinspect_t *t, int multi);

const tsinspect_counters_t *tsinspect_counters(const tsinspect_t *t);
tspack_sync_t *tsinspect_sync(tsinspect_t *t);

const psi_obs_t *tsinspect_psi(const tsinspect_t *t);

double tsinspect_pcr_max_interval(const tsinspect_t *t);

double tsinspect_max_gap_ms(const tsinspect_t *t);

double tsinspect_last_packet(const tsinspect_t *t);

void tsinspect_set_known_pids(tsinspect_t *t, const unsigned *pids, unsigned n);
int tsinspect_wants_rx_ns(const tsinspect_t *t);
void tsinspect_set_rx_ns(tsinspect_t *t, uint64_t ns);
void tsinspect_bind_psi(tsinspect_t *t, psi_t *psi);

void tsinspect_tick(tsinspect_t *t, double now);

void tsinspect_packet(tsinspect_t *t, const unsigned char *pkt);

void tsinspect_grid(tsinspect_t *t, const unsigned char *buf, size_t len);

typedef struct {
  tsinspect_t **in;
  unsigned n_in;
  tsinspect_t **out;
  unsigned n_out;
} tsinspect_set_t;

void tsinspect_set_put(metrics_writer_t *w, void *ctx);
void tsinspect_stream_label(char *out, size_t cap, int output, unsigned index);
void tsinspect_put_metrics(tsinspect_t *t, metrics_writer_t *w, const char *stream, double now_mono, double now_wall);

typedef struct tsinspect_agg tsinspect_agg_t;

tsinspect_agg_t *tsinspect_agg_new(metrics_inspect_ts_t level);
void tsinspect_agg_free(tsinspect_agg_t *a);
tsinspect_t *tsinspect_agg_add(tsinspect_agg_t *a);
void tsinspect_agg_remove(tsinspect_agg_t *a, tsinspect_t *t);
void tsinspect_agg_put(tsinspect_agg_t *a, metrics_writer_t *w, const char *stream);
void tsinspect_agg_put_cb(metrics_writer_t *w, void *ctx);

#endif
