/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <librist/librist.h>

#include "lib/sys/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/pipereader.h"
#include "lib/sys/signal.h"
#include "ristin.h"
#include "ristin_priv.h"
#include "ristlog.h"
#include "ristpeer.h"

#define RISTIN_READ_TIMEOUT_MS 200
#define RIST_STATS_INTERVAL_MS 1000 /* metrics_exporter_due() gates actual push cadence */

int receiver_stats_cb(void *arg, const struct rist_stats *stats) {
  ristin_t *r = arg;
  const struct rist_stats_receiver_flow *f = &stats->stats.receiver_flow;
  if (stats->stats_type != RIST_STATS_RECEIVER_FLOW) {
    rist_stats_free(stats);
    return 0;
  }
  metrics_entry_t e[] = {
  {METRICS_ID_RIST_RECEIVER_RECEIVED_TOTAL, NULL, f->received},
  {METRICS_ID_RIST_RECEIVER_MISSING_TOTAL, NULL, f->missing},
  {METRICS_ID_RIST_RECEIVER_RECOVERED_TOTAL, NULL, f->recovered},
  {METRICS_ID_RIST_RECEIVER_LOST_TOTAL, NULL, f->lost},
  {METRICS_ID_RIST_RECEIVER_RTT_MILLISECONDS, NULL, f->rtt},
  };
  rist_push_stats_if_due(r->mx, r->tool_version, e, sizeof e / sizeof e[0], stats);
  return 0;
}

static struct rist_ctx *ctx_start(ristin_t *r) {
  struct rist_ctx *ctx;
  enum rist_profile profile = r->simple ? RIST_PROFILE_SIMPLE : RIST_PROFILE_MAIN;

  if (rist_receiver_create(&ctx, profile, ristlog_get(r->verbose)) != 0) {
    log_line("rist: receiver create failed");
    return NULL;
  }
  if (rist_add_peer(ctx, r->peer_uri, r->secret, r->key_size, r->cname, r->buffer_ms, 0, r->simple) || rist_start(ctx) != 0) {
    rist_destroy(ctx);
    return NULL;
  }
  if (r->mx) rist_stats_callback_set(ctx, RIST_STATS_INTERVAL_MS, receiver_stats_cb, r);
  return ctx;
}

/* stale flow lingers to session timeout, restart collides */
static int ctx_restart(ristin_t *r) {
  struct rist_ctx *ctx;

  rist_destroy(r->ctx);
  r->ctx = NULL;
  ctx = ctx_start(r);
  if (!ctx) return -1;
  r->ctx = ctx;
  return 0;
}

static void *reader_main(void *arg) {
  ristin_t *r = arg;
  unsigned idle_ms = 0;
  unsigned reset_ms = 2 * r->buffer_ms + RISTIN_READ_TIMEOUT_MS;
  int got = 0;

  while (!atomic_load_explicit(&r->io.stop, memory_order_relaxed) && !signal_stop_requested()) {
    struct rist_data_block *db = NULL;
    int ret = rist_receiver_data_read2(r->ctx, &db, RISTIN_READ_TIMEOUT_MS);
    if (ret < 0) break;
    if (ret == 0 || !db) {
      idle_ms += RISTIN_READ_TIMEOUT_MS;
      if (got && r->buffer_ms && idle_ms >= reset_ms) {
        if (ctx_restart(r) != 0) break;
        got = 0;
      }
      continue;
    }
    idle_ms = 0;
    got = 1;
    if (pipe_write_all(r->io.pfd[1], db->payload, db->payload_len, &r->io.stop) < 0) {
      rist_receiver_data_block_free2(&db);
      break;
    }
    rist_receiver_data_block_free2(&db);
  }
  close(r->io.pfd[1]); /* next raw_fd_read() on pfd[0] sees EOF, stop or error alike */
  return NULL;
}

ristin_t *ristin_open(const ristin_cfg_t *cfg) {
  ristin_t *r;

  if (strncmp(cfg->peer_uri, "rist://", 7) != 0 || cfg->peer_uri[7] != '@') {
    log_line("rist: input uri must be rist://@host:port (listen)");
    return NULL;
  }

  r = calloc(1, sizeof *r);
  if (!r) return NULL;
  r->mx = cfg->mx;
  r->tool_version = cfg->tool_version;
  bufcpy(r->peer_uri, sizeof r->peer_uri, cfg->peer_uri);
  if (cfg->secret) bufcpy(r->secret, sizeof r->secret, cfg->secret);
  if (cfg->cname) bufcpy(r->cname, sizeof r->cname, cfg->cname);
  r->key_size = cfg->key_size;
  r->buffer_ms = cfg->buffer_ms;
  r->simple = cfg->profile != RISTIN_PROFILE_MAIN;
  r->verbose = cfg->verbose;
  r->ctx = ctx_start(r);
  if (!r->ctx) {
    free(r);
    return NULL;
  }
  if (pipereader_start(&r->io, reader_main, r) != 0) {
    log_line("rist: pipe/thread setup failed");
    rist_destroy(r->ctx);
    free(r);
    return NULL;
  }
  return r;
}

int ristin_fd(const ristin_t *r) { return pipereader_fd(&r->io); }

void ristin_close(ristin_t *r) {
  if (!r) return;
  pipereader_stop(&r->io);
  if (r->ctx) rist_destroy(r->ctx);
  free(r);
}
