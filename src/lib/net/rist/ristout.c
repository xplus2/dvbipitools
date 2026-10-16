/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <librist/librist.h>

#include "lib/helper/log.h"
#include "lib/sys/ioutil.h"
#include "lib/sys/signal.h"
#include "ristlog.h"
#include "ristout.h"
#include "ristpeer.h"

#define RIST_CHUNK (7 * 188) /* librist caps a single data_block well under 64K (observed max ~9968B) */
#define RIST_STATS_INTERVAL_MS 1000 /* metrics_exporter_due() gates actual push cadence */
#define RIST_DRAIN_MS_DEFAULT 1000 /* librist default recovery length */
#define RIST_STATS_FRESH_S 3.0

typedef struct {
  uint32_t peer_id;
  char cname[METRICS_LABEL_MAX + 1];
  uint64_t sent;
  uint64_t retransmitted;
  uint64_t rtt;
} peer_stat_t;

struct ristout {
  struct rist_ctx *ctx;
  metrics_exporter_t *mx;
  const char *tool_version;
  unsigned drain_ms;
  int wrote;
  int warned;
  atomic_int connected; /* up peers, mirrors up[] */
  struct rist_peer *up[RISTOUT_MAX_PEERS]; /* guarded by push_lock */
  pthread_mutex_t push_lock; /* exporter not thread safe: stats cb vs caller */
  double stats_mono;
  int npeers;
  int nstats;
  peer_stat_t stats[RISTOUT_MAX_PEERS]; /* one per peer, guarded by push_lock */
};

static int add_peers(struct rist_ctx *ctx, const ristout_cfg_t *cfg) {
  for (int i = 0; i < cfg->npeers; i++) {
    if (ristpeer_add(ctx, cfg->peer_uri[i], cfg->secret, cfg->key_size, cfg->cname, cfg->buffer_ms, 1, 0))
      return -1;
  }
  return 0;
}

static void sender_conn_cb(void *arg, struct rist_peer *peer, enum rist_connection_status st) {
  ristout_t *r = arg;
  int up = st == RIST_CONNECTION_ESTABLISHED || st == RIST_CLIENT_CONNECTED;
  int n = 0;

  pthread_mutex_lock(&r->push_lock);
  for (int i = 0; i < RISTOUT_MAX_PEERS; i++) {
    if (r->up[i] == peer) r->up[i] = NULL;
    if (up && !r->up[i] && peer) {
      r->up[i] = peer;
      peer = NULL;
    }
    if (r->up[i]) n++;
  }
  atomic_store(&r->connected, n);
  pthread_mutex_unlock(&r->push_lock);
}

static peer_stat_t *stat_slot(ristout_t *r, uint32_t peer_id) {
  for (int i = 0; i < r->nstats; i++) {
    if (r->stats[i].peer_id == peer_id) return &r->stats[i];
  }
  if (r->nstats >= RISTOUT_MAX_PEERS) return NULL;
  r->stats[r->nstats].peer_id = peer_id;
  return &r->stats[r->nstats++];
}

/* bonded peers share a cname: suffix slot index */
static void stat_label(const ristout_t *r, int idx, char *label, size_t cap) {
  sbuf_t sb;
  sbuf_init(&sb, label, cap);
  sbuf_add(&sb, r->stats[idx].cname);
  if (r->npeers > 1) {
    sbuf_add(&sb, ".");
    sbuf_add_uint(&sb, (unsigned)idx);
  }
}

static void push_peer_stats(ristout_t *r) {
  metrics_writer_t w;
  if (metrics_exporter_begin(r->mx, &w, r->tool_version)) return;
  for (int i = 0; i < r->nstats; i++) {
    char label[METRICS_LABEL_MAX + 1 + 8];
    stat_label(r, i, label, sizeof label);
    metrics_writer_put(&w, METRICS_ID_RIST_SENDER_SENT_TOTAL, label, r->stats[i].sent);
    metrics_writer_put(&w, METRICS_ID_RIST_SENDER_RETRANSMITTED_TOTAL, label, r->stats[i].retransmitted);
    metrics_writer_put(&w, METRICS_ID_RIST_SENDER_RTT_MILLISECONDS, label, r->stats[i].rtt);
  }
  metrics_exporter_send(r->mx, &w);
}

