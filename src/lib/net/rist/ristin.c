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
#include "ristpeer.h"

int ristin_stats_cb(void *arg, const struct rist_stats *stats) {
  ristin_t *r = arg;
  metrics_entry_t e[RIST_RECEIVER_FLOW_ENTRIES];
  size_t n = ristpeer_receiver_flow_entries(stats, e);

  if (n) ristpeer_push_stats_if_due(r->mx, r->tool_version, e, n, stats);
  else rist_stats_free(stats);
  return 0;
}

static struct rist_ctx *ctx_start(ristin_t *r) {
  const char *uris[1] = {r->peer_uri};
  rist_recv_cfg_t rc;

  memset(&rc, 0, sizeof rc);
  rc.uris = uris;
  rc.n = 1;
  rc.simple = r->simple;
  rc.secret = r->secret;
  rc.key_size = r->key_size;
  rc.cname = r->cname;
  rc.buffer_ms = r->buffer_ms;
  rc.verbose = r->verbose;
  if (r->mx) {
    rc.stats_cb = &ristin_stats_cb;
    rc.stats_arg = r;
  }
  return rist_receiver_open(&rc);
}

/* stale flow lingers to session timeout, restart collides */
static int ctx_restart(ristin_t *r) {
  struct rist_ctx *ctx;

  rist_destroy(r->ctx);
  r->ctx = NULL;
  ctx = ctx_start(r);
  if (!ctx) {
    log_code("LE500242", "rist: receiver restart failed");
    return -1;
  }
  r->ctx = ctx;
  return 0;
}

static void *reader_main(void *arg) {
  ristin_t *r = arg;
  ristpeer_flow_watch_t watch;

  ristpeer_flow_watch_init(&watch, r->buffer_ms);

  while (!atomic_load_explicit(&r->io.stop, memory_order_relaxed) && !signal_stop_requested()) {
    struct rist_data_block *db = NULL;
    int ret = rist_receiver_data_read2(r->ctx, &db, RIST_READ_TIMEOUT_MS);
    if (ret < 0) break;
    if (ret == 0 || !db) {
      if (ristpeer_flow_watch_idle(&watch) && ctx_restart(r) != 0) break;
      continue;
    }
    if (!ristpeer_flow_watch_data(&watch, db->flow_id)) {
      rist_receiver_data_block_free2(&db);
      continue;
    }
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
    log_code("LE000317", "rist: input uri must be rist://@host:port (listen)");
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
    log_code("LE500154", "rist: pipe/thread setup failed");
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
