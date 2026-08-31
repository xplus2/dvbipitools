/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "lib/helper/ioutil.h"

#include "export.h"

#define METRICS_DEFAULT_INTERVAL_S 5.0

void metrics_exporter_init(metrics_exporter_t *exp, metrics_component_t component, const char *metrics_id, const char *sock_path, double interval_s) {
  memset(exp, 0, sizeof *exp);
  exp->fd = -1;
  if (!metrics_id || !metrics_id[0])
    return;
  if (!sock_path || !sock_path[0])
    sock_path = METRICS_DEFAULT_SOCK_PATH;
  if (interval_s <= 0.0)
    interval_s = METRICS_DEFAULT_INTERVAL_S;
  if (strlen(sock_path) >= sizeof exp->dest.sun_path)
    return;

  exp->fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (exp->fd < 0)
    return;

  exp->dest.sun_family = AF_UNIX;
  bufcpy(exp->dest.sun_path, sizeof exp->dest.sun_path, sock_path);
  exp->dest_len = (socklen_t)(sizeof exp->dest.sun_family + strlen(sock_path) + 1);

  exp->component = component;
  bufcpy(exp->metrics_id, sizeof exp->metrics_id, metrics_id);
  exp->process_start_time = (uint64_t)time(NULL);
  exp->interval_s = interval_s;
  exp->enabled = 1;
}

void metrics_exporter_close(metrics_exporter_t *exp) {
  if (exp->fd >= 0)
    close(exp->fd);
  exp->fd = -1;
  exp->enabled = 0;
}

int metrics_exporter_enabled(const metrics_exporter_t *exp) {
  return exp->enabled;
}

int metrics_exporter_due(metrics_exporter_t *exp, double now_mono) {
  if (!exp->enabled)
    return 0;
  if (exp->last_send_mono != 0.0 && now_mono - exp->last_send_mono < exp->interval_s)
    return 0;
  exp->last_send_mono = now_mono;
  return 1;
}

void metrics_exporter_note_error(metrics_exporter_t *exp, net_err_reason_t reason) {
  if (!exp->enabled || (unsigned)reason >= NET_ERR_COUNT)
    return;
  exp->errors_total[reason]++;
}

int metrics_exporter_begin(metrics_exporter_t *exp, metrics_writer_t *w, const char *version) {
  metrics_hdr_t hdr;

  if (!exp->enabled)
    return -1;

  memset(&hdr, 0, sizeof hdr);
  hdr.proto_version = METRICS_PROTO_VERSION;
  hdr.component = exp->component;
  bufcpy(hdr.metrics_id, sizeof hdr.metrics_id, exp->metrics_id);
  hdr.process_start_time = exp->process_start_time;
  hdr.sequence = ++exp->sequence;
  hdr.snapshot_time = (uint64_t)time(NULL);

  if (metrics_writer_begin(w, &hdr))
    return -1;
  if (metrics_writer_put(w, METRICS_ID_HEADEND_INFO, version, 1))
    return -1;
  if (metrics_writer_put(w, METRICS_ID_METRICS_SNAPSHOTS_DROPPED_TOTAL, NULL, exp->snapshots_dropped))
    return -1;
  for (unsigned i = 0; i < NET_ERR_COUNT; i++) {
    if (metrics_writer_put(w, METRICS_ID_ERRORS_TOTAL, net_err_reason_name((net_err_reason_t)i), exp->errors_total[i]))
      return -1;
  }
  return 0;
}

void metrics_exporter_send(metrics_exporter_t *exp, metrics_writer_t *w) {
  size_t len;
  ssize_t n;

  if (!exp->enabled)
    return;
  len = metrics_writer_finish(w);
  if (!len) {
    exp->snapshots_dropped++;
    return;
  }
  n = sendto(exp->fd, w->buf, len, MSG_DONTWAIT | MSG_NOSIGNAL, (const struct sockaddr *)&exp->dest, exp->dest_len);
  if (n < 0 || (size_t)n != len)
    exp->snapshots_dropped++;
}

void input_metrics_note_read(input_metrics_t *im, ssize_t n, net_err_reason_t reason) {
  if (!im)
    return;
  if (n > 0) {
    im->bytes_total += (uint64_t)n;
    im->last_data_time = (double)time(NULL);
  } else if (n < 0) {
    im->errors_total[reason]++;
  }
}

void metrics_writer_put_inputs(metrics_writer_t *w, const input_metrics_t *inputs, unsigned n) {
  for (unsigned i = 0; i < n; i++) {
    const input_metrics_t *im = &inputs[i];
    char label[16];
    snprintf(label, sizeof label, "i%u", i);
    metrics_writer_put(w, METRICS_ID_INPUT_UP, label, im->up ? 1 : 0);
    metrics_writer_put(w, METRICS_ID_INPUT_BYTES_TOTAL, label, im->bytes_total);
    metrics_writer_put(w, METRICS_ID_INPUT_RECONNECTS_TOTAL, label, im->reconnects_total);
    metrics_writer_put(w, METRICS_ID_INPUT_LAST_DATA_TIME_SECONDS, label, (uint64_t)im->last_data_time);
    for (unsigned r = 0; r < NET_ERR_COUNT; r++) {
      if (im->errors_total[r]) {
        char combined[16 + 1 + 8];
        snprintf(combined, sizeof combined, "%s%c%s", label, METRICS_LABEL_SEP, net_err_reason_name((net_err_reason_t)r));
        metrics_writer_put(w, METRICS_ID_INPUT_ERRORS_TOTAL, combined, im->errors_total[r]);
      }
    }
  }
}
