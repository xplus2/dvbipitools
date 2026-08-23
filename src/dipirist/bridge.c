/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <string.h>
#include <time.h>

#include <librist/librist.h>

#include "lib/ioutil.h"
#include "lib/log.h"
#include "lib/net/ristout.h"
#include "lib/net/tssink.h"
#include "lib/net/tssource.h"
#include "lib/signal.h"

#include "args.h"
#include "bridge.h"
#include "version.h"

#define RIST_READ_TIMEOUT_MS 200
#define DIPIRIST_SENDER_DRAIN_MS_DEFAULT 1000 /* librist's own default recovery buffer length */

enum rist_profile profile_of(rist_profile_sel_t p) {
  return p == RIST_PROF_MAIN ? RIST_PROFILE_MAIN : RIST_PROFILE_SIMPLE;
}

static int rist_log_cb(void *arg, enum rist_log_level level, const char *msg) {
  (void)arg;
  (void)level;
  log_line("rist: %s", msg);
  return 0;
}

/* NULL on failure; verbose gates INFO/DEBUG, otherwise WARN+ only */
static struct rist_logging_settings *open_logging(int verbose) {
  struct rist_logging_settings *ls = NULL;

  if (rist_logging_set(&ls, verbose ? RIST_LOG_DEBUG : RIST_LOG_WARN, rist_log_cb, NULL, NULL, NULL) != 0)
    return NULL;
  rist_logging_set_global(ls); /* udpsocket_* internals log via global settings, not ctx's */
  return ls;
}

void nonrist_to_tssrc_cfg(const nonrist_t *s, const char *iface, int insecure_tls, tssrc_cfg_t *tc) {
  memset(tc, 0, sizeof *tc);
  tc->user_agent = TOOL_NAME "/" TOOL_VERSION;
  switch (s->kind) {
  case NONRIST_HTTP:
    tc->kind = TSSRC_HTTP;
    tc->http = s->http;
    tc->insecure_tls = insecure_tls;
    break;
  case NONRIST_FILE:
    if (s->file_path[0]) {
      tc->kind = TSSRC_FILE;
      tc->file_path = s->file_path;
    } else {
      tc->kind = TSSRC_STDIN;
    }
    break;
  case NONRIST_RTP:
  case NONRIST_UDP:
    tc->kind = (s->kind == NONRIST_RTP) ? TSSRC_RTP : TSSRC_UDP;
    tc->family = s->family;
    tc->group = s->group;
    tc->port = s->port;
    tc->iface = iface;
    break;
  }
}

void nonrist_to_tssink_cfg(const nonrist_t *s, const char *iface, tssink_cfg_t *tk) {
  memset(tk, 0, sizeof *tk);
  if (s->kind == NONRIST_FILE) {
    if (s->file_path[0]) {
      tk->kind = TSSINK_FILE;
      tk->file_path = s->file_path;
    } else {
      tk->kind = TSSINK_STDOUT;
    }
  } else {
    tk->kind = (s->kind == NONRIST_RTP) ? TSSINK_RTP : TSSINK_UDP;
    tk->family = s->family;
    tk->group = s->group;
    tk->port = s->port;
    tk->iface = iface;
  }
}

#define RIST_STATS_INTERVAL_MS 1000 /* metrics_exporter_due() gates actual push cadence */

static int receiver_stats_cb(void *arg, const struct rist_stats *stats) {
  metrics_exporter_t *mx = arg;
  const struct rist_stats_receiver_flow *f = &stats->stats.receiver_flow;
  metrics_writer_t w;

  if (stats->stats_type == RIST_STATS_RECEIVER_FLOW && metrics_exporter_due(mx, mono_seconds()) && !metrics_exporter_begin(mx, &w, TOOL_VERSION)) {
    metrics_writer_put(&w, METRICS_ID_RIST_RECEIVER_RECEIVED_TOTAL, NULL, f->received);
    metrics_writer_put(&w, METRICS_ID_RIST_RECEIVER_MISSING_TOTAL, NULL, f->missing);
    metrics_writer_put(&w, METRICS_ID_RIST_RECEIVER_RECOVERED_TOTAL, NULL, f->recovered);
    metrics_writer_put(&w, METRICS_ID_RIST_RECEIVER_LOST_TOTAL, NULL, f->lost);
    metrics_writer_put(&w, METRICS_ID_RIST_RECEIVER_RTT_MILLISECONDS, NULL, f->rtt);
#ifdef DIPIRIST_HAVE_AVG_BUFFER_TIME
    metrics_writer_put(&w, METRICS_ID_RIST_RECEIVER_BUFFER_MILLISECONDS, NULL, f->avg_buffer_time);
#endif
    metrics_exporter_send(mx, &w);
  }
  rist_stats_free(stats);
  return 0;
}

/* one rist_peer_create() per e bonded URI, receiver side (always listens). --secret --cname
   --buffer override URI's own query params. 0 ok, -1 error */
