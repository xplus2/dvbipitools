/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <librist/librist.h>

#include "lib/ioutil.h"
#include "lib/log.h"
#include "lib/signal.h"

#include "ristin.h"
#include "ristlog.h"

#define RISTIN_READ_TIMEOUT_MS 200
#define RIST_STATS_INTERVAL_MS 1000 /* metrics_exporter_due() gates actual push cadence */

struct ristin {
  struct rist_ctx *ctx;
  int pfd[2];
  pthread_t thread;
  atomic_int stop;
  metrics_exporter_t *mx;
  const char *tool_version;
};

static int add_peer(struct rist_ctx *ctx, const ristin_cfg_t *cfg) {
  struct rist_peer_config *pc = NULL;
  struct rist_peer *peer;

  if (rist_parse_address2(cfg->peer_uri, &pc) != 0 || !pc) {
    log_line("rist: invalid peer url: %s", cfg->peer_uri);
    return -1;
  }
  pc->initiate_conn = 0; /* input always listens */
  if (cfg->secret && cfg->secret[0])
    bufcpy(pc->secret, sizeof pc->secret, cfg->secret);
  if (cfg->cname && cfg->cname[0])
    bufcpy(pc->cname, sizeof pc->cname, cfg->cname);
  if (cfg->buffer_ms) {
    pc->recovery_length_min = cfg->buffer_ms;
    pc->recovery_length_max = cfg->buffer_ms;
  }
  if (rist_peer_create(ctx, &peer, pc) != 0) {
    log_line("rist: failed to add peer: %s", cfg->peer_uri);
    rist_peer_config_free2(&pc);
    return -1;
  }
  rist_peer_config_free2(&pc);
  return 0;
}

static int receiver_stats_cb(void *arg, const struct rist_stats *stats) {
  ristin_t *r = arg;
  const struct rist_stats_receiver_flow *f = &stats->stats.receiver_flow;
  metrics_writer_t w;

  if (stats->stats_type == RIST_STATS_RECEIVER_FLOW && metrics_exporter_due(r->mx, mono_seconds()) &&
      !metrics_exporter_begin(r->mx, &w, r->tool_version)) {
    metrics_writer_put(&w, METRICS_ID_RIST_RECEIVER_RECEIVED_TOTAL, NULL, f->received);
    metrics_writer_put(&w, METRICS_ID_RIST_RECEIVER_MISSING_TOTAL, NULL, f->missing);
    metrics_writer_put(&w, METRICS_ID_RIST_RECEIVER_RECOVERED_TOTAL, NULL, f->recovered);
    metrics_writer_put(&w, METRICS_ID_RIST_RECEIVER_LOST_TOTAL, NULL, f->lost);
    metrics_writer_put(&w, METRICS_ID_RIST_RECEIVER_RTT_MILLISECONDS, NULL, f->rtt);
    metrics_exporter_send(r->mx, &w);
  }
  rist_stats_free(stats);
  return 0;
}

/* 0 written fully, -1 unrecoverable (reader gone or stopping) */
static int write_all(const ristin_t *r, const unsigned char *buf, size_t n) {
  while (n) {
    ssize_t w = write(r->pfd[1], buf, n);

    if (w > 0) {
      buf += w;
      n -= (size_t)w;
      continue;
    }
    if (w < 0 && errno == EINTR)
      continue;
    if (w < 0 && errno == EAGAIN) {
      struct pollfd pfd = {.fd = r->pfd[1], .events = POLLOUT};

      if (poll(&pfd, 1, 100) < 0 && errno != EINTR)
        return -1;
      if (atomic_load_explicit(&r->stop, memory_order_relaxed) || signal_stop_requested())
        return -1;
      continue;
    }
    return -1; /* EPIPE: reader gone */
  }
  return 0;
}

static void *reader_main(void *arg) {
  ristin_t *r = arg;

  while (!atomic_load_explicit(&r->stop, memory_order_relaxed) && !signal_stop_requested()) {
    struct rist_data_block *db = NULL;
    int ret = rist_receiver_data_read2(r->ctx, &db, RISTIN_READ_TIMEOUT_MS);

    if (ret < 0)
      break;
    if (ret == 0 || !db)
      continue;
    if (write_all(r, db->payload, db->payload_len) < 0) {
      rist_receiver_data_block_free2(&db);
      break;
    }
    rist_receiver_data_block_free2(&db);
  }
  close(r->pfd[1]); /* next raw_fd_read() on pfd[0] sees EOF, stop or error alike */
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
  if (!r)
    return NULL;
  r->mx = cfg->mx;
  r->tool_version = cfg->tool_version;

  if (pipe(r->pfd) < 0) {
    log_line("rist: pipe() failed: %s", strerror(errno));
    free(r);
    return NULL;
  }
  if (fcntl(r->pfd[1], F_SETFL, O_NONBLOCK) < 0) {
    log_line("rist: fcntl(O_NONBLOCK) failed: %s", strerror(errno));
    close(r->pfd[0]);
    close(r->pfd[1]);
    free(r);
    return NULL;
  }

  if (rist_receiver_create(&r->ctx, profile, ristlog_get(cfg->verbose)) != 0) {
    log_line("rist: receiver create failed");
    close(r->pfd[0]);
    close(r->pfd[1]);
    free(r);
    return NULL;
  }
  if (add_peer(r->ctx, cfg) || rist_start(r->ctx) != 0) {
    rist_destroy(r->ctx);
    close(r->pfd[0]);
    close(r->pfd[1]);
    free(r);
    return NULL;
  }
  if (r->mx)
    rist_stats_callback_set(r->ctx, RIST_STATS_INTERVAL_MS, receiver_stats_cb, r);

  atomic_init(&r->stop, 0);
  if (pthread_create(&r->thread, NULL, reader_main, r) != 0) {
    log_line("rist: pthread_create failed: %s", strerror(errno));
    rist_destroy(r->ctx);
    close(r->pfd[0]);
    close(r->pfd[1]);
    free(r);
    return NULL;
  }
  return r;
}

int ristin_fd(const ristin_t *r) { return r->pfd[0]; }

void ristin_close(ristin_t *r) {
  if (!r)
    return;
  atomic_store_explicit(&r->stop, 1, memory_order_relaxed);
  pthread_join(r->thread, NULL);
  rist_destroy(r->ctx);
  close(r->pfd[0]); /* pfd[1] already closed by reader_main on exit */
  free(r);
}
