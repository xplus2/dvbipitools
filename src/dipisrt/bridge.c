/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lib/demux/crc32.h"
#include "lib/helper/log.h"
#include "lib/net/srt/srtin.h"
#include "lib/net/srt/srtout.h"
#include "lib/net/ts/sink.h"
#include "lib/net/ts/source.h"
#include "lib/tsinspect/inspect.h"
#include "lib/sys/signal.h"

#include "cli/args.h"
#include "bridge.h"
#include "version.h"

#define DIPISRT_SENDER_POLL_MS 200
#define DIPISRT_SENDER_DRAIN_MS_DEFAULT 1000 /* srt's own default latency buffer */

static void fill_common_opts(const config_t *cfg, srtcommon_opts_t *o) {
  o->passphrase = cfg->passphrase[0] ? cfg->passphrase : NULL;
  o->pbkeylen = cfg->pbkeylen;
  o->streamid = cfg->streamid[0] ? cfg->streamid : NULL;
  o->packetfilter = cfg->packetfilter[0] ? cfg->packetfilter : NULL;
  o->latency_ms = cfg->latency_ms;
}

static int run_sender(const config_t *cfg, metrics_exporter_t *mx) {
  tssrc_cfg_t tc;
  tssrc_t *src;
  srtout_cfg_t rcfg;
  srtout_t *srt;
  tsinspect_t *insp;
  tsinspect_set_t set = {&insp, 1, NULL, 0};
  struct pollfd pfd;
  unsigned char buf[65536];
  int rc = 0;
  int was_connected = 0;

  plain_endpoint_to_tssrc_cfg(&cfg->in.nonsrt, cfg->iface, TOOL_NAME "/" TOOL_VERSION, cfg->insecure_tls, &tc);
  src = tssrc_open(&tc, NULL);
  if (!src) return 1;

  memset(&rcfg, 0, sizeof rcfg);
  for (int i = 0; i < cfg->out.n_srt; i++) {
    rcfg.peers[i].host = cfg->out.srt_host[i];
    rcfg.peers[i].port = cfg->out.srt_port[i];
  }
  rcfg.npeers = cfg->out.n_srt;
  rcfg.group_mode = cfg->group_mode;
  rcfg.rendezvous = cfg->rendezvous;
  rcfg.local_host = cfg->local_host[0] ? cfg->local_host : NULL;
  rcfg.local_port = cfg->local_port;
  fill_common_opts(cfg, &rcfg.opts);
  rcfg.verbose = cfg->verbose;
  rcfg.mx = mx;
  rcfg.tool_version = TOOL_VERSION;
  rcfg.safety_mult = cfg->send_buffer_mult;
  rcfg.queue_metrics = cfg->metrics_inspect_ts == METRICS_INSPECT_TS_OFF ? 0 : cfg->metrics_inspect_ts == METRICS_INSPECT_TS_BASIC ? 1 : 2;

  srt = srtout_open(&rcfg);
  if (!srt) {
    tssrc_close(src);
    return 1;
  }

  insp = tsinspect_new_relay(cfg->metrics_inspect_ts);
  if (insp) metrics_exporter_set_extra(mx, tsinspect_set_put, &set);
  if (tsinspect_wants_rx_ns(insp)) tssrc_enable_rx_timestamps(src);

  pfd.fd = tssrc_fd(src);
  pfd.events = POLLIN;

  while (!signal_stop_requested()) {
    srtout_status_t st;
    int pr;
    net_err_reason_t reason = NET_ERR_OTHER;
    ssize_t n;

    srtout_service(srt, &st);
    if (insp) tsinspect_tick(insp, mono_seconds());
    if (st.connected != was_connected) {
      log_line(st.connected ? "srt output: connected" : "srt output: link down, reconnecting");
      was_connected = st.connected;
    }

    pr = poll(&pfd, 1, DIPISRT_SENDER_POLL_MS);
    if (pr < 0) {
      if (errno == EINTR) continue;
      log_line("srt output: poll failed: %s", strerror(errno));
      rc = 1;
      break;
    }
    if (pr == 0 || !(pfd.revents & (POLLIN | POLLERR | POLLHUP))) continue;

    n = tssrc_read(src, buf, sizeof buf, &reason);
    if (n < 0) {
      rc = 1;
      break;
    }
    if (n > 0) {
      if (insp) {
        tsinspect_set_rx_ns(insp, tssrc_last_rx_ns(src));
        tsinspect_grid(insp, buf, (size_t)n);
      }
      srtout_write(srt, buf, (size_t)n);
    }
  }
  if (rc) {
    /* eof/err, not live stop: drain srt retransmits before teardown */
    unsigned drain_ms = cfg->latency_ms ? cfg->latency_ms : DIPISRT_SENDER_DRAIN_MS_DEFAULT;
    struct timespec ts = {(time_t)(drain_ms / 1000), (long)(drain_ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
  }
  if (insp) metrics_exporter_set_extra(mx, NULL, NULL);
  tsinspect_free(insp);
  srtout_close(srt);
  tssrc_close(src);
  return rc;
}

#define RECV_DEDUP_HISTORY 6 /* reconnect can redeliver several already-written chunks */

int dedup_is_duplicate(const dedup_entry_t *hist, int hist_n, uint32_t hash, int len) {
  for (int hi = 0; hi < hist_n; hi++) {
    if (hist[hi].len == len && hist[hi].hash == hash) return 1;
  }
  return 0;
}

void dedup_record(dedup_entry_t *hist, int hist_n, int *next, uint32_t hash, int len) {
  hist[*next].hash = hash;
  hist[*next].len = len;
  *next = (*next + 1) % hist_n;
}

static int run_receiver(const config_t *cfg, metrics_exporter_t *mx) {
  tssink_cfg_t tk;
  tssink_t *sink;
  srtin_cfg_t rcfg;
  srtin_t *srt;
  tsinspect_t *insp_in;
  tsinspect_t *insp_out;
  tsinspect_set_t set = {&insp_in, 1, &insp_out, 1};
  unsigned char buf[65536];
  dedup_entry_t dedup_hist[RECV_DEDUP_HISTORY] = {{0, 0}};
  int dedup_hist_next = 0;
  int dedup_active = 0; /* set on reconnect, cleared at first non-duplicate chunk */
  int rc = 0;

  plain_endpoint_to_tssink_cfg(&cfg->out.nonsrt, cfg->iface, &tk);
  sink = tssink_open(&tk);
  if (!sink) {
    return 1;
  }

  memset(&rcfg, 0, sizeof rcfg);
  for (int i = 0; i < cfg->in.n_srt; i++) {
    rcfg.peers[i].host = cfg->in.srt_host[i];
    rcfg.peers[i].port = cfg->in.srt_port[i];
  }
  rcfg.npeers = cfg->in.n_srt;
  rcfg.group_mode = cfg->group_mode;
  rcfg.listen = cfg->in.listen;
  rcfg.rendezvous = cfg->rendezvous;
  rcfg.local_host = cfg->local_host[0] ? cfg->local_host : NULL;
  rcfg.local_port = cfg->local_port;
  fill_common_opts(cfg, &rcfg.opts);
  rcfg.verbose = cfg->verbose;
  rcfg.mx = mx;
  rcfg.tool_version = TOOL_VERSION;

  srt = srtin_open(&rcfg);
  if (!srt) {
    tssink_close(sink);
    return signal_stop_requested() ? 0 : 1;
  }

  insp_in = tsinspect_new_relay(cfg->metrics_inspect_ts);
  insp_out = tsinspect_new_relay(cfg->metrics_inspect_ts);
  if (insp_in) metrics_exporter_set_extra(mx, tsinspect_set_put, &set);

  while (!signal_stop_requested()) {
    int reconnected = 0;
    int n = srtin_read(srt, buf, sizeof buf, &reconnected);
    uint32_t h;
    if (insp_in) {
      double now = mono_seconds();
      tsinspect_tick(insp_in, now);
      if (insp_out) tsinspect_tick(insp_out, now);
    }

    if (n < 0) {
      rc = 1;
      break;
    }
    if (reconnected) dedup_active = 1;
    if (n == 0) continue;
    if (insp_in) tsinspect_grid(insp_in, buf, (size_t)n);
    h = crc32_mpeg(buf, (size_t)n);
    if (dedup_active) {
      if (dedup_is_duplicate(dedup_hist, RECV_DEDUP_HISTORY, h, n)) continue;
      dedup_active = 0;
    }
    if (tssink_write(sink, buf, (size_t)n) < 0) {
      rc = 1;
      break;
    }
    if (insp_out) tsinspect_grid(insp_out, buf, (size_t)n);
    dedup_record(dedup_hist, RECV_DEDUP_HISTORY, &dedup_hist_next, h, n);
  }

  if (insp_in) metrics_exporter_set_extra(mx, NULL, NULL);
  tsinspect_free(insp_in);
  tsinspect_free(insp_out);
  srtin_close(srt);
  tssink_close(sink);
  return rc;
}

int bridge_run(const config_t *cfg, metrics_exporter_t *mx) {
  return config_is_sender(cfg) ? run_sender(cfg, mx) : run_receiver(cfg, mx);
}
