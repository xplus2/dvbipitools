/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <poll.h>
#include <pthread.h>
#include <string.h>

#include <librist/librist.h>

#include "lib/demux/psi/psi.h"
#include "lib/demux/tspack.h"
#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/net/rist/ristout.h"
#include "lib/net/rist/ristpeer.h"
#include "lib/net/send_result.h"
#include "lib/net/ts/pace.h"
#include "lib/net/ts/sink.h"
#include "lib/net/ts/source.h"
#include "lib/tsinspect/inspect.h"
#include "lib/sys/signal.h"

#include "cli/args.h"
#include "bridge.h"
#include "version.h"

enum rist_profile profile_of(rist_profile_sel_t p) {
  return p == RIST_PROF_MAIN ? RIST_PROFILE_MAIN : RIST_PROFILE_SIMPLE;
}

#define FLOW_STATS_FRESH_S 3.0

/* exporter is not thread safe: stats cb thread vs main loop */
static pthread_mutex_t push_lock = PTHREAD_MUTEX_INITIALIZER;
static double flow_stats_mono;

int bridge_receiver_stats_cb(void *arg, const struct rist_stats *stats) {
  metrics_exporter_t *mx = arg;
  metrics_entry_t e[RIST_RECEIVER_FLOW_ENTRIES];
  size_t n = ristpeer_receiver_flow_entries(stats, e);

  if (n) {
    double now = mono_seconds();
    pthread_mutex_lock(&push_lock);
    flow_stats_mono = now;
    if (metrics_exporter_due(mx, now)) metrics_push_entries(mx, TOOL_VERSION, e, n);
    pthread_mutex_unlock(&push_lock);
  }
  rist_stats_free(stats);
  return 0;
}

void bridge_receiver_idle_push(metrics_exporter_t *mx, double now_mono) {
  metrics_writer_t w;
  pthread_mutex_lock(&push_lock);
  if ((flow_stats_mono == 0.0 || now_mono - flow_stats_mono >= FLOW_STATS_FRESH_S) && metrics_exporter_due(mx, now_mono) &&
      !metrics_exporter_begin(mx, &w, TOOL_VERSION))
    metrics_exporter_send(mx, &w);
  pthread_mutex_unlock(&push_lock);
}

#define SRC_OPEN_TIMEOUT_S 30.0
#define HTTP_IDLE_S 30.0
#define RECONNECT_MIN_S 1.0
#define RECONNECT_MAX_S 10.0

static tssrc_t *open_source(const tssrc_cfg_t *tc, metrics_exporter_t *mx) {
  net_err_reason_t reason = NET_ERR_OTHER;
  tssrc_open_t *o = tssrc_open_async_start(tc, &reason);
  double deadline = mono_seconds() + SRC_OPEN_TIMEOUT_S;
  if (!o) {
    metrics_exporter_note_error(mx, reason);
    return NULL;
  }
  while (!signal_stop_requested()) {
    tssrc_open_state_t st = tssrc_open_async_step(o, &reason);
    if (st == TSSRC_OPEN_DONE) return tssrc_open_async_take(o);
    if (st == TSSRC_OPEN_ERROR) {
      metrics_exporter_note_error(mx, reason);
      break;
    }
    if (mono_seconds() >= deadline) {
      log_code("VS600164", "http source: no response within %.0fs", SRC_OPEN_TIMEOUT_S);
      metrics_exporter_note_error(mx, NET_ERR_TIMEOUT);
      break;
    }
    struct pollfd pfd = {tssrc_open_async_poll_fd(o), tssrc_open_async_poll_events(o), 0};
    poll(&pfd, 1, 100);
  }
  tssrc_open_async_free(o);
  return NULL;
}

#define PACED_READ_CAP (7 * 188)

typedef struct {
  psi_t *psi;
  pace_ctrl_t *pace;
} pace_ctx_t;

