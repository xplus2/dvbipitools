/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

/* epoll reactor core: one epoll instance + one set of listen sockets PER WORKER THREAD (SO_REUSEPORT) */

#define _GNU_SOURCE
#include "internal.h"
#include "reactor.h"

#include "../hls/hls.h"
#include "../segment/segment.h"
#include "../segment/mp4push.h"
#include "../dash/dash.h"
#include "../dash/lldash.h"
#include "../ts/ts_push.h"
#include "../version.h"
#include "lib/helper/log.h"
#include "reactor_tls.h"
#include "qsbr.h"
#include "../core/tlscert.h"
#ifdef HAVE_HTTP3
#include "../http3/http3.h"
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <unistd.h>

long g_connections_total = 0;

/* both used by reactor_tls.c: t_reactor_fd stays -1 here (dipixy has no deferred-close reactor path) */
_Thread_local int t_reactor_fd = -1;
_Thread_local int t_close_deferred = 0;
_Thread_local int t_tls_want_write = 0;
_Thread_local int t_reactor_tid = -1;
_Thread_local int t_reactor_epfd = -1;

static const config_t *g_cfg;
static const channels_t *g_channels;
static metrics_exporter_t *g_metrics;
static void (*g_on_listening)(const config_t *cfg);
static qsbr_domain_t *g_reactor_qsbr;

const config_t *reactor_cfg(void) { return g_cfg; }
qsbr_domain_t *reactor_qsbr(void) { return g_reactor_qsbr; }
const channels_t *reactor_channels(void) { return g_channels; }
metrics_exporter_t *reactor_metrics(void) { return g_metrics; }

void reactor_reload_channels(void) { channels_reload_all((channels_t *)g_channels, reactor_cfg()); }

long reactor_connections_total(void) { return __atomic_load_n(&g_connections_total, __ATOMIC_RELAXED); }

static int g_workers;

int reactor_worker_count(void) { return __atomic_load_n(&g_workers, __ATOMIC_RELAXED); }

void reactor_arm(int epfd, conn_t *c, int want_out) { conn_epoll_mod(c, epfd, want_out); }

void reactor_teardown_common(int epfd, conn_t *c) {
  epoll_ctl(epfd, EPOLL_CTL_DEL, c->fd, NULL);
  tls_close_fd(c->fd);
  conn_free(c);
}

void reactor_close(int epfd, conn_t *c) {
  llhls_waiter_conn_closing(c);
  hls_cold_waiter_conn_closing(c);
  reactor_teardown_common(epfd, c);
}

typedef enum { RF_TSPUSH, RF_DASHCHUNK, RF_MP4PUSH, RF_WS, RF_KEEPALIVE, RF_CLOSE } reactor_finish_next_t;

void reactor_finish(int epfd, conn_t *c) {
  reactor_finish_next_t next;
  int rc = conn_flush(c, epfd);
  if (rc == CONN_FLUSH_MORE) {
    c->state = CONN_WRITING;
    return;
  }
  if (rc == CONN_FLUSH_ERROR) {
    reactor_close(epfd, c);
    return;
  }
  if (c->become_tspush)             next = RF_TSPUSH;
  else if (c->become_dashchunk)     next = RF_DASHCHUNK;
  else if (c->become_mp4push)       next = RF_MP4PUSH;
  else if (c->become_ws)            next = RF_WS;
  else if (c->keep_alive)           next = RF_KEEPALIVE;
  else                              next = RF_CLOSE;

  switch (next) {
    case RF_TSPUSH:    reactor_tspush_begin(epfd, c); break;
    case RF_DASHCHUNK: reactor_dashchunk_begin(epfd, c); break;
    case RF_MP4PUSH:   reactor_mp4push_begin(epfd, c); break;
    case RF_WS:        reactor_ws_begin(epfd, c); break;
    case RF_KEEPALIVE: reactor_keepalive(epfd, c); break;
    case RF_CLOSE:     reactor_close(epfd, c); break;
  }
}

int reactor_flush_rc(conn_t *c, int epfd) {
  int dead;
  pthread_mutex_lock(&c->out_lock);
  dead = c->dead;
  pthread_mutex_unlock(&c->out_lock);
  return dead ? CONN_FLUSH_ERROR : conn_flush(c, epfd);
}

void reactor_flush_or_close(int epfd, conn_t *c, void (*close_fn)(int, conn_t *)) {
  int dead;
  int caf;
  int rc;
  pthread_mutex_lock(&c->out_lock);
  dead = c->dead;
  caf = c->close_after_flush;
  pthread_mutex_unlock(&c->out_lock);
  rc = dead ? CONN_FLUSH_ERROR : conn_flush(c, epfd);
  if (rc == CONN_FLUSH_ERROR || (rc == CONN_FLUSH_DONE && caf)) close_fn(epfd, c);
}

void reactor_conn_flush(int epfd, conn_t *c) {
  reactor_flush_or_close(epfd, c, reactor_tspush_close);
}

/* joined before reactor_run() returns: on_listening must finish reading cfg b4 main()'s cfg goes out of scope */
static pthread_t g_on_listening_thread;
static int g_on_listening_thread_started;

