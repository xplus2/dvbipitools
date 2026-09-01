/* Copyright 2026 dvbipitools authors. Licensed under GPL-3.0-or-later.
 * See NOTICE and LICENSE for details and authorship information. */

#include "priv.h"

#include <stdlib.h>
#include <string.h>

#include "lib/helper/ioutil.h"
#include "lib/helper/log.h"

#include "../../version.h"

/* calloc+tssrc_open+backend/refcount=1. key: caller-owned strdup, NULL for rist/stdin.
   locking left to caller: srt/http hold g_lock throughout, rist/stdin call unlocked */
static capture_ctx_t *tssrc_ctx_new(char *key, const tssrc_cfg_t *cfg, net_err_reason_t *reason) {
  capture_ctx_t *c = calloc(1, sizeof *c);
  if (!c) {
    free(key);
    if (reason)
      *reason = NET_ERR_OTHER;
    return NULL;
  }
  atomic_init(&c->ts_push_head, -1);
  c->pump_shard = next_pump_shard();
  c->key = key;
  c->ts = tssrc_open(cfg, reason);
  if (!c->ts) {
    free(c->key);
    free(c);
    return NULL;
  }
  c->backend = CAP_BACKEND_TSSRC;
  c->refcount = 1;
  return c;
}

capture_ctx_t *capture_open_srt(const char *host, unsigned port) {
  char key[300];
  capture_ctx_t *c;
  tssrc_cfg_t cfg;
  char *keydup;

  {
    char portbuf[12];
    size_t off = bufcpy(key, sizeof key, "srt:");
    off += bufcpy(key + off, sizeof key - off, host);
    off += bufcpy(key + off, sizeof key - off, ":");
    uint_to_str(portbuf, port);
    bufcpy(key + off, sizeof key - off, portbuf);
  }

  pthread_mutex_lock(&g_lock);
  c = find_existing_tssrc(key);
  if (c) {
    atomic_fetch_add_explicit(&c->refcount, 1, memory_order_relaxed);
    pthread_mutex_unlock(&g_lock);
    return c;
  }

  keydup = strdup(key);
  if (!keydup) {
    pthread_mutex_unlock(&g_lock);
    return NULL;
  }
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_SRT;
  cfg.srt_listen = 0;
  cfg.srt_host = host;
  cfg.srt_port = port;
  c = tssrc_ctx_new(keydup, &cfg, NULL);
  if (!c) {
    pthread_mutex_unlock(&g_lock);
    log_line(TOOL_NAME ": srt source %s:%u: connect failed, source left unavailable", host, port);
    return NULL;
  }
  c->next = g_open;
  g_open = c;
  rebuild_snapshot();
  pthread_mutex_unlock(&g_lock);
  return c;
}

capture_ctx_t *capture_open_http_static(const char *url, int insecure_tls) {
  tssrc_cfg_t cfg;
  capture_ctx_t *c;
  http_url_t hu;
  net_err_reason_t reason;
  char *keydup;
  if (http_url_parse(url, &hu)) {
    log_line(TOOL_NAME ": http source %s: invalid URL, source left unavailable", url);
    return NULL;
  }

  pthread_mutex_lock(&g_lock);
  c = find_existing_tssrc(url);
  if (c) {
    atomic_fetch_add_explicit(&c->refcount, 1, memory_order_relaxed);
    pthread_mutex_unlock(&g_lock);
    return c;
  }

  keydup = strdup(url);
  if (!keydup) {
    pthread_mutex_unlock(&g_lock);
    return NULL;
  }
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_HTTP;
  cfg.http = hu;
  cfg.insecure_tls = insecure_tls;
  c = tssrc_ctx_new(keydup, &cfg, &reason);
  if (!c) {
    pthread_mutex_unlock(&g_lock);
    log_line(TOOL_NAME ": http source %s: %s, source left unavailable", url, net_err_reason_name(reason));
    return NULL;
  }
  c->next = g_open;
  g_open = c;
  rebuild_snapshot();
  pthread_mutex_unlock(&g_lock);
  return c;
}

static capture_ctx_t *g_rist_ctx;
static capture_ctx_t *g_stdin_ctx;

int capture_rist_init(const char *rist_uri) {
  tssrc_cfg_t cfg;
  capture_ctx_t *c;
  net_err_reason_t reason;

  if (!rist_uri) return 0;
  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_RIST;
  cfg.rist_uri = rist_uri;
  c = tssrc_ctx_new(NULL, &cfg, &reason);
  if (!c) {
    log_line(TOOL_NAME ": --rist %s: %s", rist_uri, net_err_reason_name(reason));
    return -1;
  }
  pthread_mutex_lock(&g_lock);
  c->next = g_open;
  g_open = c;
  rebuild_snapshot();
  pthread_mutex_unlock(&g_lock);
  g_rist_ctx = c;
  return 0;
}

capture_ctx_t *capture_rist_get(void) { return g_rist_ctx ? capture_ref(g_rist_ctx) : NULL; }

int capture_stdin_init(void) {
  tssrc_cfg_t cfg;
  capture_ctx_t *c;
  net_err_reason_t reason;

  memset(&cfg, 0, sizeof cfg);
  cfg.kind = TSSRC_STDIN;
  c = tssrc_ctx_new(NULL, &cfg, &reason);
  if (!c) {
    log_line(TOOL_NAME ": -i -: %s", net_err_reason_name(reason));
    return -1;
  }

  pthread_mutex_lock(&g_lock);
  c->next = g_open;
  g_open = c;
  rebuild_snapshot();
  pthread_mutex_unlock(&g_lock);
  g_stdin_ctx = c;
  return 0;
}

capture_ctx_t *capture_stdin_get(void) { return g_stdin_ctx ? capture_ref(g_stdin_ctx) : NULL; }