static int pace_pkt_cb(void *v, const unsigned char *pkt) {
  pace_ctx_t *c = v;
  pace_feed_pcr_pkt(c->pace, pkt, psi_pcr_pid(c->psi));
  psi_feed(c->psi, pkt);
  return 0;
}

typedef struct {
  tssrc_open_t *op;
  double retry_at;
  double deadline;
  double backoff;
} reconn_t;

static void reconn_failed(reconn_t *r, metrics_exporter_t *mx, net_err_reason_t why, double now) {
  metrics_exporter_note_error(mx, why);
  r->retry_at = now + r->backoff;
  r->backoff *= 2;
  if (r->backoff > RECONNECT_MAX_S) r->backoff = RECONNECT_MAX_S;
}

/* one nonblocking step, source once connected else NULL */
static tssrc_t *reconn_step(reconn_t *r, const tssrc_cfg_t *tc, metrics_exporter_t *mx, double now) {
  net_err_reason_t why = NET_ERR_OTHER;
  tssrc_open_state_t st;

  if (!r->op) {
    if (now < r->retry_at) return NULL;
    r->op = tssrc_open_async_start(tc, &why);
    if (!r->op) {
      reconn_failed(r, mx, why, now);
      return NULL;
    }
    r->deadline = now + SRC_OPEN_TIMEOUT_S;
  }
  st = tssrc_open_async_step(r->op, &why);
  if (st == TSSRC_OPEN_DONE) {
    tssrc_t *src = tssrc_open_async_take(r->op);
    r->op = NULL;
    r->backoff = RECONNECT_MIN_S;
    return src;
  }
  if (st == TSSRC_OPEN_PENDING && now < r->deadline) return NULL;
  if (st == TSSRC_OPEN_PENDING) why = NET_ERR_TIMEOUT;
  tssrc_open_async_free(r->op);
  r->op = NULL;
  reconn_failed(r, mx, why, now);
  return NULL;
}