static int add_peers(struct rist_ctx *ctx, const endpoint_t *e, const config_t *cfg) {
  for (int i = 0; i < e->n_rist; i++) {
    struct rist_peer_config *pc = NULL;
    struct rist_peer *peer;
    if (rist_parse_address2(e->rist_uri[i], &pc) != 0 || !pc) {
      log_line("rist: invalid peer url: %s", e->rist_uri[i]);
      return -1;
    }
    pc->initiate_conn = 0;
    if (cfg->secret[0])
      bufcpy(pc->secret, sizeof pc->secret, cfg->secret);
    if (cfg->cname[0])
      bufcpy(pc->cname, sizeof pc->cname, cfg->cname);
    if (cfg->buffer_ms) {
      pc->recovery_length_min = cfg->buffer_ms;
      pc->recovery_length_max = cfg->buffer_ms;
    }
    if (rist_peer_create(ctx, &peer, pc) != 0) {
      log_line("rist: failed to add peer: %s", e->rist_uri[i]);
      rist_peer_config_free2(&pc);
      return -1;
    }
    rist_peer_config_free2(&pc);
  }
  return 0;
}

static int run_sender(const config_t *cfg, metrics_exporter_t *mx) {
  tssrc_cfg_t tc;
  tssrc_t *src;
  ristout_cfg_t rcfg;
  ristout_t *rist;
  unsigned char buf[65536];
  int rc = 0;

  nonrist_to_tssrc_cfg(&cfg->in.nonrist, cfg->iface, cfg->insecure_tls, &tc);
  src = tssrc_open(&tc, NULL);
  if (!src)
    return 1;

  memset(&rcfg, 0, sizeof rcfg);
  for (int i = 0; i < cfg->out.n_rist; i++)
    rcfg.peer_uri[i] = cfg->out.rist_uri[i];
  rcfg.npeers = cfg->out.n_rist;
  rcfg.profile = cfg->profile == RIST_PROF_MAIN ? RISTOUT_PROFILE_MAIN : RISTOUT_PROFILE_SIMPLE;
  rcfg.secret = cfg->secret;
  rcfg.cname = cfg->cname;
  rcfg.buffer_ms = cfg->buffer_ms;
  rcfg.verbose = cfg->verbose;
  rcfg.mx = mx;
  rcfg.tool_version = TOOL_VERSION;

  rist = ristout_open(&rcfg);
  if (!rist) {
    tssrc_close(src);
    return 1;
  }

  {
    int rist_had_error = 0;

    while (!signal_stop_requested()) {
      net_err_reason_t reason = NET_ERR_OTHER;
      ssize_t n = tssrc_read(src, buf, sizeof buf, &reason);

      if (n < 0) {
        rc = 1;
        break;
      }
      if (n == 0)
        continue;
      if (ristout_write(rist, buf, (size_t)n) < 0) {
        if (!rist_had_error) {
          log_line("rist output: write failed, will keep retrying");
          rist_had_error = 1;
        }
      } else if (rist_had_error) {
        log_line("rist output: recovered");
        rist_had_error = 0;
      }
    }
  }
  if (rc) {
    /* eof/err, not live stop: drain arq retransmits before teardown */
    unsigned drain_ms = cfg->buffer_ms ? cfg->buffer_ms : DIPIRIST_SENDER_DRAIN_MS_DEFAULT;
    struct timespec ts = {(time_t)(drain_ms / 1000), (long)(drain_ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
  }
  ristout_close(rist);
  tssrc_close(src);
  return rc;
}

static int run_receiver(const config_t *cfg, metrics_exporter_t *mx) {
  tssink_cfg_t tk;
  tssink_t *sink;
  struct rist_ctx *ctx;
  struct rist_logging_settings *log_settings;
  int rc = 0;

  nonrist_to_tssink_cfg(&cfg->out.nonrist, cfg->iface, &tk);
  sink = tssink_open(&tk);
  if (!sink)
    return 1;

  log_settings = open_logging(cfg->verbose);
  if (rist_receiver_create(&ctx, profile_of(cfg->profile), log_settings) != 0) {
    log_line("rist: receiver create failed");
    if (log_settings)
      rist_logging_settings_free2(&log_settings);
    tssink_close(sink);
    return 1;
  }
  if (add_peers(ctx, &cfg->in, cfg) || rist_start(ctx) != 0) {
    rist_destroy(ctx);
    if (log_settings)
      rist_logging_settings_free2(&log_settings);
    tssink_close(sink);
    return 1;
  }
  rist_stats_callback_set(ctx, RIST_STATS_INTERVAL_MS, receiver_stats_cb, mx);

  while (!signal_stop_requested()) {
    struct rist_data_block *db = NULL;
    int ret = rist_receiver_data_read2(ctx, &db, RIST_READ_TIMEOUT_MS);

    if (ret < 0) {
      rc = 1;
      break;
    }
    if (ret == 0 || !db)
      continue;
    if (tssink_write(sink, db->payload, db->payload_len) < 0) {
      rc = 1;
      rist_receiver_data_block_free2(&db);
      break;
    }
    rist_receiver_data_block_free2(&db);
  }

  rist_destroy(ctx);
  if (log_settings)
    rist_logging_settings_free2(&log_settings);
  tssink_close(sink);
  return rc;
}

int bridge_run(const config_t *cfg, metrics_exporter_t *mx) {
  return config_is_sender(cfg) ? run_sender(cfg, mx) : run_receiver(cfg, mx);
}
