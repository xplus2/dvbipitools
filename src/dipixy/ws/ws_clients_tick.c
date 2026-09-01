/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "ws_clients_int.h"
#include "ws_broadcast.h"

#include <stdlib.h>

#include "lib/helper/signal.h"

#define WS_CLIENTS_PULL_IDLE_SEC 20

int *g_expired_scratch;
tick_rate_t *g_tick_rate_scratch;

static double g_tick_prev_t = -1.0; /* tick-thread-only */
static jbuf_t g_tick_msg_jbuf;

void ws_clients_tick(void) {
  double now_mono = mono_seconds();
  double dt = g_tick_prev_t > 0.0 ? now_mono - g_tick_prev_t : 0.0;
  time_t now = time(NULL);
  int has_sinks = ws_broadcast_has_sinks();
  int *expired = has_sinks ? g_expired_scratch : NULL;
  tick_rate_t *rates = has_sinks ? g_tick_rate_scratch : NULL;
  int n_expired = 0;
  int n_rate = 0;
  int i;

  g_tick_prev_t = now_mono;

  pthread_mutex_lock(&g_clients_mtx);
  for (i = 0; i < g_clients_cap; i++) {
    ws_client_t *e = &g_clients[i];
    if (!e->used)
      continue;
    if (!e->persistent && now - e->last_seen > WS_CLIENTS_PULL_IDLE_SEC) {
      hash_delete(i);
      e->used = 0;
      if (g_free_slots)
        g_free_slots[g_free_slots_n++] = i;
      if (expired)
        expired[n_expired++] = i;
      continue;
    }
    if (rates) {
      uint64_t total = atomic_load_explicit(&e->bytes_total, memory_order_relaxed);
      uint64_t delta = total > e->bytes_prev ? total - e->bytes_prev : 0;
      e->bytes_prev = total;
      rates[n_rate].id = i;
      rates[n_rate].mbps = dt > 0.0 ? (double)delta * 8.0 / dt / 1e6 : 0.0;
      n_rate++;
    }
  }
  pthread_mutex_unlock(&g_clients_mtx);

  if (!has_sinks)
    return;

  if (n_expired) {
    jbuf_t *j = &g_tick_msg_jbuf;
    jbuf_reset(j);
    jbuf_str(j, "{\"type\":\"clients.remove_many\",\"ids\":[");
    for (i = 0; i < n_expired; i++) {
      if (i)
        jbuf_str(j, ",");
      jbuf_i64(j, expired[i]);
    }
    jbuf_str(j, "]}");
    if (!j->failed)
      ws_broadcast_publish(j->buf);
  }

  {
    jbuf_t *j = &g_tick_msg_jbuf;
    jbuf_reset(j);
    jbuf_str(j, "{\"type\":\"clients.tick\",\"clients\":[");
    for (i = 0; i < n_rate; i++) {
      if (i)
        jbuf_str(j, ",");
      jbuf_str(j, "{\"id\":");
      jbuf_i64(j, rates[i].id);
      jbuf_str(j, ",\"mbps\":");
      jbuf_fixed3(j, rates[i].mbps);
      jbuf_str(j, "}");
    }
    jbuf_str(j, "]}");
    if (!j->failed)
      ws_broadcast_publish(j->buf);
  }
}

int ws_clients_build_snapshot(char **out) {
  static _Thread_local jbuf_t j;
  ws_client_snapshot_t snap;
  int n = 0;
  jbuf_reset(&j);
  jbuf_str(&j, "{\"type\":\"clients.snapshot\",\"clients\":[");
  pthread_mutex_lock(&g_clients_mtx);
  for (int i = 0; i < g_clients_cap; i++) {
    if (!g_clients[i].used) continue;
    if (n++) jbuf_str(&j, ",");
    snapshot_client(&snap, &g_clients[i]);
    emit_client_json(&j, i, &snap);
  }
  pthread_mutex_unlock(&g_clients_mtx);
  jbuf_str(&j, "]}");
  if (j.failed) return -1;
  *out = j.buf;
  return 0;
}