static int sender_stats_cb(void *arg, const struct rist_stats *stats) {
  ristout_t *r = arg;
  const struct rist_stats_sender_peer *p = &stats->stats.sender_peer;
  peer_stat_t *slot;
  if (stats->stats_type != RIST_STATS_SENDER_PEER) {
    rist_stats_free(stats);
    return 0;
  }
  pthread_mutex_lock(&r->push_lock);
  slot = stat_slot(r, p->peer_id);
  if (slot) {
    bufcpy(slot->cname, sizeof slot->cname, p->cname);
    slot->sent = p->sent;
    slot->retransmitted = p->retransmitted;
    slot->rtt = p->rtt;
  }
  r->stats_mono = mono_seconds();
  if (metrics_exporter_due(r->mx, r->stats_mono)) push_peer_stats(r);
  pthread_mutex_unlock(&r->push_lock);
  rist_stats_free(stats);
  return 0;
}

ristout_t *ristout_open(const ristout_cfg_t *cfg) {
  ristout_t *r;
  enum rist_profile profile = cfg->profile == RISTOUT_PROFILE_MAIN ? RIST_PROFILE_MAIN : RIST_PROFILE_SIMPLE;
  if (cfg->npeers <= 0 || cfg->npeers > RISTOUT_MAX_PEERS) {
    log_code("LE000318", "rist: invalid peer count %d", cfg->npeers);
    return NULL;
  }

  r = calloc(1, sizeof *r);
  if (!r) return NULL;
  pthread_mutex_init(&r->push_lock, NULL);
  r->mx = cfg->mx;
  r->npeers = cfg->npeers;
  r->tool_version = cfg->tool_version;
  r->drain_ms = cfg->buffer_ms ? cfg->buffer_ms : RIST_DRAIN_MS_DEFAULT;
  if (rist_sender_create(&r->ctx, profile, 0, ristlog_get(cfg->verbose)) != 0) {
    log_code("LE500155", "rist: sender create failed");
    pthread_mutex_destroy(&r->push_lock);
    free(r);
    return NULL;
  }
  rist_connection_status_callback_set(r->ctx, sender_conn_cb, r);
  if (add_peers(r->ctx, cfg) || rist_start(r->ctx) != 0) {
    rist_destroy(r->ctx);
    pthread_mutex_destroy(&r->push_lock);
    free(r);
    return NULL;
  }
  if (r->mx) rist_stats_callback_set(r->ctx, RIST_STATS_INTERVAL_MS, sender_stats_cb, r);
  return r;
}

int ristout_write(ristout_t *r, const unsigned char *buf, size_t n) {
  if (atomic_load(&r->connected) > 0) {
    r->warned = 0;
  } else if (!r->warned) {
    r->warned = 1;
    log_code("LW600162", "rist: no peer connected, data may be lost");
  }
  for (size_t off = 0; off < n; off += RIST_CHUNK) {
    struct rist_data_block db;
    size_t chunk = n - off < RIST_CHUNK ? n - off : RIST_CHUNK;
    memset(&db, 0, sizeof db);
    db.payload = buf + off;
    db.payload_len = chunk;
    if (rist_sender_data_write(r->ctx, &db) < 0) {
      log_code("LE100129", "rist: write failed");
      return -1;
    }
    r->wrote = 1;
  }
  return 0;
}

void ristout_close(ristout_t *r) {
  if (!r) return;
  if (r->wrote && !signal_stop_requested()) {
    struct timespec ts = {(time_t)(r->drain_ms / 1000), (long)(r->drain_ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
  }
  rist_destroy(r->ctx);
  pthread_mutex_destroy(&r->push_lock);
  free(r);
}

void ristout_idle_push(ristout_t *r, double now_mono) {
  metrics_writer_t w;

  if (!r->mx) return;
  pthread_mutex_lock(&r->push_lock);
  if ((r->stats_mono == 0.0 || now_mono - r->stats_mono >= RIST_STATS_FRESH_S) && metrics_exporter_due(r->mx, now_mono) &&
      !metrics_exporter_begin(r->mx, &w, r->tool_version))
    metrics_exporter_send(r->mx, &w);
  pthread_mutex_unlock(&r->push_lock);
}