static int run_sender(const config_t *cfg, metrics_exporter_t *mx) {
  tssrc_cfg_t tc;
  tssrc_t *src;
  ristout_cfg_t rcfg;
  ristout_t *rist;
  tsinspect_t *insp = tsinspect_new_relay(cfg->metrics_inspect_ts);
  tsinspect_set_t set = {&insp, 1, NULL, 0};
  unsigned char buf[65536];
  int rc = 0;
  int rist_had_error = 0;
  pace_ctx_t pc = {NULL, NULL};
  tspack_t pz = {{0}, 0, 0};
  size_t read_cap = sizeof buf;
  reconn_t rcn = {NULL, 0.0, 0.0, RECONNECT_MIN_S};
  double last_data;

  plain_endpoint_to_tssrc_cfg(&cfg->in.nonrist, cfg->iface, TOOL_NAME "/" TOOL_VERSION, cfg->insecure_tls, &tc);
  src = open_source(&tc, mx);
  if (!src) {
    tsinspect_free(insp);
    return 1;
  }
  if (cfg->pace) {
    pc.pace = pace_new();
    pc.psi = psi_new();
    if (!pc.pace || !pc.psi) {
      log_code("VS500101", "out of memory");
      goto fail;
    }
    read_cap = PACED_READ_CAP;
  }
  if (insp) metrics_exporter_set_extra(mx, tsinspect_set_put, &set);
  if (tsinspect_wants_rx_ns(insp)) tssrc_enable_rx_timestamps(src);

  memset(&rcfg, 0, sizeof rcfg);
  for (int i = 0; i < cfg->out.n_rist; i++)
    rcfg.peer_uri[i] = cfg->out.rist_uri[i];
  rcfg.npeers = cfg->out.n_rist;
  rcfg.profile = cfg->profile == RIST_PROF_MAIN ? RISTOUT_PROFILE_MAIN : RISTOUT_PROFILE_SIMPLE;
  rcfg.secret = cfg->secret;
  rcfg.key_size = cfg->key_size;
  rcfg.cname = cfg->cname;
  rcfg.buffer_ms = cfg->buffer_ms;
  rcfg.verbose = cfg->verbose;
  rcfg.mx = mx;
  rcfg.tool_version = TOOL_VERSION;

  rist = ristout_open(&rcfg);
  if (!rist) {
    if (insp) metrics_exporter_set_extra(mx, NULL, NULL);
    goto fail;
  }

  last_data = mono_seconds();
  while (!signal_stop_requested()) {
    net_err_reason_t reason = NET_ERR_OTHER;
    ssize_t n;
    double now;

    if (!src) {
      struct pollfd pfd = {-1, 0, 0};
      now = mono_seconds();
      if (insp) tsinspect_tick(insp, now);
      ristout_idle_push(rist, now);
      src = reconn_step(&rcn, &tc, mx, now);
      if (src) {
        log_line("http source: reconnected");
        pz = (tspack_t){{0}, 0, 0};
        last_data = now;
        continue;
      }
      if (rcn.op) {
        pfd.fd = tssrc_open_async_poll_fd(rcn.op);
        pfd.events = tssrc_open_async_poll_events(rcn.op);
      }
      poll(&pfd, 1, 100);
      continue;
    }
    n = tssrc_read(src, buf, read_cap, &reason);
    now = mono_seconds();
    if (insp) tsinspect_tick(insp, now);
    ristout_idle_push(rist, now);
    if (n > 0) last_data = now;
    if (n >= 0 && tc.kind == TSSRC_HTTP && now - last_data >= HTTP_IDLE_S) {
      n = -1;
      reason = NET_ERR_TIMEOUT;
    }
    if (n < 0) {
      if (tc.kind == TSSRC_HTTP && !signal_stop_requested()) {
        log_code("VW600163", "http source lost: %s, reconnecting", net_err_reason_name(reason));
        metrics_exporter_note_error(mx, reason);
        tssrc_close(src);
        src = NULL;
        rcn.retry_at = now + rcn.backoff;
        continue;
      }
      metrics_exporter_note_error(mx, reason);
      rc = 1;
      break;
    }
    if (n == 0) {
      if (tssrc_wait(src, 50) < 0) {
        metrics_exporter_note_error(mx, NET_ERR_OTHER);
        rc = 1;
        break;
      }
      continue;
    }
    if (pc.pace) {
      if (tssrc_is_rtp_framed(src)) pace_feed_rtp_ts(pc.pace, tssrc_last_rtp_ts(src));
      else tspack_feed(&pz, buf, (size_t)n, pace_pkt_cb, &pc);
    }
    if (insp) {
      tsinspect_set_rx_ns(insp, tssrc_last_rx_ns(src));
      tsinspect_grid(insp, buf, (size_t)n);
    }
    if (ristout_write(rist, buf, (size_t)n) < 0) {
      metrics_exporter_note_error(mx, NET_ERR_OTHER);
      if (!rist_had_error) {
        log_code("VW100107", "rist output: write failed, will keep retrying");
        rist_had_error = 1;
      }
    } else if (rist_had_error) {
      log_line("rist output: recovered");
      rist_had_error = 0;
    }
  }
  ristout_close(rist);
  tssrc_open_async_free(rcn.op);
  if (insp) metrics_exporter_set_extra(mx, NULL, NULL);
  tsinspect_free(insp);
  tssrc_close(src);
  psi_free(pc.psi);
  pace_free(pc.pace);
  return rc;

fail:
  tsinspect_free(insp);
  tssrc_close(src);
  psi_free(pc.psi);
  pace_free(pc.pace);
  return 1;
}

/* --secret --encryption-type --cname --buffer override URI's own query params */
static struct rist_ctx *receiver_start(const config_t *cfg, metrics_exporter_t *mx) {
  const char *uris[DIPIRIST_MAX_PEERS];
  rist_recv_cfg_t rc;

