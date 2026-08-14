/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>

#include <librist/librist.h>

#include "lib/ioutil.h"
#include "lib/log.h"
#include "lib/signal.h"

#include "ristout.h"

#define RIST_CHUNK (7 * 188) /* librist caps a single data_block well under 64K (observed max ~9968B) */
#define RIST_STATS_INTERVAL_MS 1000 /* metrics_exporter_due() gates actual push cadence */

struct ristout {
  struct rist_ctx *ctx;
  struct rist_logging_settings *log_settings;
  metrics_exporter_t *mx;
  const char *tool_version;
};

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

static int add_peers(struct rist_ctx *ctx, const ristout_cfg_t *cfg) {
  int i;

  for (i = 0; i < cfg->npeers; i++) {
    struct rist_peer_config *pc = NULL;
    struct rist_peer *peer;

    if (rist_parse_address2(cfg->peer_uri[i], &pc) != 0 || !pc) {
      log_line("rist: invalid peer url: %s", cfg->peer_uri[i]);
      return -1;
    }
    pc->initiate_conn = 1; /* sender always calls out */
    if (cfg->secret && cfg->secret[0])
      bufcpy(pc->secret, sizeof pc->secret, cfg->secret);
    if (cfg->cname && cfg->cname[0])
      bufcpy(pc->cname, sizeof pc->cname, cfg->cname);
    if (cfg->buffer_ms) {
      pc->recovery_length_min = cfg->buffer_ms;
      pc->recovery_length_max = cfg->buffer_ms;
    }
    if (rist_peer_create(ctx, &peer, pc) != 0) {
      log_line("rist: failed to add peer: %s", cfg->peer_uri[i]);
      rist_peer_config_free2(&pc);
      return -1;
    }
    rist_peer_config_free2(&pc);
  }
  return 0;
}

static int sender_stats_cb(void *arg, const struct rist_stats *stats) {
  ristout_t *r = arg;
  const struct rist_stats_sender_peer *p = &stats->stats.sender_peer;
  metrics_writer_t w;

  if (stats->stats_type == RIST_STATS_SENDER_PEER && metrics_exporter_due(r->mx, mono_seconds()) &&
      !metrics_exporter_begin(r->mx, &w, r->tool_version)) {
    metrics_writer_put(&w, METRICS_ID_RIST_SENDER_SENT_TOTAL, p->cname, p->sent);
    metrics_writer_put(&w, METRICS_ID_RIST_SENDER_RETRANSMITTED_TOTAL, p->cname, p->retransmitted);
    metrics_writer_put(&w, METRICS_ID_RIST_SENDER_RTT_MILLISECONDS, p->cname, p->rtt);
    metrics_exporter_send(r->mx, &w);
  }
  rist_stats_free(stats);
  return 0;
}

ristout_t *ristout_open(const ristout_cfg_t *cfg) {
  ristout_t *r;
  enum rist_profile profile = cfg->profile == RISTOUT_PROFILE_MAIN ? RIST_PROFILE_MAIN : RIST_PROFILE_SIMPLE;

  if (cfg->npeers <= 0 || cfg->npeers > RISTOUT_MAX_PEERS) {
    log_line("rist: invalid peer count %d", cfg->npeers);
    return NULL;
  }

  r = calloc(1, sizeof *r);
  if (!r)
    return NULL;
  r->mx = cfg->mx;
  r->tool_version = cfg->tool_version;

  r->log_settings = open_logging(cfg->verbose);
  if (rist_sender_create(&r->ctx, profile, 0, r->log_settings) != 0) {
    log_line("rist: sender create failed");
    if (r->log_settings)
      rist_logging_settings_free2(&r->log_settings);
    free(r);
    return NULL;
  }
  if (add_peers(r->ctx, cfg) || rist_start(r->ctx) != 0) {
    rist_destroy(r->ctx);
    if (r->log_settings)
      rist_logging_settings_free2(&r->log_settings);
    free(r);
    return NULL;
  }
  if (r->mx)
    rist_stats_callback_set(r->ctx, RIST_STATS_INTERVAL_MS, sender_stats_cb, r);
  return r;
}

int ristout_write(ristout_t *r, const unsigned char *buf, size_t n) {
  size_t off;

  for (off = 0; off < n; off += RIST_CHUNK) {
    struct rist_data_block db;
    size_t chunk = n - off < RIST_CHUNK ? n - off : RIST_CHUNK;

    memset(&db, 0, sizeof db);
    db.payload = buf + off;
    db.payload_len = chunk;
    if (rist_sender_data_write(r->ctx, &db) < 0) {
      log_line("rist: write failed");
      return -1;
    }
  }
  return 0;
}

void ristout_close(ristout_t *r) {
  if (!r)
    return;
  rist_destroy(r->ctx);
  if (r->log_settings)
    rist_logging_settings_free2(&r->log_settings);
  free(r);
}
