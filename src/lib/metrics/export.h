/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#ifndef DVBIPITOOLS_LIB_METRICS_EXPORT_H
#define DVBIPITOOLS_LIB_METRICS_EXPORT_H

#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include "../net/netconnect.h"
#include "protocol.h"

typedef struct {
  int enabled;
  int fd;
  struct sockaddr_un dest;
  socklen_t dest_len;
  metrics_component_t component;
  char metrics_id[METRICS_ID_MAX];
  uint64_t process_start_time;
  uint64_t sequence;
  double interval_s;
  double last_send_mono; /* 0 = never sent yet */
  uint64_t snapshots_dropped;
  uint64_t errors_total[NET_ERR_COUNT];
} metrics_exporter_t;

/* metrics_id NULL/empty -> stays disabled, rest becomes no-op. sock_path/interval_s <=0/NULL -> documented defaults */
void metrics_exporter_init(metrics_exporter_t *exp, metrics_component_t component, const char *metrics_id, const char *sock_path, double interval_s);

void metrics_exporter_close(metrics_exporter_t *exp);

int metrics_exporter_enabled(const metrics_exporter_t *exp);

/* call once/iteration with mono_seconds(). 1 = interval elapsed, build+send now (begin/put/send below). always 0 if disabled */
int metrics_exporter_due(metrics_exporter_t *exp, double now_mono);

/* safe to call even when disabled */
void metrics_exporter_note_error(metrics_exporter_t *exp, net_err_reason_t reason);

/* fills header + common metrics (info/dropped/errors). caller adds component entries via metrics_writer_put() before send(). -1 = disabled */
int metrics_exporter_begin(metrics_exporter_t *exp, metrics_writer_t *w, const char *version);

/* nonblocking. any failure (oversized/full buffer/no collector) counts as dropped, no retry */
void metrics_exporter_send(metrics_exporter_t *exp, metrics_writer_t *w);

/* caller keeps outside the per-input connection object - must survive its
   reconnect teardown/recreate */
typedef struct {
  int up;
  int seen_open; /* has ever opened successfully - gates reconnects_total */
  uint64_t bytes_total;
  uint64_t reconnects_total;
  uint64_t errors_total[NET_ERR_COUNT];
  double last_data_time; /* unix seconds, 0 = never */
} input_metrics_t;

/* n>0: bumps bytes_total + last_data_time. n<0: bumps errors_total[reason].
   n==0: no-op. safe to call with im NULL (disabled) */
void input_metrics_note_read(input_metrics_t *im, ssize_t n, net_err_reason_t reason);

/* label = "i<idx>", or "i<idx>" + METRICS_LABEL_SEP + reason per nonzero
   error reason (bounds snapshot size by observed error diversity, not n) */
void metrics_writer_put_inputs(metrics_writer_t *w, const input_metrics_t *inputs, unsigned n);

#endif
