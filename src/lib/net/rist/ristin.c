/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <librist/librist.h>

#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"
#include "lib/helper/pipereader.h"
#include "lib/helper/signal.h"
#include "ristin.h"
#include "ristlog.h"
#include "ristpeer.h"

#define RISTIN_READ_TIMEOUT_MS 200
#define RIST_STATS_INTERVAL_MS 1000 /* metrics_exporter_due() gates actual push cadence */

struct ristin {
  struct rist_ctx *ctx;
  pipereader_t io;
  metrics_exporter_t *mx;
  const char *tool_version;
};

static int receiver_stats_cb(void *arg, const struct rist_stats *stats) {
  ristin_t *r = arg;
  const struct rist_stats_receiver_flow *f = &stats->stats.receiver_flow;
  if (stats->stats_type != RIST_STATS_RECEIVER_FLOW) {
    rist_stats_free(stats);
    return 0;
  }
  {
    metrics_entry_t e[] = {
        {METRICS_ID_RIST_RECEIVER_RECEIVED_TOTAL, NULL, f->received},
        {METRICS_ID_RIST_RECEIVER_MISSING_TOTAL, NULL, f->missing},
        {METRICS_ID_RIST_RECEIVER_RECOVERED_TOTAL, NULL, f->recovered},
        {METRICS_ID_RIST_RECEIVER_LOST_TOTAL, NULL, f->lost},
        {METRICS_ID_RIST_RECEIVER_RTT_MILLISECONDS, NULL, f->rtt},
    };
    return rist_push_stats_if_due(r->mx, r->tool_version, e, sizeof e / sizeof e[0], stats);
  }
}

static void *reader_main(void *arg) {
  ristin_t *r = arg;

  while (!atomic_load_explicit(&r->io.stop, memory_order_relaxed) && !signal_stop_requested()) {
    struct rist_data_block *db = NULL;
    int ret = rist_receiver_data_read2(r->ctx, &db, RISTIN_READ_TIMEOUT_MS);
    if (ret < 0) break;
    if (ret == 0 || !db) continue;
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
  enum rist_profile profile = cfg->profile == RISTIN_PROFILE_MAIN ? RIST_PROFILE_MAIN : RIST_PROFILE_SIMPLE;

  if (strncmp(cfg->peer_uri, "rist://", 7) != 0 || cfg->peer_uri[7] != '@') {
    log_line("rist: input uri must be rist://@host:port (listen)");
    return NULL;
  }

  r = calloc(1, sizeof *r);
  if (!r) return NULL;
  r->mx = cfg->mx;
  r->tool_version = cfg->tool_version;
  if (rist_receiver_create(&r->ctx, profile, ristlog_get(cfg->verbose)) != 0) {
    log_line("rist: receiver create failed");
    free(r);
    return NULL;
  }
  if (rist_add_peer(r->ctx, cfg->peer_uri, cfg->secret, cfg->cname, cfg->buffer_ms, 0) || rist_start(r->ctx) != 0) {
    rist_destroy(r->ctx);
    free(r);
    return NULL;
  }
  if (r->mx) rist_stats_callback_set(r->ctx, RIST_STATS_INTERVAL_MS, receiver_stats_cb, r);
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
  rist_destroy(r->ctx);
  free(r);
}