  memset(&rc, 0, sizeof rc);
  for (int i = 0; i < cfg->in.n_rist; i++)
    uris[i] = cfg->in.rist_uri[i];
  rc.uris = uris;
  rc.n = (unsigned)cfg->in.n_rist;
  rc.simple = cfg->profile == RIST_PROF_SIMPLE;
  rc.secret = cfg->secret;
  rc.key_size = cfg->key_size;
  rc.cname = cfg->cname;
  rc.buffer_ms = cfg->buffer_ms;
  rc.verbose = cfg->verbose;
  rc.stats_cb = bridge_receiver_stats_cb;
  rc.stats_arg = mx;
  return rist_receiver_open(&rc);
}

static int run_receiver(const config_t *cfg, metrics_exporter_t *mx) {
  tssink_cfg_t tk;
  tssink_t *sink;
  struct rist_ctx *ctx;
  tsinspect_t *insp = tsinspect_new_relay(cfg->metrics_inspect_ts);
  tsinspect_set_t set = {&insp, 1, NULL, 0};
  int net_had_error = 0;
  unsigned long long net_errors = 0;
  int net_retry;
  int rc = 0;
  ristpeer_flow_watch_t watch;

  plain_endpoint_to_tssink_cfg(&cfg->out.nonrist, cfg->iface, &tk);
  tk.ttl = cfg->out_ttl;
  tk.tos = cfg->out_tos;
  tk.pack = 1;
  net_retry = tk.kind == TSSINK_UDP || tk.kind == TSSINK_RTP;
  sink = tssink_open(&tk);
  if (!sink) {
    tsinspect_free(insp);
    return 1;
  }
  ctx = receiver_start(cfg, mx);
  if (!ctx) {
    tssink_close(sink);
    tsinspect_free(insp);
    return 1;
  }
  if (insp) metrics_exporter_set_extra(mx, tsinspect_set_put, &set);
  ristpeer_flow_watch_init(&watch, cfg->buffer_ms);

  while (!signal_stop_requested()) {
    struct rist_data_block *db = NULL;
    int wr;
    int ret = rist_receiver_data_read2(ctx, &db, RIST_READ_TIMEOUT_MS);
    double now = mono_seconds();
    if (insp) tsinspect_tick(insp, now);
    bridge_receiver_idle_push(mx, now);
    if (ret < 0) {
      rc = 1;
      break;
    }
    if (ret == 0 || !db) {
      tssink_flush(sink);
      if (ristpeer_flow_watch_idle(&watch)) {
        /* stale flow lingers to session timeout, restart collides */
        rist_destroy(ctx);
        ctx = receiver_start(cfg, mx);
        if (!ctx) {
          log_code("VE500242", "rist: receiver restart failed");
          rc = 1;
          break;
        }
      }
      continue;
    }
    if (!ristpeer_flow_watch_data(&watch, db->flow_id)) {
      rist_receiver_data_block_free2(&db);
      continue;
    }
    if (insp) tsinspect_grid(insp, db->payload, db->payload_len);
    wr = tssink_write(sink, db->payload, db->payload_len);
    if (wr < 0) metrics_exporter_note_error(mx, NET_ERR_OTHER);
    if (wr < 0 && !net_retry) {
      rc = 1;
      rist_receiver_data_block_free2(&db);
      break;
    }
    note_send_result(wr >= 0, &net_had_error, &net_errors, "net");
    rist_receiver_data_block_free2(&db);
  }

  if (ctx) rist_destroy(ctx);
  if (insp) metrics_exporter_set_extra(mx, NULL, NULL);
  tsinspect_free(insp);
  tssink_close(sink);
  return rc;
}

int bridge_run(const config_t *cfg, metrics_exporter_t *mx) {
  return config_is_sender(cfg) ? run_sender(cfg, mx) : run_receiver(cfg, mx);
}
