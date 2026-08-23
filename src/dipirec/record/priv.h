/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DIPIREC_RECORD_PRIV_H
#define DIPIREC_RECORD_PRIV_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "lib/demux/psi/psi.h"
#include "lib/metrics/export.h"
#include "lib/mux/flv/flv.h"
#include "lib/net/rist/ristout.h"
#include "lib/net/rtmp/rtmpout.h"
#include "lib/net/tssink.h"
#include "lib/net/tssource.h"

#include "../args.h"
#include "../filter/pace.h"
#include "../record.h"
#include "../ret_client.h"

typedef struct {
  uri_kind_t kind;
  tssrc_t *t;
  ret_client_t *ret; /* NULL unless --ret */
} src_t;

int src_open(const config_t *cfg, src_t *s);
/* TS bytes, RTP stripped. >0 len, 0 timeout, -1 end */
ssize_t src_read(src_t *s, unsigned char *buf, size_t cap);
void src_close(src_t *s);

int open_output(const char *path);

/* file/stdout via a plain fd, rtp/udp via tssink, rist:// via ristout.
   fd valid: run_mkv needs it, always file/stdout case (args rejects mkv/mka with net -o) */
typedef struct {
  int fd;
  tssink_t *net;   /* NULL unless -o rtp:// or udp:// */
  ristout_t *rist; /* NULL unless -o rist:// */
  int net_had_error;  /* edge-log gate, net send failure never stops recording */
  int rist_had_error; /* edge-log gate, rist write failure never stops recording */
  uint64_t errors_total; /* metrics: cumulative write failures, net/rist only */
} out_sink_t;

int sink_open(const config_t *cfg, const out_target_t *t, out_sink_t *o);
int sink_write(out_sink_t *o, const unsigned char *p, size_t n);
void sink_close(out_sink_t *o);

/* failed network sink never fatal, log only on failure/recovery edge, retry every write */
void note_send_result(int ok, int *had_error, uint64_t *errors_total, const char *label);

/* N rtmpout_t, independently paced: one target down doesn't block others */
typedef struct {
  rtmpout_t *out[DIPIREC_MAX_OUT];
  int had_error[DIPIREC_MAX_OUT];
  uint64_t errors_total[DIPIREC_MAX_OUT]; /* metrics: cumulative write failures per target */
  int n;
} rtmp_fanout_t;

int rtmp_fanout_open(const config_t *cfg, rtmp_fanout_t *r);
void rtmp_fanout_close(const rtmp_fanout_t *r);
void rtmp_fanout_cb(void *ctx, flv_tag_type_t type, uint32_t timestamp_ms, const unsigned char *data, size_t len);

void push_metrics(metrics_exporter_t *mx, const config_t *cfg, const out_sink_t *sinks, int n_sinks,
                  const rtmp_fanout_t *rf, unsigned long long bytes, double start);

/* one same-line stats update, tty only */
void stats_show(const config_t *cfg, double elapsed, unsigned long long bytes, const psi_t *psi);

int run_raw(src_t *s, const config_t *cfg, out_sink_t *sinks, int n_sinks, const rtmp_fanout_t *rf,
            metrics_exporter_t *mx, unsigned long long *bytes, double start, pace_ctrl_t *pace);
int run_stream(src_t *s, const config_t *cfg, out_sink_t *sinks, int n_sinks, int mkv_fd, rtmp_fanout_t *rf,
               metrics_exporter_t *mx, unsigned long long *bytes, double start, int video_ok, unsigned pmt_pid,
               const unsigned *all_pids, int n_all_pids, pace_ctrl_t *pace);

/* mpts discovery + -p decision. 0: proceed (pmt_pid/all_pids/n_all_pids filled in).
   1: abort, message already printed. raw skips this, nothing to select there. */
int resolve_pmt_selection(const config_t *cfg, src_t *s, unsigned *pmt_pid, unsigned *all_pids, int *n_all_pids);

#endif
