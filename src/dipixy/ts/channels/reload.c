/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/helper/log.h"
#include "lib/helper/signal.h"

#include "../../version.h"
#include "../../ws/ws_broadcast.h"
#include "../../ws/ws_sources.h"

static pthread_mutex_t g_reload_mtx = PTHREAD_MUTEX_INITIALIZER;

static void free_list_items(channel_list_t l) {
  for (int j = 0; j < l.count; j++) {
    free(l.items[j].name);
    free(l.items[j].uri);
    free(l.items[j].icon_uri);
    if (l.items[j].static_ctx)
      capture_close(l.items[j].static_ctx);
  }
  free(l.items);
}

/* rebuilds src fresh, swaps atomically into ch->lists[idx] on non-empty result.
   failure or empty result keeps prior list, never blanks. shared by periodic --sds refresh and SIGHUP's full reload */
static void reload_one_list(channels_t *ch, int idx, const source_def_t *src, const config_t *cfg) {
  channel_list_t fresh;
  channel_list_t *newl, *old;
  memset(&fresh, 0, sizeof fresh);
  switch (src->kind) {
    case SRC_SDS:
      build_from_sds(&fresh, src->value, cfg->iface, cfg->sds_timeout_s);
      break;
    case SRC_M3U:
      build_from_m3u(&fresh, src->value, cfg->insecure_tls);
      break;
    case SRC_XSPF:
      build_from_xspf(&fresh, src->value, cfg->insecure_tls);
      break;
    case SRC_CSV:
      build_from_csv(&fresh, src->value, cfg->insecure_tls);
      break;
    case SRC_XML:
      build_from_xml(&fresh, src->value);
      break;
    case SRC_HTTP:
      break; /* live connection: nothing to reparse */
  }
  if (fresh.count == 0) {
    free_list_items(fresh);
    return;
  }
  newl = malloc(sizeof *newl);
  if (!newl) {
    free_list_items(fresh);
    return;
  }
  *newl = fresh;
  pthread_mutex_lock(&g_reload_mtx);
  old = atomic_load_explicit(&ch->lists[idx], memory_order_relaxed);
  atomic_store_explicit(&ch->lists[idx], newl, memory_order_release);
  wait_readers_quiescent();
  pthread_mutex_unlock(&g_reload_mtx);
  if (old) {
    free_list_items(*old);
    free(old);
  }
  log_line(TOOL_NAME ": list %d reloaded, now %d channel%s", idx + 1, fresh.count, fresh.count == 1 ? "" : "s");
  {
    char *msg;
    if (!ws_sources_build_update(ch, src, (unsigned)(idx + 1), &msg)) ws_broadcast_publish(msg);
  }
}

typedef struct {
  channels_t *ch;
  const config_t *cfg;
} refresh_arg_t;

void channels_reload_all(channels_t *ch, const config_t *cfg) {
  for (int i = 0; i < cfg->n_sources; i++) reload_one_list(ch, cfg->sources[i].ordinal - 1, &cfg->sources[i], cfg);
  log_line(TOOL_NAME ": channel lists reloaded");
}

static _Atomic int g_refresh_running;
static pthread_t g_refresh_thread;
static void *refresh_thread_fn(void *arg) {
  refresh_arg_t *a = arg;
  double next = mono_seconds() + a->cfg->sds_refresh_interval_s;

  while (g_refresh_running && !signal_stop_requested()) {
    if (signal_reload_requested()) channels_reload_all(a->ch, a->cfg);
    if (mono_seconds() >= next) {
      for (int i = 0; i < a->cfg->n_sources; i++)
        if (a->cfg->sources[i].kind == SRC_SDS)
          reload_one_list(a->ch, a->cfg->sources[i].ordinal - 1, &a->cfg->sources[i], a->cfg);
      next = mono_seconds() + a->cfg->sds_refresh_interval_s;
    }
    usleep(200000);
  }
  free(a);
  return NULL;
}

/* handles SIGHUP reload for --m3u/--xspf/--csv/--xml, no periodic --sds */
void channels_start_refresh(channels_t *ch, const config_t *cfg) {
  refresh_arg_t *a;
  if (cfg->n_sources == 0) return;
  a = malloc(sizeof *a);
  if (!a) return;
  a->ch = ch;
  a->cfg = cfg;
  g_refresh_running = 1;
  if (pthread_create(&g_refresh_thread, NULL, refresh_thread_fn, a)) {
    g_refresh_running = 0;
    free(a);
  }
}

int channels_refresh_active(void) { return g_refresh_running; }

void channels_stop_refresh(void) {
  if (!g_refresh_running) return;
  g_refresh_running = 0;
  pthread_join(g_refresh_thread, NULL);
}

void channels_free(channels_t *ch) {
  if (!ch) return;
  for (int i = 0; i < ch->n_lists; i++) {
    channel_list_t *l = atomic_load_explicit(&ch->lists[i], memory_order_relaxed);
    if (!l) continue;
    free_list_items(*l);
    free(l);
  }
  free(ch->lists);
  free(ch);
}