static void *on_listening_thread(void *arg) {
  g_on_listening((const config_t *)arg);
  return NULL;
}

static void reactor_log_status_url(const char *scheme, const listen_spec_t *ls) {
  char host[80];
  if       (ls->scope == LISTEN_ANY) snprintf(host, sizeof host, "0.0.0.0");
  else if  (ls->scope == LISTEN_V6) snprintf(host, sizeof host, "[%s]", ls->addr);
  else     snprintf(host, sizeof host, "%s", ls->addr);
  log_line_ansi(TOOL_NAME ": status \e[0;34m%s://%s:%u/\e[0m", scheme, host, ls->port);
}

void reactor_notify_listening(void) {
  log_line(TOOL_NAME ": listening on %s:%u%s", g_cfg->listen.scope == LISTEN_ANY ? "all" : g_cfg->listen.addr, g_cfg->listen.port, tls_is_running() ? " (+ tls)" : "");
  if (!g_cfg->no_status) reactor_log_status_url("http", &g_cfg->listen);
  if (!g_cfg->no_status && tls_is_running()) reactor_log_status_url("https", &g_cfg->listen_tls);
  if (g_on_listening) {
    if (pthread_create(&g_on_listening_thread, NULL, on_listening_thread, (void *)g_cfg) == 0)
      g_on_listening_thread_started = 1;
    else
      g_on_listening(g_cfg);
  }
}

/* threads[] sized to this in reactor_run, -j past it fails at startup */
#define REACTOR_MAX_WORKERS 256

int reactor_run(const config_t *cfg, const channels_t *channels, metrics_exporter_t *mx, void (*on_listening)(const config_t *cfg)) {
  int workers, n_pump, i, conn_cap;
  pthread_t threads[REACTOR_MAX_WORKERS];
  pthread_t pumps[CAPTURE_PUMP_MAX_THREADS];

  g_cfg = cfg;
  g_channels = channels;
  g_metrics = mx;
  g_on_listening = on_listening;

  reactor_raise_nofile_limit();
  conn_cap = conn_table_capacity(cfg->max_clients);
  conn_table_init(conn_cap);
  ts_push_init(!(cfg->no_ts && cfg->no_spts && cfg->no_rawaudio), cfg->max_clients);
  ws_clients_init(cfg->max_clients);
  hls_store_init(cfg->max_channels);
  hls_set_init_codecs_cb(dash_compute_codecs);
  hls_seg_init(cfg->max_channels);
  if (!(cfg->no_hls && cfg->no_llhls && cfg->no_dash && cfg->no_lldash)) hls_set_seg_pool_cap(cfg->hls_seg_pool);
  if (!cfg->no_lldash) dash_lldash_init(cfg->max_clients);
  if (!cfg->no_mp4) mp4push_init(cfg->max_clients);

  workers = reactor_resolve_workers(cfg->workers_spec, (int)sysconf(_SC_NPROCESSORS_ONLN));
  if (workers < 1) workers = 1;
  if (workers > REACTOR_MAX_WORKERS) {
    log_line(TOOL_NAME ": -j resolves to %d workers, exceeds max supported %d", workers, REACTOR_MAX_WORKERS);
    return -1;
  }
#ifdef HAVE_HTTP3
  h3_set_max_conns_per_thread(cfg->max_clients / workers);
#endif

  {
    const char *cert_path, *key_path;
    tls_set_http2_enabled(!cfg->no_http2);
    if (tlscert_find(cfg->tls_cert, cfg->tls_key, &cert_path, &key_path)) {
      if (tls_init(cert_path, key_path, conn_cap)) log_line(TOOL_NAME ": TLS init failed, --listen-tls not bound");

#ifdef HAVE_HTTP3
      if (!cfg->no_http3) h3_init(cert_path, key_path);
#endif
    } else log_line(TOOL_NAME ": no usable TLS certificate found, --listen-tls not bound");
  }

  __atomic_store_n(&g_workers, workers, __ATOMIC_RELAXED);
  tls_gc_init(workers);
  g_reactor_qsbr = qsbr_domain_create(workers);
  hls_store_set_qsbr(g_reactor_qsbr);
  channels_set_qsbr(g_reactor_qsbr);
  n_pump = workers > CAPTURE_PUMP_MAX_THREADS ? CAPTURE_PUMP_MAX_THREADS : workers;
  capture_pump_set_thread_count(n_pump);

  for (i = 0; i < n_pump; i++) if (pthread_create(&pumps[i], NULL, pump_thread, (void *)(intptr_t)i)) pumps[i] = 0;
  for (i = 1; i < workers; i++) if (pthread_create(&threads[i], NULL, worker_thread, (void *)(intptr_t)i)) threads[i] = 0;
  worker_thread((void *)(intptr_t)0);
  if (g_on_listening_thread_started) pthread_join(g_on_listening_thread, NULL);
  for (i = 1; i < workers; i++) if (threads[i]) pthread_join(threads[i], NULL);
  for (i = 0; i < n_pump; i++) if (pumps[i]) pthread_join(pumps[i], NULL);

#ifdef HAVE_HTTP3
  h3_cleanup();
#endif
  tls_cleanup();
  return 0;
}
